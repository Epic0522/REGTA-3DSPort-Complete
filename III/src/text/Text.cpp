#include "common.h"

#include "FileMgr.h"
#ifdef MORE_LANGUAGES
#include "Game.h"
#endif
#include "Frontend.h"
#include "Messages.h"
#include "Text.h"

wchar WideErrorString[25];

CText TheText;

CText::CText(void)
{
	encoding = 'e';
	memset(WideErrorString, 0, sizeof(WideErrorString));
}

void
CText::Load(void)
{
	uint8 *filedata;
	char filename[32], type[4];
	ssize_t offset, length;
	size_t sectlen;

	Unload();
	filedata = new uint8[0x40000];

	CFileMgr::SetDir("TEXT");
	switch(CMenuManager::m_PrefsLanguage){
	case CMenuManager::LANGUAGE_AMERICAN:
		sprintf(filename, "AMERICAN.GXT");
		break;
	case CMenuManager::LANGUAGE_FRENCH:
		sprintf(filename, "FRENCH.GXT");
		break;
	case CMenuManager::LANGUAGE_GERMAN:
		sprintf(filename, "GERMAN.GXT");
		break;
	case CMenuManager::LANGUAGE_ITALIAN:
		sprintf(filename, "ITALIAN.GXT");
		break;
	case CMenuManager::LANGUAGE_SPANISH:
		sprintf(filename, "SPANISH.GXT");
		break;
#ifdef MORE_LANGUAGES
	case CMenuManager::LANGUAGE_POLISH:
		sprintf(filename, "POLISH.GXT");
		break;
	case CMenuManager::LANGUAGE_RUSSIAN:
		sprintf(filename, "RUSSIAN.GXT");
		break;
	case CMenuManager::LANGUAGE_JAPANESE:
		sprintf(filename, "JAPANESE.GXT");
		break;
#endif
	}

	length = CFileMgr::LoadFile(filename, filedata, 0x40000, "rb");
	CFileMgr::SetDir("");

	offset = 0;
	while(offset < length){
		type[0] = filedata[offset++];
		type[1] = filedata[offset++];
		type[2] = filedata[offset++];
		type[3] = filedata[offset++];
		sectlen = (int)filedata[offset+3]<<24 | (int)filedata[offset+2]<<16 |
			(int)filedata[offset+1]<<8 | (int)filedata[offset+0];
		offset += 4;
		if(sectlen != 0){
			if(strncmp(type, "TKEY", 4) == 0)
				keyArray.Load(sectlen, filedata, &offset);
			else if(strncmp(type, "TDAT", 4) == 0)
				data.Load(sectlen, filedata, &offset);
			else
				offset += sectlen;
		}
	}

	keyArray.Update(data.chars);

#ifdef _3DS
	/* Keep 3DS-specific tutorials correct even when only the 3DSX is updated
	 * and an older American GXT remains on the SD card.  Every replacement is
	 * shorter than its original string, so in-place writing cannot cross the
	 * following TDAT entry. */
	if(CMenuManager::m_PrefsLanguage == CMenuManager::LANGUAGE_AMERICAN) {
		struct ControlTextPatch { const char *key, *text; };
		static const ControlTextPatch patches[] = {
			{ "HELP4_D", "Push the~h~ C-STICK~w~ up to ~h~accelerate." },
			{ "HELP5_D", "Pull the ~h~C-STICK~w~ back to ~h~brake~w~, or to ~h~reverse~w~ if the vehicle has stopped." },
			{ "EBAL_3", "The lower screen shows the ~h~radar~w~. Use it to navigate the city. Follow the ~h~blip~w~ to find the hideout!" },
		};
		for(const ControlTextPatch &patch : patches) {
			wchar *dst = Get(patch.key);
			const unsigned char *src = (const unsigned char*)patch.text;
			while(*src)
				*dst++ = *src++;
			*dst = 0;
		}
	}
#endif

	delete[] filedata;
}

void
CText::Unload(void)
{
	CMessages::ClearAllMessagesDisplayedByGame();
	data.Unload();
	keyArray.Unload();
}

