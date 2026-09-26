// LCS stock radio/ambience only. The worker owns a decoder-only CStream with
// no OpenAL sources. Requests and PCM publication hold the lock briefly;
// opening, seeking, decoding and destruction never hold it.
#include <vector>

struct CStream::AsyncState {
	struct Block {
		std::vector<uint8> pcm;
		uint32 rate = 0, start = 0;
	} blocks[4], upload;
	LightLock lock;
	LightEvent wake;
	Thread thread;
	bool quit = false, requested = false, done = true, loop = false;
	uint32 generation = 0, position = 0, overrideRate = 32000, length = 0;
	char filename[128];
	unsigned head = 0, count = 0;
	// Playback fields below are exclusively owned by the game thread.
	bool active = false, resume = false, used[4] = {false, false, false, false};
	unsigned queued = 0, order[4];
	uint32 start[4], rate[4], frames[4], playPosition = 0;
	AsyncState() : thread(nil)
	{
		filename[0] = 0;
		LightLock_Init(&lock);
		LightEvent_Init(&wake, RESET_ONESHOT);
	}
	static void Run(void *arg)
	{
		AsyncState *s = (AsyncState *)arg;
		ALuint sources[2] = {AL_NONE, AL_NONE}, buffers[NUM_STREAMBUFFERS] = {0};
		CStream decoder(sources, buffers);
		uint32 seen = 0;
		bool opened = false, looping = false, ended = true;
		Block block;
		for(;;) {
			LightLock_Lock(&s->lock);
			if(s->quit) {
				LightLock_Unlock(&s->lock);
				break;
			}
			const uint32 generation = s->generation;
			const bool changed = generation != seen;
			const bool requested = s->requested;
			const bool full = s->count == 4;
			char filename[128];
			uint32 position = 0, rate = 0;
			if(changed) {
				memcpy(filename, s->filename, sizeof(filename));
				position = s->position;
				rate = s->overrideRate;
				looping = s->loop;
			}
			LightLock_Unlock(&s->lock);
			if(changed) {
				seen = generation;
				decoder.Close();
				opened = false;
				ended = true;
				if(requested) {
					// Preserve LCS's WAV -> MP3 -> VB dataset contract, on the worker.
					char path[128];
					for(const char *extension : {".WAV", ".MP3", ".VB"}) {
						snprintf(path, sizeof(path), "%s%s", filename, extension);
						opened = decoder.Open(path, rate);
						if(opened) break;
						decoder.Close();
					}
					if(opened) {
						IDecoder *d = decoder.m_pSoundFile;
						const uint32 channels = d->GetChannels(), bytes = d->GetBufferSize();
						opened = d->GetSampleSize() == 2 && channels >= 1 && channels <= 2 && d->GetSampleRate() != 0 && bytes > 0 &&
						         bytes <= 128 * 1024 && decoder.m_pBuffer;
						if(opened) d->Seek(position);
					}
					ended = !opened;
				}
				const uint32 length = opened ? decoder.m_pSoundFile->GetLength() : 0;
				LightLock_Lock(&s->lock);
				if(s->generation == seen) {
					s->done = ended;
					s->length = length;
				}
				LightLock_Unlock(&s->lock);
				// Recheck cancellation before decoding after a slow open/seek.
				continue;
			}
			if(!opened || ended || full) {
				LightEvent_Wait(&s->wake);
				continue;
			}
			IDecoder *d = decoder.m_pSoundFile;
			block.start = d->Tell();
			uint32 bytes = d->Decode(decoder.m_pBuffer);
			if(!bytes && looping) {
				d->Seek(0);
				block.start = d->Tell();
				bytes = d->Decode(decoder.m_pBuffer);
			}
			ended = bytes == 0;
			block.rate = d->GetSampleRate();
			if(bytes) {
				const uint32 monoBytes = bytes / d->GetChannels();
				block.pcm.resize(monoBytes);
				const int16 *left = (const int16 *)decoder.m_pBuffer;
				const int16 *right = (const int16 *)((uint8 *)decoder.m_pBuffer + monoBytes);
				int16 *out = (int16 *)block.pcm.data();
				for(uint32 i = 0; i < monoBytes / 2; i++) out[i] = d->GetChannels() == 2 ? (int16)(((int32)left[i] + right[i]) / 2) : left[i];
			}
			LightLock_Lock(&s->lock);
			if(s->generation == seen && !s->quit) {
				if(ended)
					s->done = true;
				else {
					Block &dest = s->blocks[(s->head + s->count) % 4];
					dest.pcm.swap(block.pcm);
					dest.rate = block.rate;
					dest.start = block.start;
					++s->count;
				}
			}
			LightLock_Unlock(&s->lock);
		}
	}
};

