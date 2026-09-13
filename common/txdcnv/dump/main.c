#include <assert.h>
#include <stdbool.h>
#include <errno.h>
#include <sys/types.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <stdio.h>
#include <string.h>
#include <stdint.h>

/* enum RwCorePluginID */
/* { */
/* 	rwID_NAOBJECT               = 0x00, */
/* 	rwID_STRUCT                 = 0x01, */
/* 	rwID_STRING                 = 0x02, */
/* 	rwID_EXTENSION              = 0x03, */
/* 	rwID_CAMERA                 = 0x05, */
/* 	rwID_TEXTURE                = 0x06, */
/* 	rwID_MATERIAL               = 0x07, */
/* 	rwID_MATLIST                = 0x08, */
/* 	rwID_ATOMICSECT             = 0x09, */
/* 	rwID_PLANESECT              = 0x0A, */
/* 	rwID_WORLD                  = 0x0B, */
/* 	rwID_SPLINE                 = 0x0C, */
/* 	rwID_MATRIX                 = 0x0D, */
/* 	rwID_FRAMELIST              = 0x0E, */
/* 	rwID_GEOMETRY               = 0x0F, */
/* 	rwID_CLUMP                  = 0x10, */
/* 	rwID_LIGHT                  = 0x12, */
/* 	rwID_UNICODESTRING          = 0x13, */
/* 	rwID_ATOMIC                 = 0x14, */
/* 	rwID_TEXTURENATIVE          = 0x15, */
/* 	rwID_TEXDICTIONARY          = 0x16, */
/* 	rwID_ANIMDATABASE           = 0x17, */
/* 	rwID_IMAGE                  = 0x18, */
/* 	rwID_SKINANIMATION          = 0x19, */
/* 	rwID_GEOMETRYLIST           = 0x1A, */
/* 	rwID_HANIMANIMATION         = 0x1B, */
/* 	rwID_TEAM                   = 0x1C, */
/* 	rwID_CROWD                  = 0x1D, */
/* 	rwID_DMORPHANIMATION        = 0x1E, */
/* 	rwID_RIGHTTORENDER          = 0x1f, */
/* 	rwID_MTEFFECTNATIVE         = 0x20, */
/* 	rwID_MTEFFECTDICT           = 0x21, */
/* 	rwID_TEAMDICTIONARY         = 0x22, */
/* 	rwID_PITEXDICTIONARY        = 0x23, */
/* 	rwID_TOC                    = 0x24, */
/* 	rwID_PRTSTDGLOBALDATA       = 0x25, */
/* 	/\* Insert before MAX and increment MAX *\/ */
/* 	rwID_COREPLUGINIDMAX        = 0x26, */
/* }; */

/* typedef struct ChunkHeaderInfo_t */
/* { */
/* 	uint32_t type; */
/* 	uint32_t length; */
/* 	uint32_t version, build; */
/* } ChunkHeaderInfo; */

/* int */
/* libraryIDUnpackVersion(uint32_t libid) */
/* { */
/* 	if(libid & 0xFFFF0000) */
/* 		return ((libid>>14 & 0x3FF00) + 0x30000) | */
/* 		       (libid>>16 & 0x3F); */
/* 	else */
/* 		return libid<<8; */
/* } */

/* int */
/* libraryIDUnpackBuild(uint32_t libid) */
/* { */
/* 	if(libid & 0xFFFF0000) */
/* 		return libid & 0xFFFF; */
/* 	else */
/* 		return 0; */
/* } */

/* int */
/* readChunkHeaderInfo(FILE *fp, ChunkHeaderInfo *header) */
/* { */
/* 	struct { */
/* 		int32_t type, size; */
/* 		uint32_t id; */
/* 	} buf; */
/* 	fread(&buf, 12, 1, fp); */
/* 	if(feof(fp)){ */
/* 		return false; */
/* 	} */
/* 	assert(header != NULL); */
/* 	header->type = buf.type; */
/* 	header->length = buf.size; */
/* 	header->version = libraryIDUnpackVersion(buf.id); */
/* 	header->build = libraryIDUnpackBuild(buf.id); */
/* 	return true; */
/* } */

/* bool */
/* findChunk(FILE *fp, uint32_t type, uint32_t *length, uint32_t *version) */
/* { */
/* 	ChunkHeaderInfo header; */
/* 	while(readChunkHeaderInfo(fp, &header)){ */
/* 		if(header.type == rwID_NAOBJECT) */
/* 			return false; */
/* 		if(header.type == type){ */
/* 			if(length) */
/* 				*length = header.length; */
/* 			if(version) */
/* 				*version = header.version; */
/* 			return true; */
/* 		} */
/* 		fseek(fp, header.length, SEEK_CUR); */
/* 	} */
/* 	return false; */
/* } */

/* int */
/* readTexture(FILE *fp) */
/* { */
/* 	uint32_t size, version; */

/* 	if(!RwStreamFindChunk(stream, rwID_TEXTURENATIVE, &size, &version)) */
/* 		return nil; */

/* 	if(!READNATIVE(stream, &tex, size)) */
/* 		return nil; */

/* 	return tex; */
/* } */


/* int */
/* readTXD(FILE *fp) */
/* { */
/* 	uint32_t size, version; */
/* 	int32_t numTextures, ti; */

/* 	if(!findChunk(fp, rwID_STRUCT, &size, &version) || */
/* 	   !fread(&numTextures, sizeof(int32_t), 1, fp)){ */
/* 		return 0; */
/* 	} */

/* 	for(ti = 0; ti < numTextures; ti++){ */
		
/* 	} */
/* } */

int
extractIMG(char *dir_path, char *img_path, char *out_path)
{
	int status = 0;
	char path[128];
	FILE *dir_fp, *out_fp;
	int img_fd;
	size_t img_size;
	uint8_t *img_data = NULL;
	
	struct
	{
		uint32_t offset;
		uint32_t size;
		char name[24];
	} entry;

	dir_fp = fopen(dir_path, "rb");
	img_fd = open(img_path, 0);

	if(!dir_fp || !img_fd){
		goto die;
	}

	mkdir(out_path, 00700);

	img_size = lseek(img_fd, 0, SEEK_END);
	lseek(img_fd, 0, SEEK_SET); /* do I need this? */
	img_data = mmap(NULL, img_size, PROT_READ, MAP_PRIVATE, img_fd, 0);

	if(!img_data){
		goto die;
	}

	while(!feof(dir_fp)){
		if(!fread(&entry, sizeof(entry), 1, dir_fp)){
			goto die;
		}

		printf("%s %x %x\n", entry.name, entry.offset, entry.size);
		
		snprintf(path, sizeof(path), "%s/%s", out_path, entry.name);
		out_fp = fopen(path, "w");

		if(!out_fp){
			fprintf(stderr, "failed to open %s for writing\n", path);
			goto die;
		}

		if(entry.size &&
		   fwrite(img_data + entry.offset * 2048,
			  2048,
			  entry.size,
			  out_fp) != entry.size){
			fprintf(stderr, "failed to write to %s [%s] \n", path, strerror(errno));
			goto die;
		}

		fclose(out_fp);
		out_fp = NULL;
	}

	status = 1;
die:
	if(img_data){
		munmap(img_data, img_size);
	}
	if(out_fp){
		fclose(out_fp);
	}
	if(dir_fp){
		fclose(dir_fp);
	}
	if(img_fd){
		close(img_fd);
	}
	
	return status;	
}

int
main(int argc, char *argv[])
{
	if(argc < 4){
		return 1;
	}
	extractIMG(argv[1], argv[2], argv[3]);
	return 0;
}
