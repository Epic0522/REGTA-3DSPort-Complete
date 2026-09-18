#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <3ds.h>
#include <AL/al.h>
#include <AL/alc.h>

#include "3dsmovie.h"

namespace {

static const size_t MOVIE_NAL_CAPACITY = 1024 * 1024;
static const size_t MOVIE_AUDIO_CHUNK = 32 * 1024;
static const int MOVIE_AUDIO_BUFFERS = 4;
static const int MOVIE_AUDIO_RATE = 32000;

struct MovieHeader {
	char magic[8];
	u16 width;
	u16 height;
	u32 fpsNumerator;
	u32 fpsDenominator;
};

static bool
ReadExact(FILE *file, void *dst, size_t size)
{
	return fread(dst, 1, size, file) == size;
}

static bool
MovieSkipPressed(void)
{
	hidScanInput();
	const u32 keys = hidKeysDown();
	return (keys & (KEY_A | KEY_B | KEY_X | KEY_Y | KEY_START | KEY_SELECT |
		KEY_L | KEY_R | KEY_ZL | KEY_ZR)) != 0;
}

class MovieAudio {
	FILE *file;
	ALuint source;
	ALuint buffers[MOVIE_AUDIO_BUFFERS];
	u8 *pcm;
	int queued;
	bool ready;
	bool started;

	bool Fill(ALuint buffer)
	{
		const size_t bytes = fread(pcm, 1, MOVIE_AUDIO_CHUNK, file);
		if(bytes == 0)
			return false;
		alBufferData(buffer, AL_FORMAT_MONO16, pcm, (ALsizei)(bytes & ~1U), MOVIE_AUDIO_RATE);
		return alGetError() == AL_NO_ERROR;
	}

public:
	MovieAudio() : file(NULL), source(0), pcm(NULL), queued(0), ready(false), started(false)
	{
		memset(buffers, 0, sizeof(buffers));
	}

	bool Prepare(const char *path)
	{
		if(alcGetCurrentContext() == NULL)
			return false;
		file = fopen(path, "rb");
		if(file == NULL)
			return false;

		pcm = (u8*)malloc(MOVIE_AUDIO_CHUNK);
		if(pcm == NULL)
			return false;

		alGetError();
		alGenSources(1, &source);
		alGenBuffers(MOVIE_AUDIO_BUFFERS, buffers);
		alSourcei(source, AL_SOURCE_RELATIVE, AL_TRUE);
		alSource3f(source, AL_POSITION, 0.0f, 0.0f, 0.0f);
		alSourcef(source, AL_GAIN, 1.0f);
		if(alGetError() != AL_NO_ERROR)
			return false;

		for(int i = 0; i < MOVIE_AUDIO_BUFFERS; i++) {
			if(!Fill(buffers[i]))
				break;
			alSourceQueueBuffers(source, 1, &buffers[i]);
			queued++;
		}
		ready = queued != 0;
		return ready;
	}

	void Start()
	{
		if(ready && !started) {
			alSourcePlay(source);
			started = true;
		}
	}

	void Pump()
	{
		if(!ready || !started)
			return;

		ALint processed = 0;
		alGetSourcei(source, AL_BUFFERS_PROCESSED, &processed);
		while(processed-- > 0) {
			ALuint buffer = 0;
			alSourceUnqueueBuffers(source, 1, &buffer);
			queued--;
			if(Fill(buffer)) {
				alSourceQueueBuffers(source, 1, &buffer);
				queued++;
			}
		}

		ALint state = AL_STOPPED;
		alGetSourcei(source, AL_SOURCE_STATE, &state);
		if(queued > 0 && state != AL_PLAYING)
			alSourcePlay(source);
	}

