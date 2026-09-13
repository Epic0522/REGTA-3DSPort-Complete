#ifdef RW_3DS

namespace rw {
namespace c3d {

// enum {
//       MAX_UNIFORMS = 40,
// };

// struct UniformRegistry
// {
// 	int32 numUniforms;
// 	char *uniformNames[MAX_UNIFORMS];
// };

// int32 registerUniform(const char *name);
// int32 findUniform(const char *name);
// int32 registerBlock(const char *name);
// int32 findBlock(const char *name);

// extern UniformRegistry uniformRegistry;

struct Shader
{
	shaderProgram_s vsh_program;
	void (*combiner)(void);
	
	static DVLB_s *dvlb;
	static void loadDVLB(u8* shbinData, u32 shbinSize);
	static Shader *create(u32 prgId, void (*combiner)(void));
	
	void use(void);
	void destroy(void);
};

extern Shader *currentShader;

// fragment "shader" programs
void combiner_simple();
void combiner_matfx();

// bitch-made 'Gl wrappers
static inline void
c3dUniformMatrix4fv(int id, int count, int transpose, C3D_Mtx *mtx)
{
	C3D_FVUnifMtxNx4(GPU_VERTEX_SHADER, id, mtx, count * 4);
}

static inline void
c3dUniform4fv(int id, int count, float *fv)
{
	int i;
	C3D_FVec *ptr = C3D_FVUnifWritePtr(GPU_VERTEX_SHADER, id, count);
	for (i = 0; i < count; i++, fv+=4){
		ptr[i].x = fv[0];
		ptr[i].y = fv[1];
		ptr[i].z = fv[2];
		ptr[i].w = fv[3];
	}
}

static inline void
c3dUniform2fv(int id, int count, float *fv)
{
	int i;
	C3D_FVec* ptr = C3D_FVUnifWritePtr(GPU_VERTEX_SHADER, id, count);
	for(i = 0; i < count; i++, fv+=2){
		ptr[i].x = fv[0];
		ptr[i].y = fv[1];
	}
}
  
}
}
  
#endif
