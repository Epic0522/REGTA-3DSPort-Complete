#include <assert.h>
#include <stdio.h>
#include <stdint.h>
#include "Directory.h"

CDirectory::CDirectory(int32_t maxEntries)
 : numEntries(0), maxEntries(maxEntries)
{
	entries = new DirectoryInfo[maxEntries];
}

CDirectory::~CDirectory(void)
{
	delete[] entries;
}

void
CDirectory::ReadDirFile(const char *filename)
{
	FILE *fp;
	DirectoryInfo dirinfo;
	fp = fopen(filename, "rb");
	while(fread(&dirinfo, sizeof(dirinfo), 1, fp)){
		AddItem(dirinfo);
	}
	fclose(fp);
}

bool
CDirectory::WriteDirFile(const char *filename)
{
	FILE *fp;
	size_t n;
	fp = fopen(filename, "wb");
	n = fwrite(entries, sizeof(DirectoryInfo), numEntries, fp);
	fclose(fp);
	return (n == numEntries);
}

void
CDirectory::AddItem(const DirectoryInfo &dirinfo)
{
	assert(numEntries < maxEntries);
	// don't add if already exists
	// uint32_t offset, size;
	// if(FindItem(dirinfo.name, offset, size))
	// 	return;
	entries[numEntries++] = dirinfo;
}

// void
// CDirectory::AddItem(const DirectoryInfo &dirinfo, int32_t imgId)
// {
// 	DirectoryInfo di = dirinfo;
// 	di.offset |= imgId<<24;
// 	AddItem(di);
// }

// bool
// CDirectory::FindItem(const char *name, uint32_t &offset, uint32_t &size)
// {
// 	int i;

// 	for(i = 0; i < numEntries; i++)
// 		if(!CGeneral::faststricmp(entries[i].name, name)){
// 			offset = entries[i].offset;
// 			size = entries[i].size;
// 			return true;
// 		}
// 	return false;
// }