wchar*
CText::Get(const char *key)
{
#ifdef _3DS
	if (strcmp(key, "F3BGM") == 0) {
		static wchar label[] = { 'F','i','n','a','l',' ','m','i','s','s','i','o','n',' ','B','G','M',0 };
		return label;
	}
#endif
#ifdef _3DS
	if (strcmp(key, "FEM_SL0") == 0) {
		static wchar label[] = { 'S','A','V','E',' ','S','L','O','T',0 };
		return label;
	}
#endif
#ifdef _3DS
	if(CMenuManager::m_PrefsLanguage == CMenuManager::LANGUAGE_AMERICAN) {
		static const char *keys[] = { "FEC_SM3", "FEC_RS3", "FEC_HO3", "FEC_R3", "HORN1" };
		static const char *texts[] = {
			"Special mission trigger (TOUCH, THEN TAP R3)",
			"Radio station cycle (TOUCH, THEN TAP L3)",
			"Horn (TOUCH, THEN TAP L3)",
			"(TOUCH, THEN TAP R3)",
			"~h~TOUCH, THEN TAP L3~w~ to activate the ~h~horn.",
		};
		static wchar translated[ARRAY_SIZE(keys)][64];
		static bool initialized;
		if(!initialized) {
			for(uint32 i = 0; i < ARRAY_SIZE(keys); i++) {
				const unsigned char *src = (const unsigned char*)texts[i];
				wchar *dst = translated[i];
				while(*src)
					*dst++ = *src++;
				*dst = 0;
			}
			initialized = true;
		}
		for(uint32 i = 0; i < ARRAY_SIZE(keys); i++)
			if(strcmp(key, keys[i]) == 0)
				return translated[i];
	}
#endif
#if defined (FIX_BUGS) || defined(FIX_BUGS_64)
	return keyArray.Search(key, data.chars);
#else
	return keyArray.Search(key);
#endif
}

wchar UpperCaseTable[128] = {
	128, 129, 130, 131, 132, 133, 134, 135, 136, 137, 138,
	139, 140, 141, 142, 143, 144, 145, 146, 147, 148, 149,
	150, 128, 129, 130, 131, 132, 133, 134, 135, 136, 137,
	138, 139, 140, 141, 142, 143, 144, 145, 146, 147, 148,
	149, 173, 173, 175, 176, 177, 178, 179, 180, 181, 182,
	183, 184, 185, 186, 187, 188, 189, 190, 191, 192, 193,
	194, 195, 196, 197, 198, 199, 200, 201, 202, 203, 204,
	205, 206, 207, 208, 209, 210, 211, 212, 213, 214, 215,
	216, 217, 218, 219, 220, 221, 222, 223, 224, 225, 226,
	227, 228, 229, 230, 231, 232, 233, 234, 235, 236, 237,
	238, 239, 240, 241, 242, 243, 244, 245, 246, 247, 248,
	249, 250, 251, 252, 253, 254, 255
};

wchar FrenchUpperCaseTable[128] = {
	128, 129, 130, 131, 132, 133, 134, 135, 136, 137, 138,
	139, 140, 141, 142, 143, 144, 145, 146, 147, 148, 149,
	150, 65, 65, 65, 65, 132, 133, 69, 69, 69, 69, 73, 73,
	73, 73, 79, 79, 79, 79, 85, 85, 85, 85, 173, 173, 175,
	176, 177, 178, 179, 180, 181, 182, 183, 184, 185, 186,
	187, 188, 189, 190, 191, 192, 193, 194, 195, 196, 197,
	198, 199, 200, 201, 202, 203, 204, 205, 206, 207, 208,
	209, 210, 211, 212, 213, 214, 215, 216, 217, 218, 219,
	220, 221, 222, 223, 224, 225, 226, 227, 228, 229, 230,
	231, 232, 233, 234, 235, 236, 237, 238, 239, 240, 241,
	242, 243, 244, 245, 246, 247, 248, 249, 250, 251, 252,
	253, 254, 255
};

wchar
CText::GetUpperCase(wchar c)
{
	switch (encoding)
	{
	case 'e':
		if (c >= 'a' && c <= 'z')
			return c - 32;
		break;
	case 'f':
		if (c >= 'a' && c <= 'z')
			return c - 32;

		if (c >= 128 && c <= 255)
			return FrenchUpperCaseTable[c-128];
		break;
	case 'g':
	case 'i':
	case 's':
		if (c >= 'a' && c <= 'z')
			return c - 32;

		if (c >= 128 && c <= 255)
			return UpperCaseTable[c-128];
		break;
	default:
		break;
	}
	return c;
}

