#ifdef RW_3DS

#include <stdlib.h>
#include <stdio.h>

#include <rw.h>
#include "skeleton.h"

using namespace sk;
using namespace rw;

enum mousebutton {
BUTTON_LEFT = 0x1,
};

#define CLAMPINT(x, lo, hi) (((x) < (lo)) ? (lo) : ((x) > (hi) ? (hi) : x))

typedef struct {
	u32 mask;
	u32 key;
} KeyBind;

KeyBind gKeyMap[] = {
	{KEY_DUP,        sk::KEY_UP},
	{KEY_DDOWN,      sk::KEY_DOWN},
	{KEY_DLEFT,      sk::KEY_LEFT},
	{KEY_DRIGHT,     sk::KEY_RIGHT},
	{KEY_CPAD_UP,    'W'},
	{KEY_CPAD_DOWN,  'S'},
	{KEY_CPAD_LEFT,  'A'},
	{KEY_CPAD_RIGHT, 'D'},
	{KEY_R,          'R'},
	{KEY_L,          'F'},
};

int
main(int argc, char *argv[])
{
	TickCounter cnt;
	sk::MouseState ms;
	u32 kDown, kUp, kHeld;
	touchPosition touch, origin;
	float timeDelta;
	int i;
	
	args.argc = argc;
	args.argv = argv;

	gdbHioDevInit();
	gdbHioDevRedirectStdStreams(true, true, true);
	
	if(EventHandler(INITIALIZE, nil) == EVENTERROR)
		return 0;

	engineOpenParams.width = sk::globals.width;
	engineOpenParams.height = sk::globals.height;
	engineOpenParams.windowtitle = sk::globals.windowtitle;
	engineOpenParams.window = nil;

	if(EventHandler(RWINITIALIZE, nil) == EVENTERROR)
		return 0;

	osTickCounterStart(&cnt);
	while(aptMainLoop() && (!sk::globals.quit)){
		hidScanInput();
		kUp = hidKeysUp();
		kDown = hidKeysDown();
		kHeld = hidKeysHeld();
		
		if(kDown & KEY_START){
			sk::globals.quit = true;
		}

		for (i = 0; i < sizeof(gKeyMap)/sizeof(gKeyMap[0]); i++){
			if (kDown & gKeyMap[i].mask){
				EventHandler(KEYDOWN, &gKeyMap[i].key);
			}else if (kUp & gKeyMap[i].mask){
				EventHandler(KEYUP, &gKeyMap[i].key);
			}
		}

		if(kDown & KEY_A){
			ms.buttons = BUTTON_LEFT;
			EventHandler(MOUSEBTN, &ms);
		}else if(kUp & KEY_A){
			ms.buttons = 0;
			EventHandler(MOUSEBTN, &ms);
		}

		if (kDown & KEY_TOUCH){
			hidTouchRead(&origin);
		}else if(kHeld & KEY_TOUCH){
			hidTouchRead(&touch);
			int dx = touch.px - origin.px;
			int dy = touch.py - origin.py;
			if (dx || dy){
				origin.px = touch.px;
				origin.py = touch.py;
				// dx = abs(dx) * dx;
				// dy = abs(dy) * dy;
				ms.posx = CLAMPINT(ms.posx + dx, 0, 400);
				ms.posy = CLAMPINT(ms.posy + dy, 0, 240);
				EventHandler(MOUSEMOVE, &ms);
			}
		}		

		osTickCounterUpdate(&cnt);
		timeDelta = (float)osTickCounterRead(&cnt) / 1000.0f;
		EventHandler(IDLE, &timeDelta);
	}

	EventHandler(RWTERMINATE, nil);

	return 0;
}

namespace sk {

void
SetMousePosition(int x, int y)
{  
}

}

#endif
