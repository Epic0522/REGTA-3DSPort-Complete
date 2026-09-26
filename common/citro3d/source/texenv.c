#include "internal.h"

C3D_TexEnv* C3D_GetTexEnv(int id)
{
	C3D_Context* ctx = C3Di_GetContext();

	if (!(ctx->flags & C3DiF_Active))
		return NULL;

	ctx->flags |= C3DiF_TexEnv(id);
	return &ctx->texEnv[id];
}

void C3D_SetTexEnv(int id, C3D_TexEnv* env)
{
	C3D_Context* ctx = C3Di_GetContext();

	if (!(ctx->flags & C3DiF_Active))
		return;

	ctx->flags |= C3DiF_TexEnv(id);
	if (env)
		memcpy(&ctx->texEnv[id], env, sizeof(*env));
	else
		C3D_TexEnvInit(&ctx->texEnv[id]);
}

void C3D_DirtyTexEnv(C3D_TexEnv* env)
{
	C3D_Context* ctx = C3Di_GetContext();

	if (!(ctx->flags & C3DiF_Active))
		return;

	u32 id = env-ctx->texEnv;
	if (id < 6)
		ctx->flags |= C3DiF_TexEnv(id);
}

void C3D_SetTexEnvColor(int id, u32 color)
{
	C3D_Context* ctx = C3Di_GetContext();
	if (!(ctx->flags & C3DiF_Active) || (unsigned)id >= 6)
		return;
	if (ctx->texEnv[id].color == color)
		return;
	ctx->texEnv[id].color = color;
	ctx->texEnvColorDirty |= BIT(id);
}

void C3Di_TexEnvBind(int id, C3D_TexEnv* env)
{
	if (id >= 4) id += 2;
	GPUCMD_AddIncrementalWrites(GPUREG_TEXENV0_SOURCE + id*8, (u32*)env, sizeof(C3D_TexEnv)/sizeof(u32));
}

void C3Di_UpdateTexEnv(void)
{
	C3D_Context* ctx = C3Di_GetContext();
	for (int i = 0; i < 6; i ++)
	{
		// Get/SetTexEnv and APT restore still invalidate the complete stage.
		// A full upload includes its color and takes precedence over a partial one.
		if (ctx->flags & C3DiF_TexEnv(i))
			C3Di_TexEnvBind(i, &ctx->texEnv[i]);
		else if (ctx->texEnvColorDirty & BIT(i))
		{
			int regId = i >= 4 ? i+2 : i;
			GPUCMD_AddWrite(GPUREG_TEXENV0_COLOR + regId*8, ctx->texEnv[i].color);
		}
	}
	ctx->flags &= ~C3DiF_TexEnvAll;
	ctx->texEnvColorDirty = 0;
}

void C3D_TexEnvBufUpdate(int mode, int mask)
{
	C3D_Context* ctx = C3Di_GetContext();

	if (!(ctx->flags & C3DiF_Active))
		return;

	u32 val = ctx->texEnvBuf;
	mask &= 0xF;

	if (mode & C3D_RGB)
	{
		val &= ~(0xF << 8);
		val |= mask << 8;
	}

	if (mode & C3D_Alpha)
	{
		val &= ~(0xF << 12);
		val |= mask << 12;
	}

	ctx->texEnvBuf = val;
	ctx->flags |= C3DiF_TexEnvBuf;
}

void C3D_TexEnvBufColor(u32 color)
{
	C3D_Context* ctx = C3Di_GetContext();

	if (!(ctx->flags & C3DiF_Active))
		return;

	ctx->texEnvBufClr = color;
	ctx->flags |= C3DiF_TexEnvBuf;
}

void C3D_ConfigureTexEnv(void (*configure)(void))
{
	C3D_Context* ctx = C3Di_GetContext();
	if (!(ctx->flags & C3DiF_Active))
		return;

	// A local before/after comparison, not a second GPU-state cache. Never
	// discard a dirty flag that existed on entry (including APT restoration).
	C3D_TexEnv previous[6];
	memcpy(previous, ctx->texEnv, sizeof(previous));
	u32 pending = ctx->flags;
	u32 previousBuf = ctx->texEnvBuf;
	u32 previousBufClr = ctx->texEnvBufClr;
	for (int i = 0; i < 6; ++i)
		C3D_SetTexEnv(i, NULL);
	C3D_TexEnvBufUpdate(C3D_Both, 0);
	if (configure)
		configure();

	u32 unchanged = 0;
	for (int i = 0; i < 6; ++i)
		if (memcmp(&previous[i], &ctx->texEnv[i], sizeof(previous[i])) == 0)
			unchanged |= C3DiF_TexEnv(i);
	if (previousBuf == ctx->texEnvBuf && previousBufClr == ctx->texEnvBufClr)
		unchanged |= C3DiF_TexEnvBuf;
	ctx->flags &= ~(unchanged & ~pending);
}
