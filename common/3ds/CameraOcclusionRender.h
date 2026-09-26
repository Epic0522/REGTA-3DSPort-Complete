#pragma once
#ifdef _3DS
#include "CameraOcclusion.h"
namespace CameraOcclusion3DS
{
struct RenderScope {
	rw::c3d::EntityRenderStyle previous;
	uint32 zwrite;
	bool changed;
	RenderScope(CEntity *entity) : previous(rw::c3d::getEntityRenderStyle()), zwrite(0), changed(false)
	{
		const float alpha = Alpha(entity);
		if(alpha >= 1.f) return;
		auto style = previous;
		if(style.opacity > alpha) style.opacity = alpha;
		rw::c3d::setEntityRenderStyle(style);
		zwrite = rw::GetRenderState(rw::ZWRITEENABLE);
		rw::SetRenderState(rw::ZWRITEENABLE, 0);
		changed = true;
	}
	~RenderScope()
	{
		if(changed) {
			rw::c3d::setEntityRenderStyle(previous);
			rw::SetRenderState(rw::ZWRITEENABLE, zwrite);
		}
	}
};
} // namespace CameraOcclusion3DS
#endif
