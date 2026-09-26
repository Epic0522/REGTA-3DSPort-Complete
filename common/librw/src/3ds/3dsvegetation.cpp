#include <math.h>
#include <stdio.h>
#include <string.h>

#include "../rwbase.h"
#include "../rwplg.h"
#include "../rwrender.h"
#include "../rwengine.h"
#include "../rwpipeline.h"
#include "../rwobjects.h"

#include "rw3ds.h"

#ifdef RW_3DS
namespace rw
{
namespace c3d
{

struct Canopy {
	uint32 hash, vertices, triangles, mask, width, height;
	V3d centre;
	float span, rise;
	Texture *texture;
};
static Canopy canopies[64];
static uint32 canopyCount;
static bool cacheLoaded;
struct VegetationBlendState {
	Atomic *atomic;
	float blend;
	bool proxy, moving;
	uint32 stamp;
};
static VegetationBlendState vegetationBlendStates[256];
static uint32 vegetationBlendStamp;

static void
resetVegetationBlendStates(void)
{
	memset(vegetationBlendStates, 0, sizeof(vegetationBlendStates));
	vegetationBlendStamp = 0;
}

void
loadVegetationCache(void)
{
	if(cacheLoaded) return;
	cacheLoaded = true;
	FILE *f = fopen("models/vegetation.vgi", "rb");
	if(!f) return;
	char magic[4];
	uint32 count;
	if(fread(magic, 1, 4, f) != 4 || memcmp(magic, "VGI1", 4) || fread(&count, 4, 1, f) != 1 || count > 64) {
		fclose(f);
		return;
	}
	for(uint32 i = 0; i < count; ++i) {
		Canopy c = {};
		// Fixed little-endian on-disk record: 6 uint32s followed by 5 floats.
		uint32 fields[6];
		float shape[5];
		if(fread(fields, 4, 6, f) != 6 || fread(shape, 4, 5, f) != 5) break;
		if(fields[4] != 128 || fields[5] != 128 || fields[1] > 8192 || fields[2] > 16384) break;
		bool valid = shape[3] > .01f && shape[3] < 200.f && shape[4] > .01f && shape[4] < 200.f;
		for(int j = 0; j < 5; ++j) valid = valid && isfinite(shape[j]);
		if(!valid || linearSpaceFree() < (8u << 20)) {
			if(fseek(f, 128 * 128 * 4, SEEK_CUR)) break;
			continue;
		}
		Image *image = Image::create(128, 128, 32);
		image->allocate();
		if(fread(image->pixels, 1, 128 * 128 * 4, f) != 128 * 128 * 4) {
			image->destroy();
			break;
		}
		Raster *raster = Raster::create(128, 128, 16, Raster::TEXTURE | Raster::C1555);
		if(raster && raster->setFromImage(image)) {
			c.texture = Texture::create(raster);
			c.texture->setFilter(Texture::LINEAR);
			c.texture->setAddressU(Texture::CLAMP);
			c.texture->setAddressV(Texture::CLAMP);
			c.hash = fields[0];
			c.vertices = fields[1];
			c.triangles = fields[2];
			c.mask = fields[3];
			c.width = fields[4];
			c.height = fields[5];
			c.centre = {shape[0], shape[1], shape[2]};
			c.span = shape[3];
			c.rise = shape[4];
			canopies[canopyCount++] = c;
		} else if(raster)
			raster->destroy();
		image->destroy();
	}
	fclose(f);
}

void
closeVegetationCache(void)
{
	while(canopyCount) {
		Canopy &canopy = canopies[--canopyCount];
		Texture *texture = canopy.texture;
		canopy.texture = nil;
		if(texture) texture->destroy();
	}
	cacheLoaded = false;
	resetVegetationBlendStates();
}

static void
hashBytes(uint32 &hash, const void *data, size_t size)
{
	const uint8 *bytes = (const uint8 *)data;
	while(size--) hash = (hash ^ *bytes++) * 16777619u;
}

int32
findVegetationProxy(Geometry *geo)
{
	if(!cacheLoaded) return -2;
	if(!canopyCount || !geo->numTexCoordSets || !geo->texCoords[0] || geo->numMorphTargets != 1) return -1;
	bool possible = false;
	for(uint32 i = 0; i < canopyCount; ++i)
		if(canopies[i].vertices == (uint32)geo->numVertices && canopies[i].triangles == (uint32)geo->numTriangles) possible = true;
	if(!possible) return -1;
	uint32 hash = 2166136261u;
	hashBytes(hash, &geo->numVertices, 4);
	hashBytes(hash, &geo->numTriangles, 4);
	hashBytes(hash, geo->morphTargets[0].vertices, geo->numVertices * 12);
	hashBytes(hash, geo->texCoords[0], geo->numVertices * 8);
	for(int i = 0; i < geo->matList.numMaterials; ++i) {
		Texture *tex = geo->matList.materials[i]->texture;
		const char *p = tex ? tex->name : "";
		for(int j = 0; j < 32; ++j) {
			uint8 c = (uint8)p[j];
			if(c >= 'A' && c <= 'Z') c += 'a' - 'A';
			hashBytes(hash, &c, 1);
			if(!c) break;
		}
	}
	for(uint32 i = 0; i < canopyCount; ++i)
		if(canopies[i].hash == hash) return (int32)i;
	return -1;
}

static float vegetationLodDistance = 33.6f;
void
setVegetationLodDistance(float distance)
{
	// Both 2D presets restore the real canopy farther away; stereo keeps G.
	if(distance > 0.0f) vegetationLodDistance = distance * (stereoControlsActive() ? 1.0f : 1.5f);
}

float
vegetationBlend(Atomic *atomic, int32 proxy)
{
	if(proxy < 0 || (uint32)proxy >= canopyCount) return 0.f;
	const V3d &right = ((Camera *)engine->currentCamera)->getFrame()->getLTM()->right;
	if(right.x * right.x + right.y * right.y < .01f) return 0.f;
	const Canopy &c = canopies[proxy];
	Matrix *m = atomic->getFrame()->getLTM();
	V3d centre;
	V3d::transformPoints(&centre, &c.centre, 1, m);
	const V3d &eye = ((Camera *)engine->currentCamera)->getFrame()->getLTM()->pos;
	V3d offset = {centre.x - eye.x, centre.y - eye.y, centre.z - eye.z};
	const float distance = length(offset);
	VegetationBlendState *state = nil, *oldest = &vegetationBlendStates[0];
	for(uint32 i = 0; i < 256; ++i) {
		if(vegetationBlendStates[i].atomic == atomic) {
			state = &vegetationBlendStates[i];
			break;
		}
		if(!vegetationBlendStates[i].atomic) {
			oldest = &vegetationBlendStates[i];
			break;
		}
		if(vegetationBlendStates[i].stamp < oldest->stamp) oldest = &vegetationBlendStates[i];
	}
	if(!state) {
		state = oldest;
		state->atomic = atomic;
		state->proxy = distance >= vegetationLodDistance;
		state->blend = state->proxy ? 1.f : 0.f;
		state->moving = false;
	}
	state->stamp = ++vegetationBlendStamp;
	/* Distance starts a handoff but never holds it halfway. Finish the current
	 * direction first; hysteresis prevents rapid reversals at the boundary. */
	if(!state->moving) {
		if(!state->proxy && distance > vegetationLodDistance + 1.5f) {
			state->proxy = true;
			state->moving = true;
		} else if(state->proxy && distance < vegetationLodDistance - 1.5f) {
			state->proxy = false;
			state->moving = true;
		}
	}
	if(state->moving) {
		const float target = state->proxy ? 1.f : 0.f;
		const float step = .085f;
		if(state->blend < target)
			state->blend = fminf(target, state->blend + step);
		else
			state->blend = fmaxf(target, state->blend - step);
		if(state->blend == target) state->moving = false;
	}
	return state->blend;
}

bool
vegetationMaterial(Geometry *geo, int32 proxy, Material *material)
{
	if(proxy < 0 || (uint32)proxy >= canopyCount) return false;
	for(int i = 0; i < geo->matList.numMaterials && i < 32; ++i)
		if(geo->matList.materials[i] == material && (canopies[proxy].mask & (1u << i))) return true;
	return false;
}

void
renderVegetationProxy(Atomic *atomic, int32 proxy, float blend)
{
	if(blend <= 0.f) return;
	const Canopy &c = canopies[proxy];
	Matrix *original = atomic->getFrame()->getLTM();
	Matrix *camera = ((Camera *)engine->currentCamera)->getFrame()->getLTM();
	Matrix billboard;
	billboard.setIdentity();
	V3d::transformPoints(&billboard.pos, &c.centre, 1, original);
	float xy = sqrtf(camera->right.x * camera->right.x + camera->right.y * camera->right.y);
	if(xy < .001f) return;
	float sx = fmaxf(length(original->right), length(original->up));
	float sz = length(original->at);
	billboard.right = {camera->right.x / xy * c.span * .5f * sx, camera->right.y / xy * c.span * .5f * sx, 0.f};
	billboard.at = {0.f, 0.f, c.rise * .5f * sz};
	billboard.update();
	int alpha = 255;
	for(int i = 0; i < atomic->geometry->matList.numMaterials && i < 32; ++i)
		if(c.mask & (1u << i)) {
			int materialAlpha = atomic->geometry->matList.materials[i]->color.alpha;
			if(materialAlpha < alpha) alpha = materialAlpha;
		}
	// The proxy uses im3D, whose shader resets the mesh-only entity-alpha stage.
	// Carry the same distance/contact fade in the quad vertices, exactly once.
	const float entityOpacity = fminf(1.f, fmaxf(0.f, getEntityRenderStyle().opacity));
	const float proxyAlpha = alpha * blend * entityOpacity;
	RGBAf ambient = getCurrentAmbientLight();
	uint8 rgb[3];
	const float light[3] = {ambient.red, ambient.green, ambient.blue};
	for(int i = 0; i < 3; ++i) rgb[i] = (uint8)(fminf(1.f, fmaxf(.15f, light[i])) * 255.f);
	Im3DVertex vertices[4];
	const float x[4] = {-1, 1, 1, -1}, z[4] = {-1, -1, 1, 1};
	for(int i = 0; i < 4; ++i) {
		vertices[i].position = {x[i], 0, z[i]};
		vertices[i].setColor(rgb[0], rgb[1], rgb[2], (uint8)proxyAlpha);
		vertices[i].u = (x[i] + 1) * .5f;
		vertices[i].v = (1 - z[i]) * .5f;
	}
	uint16 indices[6] = {0, 1, 2, 0, 2, 3};
	const uint32 cull = GetRenderState(CULLMODE);
	const uint32 alphaFunc = GetRenderState(ALPHATESTFUNC);
	const uint32 alphaRef = GetRenderState(ALPHATESTREF);
	SetRenderState(CULLMODE, CULLNONE);
	SetRenderState(VERTEXALPHA, 1);
	SetRenderState(ALPHATESTFUNC, ALPHAGREATEREQUAL);
	SetRenderState(ALPHATESTREF, proxyAlpha < 255.f ? 3 : 128);
	setTexture(0, c.texture);
	// im3D submits the same world-space quad to both eye projections.
	im3d::Transform(vertices, 4, &billboard, im3d::EVERYTHING);
	im3d::RenderIndexedPrimitive(PRIMTYPETRILIST, indices, 6);
	im3d::End();
	SetRenderState(CULLMODE, cull);
	SetRenderState(ALPHATESTFUNC, alphaFunc);
	SetRenderState(ALPHATESTREF, alphaRef);
}

} // namespace c3d
} // namespace rw
#endif
