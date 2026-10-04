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

#ifdef RW_3DS

#include "rw3ds.h"
#include "rw3dsshader.h"
#include "default_shbin.h"
#include "ps2fog_shbin.h"

namespace rw {
namespace c3d {

Shader *currentShader = NULL;

DVLB_s* Shader::dvlb = NULL;
DVLB_s* Shader::fogDvlb = NULL;
	
void
Shader::loadDVLB(u8* shbinData, u32 shbinSize)
{
	Shader::dvlb = DVLB_ParseFile((u32*)shbinData, shbinSize);
	Shader::fogDvlb = DVLB_ParseFile((u32*)ps2fog_shbin, ps2fog_shbin_size);
}
  
Shader*
Shader::create(u32 prgId, void (*combiner)(void), bool usesLighting)
{
	int i;
	Shader *sh = rwNewT(Shader, 1, MEMDUR_EVENT | ID_DRIVER);

	sh->combiner = combiner;
	sh->usesLighting = usesLighting;
	shaderProgramInit(&sh->vsh_program);
	shaderProgramSetVsh(&sh->vsh_program, &Shader::dvlb->DVLE[prgId]);
	int fogId = -1;
	if(prgId == VSH_PRG_DEFAULT) fogId = VSH_PRG_DEFAULTFOG;
	else if(prgId == VSH_PRG_MATFX) fogId = VSH_PRG_MATFXFOG;
	else if(prgId == VSH_PRG_MATFXBASE) fogId = VSH_PRG_MATFXBASEFOG;
	else if(prgId == VSH_PRG_IM3D) fogId = VSH_PRG_IM3DFOG;
	sh->hasFogProgram = fogId >= 0 && Shader::fogDvlb &&
		(uint32)fogId < Shader::fogDvlb->numDVLE;
	sh->fogSelected = false;
	if(sh->hasFogProgram){
		shaderProgramInit(&sh->fog_program);
		shaderProgramSetVsh(&sh->fog_program, &Shader::fogDvlb->DVLE[fogId]);
	}
	return sh;
}

void
Shader::use(void)
{
	if(currentShader != this) {
		C3D_BindProgram(this->fogSelected ? &this->fog_program : &this->vsh_program);
		// Rebuild the same complete chain, but don't resend unchanged stages.
		// Pending full invalidations, including HOME restore, remain intact.
		C3D_ConfigureTexEnv(this->combiner);
		// The complete-chain reset also replaces the final fog/opacity stage,
		// even when a shader is selected again without an intervening draw.
		invalidateFinalRenderStageCache();
		currentShader = this;
	}
}

void
Shader::selectFog(bool enabled)
{
	enabled = enabled && this->hasFogProgram;
	if(this->fogSelected == enabled) return;
	this->fogSelected = enabled;
	C3D_BindProgram(enabled ? &this->fog_program : &this->vsh_program);
}

void
Shader::destroy(void)
{
  shaderProgramFree(&this->vsh_program);
  if(this->hasFogProgram) shaderProgramFree(&this->fog_program);
  // DVLB_Free(this->vsh_dvlb);
  rwFree(this);
}

void
combiner_simple()
{
	C3D_TexEnv *env0 = C3D_GetTexEnv(0);
	C3D_TexEnv *env1 = C3D_GetTexEnv(1);
	C3D_TexEnvInit(env0);
	C3D_TexEnvInit(env1);
	C3D_TexEnvSrc(env0, C3D_Both, GPU_TEXTURE0, GPU_PRIMARY_COLOR, 0);
	C3D_TexEnvFunc(env0, C3D_Both, GPU_MODULATE);
}

void
combiner_matfx()
{
	C3D_TexEnv *env0 = C3D_GetTexEnv(0);
	C3D_TexEnv *env1 = C3D_GetTexEnv(1);
	C3D_TexEnvInit(env0);
	C3D_TexEnvInit(env1);

	// I really gotta rack my brain to improve this
	// but fuck it, it looks shiny, and according to
	// the mobile ports, shiny is good.
	
	/* stage 1: keep the base texture independent of the unreliable 3DS
	 * per-vertex lighting colour.  Environment reflection is added below. */
	C3D_TexEnvFunc(env0, C3D_RGB, GPU_MODULATE);
	C3D_TexEnvSrc(env0, C3D_RGB, GPU_TEXTURE0, GPU_CONSTANT);
	/* Keep material/vertex alpha for glass and translucent lamp covers while
	 * continuing to bypass PRIMARY_COLOR.rgb. */
	C3D_TexEnvFunc(env0, C3D_Alpha, GPU_MODULATE);
	C3D_TexEnvSrc(env0, C3D_Alpha, GPU_TEXTURE0, GPU_PRIMARY_COLOR);

	// /* stage 2: */
	C3D_TexEnvFunc(env1, C3D_Alpha, GPU_REPLACE);
	C3D_TexEnvSrc(env1,  C3D_Alpha, GPU_PREVIOUS);
	
	/* base + environment * coefficient.  The old ADD_MULTIPLY path ignored
	 * MatFX shininess and could blow a whole vehicle face out to white. */
	C3D_TexEnvFunc(env1, C3D_RGB, GPU_MULTIPLY_ADD);
	C3D_TexEnvSrc(env1,  C3D_RGB, GPU_TEXTURE1, GPU_CONSTANT, GPU_PREVIOUS);
	C3D_TexEnvOpRgb(env1, GPU_TEVOP_RGB_SRC_COLOR, GPU_TEVOP_RGB_SRC_COLOR,
	               GPU_TEVOP_RGB_SRC_COLOR);
}

  // ORIGINAL GLSL
  //    vec4 pass1 = v_color;
  // 	vec4 envColor = max(pass1, u_colorClamp);
  // 	pass1 *= texture(tex0, vec2(v_tex0.x, 1.0-v_tex0.y));

  // 	vec4 pass2 = envColor*shininess*texture(tex1, vec2(v_tex1.x, 1.0-v_tex1.y));

  // 	pass1.rgb = mix(u_fogColor.rgb, pass1.rgb, v_fog);
  // 	pass2.rgb = mix(vec3(0.0, 0.0, 0.0), pass2.rgb, v_fog);

  // 	float fba = max(pass1.a, disableFBA);
  // 	vec4 color;
  // 	color.rgb = pass1.rgb*pass1.a + pass2.rgb*fba;
  // 	color.a = pass1.a;

  // 	DoAlphaTest(color.a);

  // 	FRAGCOLOR(color);

  // DUMBED DOWN GLSL
  // 	vec4 pass1 = v_color * texture(tex0, vec2(v_tex0.x, 1.0-v_tex0.y));
  // 	vec4 pass2 = texture(tex1, vec2(v_tex1.x, 1.0-v_tex1.y));
  // 	color.rgb = (pass1.rgb + pass2.rgb)*pass1.a
  // 	color.a = pass1.a;
  
}
}

#endif
