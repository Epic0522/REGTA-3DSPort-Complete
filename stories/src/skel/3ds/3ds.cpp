#ifdef RW_3DS

#include <malloc.h>
#include <unistd.h>
#include <errno.h>
#include <locale.h>
#include <signal.h>
#include <stddef.h>

#include "common.h"
#include <stdio.h>
#include "rwcore.h"
#include "skeleton.h"
#include "platform.h"
#include "crossplatform.h"

#include "main.h"
#include "FileMgr.h"
#include "Text.h"
#include "Pad.h"
#include "Timer.h"
#include "DMAudio.h"
#include "ControllerConfig.h"
#include "Frontend.h"
#include "Game.h"
#include "PCSave.h"
#include "MemoryCard.h"
#include "Sprite2d.h"
#include "AnimViewer.h"
#include "Font.h"
#include "MemoryMgr.h"
#include "3dsmovie.h"

#define MAX_SUBSYSTEMS		(16)

RwUInt32 gGameState;

rw::EngineOpenParams openParams;

static RwBool             useDefault;
static RwBool		  ForegroundApp = TRUE;
static RwBool		  RwInitialised = FALSE;
static RwSubSystemInfo    GsubSysInfo[MAX_SUBSYSTEMS];
static RwInt32		  GnumSubSystems = 0;
static RwInt32		  GcurSel = 0, GcurSelVM = 0;

static psGlobalType PsGlobal;
#define PSGLOBAL(var) (((psGlobalType *)(RsGlobal.ps))->var)

long _dwOperatingSystemVersion	= OS_WINXP;
size_t _dwMemAvailPhys		= (60 << 20);

extern "C" {
	/* what the fuck??? */
	uint32_t __stacksize__
	__attribute__((used,retain,section(".data.__stacksize__")))
	= (1 << 20);

	// uint32_t __attribute__((used,retain))
	// __ctru_linear_heap_size = (60 << 20);
}

/* make cwd more unix-like */

char*
getcwd_3ds(char *buf, size_t size)
{
	char cwd[128];
	getcwd(cwd, 128);
	if(!strncmp(cwd, "sdmc:", 5)){
		strcpy(buf, &cwd[5]);
	}else{
		strcpy(buf, cwd);
	}
	return buf;
}

/* missing from newlib */

ssize_t
readlink(const char *pathname, char *buf, size_t bufsiz)
{
	/* don't need actual implementation because no links of fat32 */
	return 0;
}


char *
realpath(const char *path, char *canon)
{
	char buf[128];
	char cwd[128];
	char *src = buf;
	char *dst = canon;
	char *stack[64];
	int i, depth = 0;

	buf[0] = 0;

	if (strncmp(path, "/", 1)){
		getcwd_3ds(buf, 64);
		strcat(buf, "/");
	}
	strcat(buf, path);

	while(*src){
		if (!strncmp(src, "/../", 4)){
			src++;
			do {
				src+=3;
				if (depth) depth--;
			} while (!strncmp(src, "../", 3));
			dst = stack[depth];
		}else if (!strncmp(src, "/./", 3)){
			do {
				while(*(++src) == '/');
			} while (!strncmp(src, "./", 2));
			*(dst++) = '/';				
			stack[depth++] = dst;
		}else if (*src == '/' || *src == '\\'){
			do { ++src; } while(*src == '/' || *src == '\\');
			*(dst++) = '/';				
			stack[depth++] = dst;
		}else {
			*(dst++) = *(src++);
		}			
	}
	*dst = 0;

	return canon;
}

/*
 *****************************************************************************
 */
void
_psCreateFolder(const char *path)
{
	struct stat info;
	char fullpath[PATH_MAX];
	realpath(path, fullpath);

	if (lstat(fullpath, &info) != 0) {
		if (errno == ENOENT || (errno != EACCES && !S_ISDIR(info.st_mode))) {
			mkdir(fullpath, 0755);
		}
	}
}

/*
 *****************************************************************************
 */
const char*
_psGetUserFilesFolder()
{
	static char szUserFiles[256];
	strcpy(szUserFiles, "userfiles");
	_psCreateFolder(szUserFiles);
	return szUserFiles;
}

