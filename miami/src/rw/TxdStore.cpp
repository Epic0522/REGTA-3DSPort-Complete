#include "common.h"

#include "templates.h"
#include "General.h"
#include "Streaming.h"
#include "RwHelper.h"
#include "TxdStore.h"
#include "ModelIndices.h"
#include "ModelInfo.h"

CPool<TxdDef,TxdDef> *CTxdStore::ms_pTxdPool;
RwTexDictionary *CTxdStore::ms_pStoredTxd;

#ifdef RW_3DS
static RwTexture*
ProtectPlayerTextureFromShrinking(RwTexture *texture, void*)
{
	if(texture && texture->raster)
		rw::c3d::TexSetShrinkProtected(GETC3DRASTEREXT(texture->raster));
	return texture;
}

static void
ProtectPlayerTxdFromShrinking(int slot)
{
	CBaseModelInfo *playerInfo = CModelInfo::GetModelInfo(MI_PLAYER);
	TxdDef *def = CTxdStore::GetSlot(slot);
	if(playerInfo && playerInfo->GetTxdSlot() == slot && def && def->texDict)
		RwTexDictionaryForAllTextures(def->texDict,
			ProtectPlayerTextureFromShrinking, nil);
}
#endif

void
CTxdStore::Initialise(void)
{
	if(ms_pTxdPool == nil)
		ms_pTxdPool = new CPool<TxdDef,TxdDef>(TXDSTORESIZE, "TexDictionary");
}

void
CTxdStore::Shutdown(void)
{
	if(ms_pTxdPool)
		delete ms_pTxdPool;
}

void
CTxdStore::GameShutdown(void)
{
	int i;

	for(i = 0; i < TXDSTORESIZE; i++){
		TxdDef *def = GetSlot(i);
		if(def && GetNumRefs(i) == 0)
			RemoveTxdSlot(i);
	}
}

int
CTxdStore::AddTxdSlot(const char *name)
{
	TxdDef *def = ms_pTxdPool->New();
	assert(def);
	def->texDict = nil;
	def->refCount = 0;
	strcpy(def->name, name);
	return ms_pTxdPool->GetJustIndex(def);
}

void
CTxdStore::RemoveTxdSlot(int slot)
{
	TxdDef *def = GetSlot(slot);
	if(def->texDict)
		RwTexDictionaryDestroy(def->texDict);
	ms_pTxdPool->Delete(def);
}

int
CTxdStore::FindTxdSlot(const char *name)
{
	int size = ms_pTxdPool->GetSize();
	for(int i = 0; i < size; i++){
		TxdDef *def = GetSlot(i);
		if(def && !CGeneral::faststricmp(def->name, name))
			return i;
	}
	return -1;
}

char*
CTxdStore::GetTxdName(int slot)
{
	return GetSlot(slot)->name;
}

void
CTxdStore::PushCurrentTxd(void)
{
	ms_pStoredTxd = RwTexDictionaryGetCurrent();
}

void
CTxdStore::PopCurrentTxd(void)
{
	RwTexDictionarySetCurrent(ms_pStoredTxd);
	ms_pStoredTxd = nil;
}

void
CTxdStore::SetCurrentTxd(int slot)
{
	RwTexDictionarySetCurrent(GetSlot(slot)->texDict);
}

void
CTxdStore::Create(int slot)
{
	GetSlot(slot)->texDict = RwTexDictionaryCreate();
}

int
CTxdStore::GetNumRefs(int slot)
{
	return GetSlot(slot)->refCount;
}

void
CTxdStore::AddRef(int slot)
{
	GetSlot(slot)->refCount++;
}

void
CTxdStore::RemoveRef(int slot)
{
	if(--GetSlot(slot)->refCount <= 0)
		CStreaming::RemoveTxd(slot);
}

void
CTxdStore::RemoveRefWithoutDelete(int slot)
{
	GetSlot(slot)->refCount--;
}

bool
CTxdStore::LoadTxd(int slot, RwStream *stream)
{
	TxdDef *def = GetSlot(slot);

	if(RwStreamFindChunk(stream, rwID_TEXDICTIONARY, nil, nil)){
		def->texDict = RwTexDictionaryGtaStreamRead(stream);
#ifdef RW_3DS
		ProtectPlayerTxdFromShrinking(slot);
#endif
		return def->texDict != nil;
	}
	printf("Failed to load TXD\n");
	return false;
}

bool
CTxdStore::LoadTxd(int slot, const char *filename)
{
	RwStream *stream;
	bool ret;

	ret = false;
	_rwD3D8TexDictionaryEnableRasterFormatConversion(true);
	do
		stream = RwStreamOpen(rwSTREAMFILENAME, rwSTREAMREAD, filename);
	while(stream == nil);
	ret = LoadTxd(slot, stream);
	RwStreamClose(stream, nil);
	return ret;
}

bool
CTxdStore::StartLoadTxd(int slot, RwStream *stream)
{
	TxdDef *def = GetSlot(slot);
	if(RwStreamFindChunk(stream, rwID_TEXDICTIONARY, nil, nil)){
		def->texDict = RwTexDictionaryGtaStreamRead1(stream);
#ifdef RW_3DS
		ProtectPlayerTxdFromShrinking(slot);
#endif
		return def->texDict != nil;
	}else{
		printf("Failed to load TXD\n");
		return false;
	}
}

bool
CTxdStore::FinishLoadTxd(int slot, RwStream *stream)
{
	TxdDef *def = GetSlot(slot);
	def->texDict = RwTexDictionaryGtaStreamRead2(stream, def->texDict);
#ifdef RW_3DS
	ProtectPlayerTxdFromShrinking(slot);
#endif
	return def->texDict != nil;
}

void
CTxdStore::RemoveTxd(int slot)
{
	TxdDef *def = GetSlot(slot);
	if(def->texDict)
		RwTexDictionaryDestroy(def->texDict);
	def->texDict = nil;
}
