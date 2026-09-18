#pragma once
#include "MenuCacheIO.h"

// Call only after all menu sprites have acquired their references. A texture
// with just its dictionary reference has no menu consumer (e.g. a controller
// diagram). Do not keep those rasters resident between pauses.
static RwTexture *ReleaseUnusedMenuTexture3DS(RwTexture *texture, void *)
{
	if (texture->refCount == 1)
		RwTextureDestroy(texture);
	return texture;
}

static bool LoadMenuTextureCache3DS(int slot, const char *source)
{
	char *resolved = casepath(source);
	const char *sourcePath = resolved ? resolved : source;
	MenuCacheIO::Header identity;
	bool loaded = false;
	if (MenuCacheIO::Source(sourcePath, identity)) {
		char cache[512];
		snprintf(cache, sizeof(cache), "%s.menu3ds-v1", sourcePath);
		uint32_t size;
		void *bytes = MenuCacheIO::Read(cache, identity, size);
		if (bytes) {
			RwMemory memory = { static_cast<RwUInt8 *>(bytes), size };
			RwStream *stream = RwStreamOpen(rwSTREAMMEMORY, rwSTREAMREAD, &memory);
			if (stream) {
				loaded = CTxdStore::LoadTxd(slot, stream);
				RwStreamClose(stream, nil);
			}
			free(bytes);
		}
	}
	free(resolved);
	if (loaded) return false;
	// Cache is optional: missing, stale, damaged or unwritable never blocks play.
	CTxdStore::LoadTxd(slot, source);
	return true;
}

static void FinishMenuTextureCache3DS(int slot, const char *source, bool needsCache)
{
	RwTexDictionary *dictionary = CTxdStore::GetSlot(slot)->texDict;
	if (!dictionary) return;
	RwTexDictionaryForAllTextures(dictionary, ReleaseUnusedMenuTexture3DS, nil);
	if (!needsCache) return;
	char *resolved = casepath(source);
	const char *sourcePath = resolved ? resolved : source;
	MenuCacheIO::Header identity;
	if (!MenuCacheIO::Source(sourcePath, identity)) {
		free(resolved);
		return;
	}
	char cache[512], temporary[520];
	snprintf(cache, sizeof(cache), "%s.menu3ds-v1", sourcePath);
	snprintf(temporary, sizeof(temporary), "%s.tmp", cache);
	free(resolved);
	RwStream *stream = RwStreamOpen(rwSTREAMFILENAME, rwSTREAMWRITE, temporary);
	bool ok = false;
	if (stream) {
		const uint32_t expected = dictionary->streamGetSize() + 12;
		ok = RwStreamWrite(stream, &identity, sizeof(identity)) != nil &&
		    RwTexDictionaryStreamWrite(dictionary, stream) != nil &&
		    stream->tell() == sizeof(identity) + expected;
		RwStreamClose(stream, nil);
		if (ok) ok = MenuCacheIO::Publish(temporary, cache, identity);
	}
	if (!ok) {
		remove(temporary);
		printf("Menu texture cache unavailable: %s\n", cache);
	}
}