/*
 *****************************************************************************
 */
RwBool
psCameraBeginUpdate(RwCamera *camera)
{
	if(!RwCameraBeginUpdate(Scene.camera)){
		/* should never occur */
		ForegroundApp = FALSE;
		RsEventHandler(rsACTIVATE, (void*)FALSE);
		return FALSE;
	}
	return TRUE;
}

/*
 *****************************************************************************
 */
void
psCameraShowRaster(RwCamera *camera)
{
	if(FrontEndMenuManager.m_PrefsVsync){
		RwCameraShowRaster(camera, NULL, rwRASTERFLIPWAITVSYNC);
	}else{
		RwCameraShowRaster(camera, NULL, rwRASTERFLIPDONTWAIT);
	}
}

/*
 *****************************************************************************
 */
RwImage*
psGrabScreen(RwCamera *pCamera)
{
	rw::Image *image = RwCameraGetRaster(pCamera)->toImage();
	image->removeMask();
	return image;
}

/*
 *****************************************************************************
 */
double
psTimer(void)
{
	return osGetTime();
}

/*
 *****************************************************************************
 */
void
psMouseSetPos(RwV2d *pos)
{
}

/*
 *****************************************************************************
 */
RwMemoryFunctions*
psGetMemoryFunctions(void)
{
#ifdef USE_CUSTOM_ALLOCATOR
	return &memFuncs;
#else
	return nil;
#endif
}

/*
 *****************************************************************************
 */
RwBool
psInstallFileSystem(void)
{
	return TRUE;
}

/*
 *****************************************************************************
 */
RwBool
psNativeTextureSupport(void)
{
	return TRUE;
}

/*
 *****************************************************************************
 */
RwBool
psInitialize(void)
{
	RsGlobal.ps = &PsGlobal;
	CFileMgr::Initialise();

	/* PC menu */
	{
		C_PcSave::SetSaveDirectory(_psGetUserFilesFolder());
		InitialiseLanguage();
		FrontEndMenuManager.LoadSettings();
	}

	gGameState = GS_START_UP;
	TRACE("gGameState = GS_START_UP");

	TheText.Unload();	// why?
	return TRUE;
}

/*
 *****************************************************************************
 */
void
psTerminate(void)
{
	return;
}

/*
 *****************************************************************************
 */
static RwChar **_VMList;

RwInt32
_psGetNumVideModes()
{
	return RwEngineGetNumVideoModes();
}

/*
 *****************************************************************************
 */
RwBool
_psFreeVideoModeList()
{
	RwInt32 i, numModes = _psGetNumVideModes();
	
	if(_VMList == nil){
		return TRUE;
	}
	
	for(i = 0; i < numModes; i++){
		RwFree(_VMList[i]);
	}
	
	RwFree(_VMList);
	_VMList = nil;
	
	return TRUE;
}
							
/*
 *****************************************************************************
 */							
RwChar**
_psGetVideoModeList()
{
	RwInt32 i, numModes;
	
	if(_VMList != nil){
		return _VMList;
	}
	
	numModes = RwEngineGetNumVideoModes();
	_VMList = (RwChar**)RwCalloc(numModes, sizeof(RwChar*));
	
	for(i = 0; i < numModes; i++){
		RwVideoMode vm;
		RwEngineGetVideoModeInfo(&vm, i);
		_VMList[i] = (RwChar*)RwCalloc(100, sizeof(RwChar));
		rwsprintf(_VMList[i],"%d X %d X %d", vm.width, vm.height, vm.depth);
	}
	
	return _VMList;
}

/*
 *****************************************************************************
 */
void
_psSelectScreenVM(RwInt32 videoMode)
{
	RwTexDictionarySetCurrent(nil);
	FrontEndMenuManager.UnloadTextures();	
	if(!_psSetVideoMode(RwEngineGetCurrentSubSystem(), videoMode)){
		RsGlobal.quit = TRUE;
		printf("ERROR: Failed to select new screen resolution\n");
	}else{
		FrontEndMenuManager.LoadAllTextures();
	}
}

