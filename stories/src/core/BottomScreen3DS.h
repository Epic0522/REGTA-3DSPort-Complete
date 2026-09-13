#pragma once

#ifdef ENABLE_3DS_BOTTOM_RADAR
void InitialiseBottomScreen(void);
void RenderBottomScreen(void);
void ShutdownBottomScreen(void);
class CRect;
bool DrawLCSMenuMap3DS(const CRect &rect);
#endif
