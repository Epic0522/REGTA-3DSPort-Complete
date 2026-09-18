#pragma once
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

namespace MenuCacheIO {
// Bump when the native raster writer or the menu's retained texture set changes.
static const uint32_t Version = 1;
static const uint32_t MaxPayload = 32 * 1024 * 1024;
struct Header {
    uint32_t magic, version;
    uint64_t sourceSize, sourceMtime;
    uint32_t payloadSize, payloadHash;
};
static_assert(sizeof(Header) == 32, "Stable menu cache header");

inline uint32_t Hash(const void *data, size_t size, uint32_t hash = 2166136261u)
{
    const unsigned char *bytes = static_cast<const unsigned char *>(data);
    while (size--) hash = (hash ^ *bytes++) * 16777619u;
    return hash;
}

inline bool Source(const char *path, Header &header)
{
    struct stat info;
    if (stat(path, &info) != 0 || info.st_size <= 0) return false;
    memset(&header, 0, sizeof(header));
    header.magic = 0x334D5447; // GTM3
    header.version = Version;
    header.sourceSize = info.st_size;
    header.sourceMtime = info.st_mtime;
    return true;
}

// One disk read, then feed the validated native bytes to RW's memory stream.
// The temporary buffer is released immediately after creating the rasters.
inline void *Read(const char *path, const Header &source, uint32_t &size)
{
    size = 0;
    FILE *file = fopen(path, "rb");
    if (!file) return NULL;
    Header saved;
    struct stat info;
    bool valid = fread(&saved, 1, sizeof(saved), file) == sizeof(saved) &&
        saved.magic == source.magic && saved.version == Version &&
        saved.sourceSize == source.sourceSize && saved.sourceMtime == source.sourceMtime &&
        saved.payloadSize >= 28 && saved.payloadSize <= MaxPayload &&
        fstat(fileno(file), &info) == 0 &&
        uint64_t(info.st_size) == sizeof(saved) + saved.payloadSize;
    void *data = valid ? malloc(saved.payloadSize) : NULL;
    if (data && (fread(data, 1, saved.payloadSize, file) != saved.payloadSize ||
                 Hash(data, saved.payloadSize) != saved.payloadHash)) {
        free(data);
        data = NULL;
    }
    fclose(file);
    if (data) size = saved.payloadSize;
    return data;
}

// The caller writes a placeholder header followed by an RW native dictionary.
// Seal only a complete write, then atomically replace the old cache.
inline bool Publish(const char *temporary, const char *path, Header source)
{
    FILE *file = fopen(temporary, "r+b");
    if (!file) return false;
    struct stat info;
    bool ok = fstat(fileno(file), &info) == 0 &&
        info.st_size >= long(sizeof(Header) + 28) &&
        uint64_t(info.st_size) <= sizeof(Header) + MaxPayload;
    if (ok) {
        source.payloadSize = info.st_size - sizeof(Header);
        source.payloadHash = 2166136261u;
        ok = fseek(file, sizeof(Header), SEEK_SET) == 0;
        unsigned char buffer[16384];
        uint32_t remaining = source.payloadSize;
        while (ok && remaining) {
            size_t amount = remaining < sizeof(buffer) ? remaining : sizeof(buffer);
            ok = fread(buffer, 1, amount, file) == amount;
            if (ok) source.payloadHash = Hash(buffer, amount, source.payloadHash);
            remaining -= amount;
        }
        ok = ok && fseek(file, 0, SEEK_SET) == 0 &&
            fwrite(&source, 1, sizeof(source), file) == sizeof(source) &&
            fflush(file) == 0 && fsync(fileno(file)) == 0;
    }
    if (fclose(file) != 0) ok = false;
    return ok && rename(temporary, path) == 0;
}
} // namespace MenuCacheIO