/*
 *****************************************************************************
 */
RwBool
IsForegroundApp()
{
	return TRUE;
}

/*
 *****************************************************************************
 */
RwBool
psSelectDevice()
{
	RwVideoMode vm;
	RwInt32	subSysNum;
	RwInt32	AutoRenderer = 0;

	/* Set the driver to use the correct sub system */
	if(!RwEngineSetSubSystem(GcurSel)){
		return FALSE;
	}

	RwEngineGetVideoModeInfo(&vm, GcurSelVM);
	if(!RwEngineSetVideoMode(GcurSelVM)){
		return FALSE;
	}

	RsGlobal.maximumWidth = vm.width;
	RsGlobal.maximumHeight = vm.height;
	RsGlobal.width = vm.width;
	RsGlobal.height = vm.height;
	
	return TRUE;
} 

long _InputInitialiseMouse(bool exclusive)
{
	return 0;
}

void _InputShutdownMouse()
{
}

// Not "needs exclusive" on GLFW, but more like "needs to change mode"
bool _InputMouseNeedsExclusive()
{
	return false;
}


void
psPostRWinit(void)
{
	RwVideoMode vm;
	RwEngineGetVideoModeInfo(&vm, GcurSelVM);

	memset(&PsGlobal, 0, sizeof(PsGlobal));
	
	CPad::GetPad(0)->Clear(true);
	CPad::GetPad(1)->Clear(true);
}

/*
 *****************************************************************************
 */
RwBool
_psSetVideoMode(RwInt32 subSystem, RwInt32 videoMode)
{
	RwRect r;
	
	RwInitialised = FALSE;
	RsEventHandler(rsRWTERMINATE, nil);
	GcurSel = subSystem;
	GcurSelVM = videoMode;
	useDefault = TRUE;
	
	if(RsEventHandler(rsRWINITIALIZE, &openParams) == rsEVENTERROR){
		return FALSE;
	}

	RwInitialised = TRUE;
	useDefault = FALSE;
	
	r.x = 0;
	r.y = 0;
	r.w = RsGlobal.maximumWidth;
	r.h = RsGlobal.maximumHeight;

	RsEventHandler(rsCAMERASIZE, &r);
	psPostRWinit();
	
	return TRUE;
}
 
/*
 *****************************************************************************
 */
