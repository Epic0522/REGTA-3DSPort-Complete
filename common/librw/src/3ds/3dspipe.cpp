#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>

#include "../rwbase.h"
#include "../rwerror.h"
#include "../rwplg.h"
#include "../rwpipeline.h"
#include "../rwobjects.h"
#include "../rwengine.h"

#include "rw3ds.h"
#include "rw3dsshader.h"

namespace rw {
namespace c3d {

#ifdef RW_3DS

void
freeInstanceData(Geometry *geometry)
{
	uint32 i;
	
	if(geometry->instData == nil ||
	   geometry->instData->platform != PLATFORM_3DS)
		return;
	InstanceDataHeader *header = (InstanceDataHeader*)geometry->instData;
	geometry->instData = nil;

	InstanceData *inst = header->inst;
	for (i = 0; i < header->numMeshes; i++, inst++){
		linearFree(inst->indexBuffer);
	}

	linearFree(header->vertexBuffer);
	rwFree(header->inst);
	rwFree(header);
}

void*
destroyNativeData(void *object, int32, int32)
{
	freeInstanceData((Geometry*)object);
	return object;
}

static InstanceDataHeader*
instanceMesh(rw::ObjPipeline *rwpipe, Geometry *geo)
{
	InstanceDataHeader *header = rwNewT(InstanceDataHeader, 1, MEMDUR_EVENT | ID_GEOMETRY);
	MeshHeader *meshh = geo->meshHeader;
	geo->instData = header;
	header->platform = PLATFORM_3DS;

	header->serialNumber = meshh->serialNum;
	header->numMeshes = meshh->numMeshes;
	header->primType = meshh->flags == 1 ? GPU_TRIANGLE_STRIP : GPU_TRIANGLES;
	header->totalNumVertex = geo->numVertices;
	header->totalNumIndex = meshh->totalIndices;
	header->inst = rwNewT(InstanceData, header->numMeshes, MEMDUR_EVENT | ID_GEOMETRY);

	InstanceData *inst = header->inst;
	Mesh *mesh = meshh->getMeshes();
	for(uint32 i = 0; i < header->numMeshes; i++){
		findMinVertAndNumVertices(mesh->indices,
					  mesh->numIndices,
		                          &inst->minVert,
					  &inst->numVertices);
		assert(inst->minVert != 0xFFFFFFFF);
		inst->numIndex = mesh->numIndices;
		inst->material = mesh->material;
		inst->vertexAlpha = 0;
		inst->program = 0;
		inst->indexBuffer = (uint16*)safeLinearAlloc(inst->numIndex * 2);
		assert(inst->indexBuffer);
		memcpy(inst->indexBuffer, mesh->indices, inst->numIndex*2);
		mesh++;
		inst++;
	}

	header->vertexBuffer = nil;
	header->numAttribs = 0;

	return header;
}

static void
instance(rw::ObjPipeline *rwpipe, Atomic *atomic)
{
	ObjPipeline *pipe = (ObjPipeline*)rwpipe;
	Geometry *geo = atomic->geometry;
	// don't try to (re)instance native data
	if(geo->flags & Geometry::NATIVE)
		return;

	InstanceDataHeader *header = (InstanceDataHeader*)geo->instData;
	if(geo->instData){
		// Already have instanced data, so check if we have to reinstance
		assert(header->platform == PLATFORM_3DS);
		if(header->serialNumber != geo->meshHeader->serialNum){
			// Mesh changed, so reinstance everything
			freeInstanceData(geo);
		}
	}

	// no instance or complete reinstance
	if(geo->instData == nil){
		geo->instData = instanceMesh(rwpipe, geo);
		pipe->instanceCB(geo, (InstanceDataHeader*)geo->instData, 0);
	}else if(geo->lockedSinceInst)
		pipe->instanceCB(geo, (InstanceDataHeader*)geo->instData, 1);

	geo->lockedSinceInst = 0;
}

static void
uninstance(rw::ObjPipeline *rwpipe, Atomic *atomic)
{
	assert(0 && "can't uninstance");
}

static void
render(rw::ObjPipeline *rwpipe, Atomic *atomic)
{
	ObjPipeline *pipe = (ObjPipeline*)rwpipe;
	Geometry *geo = atomic->geometry;
	pipe->instance(atomic);
	assert(geo->instData != nil);
	assert(geo->instData->platform == PLATFORM_3DS);
	if(pipe->renderCB)
		pipe->renderCB(atomic, (InstanceDataHeader*)geo->instData);
}

void
ObjPipeline::init(void)
{
	this->rw::ObjPipeline::init(PLATFORM_3DS);
	this->impl.instance = c3d::instance;
	this->impl.uninstance = c3d::uninstance;
	this->impl.render = c3d::render;
	this->instanceCB = nil;
	this->uninstanceCB = nil;
	this->renderCB = nil;
}

ObjPipeline*
ObjPipeline::create(void)
{
	ObjPipeline *pipe = rwNewT(ObjPipeline, 1, MEMDUR_GLOBAL);
	pipe->init();
	return pipe;
}

void
defaultInstanceCB(Geometry *geo, InstanceDataHeader *header, bool32 reinstance)
{
	AttribDesc *attribs = header->attribDesc;
	bool isPrelit = !!(geo->flags & Geometry::PRELIT);
	bool hasNormals = !!(geo->flags & Geometry::NORMALS);
	uint8 *verts, offset;
	
	if(!reinstance){
		/* Create attribute descriptions */
		memset(attribs, 0, sizeof(header->attribDesc));
		header->numAttribs = 0;
		header->stride = 0;

#define ATTRIB(_REG, _COUNT, _GPU_TYPE, _NAT_TYPE)			\
		{							\
 			attribs[_REG].index  = header->numAttribs;	\
 			attribs[_REG].offset = header->stride;		\
			attribs[_REG].count  = _COUNT;			\
 			attribs[_REG].type   = _GPU_TYPE;		\
 			header->stride += sizeof(_NAT_TYPE) * _COUNT;	\
 			header->numAttribs++;				\
 		}
		
		// Positions
		ATTRIB(ATTRIB_POS, 3, GPU_FLOAT, float32);

		// Normals
		if(hasNormals){
			ATTRIB(ATTRIB_NORMAL, 4, GPU_BYTE, int8);
		}

		// Prelighting
		if(isPrelit){
			ATTRIB(ATTRIB_COLOR, 4, GPU_UNSIGNED_BYTE, uint8);
		}

		// Texture coordinates
		for(int32 n = 0; n < geo->numTexCoordSets; n++){
			ATTRIB(ATTRIB_TEXCOORDS0+n, 2, GPU_FLOAT, float32);
		}

#undef ATTRIB		

		/* Allocate vertex buffer */
		header->vertexBuffer = (uint8*)safeLinearAlloc(header->totalNumVertex * header->stride);
		assert(header->vertexBuffer);
	}

	/* Fill vertex buffer */
	verts = header->vertexBuffer;

	// Positions
	if(!reinstance || geo->lockedSinceInst&Geometry::LOCKVERTICES){
		offset = attribs[ATTRIB_POS].offset;
		instV3d(VERT_FLOAT3,
			verts + offset,
			geo->morphTargets[0].vertices,
			header->totalNumVertex,
			header->stride);
	}

	// Normals
	if(hasNormals && (!reinstance || geo->lockedSinceInst&Geometry::LOCKNORMALS)){
		offset = attribs[ATTRIB_NORMAL].offset;
		instV3d(VERT_COMPNORM2,
			verts + offset,
			geo->morphTargets[0].normals,
			header->totalNumVertex,
			header->stride);
	}

	// Prelighting
	if(isPrelit && (!reinstance || geo->lockedSinceInst&Geometry::LOCKPRELIGHT)){
		offset = attribs[ATTRIB_COLOR].offset;
		int n = header->numMeshes;
		InstanceData *inst = header->inst;
		while(n--){
			assert(inst->minVert != 0xFFFFFFFF);
			inst->vertexAlpha =
			  instColor(VERT_RGBA,
				    verts + offset + header->stride * inst->minVert,
				    geo->colors + inst->minVert,
				    inst->numVertices,
				    header->stride);
			inst++;
		}
	}

	// Texture coordinates
	for(int32 n = 0; n < geo->numTexCoordSets; n++){
		if(!reinstance || geo->lockedSinceInst&(Geometry::LOCKTEXCOORDS<<n)){
			offset = attribs[ATTRIB_TEXCOORDS0+n].offset;
			instTexCoords(VERT_FLOAT2,
				      verts + offset,
				      geo->texCoords[n],
				      header->totalNumVertex,
				      header->stride);
		}
	}

	genAttribPointers(header);
}

void
defaultUninstanceCB(Geometry *geo, InstanceDataHeader *header)
{
	assert(0 && "can't uninstance");
}

ObjPipeline*
makeDefaultPipeline(void)
{
	ObjPipeline *pipe = ObjPipeline::create();
	pipe->instanceCB = defaultInstanceCB;
	pipe->uninstanceCB = defaultUninstanceCB;
	pipe->renderCB = defaultRenderCB;
	return pipe;
}

#else
void *destroyNativeData(void *object, int32, int32) { return object; }
#endif

}
}
