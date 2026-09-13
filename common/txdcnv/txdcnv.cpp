#include <assert.h>
#include <cstring>
#include <cstdio>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>
#include <fcntl.h>           /* Definition of AT_* constants */

#include "rw.h"
#include "Directory.h"
#include "src/3ds/tex/swizzle.h"
#include "src/3ds/tex/rg_etc1.h"

namespace rw {
namespace c3d {
extern int gTexQuality;
extern int gForceCompression;
extern int gForceMipmaps;
}
}

using namespace rw;

typedef Texture *(*TextureCallBack)(Texture *texture, void *pData);

const TexDictionary *
TexDictionaryForAllTextures(const TexDictionary * dict, TextureCallBack fpCallBack, void *pData)
{
	FORLIST(lnk, ((TexDictionary*)dict)->textures)
		if(fpCallBack(Texture::fromDict(lnk), pData) == nil)
			break;
	return dict;
}

bool
NativeTextureHackRead(Stream *stream, Texture **tex, int32_t size)
{
	*tex = Texture::streamReadNative(stream);
	(*tex)->raster = Raster::convertTexToCurrentPlatform((*tex)->raster);
	return *tex != nil;
}

Texture*
TextureGtaStreamRead(Stream *stream)
{
	uint32_t size, version;
	Texture *tex;

	if(!findChunk(stream, ID_TEXTURENATIVE, &size, &version))
		return nil;

	if(!NativeTextureHackRead(stream, &tex, size))
		return nil;

	return tex;
}

Texture*
destroyTexture(Texture *texture, void *data)
{
	texture->destroy();
	return NULL;
}

TexDictionary*
TexDictionaryGtaStreamRead(Stream *stream)
{
	int32_t numTextures;
	uint32_t size, version;
	TexDictionary *texDict;
	Texture *tex;

	if(!findChunk(stream, ID_STRUCT, &size, &version))
		return nil;
	
	if(stream->read8(&numTextures, size) != size)
		return nil;

	texDict = TexDictionary::create();
	if(texDict == nil)
		return nil;

	while(numTextures--){
		tex = TextureGtaStreamRead(stream);
		printf("[%s]\n", tex->name);
		if(tex == nil){
			TexDictionaryForAllTextures(texDict, destroyTexture, nil);
			texDict->destroy();
			return nil;
		}
		texDict->addFront(tex);
	}

	return texDict;
}

void
rewriteTexDictionary(Stream *in, Stream *out)
{
	TexDictionary *texDict = TexDictionaryGtaStreamRead(in);
	texDict->streamWrite(out);
	texDict->destroy();
}

void
rewriteStream(Stream *in, Stream *out)
{
	ChunkHeaderInfo header;
	readChunkHeaderInfo(in, &header);

	if(header.type == ID_TEXDICTIONARY){
		rewriteTexDictionary(in, out);
	}else{
		fprintf(stderr, "cannot rewrite\n");
		exit(1);
	}
}

void
rewriteTXD(char *src_dir, char *dst_dir, char *sub_dir, char *filename)
{
	StreamFile in, out;
	char src_path[128];
	char dst_path[128];

	sprintf(src_path, "%s/%s/%s", src_dir, sub_dir, filename);
	sprintf(dst_path, "%s/%s/%s", dst_dir, sub_dir, filename);

	if(!access(dst_path, F_OK)){
		fprintf(stderr, "[%s] exists. Delete if you want to rebuild.\n", dst_path);
		return 1;
	}
	
	printf("[TXD: %s]\n", dst_path);
	in.open(src_path, "r");
	out.open(dst_path, "w");

	rewriteStream(&in, &out);
}

int
rewriteIMG(char *src_dir, char *dst_dir, char *sub_dir, char *img_filename, char *dir_filename)
{
	StreamFile img_src, img_dst;
	CDirectory dir_src(4096), dir_dst(4096);
	char src_img_path[128];
	char src_dir_path[128];
	char dst_img_path[128];
	char dst_dir_path[128];
	ChunkHeaderInfo header;
	int dir_i;
	
	sprintf(src_img_path, "%s/%s/%s", src_dir, sub_dir, img_filename);
	sprintf(src_dir_path, "%s/%s/%s", src_dir, sub_dir, dir_filename);
	sprintf(dst_img_path, "%s/%s/%s", dst_dir, sub_dir, img_filename);
	sprintf(dst_dir_path, "%s/%s/%s", dst_dir, sub_dir, dir_filename);

	if(!access(dst_img_path, F_OK)){
		fprintf(stderr, "[%s] exists. Delete if you want to rebuild.\n", dst_img_path);
		return 1;
	}
	
	dir_src.ReadDirFile(src_dir_path);
	img_src.open(src_img_path, "r");
	img_dst.open(dst_img_path, "w");
	
	for(dir_i = 0; dir_i < dir_src.numEntries; dir_i++){
		CDirectory::DirectoryInfo *dir_entry = &dir_src.entries[dir_i];
		CDirectory::DirectoryInfo new_entry = *dir_entry;
		int size, sectors, padding, i;
		new_entry.offset = img_dst.tell() / 2048;
		
		img_src.seek(dir_entry->offset * 2048, SEEK_SET);
		readChunkHeaderInfo(&img_src, &header);
		img_src.seek(dir_entry->offset * 2048, SEEK_SET);

		if(header.type == ID_TEXDICTIONARY){
			rewriteStream(&img_src, &img_dst);
		}else{
			size_t size = dir_entry->size * 2048;
			uint8 *data = malloc(size);
			img_src.read8(data, size);
			img_dst.write8(data, size);
			free(data);			
		}

		size       = img_dst.tell() - new_entry.offset * 2048;
		sectors    = (size + 2047) / 2048;
		padding    = sectors * 2048 - size;
		new_entry.size = sectors;
		dir_dst.AddItem(new_entry);

		for(i = 0; i < padding; i++){
			img_dst.writeU8(0);
		}
	}

	img_src.close();
	img_dst.close();
	dir_dst.WriteDirFile(dst_dir_path);
	
	return 0;
}