void
InitialiseLanguage()
{
	// Mandatory for Linux(Unix? Posix?) to set lang. to environment lang.
	// supported by newlib
	setlocale(LC_ALL, "");	

	char *systemLang, *keyboardLang;

	systemLang = setlocale (LC_ALL, NULL);
	keyboardLang = setlocale (LC_CTYPE, NULL);
	
	short primUserLCID, primSystemLCID;
	primUserLCID = primSystemLCID = !strncmp(systemLang, "fr_",3) ? LANG_FRENCH :
					!strncmp(systemLang, "de_",3) ? LANG_GERMAN :
					!strncmp(systemLang, "en_",3) ? LANG_ENGLISH :
					!strncmp(systemLang, "it_",3) ? LANG_ITALIAN :
					!strncmp(systemLang, "es_",3) ? LANG_SPANISH :
					LANG_OTHER;

	short primLayout = !strncmp(keyboardLang, "fr_",3) ? LANG_FRENCH : (!strncmp(keyboardLang, "de_",3) ? LANG_GERMAN : LANG_ENGLISH);

	short subUserLCID, subSystemLCID;
	subUserLCID = subSystemLCID = !strncmp(systemLang, "en_AU",5) ? SUBLANG_ENGLISH_AUS : SUBLANG_OTHER;
	short subLayout = !strncmp(keyboardLang, "en_AU",5) ? SUBLANG_ENGLISH_AUS : SUBLANG_OTHER;

	if (   primUserLCID	  == LANG_GERMAN
		|| primSystemLCID == LANG_GERMAN
		|| primLayout	  == LANG_GERMAN )
	{
		CGame::nastyGame = false;
		FrontEndMenuManager.m_PrefsAllowNastyGame = false;
		CGame::germanGame = true;
	}
	
	if (   primUserLCID	  == LANG_FRENCH
		|| primSystemLCID == LANG_FRENCH
		|| primLayout	  == LANG_FRENCH )
	{
		CGame::nastyGame = false;
		FrontEndMenuManager.m_PrefsAllowNastyGame = false;
		CGame::frenchGame = true;
	}
	
	if (   subUserLCID	 == SUBLANG_ENGLISH_AUS
		|| subSystemLCID == SUBLANG_ENGLISH_AUS
		|| subLayout	 == SUBLANG_ENGLISH_AUS )
		CGame::noProstitutes = true;

#ifdef NASTY_GAME
	CGame::nastyGame = true;
	FrontEndMenuManager.m_PrefsAllowNastyGame = true;
	CGame::noProstitutes = false;
#endif
	
	int32 lang;
	
	switch ( primSystemLCID )
	{
		case LANG_GERMAN:
		{
			lang = LANG_GERMAN;
			break;
		}
		case LANG_FRENCH:
		{
			lang = LANG_FRENCH;
			break;
		}
		case LANG_SPANISH:
		{
			lang = LANG_SPANISH;
			break;
		}
		case LANG_ITALIAN:
		{
			lang = LANG_ITALIAN;
			break;
		}
		default:
		{
			lang = ( subSystemLCID == SUBLANG_ENGLISH_AUS ) ? -99 : LANG_ENGLISH;
			break;
		}
	}
	
	FrontEndMenuManager.OS_Language = primUserLCID;

	switch(lang){
	case LANG_GERMAN:
		FrontEndMenuManager.m_PrefsLanguage = FrontEndMenuManager.LANGUAGE_GERMAN;
		break;

	case LANG_SPANISH:
		FrontEndMenuManager.m_PrefsLanguage = FrontEndMenuManager.LANGUAGE_SPANISH;
		break;

	case LANG_FRENCH:
		FrontEndMenuManager.m_PrefsLanguage = FrontEndMenuManager.LANGUAGE_FRENCH;
		break;

	case LANG_ITALIAN:
		FrontEndMenuManager.m_PrefsLanguage = FrontEndMenuManager.LANGUAGE_ITALIAN;
		break;

	default:
		FrontEndMenuManager.m_PrefsLanguage = FrontEndMenuManager.LANGUAGE_AMERICAN;
		break;
	}

	setlocale(LC_CTYPE, "C");
	setlocale(LC_COLLATE, "C");
	setlocale(LC_NUMERIC, "C");

	TheText.Unload();
	TheText.Load();
}

void
HandleExit()
{
	return;
}

/*
 *****************************************************************************
 */

// R* calls that in ControllerConfig, idk why
void
_InputTranslateShiftKeyUpDown(RsKeyCodes *rs)
{
}

/*
 *****************************************************************************
 */