void
CStream::CancelAsync()
{
	if(!m_async) return;
	LightLock_Lock(&m_async->lock);
	++m_async->generation;
	m_async->requested = false;
	m_async->done = true;
	m_async->count = 0;
	m_async->head = 0;
	LightLock_Unlock(&m_async->lock);
	LightEvent_Signal(&m_async->wake);
	m_async->active = false;
	SetPlay(false);
	ClearBuffers();
	m_async->queued = 0;
	memset(m_async->used, 0, sizeof(m_async->used));
}

void
CStream::DestroyAsync()
{
	if(!m_async) return;
	CancelAsync();
	LightLock_Lock(&m_async->lock);
	m_async->quit = true;
	LightLock_Unlock(&m_async->lock);
	LightEvent_Signal(&m_async->wake);
	if(m_async->thread) {
		threadJoin(m_async->thread, U64_MAX);
		threadFree(m_async->thread);
	}
	delete m_async;
	m_async = nil;
}

bool
CStream::BeginAsyncStream(const char *filename, uint32 position, uint32 rate, bool loop)
{
	if(strlen(filename) >= 124 || !HasSource()) return false;
	// Keep the worker across switches. Only final destruction joins it.
	if(m_async) CancelAsync();
	// A cutscene may have used the legacy decoder since the last radio session.
	Close();
	if(!m_async) {
		m_async = new AsyncState;
		m_async->thread = threadCreate(AsyncState::Run, m_async, 128 * 1024, 0x31, 2, false);
		if(!m_async->thread) {
			delete m_async;
			m_async = nil;
			return false;
		}
	}
	m_bPaused = false;
	m_bActive = true;
	m_bReset = false;
	m_bFullInitialQueue = false;
	SetPitch(1.0f);
	alSourcei(m_pAlSources[0], AL_LOOPING, AL_FALSE);
	LightLock_Lock(&m_async->lock);
	strcpy(m_async->filename, filename);
	m_async->position = position;
	m_async->overrideRate = rate;
	m_async->loop = loop;
	m_async->length = 0;
	m_async->requested = true;
	m_async->done = false;
	++m_async->generation;
	LightLock_Unlock(&m_async->lock);
	m_async->playPosition = position;
	m_async->active = true;
	LightEvent_Signal(&m_async->wake);
	return true;
}

void
CStream::UpdateAsync()
{
	if(m_bPaused || m_bReset || !m_bActive) return;
	ALint processed = 0;
	alGetSourcei(m_pAlSources[0], AL_BUFFERS_PROCESSED, &processed);
	while(processed-- > 0 && m_async->queued) {
		ALuint buffer = 0;
		alSourceUnqueueBuffers(m_pAlSources[0], 1, &buffer);
		for(unsigned i = 0; i < 4; i++)
			if(m_alBuffers[i * 2] == buffer) {
				m_async->used[i] = false;
				m_async->playPosition = m_async->start[i] + (uint32)((uint64)m_async->frames[i] * 1000 / m_async->rate[i]);
			}
		--m_async->queued;
		for(unsigned i = 0; i < m_async->queued; i++) m_async->order[i] = m_async->order[i + 1];
	}
	// At most 500ms of ready PCM uploads per game frame; never decode or wait.
	for(unsigned refill = 0; refill < 2 && m_async->queued < 4; refill++) {
		unsigned slot = 0;
		while(slot < 4 && m_async->used[slot]) ++slot;
		if(slot == 4) break;
		LightLock_Lock(&m_async->lock);
		if(!m_async->count) {
			LightLock_Unlock(&m_async->lock);
			break;
		}
		AsyncState::Block &b = m_async->blocks[m_async->head];
		m_async->upload.pcm.swap(b.pcm);
		m_async->upload.rate = b.rate;
		m_async->upload.start = b.start;
		m_async->head = (m_async->head + 1) % 4;
		--m_async->count;
		LightLock_Unlock(&m_async->lock);
		LightEvent_Signal(&m_async->wake);
		const AsyncState::Block &pcm = m_async->upload;
		alBufferData(m_alBuffers[slot * 2], AL_FORMAT_MONO16, pcm.pcm.data(), pcm.pcm.size(), pcm.rate);
		alSourceQueueBuffers(m_pAlSources[0], 1, &m_alBuffers[slot * 2]);
		m_async->used[slot] = true;
		m_async->order[m_async->queued++] = slot;
		m_async->start[slot] = pcm.start;
		m_async->rate[slot] = pcm.rate;
		m_async->frames[slot] = pcm.pcm.size() / 2;
	}
	if(m_async->queued) SetPlay(true);
}