int
prepareDirectoryGTAIII(char *out_dir)
{
	char path[128];

	sprintf(path, "%s/models", out_dir);
	mkdir(path, 0777);

	sprintf(path, "%s/txd", out_dir);
	mkdir(path, 0777);

	return 0;
}

void
rewriteNativeDataGTAIII(char *src_path, char *dst_path)
{
	//models
	rewriteIMG(src_path, dst_path, "models", "gta3.img", "gta3.dir");
	rewriteTXD(src_path, dst_path, "models", "fonts.txd");
	rewriteTXD(src_path, dst_path, "models", "frontend.txd");
	rewriteTXD(src_path, dst_path, "models", "generic.txd");
	rewriteTXD(src_path, dst_path, "models", "hud.txd");
	rewriteTXD(src_path, dst_path, "models", "menu.txd");
	rewriteTXD(src_path, dst_path, "models", "MISC.TXD");
	rewriteTXD(src_path, dst_path, "models", "particle.txd");

	//screens
	rewriteTXD(src_path, dst_path, "txd", "LOADSC0.TXD");
	rewriteTXD(src_path, dst_path, "txd", "LOADSC12.TXD");
	rewriteTXD(src_path, dst_path, "txd", "LOADSC16.TXD");
	rewriteTXD(src_path, dst_path, "txd", "LOADSC2.TXD");
	rewriteTXD(src_path, dst_path, "txd", "LOADSC23.TXD");
	rewriteTXD(src_path, dst_path, "txd", "LOADSC4.TXD");
	rewriteTXD(src_path, dst_path, "txd", "LOADSC8.TXD");
	rewriteTXD(src_path, dst_path, "txd", "SPLASH2.TXD");
	rewriteTXD(src_path, dst_path, "txd", "LOADSC1.TXD");
	rewriteTXD(src_path, dst_path, "txd", "LOADSC13.TXD");
	rewriteTXD(src_path, dst_path, "txd", "LOADSC17.TXD");
	rewriteTXD(src_path, dst_path, "txd", "LOADSC20.TXD");
	rewriteTXD(src_path, dst_path, "txd", "LOADSC24.TXD");
	rewriteTXD(src_path, dst_path, "txd", "LOADSC5.TXD");
	rewriteTXD(src_path, dst_path, "txd", "LOADSC9.TXD");
	rewriteTXD(src_path, dst_path, "txd", "SPLASH3.TXD");
	rewriteTXD(src_path, dst_path, "txd", "LOADSC10.TXD");
	rewriteTXD(src_path, dst_path, "txd", "LOADSC14.TXD");
	rewriteTXD(src_path, dst_path, "txd", "LOADSC18.TXD");
	rewriteTXD(src_path, dst_path, "txd", "LOADSC21.TXD");
	rewriteTXD(src_path, dst_path, "txd", "LOADSC25.TXD");
	rewriteTXD(src_path, dst_path, "txd", "LOADSC6.TXD");
	rewriteTXD(src_path, dst_path, "txd", "NEWS.TXD");
	rewriteTXD(src_path, dst_path, "txd", "mainsc1.txd");
	rewriteTXD(src_path, dst_path, "txd", "LOADSC11.TXD");
	rewriteTXD(src_path, dst_path, "txd", "LOADSC15.TXD");
	rewriteTXD(src_path, dst_path, "txd", "LOADSC19.TXD");
	rewriteTXD(src_path, dst_path, "txd", "LOADSC22.TXD");
	rewriteTXD(src_path, dst_path, "txd", "LOADSC3.TXD");
	rewriteTXD(src_path, dst_path, "txd", "LOADSC7.TXD");
	rewriteTXD(src_path, dst_path, "txd", "SPLASH1.TXD");
	rewriteTXD(src_path, dst_path, "txd", "mainsc2.txd");
}

bool
attachPlugins(void)
{
	return true;
}

bool
InitRW(void)
{
	if(!rw::Engine::init())
		return false;
	if(!attachPlugins())
		return false;
	if(!rw::Engine::open(NULL))
		return false;
	if(!rw::Engine::start())
		return false;
	rw::Image::setSearchPath("./");
	return true;
}

int
main(int argc, char *argv[])
{
	char *src_path, *dst_path;
	rw::platform = PLATFORM_3DS;
	c3d::gTexQuality = rg_etc1::cMediumQuality;
	c3d::gForceMipmaps = 1;
	c3d::gForceCompression = 1;
	InitRW();
	
	if(argc < 3){
		fprintf(stderr, "usage: %s [game-dir] [output-dir]\n", argv[0]);
		return 1;
	}

	src_path = argv[1];
	dst_path = argv[2];
	
	if(prepareDirectoryGTAIII(dst_path)){
		fprintf(stderr, "cannot create output directory.\n");
		return 1;
	}
	
	rewriteNativeDataGTAIII(src_path, dst_path);

	return 0;
}

// int
// old_main(int argc, char *argv[])
// {
// 	StreamFile in, out;

// 	rw::platform = PLATFORM_3DS;

// 	InitRW();
	
// 	if(argc < 3){
// 		printf("usage: %s [in] [out]\n", argv[0]);
// 		return 1;
// 	}
	
// 	in.open(argv[1], "r");
// 	out.open(argv[2], "w");
// 	rewriteStream(&in, &out);

// 	return 0;
// }