void
stateMachine()
{
	float ms;
	switch (gGameState){
	case GS_START_UP:
		TRACE("gGameState = GS_INIT_ONCE");
#ifdef NO_MOVIES
		gGameState = GS_INIT_ONCE;
#else
		gGameState = GS_INIT_LOGO_MPEG;
#endif
		break;

	case GS_INIT_LOGO_MPEG:
		TRACE("gGameState = GS_LOGO_MPEG;");
		gGameState = GS_LOGO_MPEG;
		break;

	case GS_LOGO_MPEG:
		gGameState++;
		break;

	case GS_INIT_INTRO_MPEG:
		TRACE("gGameState = GS_INTRO_MPEG;");
		gGameState = GS_INTRO_MPEG;
		break;

	case GS_INTRO_MPEG:
		TRACE("gGameState = GS_INTRO_MPEG;");
		gGameState++;
		break;

	case GS_INIT_ONCE:
		TRACE("gGameState = GS_INIT_FRONTEND;");
		LoadingScreen(nil, nil, "loadsc0");
		if ( !CGame::InitialiseOnceAfterRW() )
			RsGlobal.quit = TRUE;
		else {
		#ifndef EMULATOR_SKIP_STARTUP_MOVIES
			Play3DSStartupMovies();
		#endif
		}
		gGameState = GS_INIT_FRONTEND;
		break;
					
	case GS_INIT_FRONTEND:
		TRACE("gGameState = GS_FRONTEND;");
		LoadingScreen(nil, nil, "sceelee");
		FrontEndMenuManager.m_bGameNotLoaded = true;
		FrontEndMenuManager.m_bStartUpFrontEndRequested = true;
		gGameState = GS_FRONTEND;
		break;
					
	case GS_FRONTEND:
		RsEventHandler(rsFRONTENDIDLE, nil);
		
		if(!FrontEndMenuManager.m_bMenuActive ||
		   FrontEndMenuManager.m_bWantToLoad){
			TRACE("gGameState = GS_INIT_PLAYING_GAME;");
			gGameState = GS_INIT_PLAYING_GAME;
			
		}
		if(FrontEndMenuManager.m_bWantToLoad){
			TRACE("gGameState = GS_PLAYING_GAME;");
			InitialiseGame();
			FrontEndMenuManager.m_bGameNotLoaded = false;
			gGameState = GS_PLAYING_GAME;
			
		}
		break;
					
	case GS_INIT_PLAYING_GAME:
		TRACE("gGameState = GS_PLAYING_GAME;");
		InitialiseGame();
		FrontEndMenuManager.m_bGameNotLoaded = false;
		gGameState = GS_PLAYING_GAME;
		break;
					
	case GS_PLAYING_GAME:
		ms = (float)CTimer::GetCurrentTimeInCycles() /
		     (float)CTimer::GetCyclesPerMillisecond();
		if(RwInitialised){
			if (!FrontEndMenuManager.m_PrefsFrameLimiter ||
			    (1000.0f / (float)RsGlobal.maxFPS) < ms)
				RsEventHandler(rsIDLE, (void*)TRUE);
		}
	}
}

static void
ctrInit()
{
	osSetSpeedupEnable(true);
	gdbHioDevInit();
	gdbHioDevRedirectStdStreams(true, true, true);
	if(chdir("sdmc:/3ds/relcs") != 0){
		printf("sdmc:/3ds/relcs is missing.\n");
		exit(1);
	}
}

void
resetCamera()
{
	RwRect r;
	r.x = 0;
	r.y = 0;
	r.w = RsGlobal.maximumWidth;
	r.h = RsGlobal.maximumHeight;
	RsEventHandler(rsCAMERASIZE, &r);
}

void
gameReload()
{
	CPad::ResetCheats();
	DMAudio.ChangeMusicMode(MUSICMODE_DISABLE);
	CTimer::Stop();

	if(FrontEndMenuManager.m_bWantToLoad){
		CGame::ShutDownForRestart();
		CGame::InitialiseWhenRestarting();
		DMAudio.ChangeMusicMode(MUSICMODE_GAME);
		LoadSplash(GetLevelSplashScreen(CGame::currLevel));
		FrontEndMenuManager.m_bWantToLoad = false;
	}else{
		if(gGameState == GS_PLAYING_GAME){
			CGame::ShutDown();
		}
		CTimer::Stop();
		if(FrontEndMenuManager.m_bFirstTime == true){
			TRACE("gGameState = GS_INIT_FRONTEND;");
			gGameState = GS_INIT_FRONTEND;
		}else{
			TRACE("gGameState = GS_INIT_PLAYING_GAME;");
			gGameState = GS_INIT_PLAYING_GAME;
		}
	}

	FrontEndMenuManager.m_bFirstTime = false;
	FrontEndMenuManager.m_bWantToRestart = false;
}

void
gameResizeCamera()
{
	RwRect r = {
		.x =   0, .y =   0,
		.w = 400, .h = 240,
	};

	RsEventHandler(rsCAMERASIZE, &r);
}

void
gameLoadSettings()
{
	CFileMgr::SetDirMyDocuments();
	int32 gta3set = CFileMgr::OpenFile("gta3.set", "r");
		
	if ( gta3set )
	{
		ControlsManager.LoadSettings(gta3set);
		CFileMgr::CloseFile(gta3set);
	}
		
	CFileMgr::SetDir("");
}