	~MovieAudio()
	{
		if(source != 0) {
			alSourceStop(source);
			ALint count = 0;
			alGetSourcei(source, AL_BUFFERS_QUEUED, &count);
			while(count-- > 0) {
				ALuint buffer = 0;
				alSourceUnqueueBuffers(source, 1, &buffer);
			}
			alDeleteSources(1, &source);
			alDeleteBuffers(MOVIE_AUDIO_BUFFERS, buffers);
		}
		if(file != NULL)
			fclose(file);
		free(pcm);
	}
};

static void
SetMovieFramebufferFormat(bool movieFormat)
{
	gfxSetScreenFormat(GFX_BOTTOM, movieFormat ? GSP_RGB565_OES : GSP_BGR8_OES);
	const size_t bytes = 320 * 240 * (movieFormat ? 2 : 3);
	for(int i = 0; i < 2; i++) {
		u8 *framebuffer = gfxGetFramebuffer(GFX_BOTTOM, GFX_LEFT, NULL, NULL);
		memset(framebuffer, 0, bytes);
		GSPGPU_FlushDataCache(framebuffer, bytes);
		// Swap only the movie screen.  A global swap also alternates the two
		// upper buffers, making the retained loadsc0 image flash during playback.
		gfxScreenSwapBuffers(GFX_BOTTOM, false);
	}
}

static bool
Play3DSMovie(const char *videoPath, const char *audioPath)
{
	bool isNew3DS = false;
	if(R_FAILED(APT_CheckNew3DS(&isNew3DS)) || !isNew3DS)
		return false;

	bool hasMvd = false;
	if(R_FAILED(srvIsServiceRegistered(&hasMvd, "mvd:STD")) || !hasMvd)
		return false;

	FILE *video = fopen(videoPath, "rb");
	if(video == NULL)
		return false;

	MovieHeader header;
	if(!ReadExact(video, &header, sizeof(header)) ||
		memcmp(header.magic, "R3MVD01", 7) != 0 ||
		header.width != 240 || header.height != 320 ||
		header.fpsNumerator == 0 || header.fpsDenominator == 0) {
		fclose(video);
		return false;
	}

	u8 *nal = (u8*)linearMemAlign(MOVIE_NAL_CAPACITY, 0x40);
	if(nal == NULL) {
		fclose(video);
		return false;
	}

	Result result = mvdstdInit(MVDMODE_VIDEOPROCESSING, MVD_INPUT_H264,
		MVD_OUTPUT_BGR565, MVD_DEFAULT_WORKBUF_SIZE, NULL);
	if(result != 0) {
		linearFree(nal);
		fclose(video);
		return false;
	}

	MovieAudio audio;
	audio.Prepare(audioPath); // A missing audio sidecar leaves a silent movie.
	SetMovieFramebufferFormat(true);

	MVDSTD_Config config;
	u8 *framebuffer = gfxGetFramebuffer(GFX_BOTTOM, GFX_LEFT, NULL, NULL);
	mvdstdGenerateDefaultConfig(&config, header.width, header.height,
		header.width, header.height, NULL, (u32*)framebuffer, (u32*)framebuffer);

	u32 frameNumber = 0;
	u64 startTime = 0;
	bool skipped = false;
	while(!skipped) {
		u32 nalSize = 0;
		if(!ReadExact(video, &nalSize, sizeof(nalSize)) || nalSize == 0)
			break;
		if(nalSize > MOVIE_NAL_CAPACITY || !ReadExact(video, nal, nalSize))
			break;

		GSPGPU_FlushDataCache(nal, nalSize);
		MVDSTD_ProcessNALUnitOut output;
		memset(&output, 0, sizeof(output));
		result = mvdstdProcessVideoFrame(nal, nalSize, 0, &output);
		if(!MVD_CHECKNALUPROC_SUCCESS(result))
			break;

		if(result == MVD_STATUS_PARAMSET || result == MVD_STATUS_INCOMPLETEPROCESSING)
			continue;

		if(frameNumber == 0) {
			startTime = osGetTime();
			audio.Start();
		} else {
			const u64 target = startTime +
				((u64)frameNumber * 1000ULL * header.fpsDenominator) / header.fpsNumerator;
			while(osGetTime() < target) {
				audio.Pump();
				if(MovieSkipPressed()) {
					skipped = true;
					break;
				}
				gspWaitForVBlank();
			}
			if(skipped)
				break;
		}

		framebuffer = gfxGetFramebuffer(GFX_BOTTOM, GFX_LEFT, NULL, NULL);
		config.physaddr_outdata0 = osConvertVirtToPhys(framebuffer);
		config.physaddr_outdata1 = config.physaddr_outdata0;
		result = mvdstdRenderVideoFrame(&config, true);
		if(result != MVD_STATUS_OK)
			break;

		gfxScreenSwapBuffers(GFX_BOTTOM, false);
		frameNumber++;
		audio.Pump();
		if(MovieSkipPressed())
			skipped = true;
	}

	SetMovieFramebufferFormat(false);
	mvdstdExit();
	linearFree(nal);
	fclose(video);
	return frameNumber != 0;
}

} // namespace

void
Play3DSStartupMovies(void)
{
	Play3DSMovie("movies/Logo.3mv", "movies/Logo.pcm");
	Play3DSMovie("movies/GTAtitles.3mv", "movies/GTAtitles.pcm");
}
