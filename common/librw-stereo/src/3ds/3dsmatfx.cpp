#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>

#include "../rwbase.h"
#include "../rwerror.h"
#include "../rwplg.h"
#include "../rwrender.h"
#include "../rwengine.h"
#include "../rwpipeline.h"
#include "../rwobjects.h"
#include "../rwanim.h"
#include "../rwplugins.h"

#include "rw3ds.h"
#include "rw3dsplg.h"
#include "rw3dsimpl.h"
#include "rw3dsshader.h"

namespace rw {
namespace c3d {

#ifdef RW_3DS

#include "default_shbin.h"

#define U(x) (VSH_FVEC_##x)

static Shader *envShader;

void
matfxDefaultRender(InstanceDataHeader *header, InstanceData *inst, uint32 flags)
{
	Material *m = inst->material;
	
	defaultShader->use();
	setMaterial(flags, m->color, m->surfaceProps);
	setTexture(0, m->texture);
	rw::SetRenderState(VERTEXALPHA, inst->vertexAlpha || m->color.alpha != 0xFF);
	
	drawInst(header, inst);
}

static Frame *lastEnvFrame;

static RawMatrix normal2texcoord = {
	{ 0.5f,  0.0f, 0.0f }, 0.0f,
	{ 0.0f, -0.5f, 0.0f }, 0.0f,
	{ 0.0f,  0.0f, 1.0f }, 0.0f,
	{ 0.5f,  0.5f, 0.0f }, 1.0f
};

void
uploadEnvMatrix(Frame *frame)
{
	Matrix invMat;
	if(frame == nil)
		frame = engine->currentCamera->getFrame();

	// cache the matrix across multiple meshes
	static RawMatrix envMtx;
	RawMatrix invMtx;
	C3D_Mtx mtx;
	
	Matrix::invert(&invMat, frame->getLTM());
	convMatrix(&invMtx, &invMat);
	invMtx.pos.set(0.0f, 0.0f, 0.0f);
	RawMatrix::mult(&envMtx, &invMtx, &normal2texcoord);

	mtx.r[0].x =  invMtx.right.x; /*code monkey garbage*/
	mtx.r[1].x =  invMtx.right.y; /*god damn it I suck*/
	mtx.r[2].x =  invMtx.right.z; /*why not just write a function that takes RawMatrix */
	mtx.r[3].x =  invMtx.rightw;  /*and transpose it*/
	mtx.r[0].y =  invMtx.up.x;
	mtx.r[1].y =  invMtx.up.y;
	mtx.r[2].y =  invMtx.up.z;
	mtx.r[3].y =  invMtx.upw;
	mtx.r[0].z =  invMtx.at.x;
	mtx.r[1].z =  invMtx.at.y;
	mtx.r[2].z =  invMtx.at.z;
	mtx.r[3].z =  invMtx.atw;
	mtx.r[0].w =  invMtx.pos.x;
	mtx.r[1].w =  invMtx.pos.y;
	mtx.r[2].w =  invMtx.pos.z;
	mtx.r[3].w =  invMtx.posw;
	
	c3dUniformMatrix4fv(U(u_texMatrix), 1, 0, &mtx);
}

void
matfxEnvRender(InstanceDataHeader *header, InstanceData *inst, uint32 flags, MatFX::Env *env)
{
	Material *m;
	m = inst->material;

	if(env->tex == nil || env->coefficient == 0.0f){
		matfxDefaultRender(header, inst, flags);
		return;
	}

	envShader->use();

	setTexture(0, m->texture);
	setTexture(1, env->tex);
	uploadEnvMatrix(env->frame);

	setMaterial(flags, m->color, m->surfaceProps);

	// float fxparams[2];
	// fxparams[0] = env->coefficient;
	// fxparams[1] = env->fbAlpha ? 0.0f : 1.0f;

	// c3dUniform2fv(U(u_fxparams), 1, fxparams);
	// static float zero[4];
	// static float one[4] = { 1.0f, 1.0f, 1.0f, 1.0f };
	// This clamps the vertex color below. With it we can achieve both PC and PS2 style matfx
	// if(MatFX::modulateEnvMap)
	// 	c3dUniform4fv(U(u_colorClamp), 1, zero);
	// else
	// 	c3dUniform4fv(U(u_colorClamp), 1, one);

	rw::SetRenderState(VERTEXALPHA, 1);
	rw::SetRenderState(SRCBLEND, BLENDONE);
	drawInst(header, inst);
	rw::SetRenderState(SRCBLEND, BLENDSRCALPHA);
}

void
matfxRenderCB(Atomic *atomic, InstanceDataHeader *header)
{
	uint32 flags = atomic->geometry->flags;
	setWorldMatrix(atomic->getFrame()->getLTM());
	lightingCB(atomic);

	setAttribPointers(header);
	lastEnvFrame = nil;

	InstanceData *inst = header->inst;
	int32 n = header->numMeshes;

	while(n--){
		MatFX *matfx = MatFX::get(inst->material);

		if(matfx == nil)
			matfxDefaultRender(header, inst, flags);
		else switch(matfx->type){
		case MatFX::ENVMAP:
			matfxEnvRender(header, inst, flags, &matfx->fx[0].env);
			break;
		default:
			matfxDefaultRender(header, inst, flags);
			break;
		}
		inst++;
	}
}

ObjPipeline*
makeMatFXPipeline(void)
{
	ObjPipeline *pipe = ObjPipeline::create();
	pipe->instanceCB = defaultInstanceCB;
	pipe->uninstanceCB = defaultUninstanceCB;
	pipe->renderCB = matfxRenderCB;
	pipe->pluginID = ID_MATFX;
	pipe->pluginData = 0;
	return pipe;
}

static void*
matfxOpen(void *o, int32, int32)
{
	matFXGlobals.pipelines[PLATFORM_3DS] = makeMatFXPipeline();
	envShader = Shader::create(VSH_PRG_MATFX, combiner_matfx);
	assert(envShader);
	return o;
}

static void*
matfxClose(void *o, int32, int32)
{
	((ObjPipeline*)matFXGlobals.pipelines[PLATFORM_3DS])->destroy();
	matFXGlobals.pipelines[PLATFORM_3DS] = nil;
	envShader->destroy();
	envShader = nil;
	return o;
}

void
initMatFX(void)
{
	Driver::registerPlugin(PLATFORM_3DS, 0, ID_MATFX,
			       matfxOpen, matfxClose);
}

#else

void initMatFX(void) { }

#endif

}
}