void
callTheMaid()
{
#ifdef RESTORIES_3DS_BUILD
	/* This is the final process-exit path, not the in-game restart path.  A
	 * long session can leave the linear allocator's address tree too fragile
	 * for the thousands of render-resource frees in CGame::ShutDown; the OS is
	 * about to reclaim the complete process address space anyway.  Returning
	 * directly avoids touching damaged allocator bookkeeping during exit. */
	return;
#else
	if(gGameState == GS_PLAYING_GAME){
		CGame::ShutDown();
	}
	DMAudio.Terminate();

	/* no pointer left behind */
	_psFreeVideoModeList();

	/* Tidy up the 3D (RenderWare) components of the application... */
	RsEventHandler(rsRWTERMINATE, nil);

	/* Free the platform dependent data... */
	RsEventHandler(rsTERMINATE, nil);
#endif
}

extern char* fake_heap_start;
extern char* fake_heap_end;

void
memoryInfo()
{
	struct mallinfo minfo;
	static int vram[2] = {0,0}, lram[2] = {0,0}, heap[2] = {0, 0};

	minfo = mallinfo();
	heap[0] = fake_heap_end - fake_heap_start - minfo.arena;
	vram[0] = vramSpaceFree();
	lram[0] = linearSpaceFree();

	if (heap[0] != heap[1] ||
	    vram[0] != vram[1] ||
	    lram[0] != lram[1]){
		heap[1] = heap[0];
		vram[1] = vram[0];
		lram[1] = lram[0];
		int hrv = heap[0] < (1<<20) ? heap[0]>>10 : heap[0]>>20;
		const char *hrs = heap[0] < (1<<20) ? "Kb" : "Mb";
		int vrv = vram[0] < (1<<20) ? vram[0]>>10 : vram[0]>>20;
		const char *vrs = vram[0] < (1<<20) ? "Kb" : "Mb";
		int lrv = lram[0] < (1<<20) ? lram[0]>>10 : lram[0]>>20;
		const char *lrs = lram[0] < (1<<20) ? "Kb" : "Mb";
		printf("heap:	%d %s	vram:	%d %s	lram:	%d %s\n",
		       hrv, hrs, vrv, vrs, lrv, lrs);
	}
}

int
main(int argc, char *argv[])
{
	/* setup stdio and change directory to /3ds/relcs */
	ctrInit();
	
	/* Initialize the platform independent data.
	 * This will in turn call psInitialize */
	if(RsEventHandler(rsINITIALIZE, nil) == rsEVENTERROR){
		return 1;
	}

	/* Parameters to be used in RwEngineOpen / rsRWINITIALISE event */
	
	openParams.width = RsGlobal.maximumWidth;
	openParams.height = RsGlobal.maximumHeight;
	openParams.windowtitle = RsGlobal.appName;
	openParams.window = nil;
	
	ControlsManager.MakeControllerActionsBlank();
	ControlsManager.InitDefaultControlConfiguration();

	/* Initialize the 3D (RenderWare) components of the app... */
	if(RsEventHandler(rsRWINITIALIZE, &openParams) == rsEVENTERROR){
		RsEventHandler(rsTERMINATE, nil);
		return 1;
	}
	
	psPostRWinit();

	ControlsManager.InitDefaultControlConfigMouse(MousePointerStateHelper.GetMouseSetUp());

	gameResizeCamera();	// why?

	gameLoadSettings();

	while(1){
		RwInitialised = TRUE;
		
		/* run the state machine */
		while(!RsGlobal.quit &&
		      !FrontEndMenuManager.m_bWantToRestart &&
		      aptMainLoop()){
			stateMachine();
			memoryInfo();
		}

		/* About to shut down or restart - block resize events again... */
		RwInitialised = FALSE;
		FrontEndMenuManager.UnloadTextures();

		if(!FrontEndMenuManager.m_bWantToRestart)
			break;

		gameReload();
	}
	
	callTheMaid();
	return 0;
}

#endif
