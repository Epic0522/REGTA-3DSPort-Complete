#pragma once

#ifdef _3DS
static wchar *PS2GraphicsDraw(bool *disabled, bool)
{
	*disabled = false;
	return TheText.Get(rw::c3d::ps2GraphicsEnabled() ? "FEM_ON" : "FEM_OFF");
}

static void PS2GraphicsButtonPress(int8 action)
{
	if(action != FEOPTION_ACTION_SELECT && action != FEOPTION_ACTION_LEFT &&
	   action != FEOPTION_ACTION_RIGHT) return;
	rw::c3d::setPS2Graphics(!rw::c3d::ps2GraphicsEnabled());
#ifdef LOAD_INI_SETTINGS
	SaveINISettings();
#endif
}
#endif