void
CText::UpperCase(wchar *s)
{
	while(*s){
		*s = GetUpperCase(*s);
		s++;
	}
}


void
CKeyArray::Load(size_t length, uint8 *data, ssize_t *offset)
{
	size_t i;
	uint8 *rawbytes;

	// You can make numEntries size_t if you want to exceed 32-bit boundaries, everything else should be ready.
	numEntries = (int)(length / sizeof(CKeyEntry));
	entries = new CKeyEntry[numEntries];
	rawbytes = (uint8*)entries;

	for(i = 0; i < length; i++)
		rawbytes[i] = data[(*offset)++];
}

void
CKeyArray::Unload(void)
{
	delete[] entries;
	entries = nil;
	numEntries = 0;
}

void
CKeyArray::Update(wchar *chars)
{
#if !defined(FIX_BUGS) && !defined(FIX_BUGS_64)
	int i;
	for(i = 0; i < numEntries; i++)
		entries[i].value = (wchar*)((uint8*)chars + (uintptr)entries[i].value);
#endif
}

CKeyEntry*
CKeyArray::BinarySearch(const char *key, CKeyEntry *entries, int16 low, int16 high)
{
	int mid;
	int diff;

	if(low > high)
		return nil;

	mid = (low + high)/2;
	diff = strcmp(key, entries[mid].key);
	if(diff == 0)
		return &entries[mid];
	if(diff < 0)
		return BinarySearch(key, entries, low, mid-1);
	if(diff > 0)
		return BinarySearch(key, entries, mid+1, high);
	return nil;
}

wchar*
#if defined (FIX_BUGS) || defined(FIX_BUGS_64)
CKeyArray::Search(const char *key, wchar *data)
#else
CKeyArray::Search(const char *key)
#endif
{
	CKeyEntry *found;
	char errstr[25];
	int i;

#if defined (FIX_BUGS) || defined(FIX_BUGS_64)
	found = BinarySearch(key, entries, 0, numEntries-1);
	if(found)
		return (wchar*)((uint8*)data + found->valueOffset);
#else
	found = BinarySearch(key, entries, 0, numEntries-1);
	if(found)
		return found->value;
#endif
	sprintf(errstr, "%s missing", key);
	for(i = 0; i < 25; i++)
		WideErrorString[i] = errstr[i];
	return WideErrorString;
}


void
CData::Load(size_t length, uint8 *data, ssize_t *offset)
{
	size_t i;
	uint8 *rawbytes;

	// You can make numChars size_t if you want to exceed 32-bit boundaries, everything else should be ready.
	numChars = (int)(length / sizeof(wchar));
	chars = new wchar[numChars];
	rawbytes = (uint8*)chars;

	for(i = 0; i < length; i++)
		rawbytes[i] = data[(*offset)++];
}

void
CData::Unload(void)
{
	delete[] chars;
	chars = nil;
	numChars = 0;
}

char*
UnicodeToAscii(wchar *src)
{
	static char aStr[256];
	int len;
	for(len = 0; *src != '\0' && len < 256-1; len++, src++)
#ifdef MORE_LANGUAGES
		if(*src < 128 || ((CGame::russianGame || CGame::japaneseGame) && *src < 256))
#else
		if(*src < 128)
#endif
			aStr[len] = *src;
		else
			aStr[len] = '#';
	aStr[len] = '\0';
	return aStr;
}

char*
UnicodeToAsciiForSaveLoad(wchar *src)
{
	static char aStr[256];
	int len;
	for(len = 0; *src != '\0' && len < 256; len++, src++)
		if(*src < 256)
			aStr[len] = *src;
		else
			aStr[len] = '#';
	aStr[len] = '\0';
	return aStr;
}

char*
UnicodeToAsciiForMemoryCard(wchar *src)
{
	static char aStr[256];
	int len;
	for(len = 0; *src != '\0' && len < 256; len++, src++)
		if(*src < 256)
			aStr[len] = *src;
		else
			aStr[len] = '#';
	aStr[len] = '\0';
	return aStr;
}

void
TextCopy(wchar *dst, const wchar *src)
{
	while((*dst++ = *src++) != '\0');
}
