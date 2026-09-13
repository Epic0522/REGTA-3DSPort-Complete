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

namespace rw {
namespace c3d {

Shader *currentShader = NULL;

DVLB_s* Shader::dvlb = NULL;
	
void
Shader::loadDVLB(u8* shbinData, u32 shbinSize)
{
	Shader::dvlb = DVLB_ParseFile((u32*)shbinData, shbinSize);
}
  
Shader*
Shader::create(u32 prgId, void (*combiner)(void))
{
	int i;
	Shader *sh = rwNewT(Shader, 1, MEMDUR_EVENT | ID_DRIVER);

	sh->combiner = combiner;
	shaderProgramInit(&sh->vsh_program);
	shaderProgramSetVsh(&sh->vsh_program, &Shader::dvlb->DVLE[prgId]);

	return sh;
}

void
Shader::use(void)
{
	if(currentShader != this) {
		C3D_BindProgram(&this->vsh_program);
		this->combiner();
		currentShader = this;
	}
}

void
Shader::destroy(void)
{
  shaderProgramFree(&this->vsh_program);
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
	
	/* stage 1: */
	C3D_TexEnvFunc(env0, C3D_Both, GPU_MODULATE);
	C3D_TexEnvSrc(env0, C3D_Both, GPU_TEXTURE0, GPU_PRIMARY_COLOR, 0);

	// /* stage 2: */
	C3D_TexEnvFunc(env1, C3D_Alpha, GPU_REPLACE);
	C3D_TexEnvSrc(env1,  C3D_Alpha, GPU_PREVIOUS);
	
	C3D_TexEnvFunc(env1, C3D_RGB, GPU_ADD_MULTIPLY);
	C3D_TexEnvSrc(env1,  C3D_RGB, GPU_TEXTURE1,    GPU_PREVIOUS,            GPU_PREVIOUS);
	C3D_TexEnvOpRgb(env1, GPU_TEVOP_RGB_SRC_COLOR, GPU_TEVOP_RGB_SRC_COLOR, GPU_TEVOP_RGB_SRC_ALPHA);
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
