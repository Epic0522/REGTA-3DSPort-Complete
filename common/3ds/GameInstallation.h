#pragma once
#include "InstallManifest.h"
#include "Sha256.h"
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>

namespace RegtaInstall
{
enum Game { III, VC, LCS };

inline char *
errorMessage()
{
	static char message[384];
	return message;
}

inline bool
failure(const char *path, const char *reason)
{
	snprintf(errorMessage(), 384, "Game installation incomplete.~n~%s: %s~n~Please reinstall the game files.~n~Press B.", reason, path);
	FILE *log = fopen("regta-install-check.log", "w");
	if(log) {
		fprintf(log, "Game installation incomplete.\n%s: %s\nPlease reinstall the game files.\n", path, reason);
		fclose(log);
	}
	return false;
}

inline bool
present(const char *path)
{
	struct stat info;
	return stat(path, &info) == 0 && S_ISREG(info.st_mode) && info.st_size > 0;
}

// Only shipped overrides have an exact content contract. Original archives
// vary by release; saves, configuration, generated caches and extra files are
// deliberately not fingerprinted. FAT on the console is case-insensitive.
inline bool
validate(Game game, void (*service)() = 0)
{
	const char *core[] = {"models/gta3.img", "models/gta3.dir", "models/fonts.txd", "data/main.scm"};
	for(unsigned i = 0; i < sizeof(core) / sizeof(core[0]); ++i)
		if(!present(core[i])) return failure(core[i], "Missing or empty file");
	const char *world = game == III ? "data/gta3.dat" : game == VC ? "data/gta_vc.dat" : "data/gta_lcs.dat";
	if(!present(world)) return failure(world, "Missing or empty file");
	const Entry *entries = game == III ? re3 : game == VC ? revc : relcs;
	const unsigned count = game == III ? sizeof(re3) / sizeof(re3[0]) : game == VC ? sizeof(revc) / sizeof(revc[0]) : sizeof(relcs) / sizeof(relcs[0]);
	unsigned char buffer[16384];
	for(unsigned i = 0; i < count; ++i) {
		const Entry &entry = entries[i];
		struct stat info;
		if(stat(entry.path, &info) || !S_ISREG(info.st_mode)) return failure(entry.path, "Missing file");
		if(info.st_size != entry.size) return failure(entry.path, "Incorrect file");
		FILE *file = fopen(entry.path, "rb");
		if(!file) return failure(entry.path, "Cannot read file");
		Sha256 hash;
		size_t bytes, total = 0;
		while((bytes = fread(buffer, 1, sizeof(buffer), file)) != 0) {
			hash.update(buffer, bytes);
			total += bytes;
			if(service) service();
		}
		unsigned char digest[32];
		hash.finish(digest);
		bool ok = !ferror(file) && total == entry.size && memcmp(digest, entry.sha256, 32) == 0;
		fclose(file);
		if(!ok) return failure(entry.path, "Incorrect or unreadable file");
	}
	return true;
}
} // namespace RegtaInstall
