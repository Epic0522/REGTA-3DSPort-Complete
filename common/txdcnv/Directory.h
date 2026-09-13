#pragma once

class CDirectory
{
public:
	struct DirectoryInfo {
		uint32_t offset;
		uint32_t size;
		char name[24];
	};
	DirectoryInfo *entries;
	int32_t maxEntries;
	int32_t numEntries;

	CDirectory(int32_t maxEntries);
	~CDirectory(void);

	void ReadDirFile(const char *filename);
	bool WriteDirFile(const char *filename);
	void AddItem(const DirectoryInfo &dirinfo);
	// void AddItem(const DirectoryInfo &dirinfo, int32_t imgId);
	// bool FindItem(const char *name, uint32_t &offset, uint32_t &size);
};
