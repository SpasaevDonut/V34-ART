// In-game control panel for CS:S V34 ADVANCED RECORDING TOOLS.
// The overlay is rendered through process-wide D3D9 function detours.
// EndScene is the primary render point; device/swap-chain Present provide a safe fallback.

#include "art_internal.h"
#include "art_gui.h"
#include "art_hlae.h"
#include "art_ffmpeg.h"

#define DIRECTINPUT_VERSION 0x0800
#include <d3d9.h>
#include <shellapi.h>
#include <algorithm>
#include <errno.h>
#include <float.h>
#include <limits.h>
#include <math.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <string>
#include <vector>

#include "imgui.h"
#include "backends/imgui_impl_dx9.h"
#include "backends/imgui_impl_win32.h"
#include "MinHook.h"
#include "materialsystem/imaterial.h"
#include "materialsystem/imaterialvar.h"
#include "texture_group_names.h"
#include "engine/ivmodelinfo.h"
#include "../third_party/advancedfx/shared/AfxMath.h"

extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler( HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam );

#pragma comment( lib, "d3d9.lib" )
#pragma comment( lib, "shell32.lib" )

// memdbgon must be the last include file in a .cpp file.
#include "tier0/memdbgon.h"

namespace art
{
volatile LONG g_bGlobalFovOverride = FALSE;
float g_flGlobalFov = 90.0f;
volatile LONG g_bGlobalFovHandleZoom = TRUE;
float g_flGlobalFovMinUnzoomedFov = 90.0f;
volatile LONG g_bArtGuiTerminating = FALSE;

void BeginArtGuiTermination()
{
	InterlockedExchange( &g_bArtGuiTerminating, TRUE );
}

bool IsArtGuiTerminating()
{
	return InterlockedCompareExchange( &g_bArtGuiTerminating, FALSE, FALSE ) != FALSE;
}

bool IsArtGlobalFovOverrideActive()
{
	return InterlockedCompareExchange( &g_bGlobalFovOverride, FALSE, FALSE ) != FALSE;
}

bool ApplyArtGlobalFov( CViewSetup &view )
{
	if ( !IsArtGlobalFovOverrideActive() )
		return false;

	const float gameFov = view.fov;
	if ( InterlockedCompareExchange( &g_bGlobalFovHandleZoom, FALSE, FALSE ) != FALSE &&
		gameFov < g_flGlobalFovMinUnzoomedFov )
	{
		return false;
	}

	view.fov = g_flGlobalFov;
	view.fovViewmodel = g_flGlobalFov;
	return true;
}

namespace
{
	static const char *kGuiVersion = V34_ART_VERSION_STRING;
	static const char *kMinHookVersionPinned = "1.3.4";
	static const int kD3D9ResetIndex = 16;
	static const int kD3D9PresentIndex = 17;
	static const int kD3D9EndSceneIndex = 42;
	static const int kD3D9SwapChainPresentIndex = 3;
	static const int kMaxCommandHistory = 18;
	static const int kDemoPlayerRefreshMinimumAttempts = 6;
	static const int kDemoPlayerRefreshMaximumAttempts = 60;
	static const DWORD kDemoPlayerRefreshInitialDelayMs = 250;
	static const DWORD kDemoPlayerRefreshIntervalMs = 500;
	enum ToggleModifierBits
	{
		TOGGLE_MODIFIER_NONE = 0,
		TOGGLE_MODIFIER_SHIFT = 1 << 0,
		TOGGLE_MODIFIER_CONTROL = 1 << 1,
		TOGGLE_MODIFIER_ALT = 1 << 2
	};

	typedef HRESULT ( WINAPI *ResetFn )( IDirect3DDevice9 *, D3DPRESENT_PARAMETERS * );
	typedef HRESULT ( WINAPI *PresentFn )( IDirect3DDevice9 *, const RECT *, const RECT *, HWND, const RGNDATA * );
	typedef HRESULT ( WINAPI *EndSceneFn )( IDirect3DDevice9 * );
	typedef HRESULT ( WINAPI *SwapChainPresentFn )( IDirect3DSwapChain9 *, const RECT *, const RECT *, HWND,
		const RGNDATA *, DWORD );
	typedef BOOL ( WINAPI *GetCursorPosFn )( POINT * );
	typedef BOOL ( WINAPI *SetCursorPosFn )( int, int );
	typedef BOOL ( WINAPI *ClipCursorFn )( const RECT * );
	typedef HCURSOR ( WINAPI *SetCursorFn )( HCURSOR );

	struct DemoPlayerEntry
	{
		int entityIndex;
		int userId;
		std::string name;
	};

	struct TakeEntry
	{
		std::string name;
		std::string absolutePath;
		unsigned long files;
		unsigned __int64 bytes;
		FILETIME modified;
	};

	struct GuiState
	{
		bool installed;
		bool imguiReady;
		bool commandsRegistered;
		bool mouseStateStored;
		bool refreshConfigs;
		bool refreshTakes;
		bool refreshDemos;
		bool refreshDemoPlayers;
		bool demoWasPlaying;
		bool spectatorUiHidden;
		bool configSelectionChanged;
		int selectedPage;
		int selectedConfig;
		int selectedTake;
		int selectedDemo;
		int selectedDemoPlayer;
		int spectatorMode;
		int demoPlayerRefreshAttempts;
		int demoPlayerStableScans;
		int demoPlayerLastScanCount;
		unsigned long demoPlayerLastSignature;
		DWORD nextDemoPlayerRefreshAt;
		int hostFramerate;
		int refreshDelay;
		int toggleKey;
		int toggleModifiers;
		bool waitingForToggleKey;
		int hlaeInputHoldKey;
		bool waitingForHlaeInputHoldKey;
		bool experimentalOptionsEnabled;
		bool autoPauseDemoAfterRecording;
		bool autoResumeDemoOnRecordingStart;
		bool showCaptureHelp;
		bool showHlaeCampathHelp;
		bool showHlaeInputHelp;
		bool resetInfoScroll;
		float globalFov;
		bool globalFovDefault;
		float accentColor[4];
		float windowColor[4];
		float panelColor[4];
		float sidebarColor[4];
		float textColor[4];
		float mutedTextColor[4];
		float borderColor[4];
		float controlColor[4];
		float selectionColor[4];
		char takeName[64];
		char demoName[MAX_PATH];
		int demoTick;
		int currentDemoTick;
		bool currentDemoTickValid;
		float demoTickRate;
		float demoTickFraction;
		float demoClockTime;
		char outputPath[MAX_PATH];
		char prefix[48];
		char configName[48];
		char takeRename[64];
		char hlaeCampathFile[MAX_PATH];
		char hlaeCamioFile[MAX_PATH];
		char hlaeAgrFile[MAX_PATH];
		char hlaeBvhFile[MAX_PATH];
		char hlaeInputMemName[64];
		char hlaeInputMemFile[MAX_PATH];
		int hlaeCampathKeyframe;
		float hlaeFov;
		float hlaeBvhFps;
		bool hlaeInputDirectInitialized;
		float hlaeInputPosition[3];
		float hlaeInputAngles[3];
		float hlaeInputDirectFov;
		bool hlaeInputWhileGui;
		bool hlaeInputWhileGuiActive;
		LONG hlaeInputCursorAnchorX;
		LONG hlaeInputCursorAnchorY;
		bool hlaeInputCursorAnchorValid;
		int hlaeInputMouseSource;
		float hlaeCampathOffsetValue;
		bool hlaeCampathOffsetInitialized;
		float mainWindowX;
		float mainWindowY;
		float mainWindowWidth;
		float mainWindowHeight;
		char takeRootSnapshot[MAX_PATH];
		char consoleCommand[512];
		char commandFilter[64];
		char lastError[256];
		char previousMouseEnable[32];
		char ffmpegPath[MAX_PATH];
		char ffmpegCustomArgs[1024];
		ArtFfmpegTestResult ffmpegTestResult;
		bool ffmpegTestPerformed;
		std::vector<std::string> configs;
		std::vector<TakeEntry> takes;
		std::vector<std::string> demos;
		std::vector<DemoPlayerEntry> demoPlayers;
		std::vector<std::string> commandHistory;

		GuiState()
			: installed( false ), imguiReady( false ), commandsRegistered( false ),
			  mouseStateStored( false ), refreshConfigs( true ), refreshTakes( true ),
			  refreshDemos( true ), refreshDemoPlayers( true ),
			  demoWasPlaying( false ), spectatorUiHidden( false ),
			  configSelectionChanged( false ), ffmpegTestPerformed( false ),
			  selectedPage( 0 ), selectedConfig( -1 ), selectedTake( -1 ),
			  selectedDemo( -1 ), selectedDemoPlayer( -1 ),
			  spectatorMode( 3 ), demoPlayerRefreshAttempts( 0 ),
			  demoPlayerStableScans( 0 ), demoPlayerLastScanCount( -1 ),
			  demoPlayerLastSignature( 0 ), nextDemoPlayerRefreshAt( 0 ),
			  hostFramerate( 0 ), refreshDelay( 0 ),
			  toggleKey( VK_F3 ), toggleModifiers( TOGGLE_MODIFIER_SHIFT ), waitingForToggleKey( false ),
			  hlaeInputHoldKey( VK_LBUTTON ), waitingForHlaeInputHoldKey( false ),
			  experimentalOptionsEnabled( false ), autoPauseDemoAfterRecording( false ),
			  autoResumeDemoOnRecordingStart( false ),
			  showCaptureHelp( false ), showHlaeCampathHelp( false ),
			  showHlaeInputHelp( false ), resetInfoScroll( false ),
			  globalFov( 90.0f ), globalFovDefault( true )
		{
			accentColor[0] = 209.0f / 255.0f; accentColor[1] = 114.0f / 255.0f; accentColor[2] = 71.0f / 255.0f; accentColor[3] = 1.00f;
			windowColor[0] = 14.0f / 255.0f; windowColor[1] = 10.0f / 255.0f; windowColor[2] = 9.0f / 255.0f; windowColor[3] = 200.0f / 255.0f;
			panelColor[0] = 20.0f / 255.0f; panelColor[1] = 15.0f / 255.0f; panelColor[2] = 13.0f / 255.0f; panelColor[3] = 150.0f / 255.0f;
			sidebarColor[0] = 10.0f / 255.0f; sidebarColor[1] = 8.0f / 255.0f; sidebarColor[2] = 6.0f / 255.0f; sidebarColor[3] = 235.0f / 255.0f;
			textColor[0] = 235.0f / 255.0f; textColor[1] = 237.0f / 255.0f; textColor[2] = 247.0f / 255.0f; textColor[3] = 1.00f;
			mutedTextColor[0] = 143.0f / 255.0f; mutedTextColor[1] = 150.0f / 255.0f; mutedTextColor[2] = 173.0f / 255.0f; mutedTextColor[3] = 1.00f;
			borderColor[0] = 88.0f / 255.0f; borderColor[1] = 52.0f / 255.0f; borderColor[2] = 35.0f / 255.0f; borderColor[3] = 217.0f / 255.0f;
			controlColor[0] = 34.0f / 255.0f; controlColor[1] = 27.0f / 255.0f; controlColor[2] = 23.0f / 255.0f; controlColor[3] = 245.0f / 255.0f;
			selectionColor[0] = 181.0f / 255.0f; selectionColor[1] = 34.0f / 255.0f; selectionColor[2] = 0.0f; selectionColor[3] = 235.0f / 255.0f;
			takeName[0] = '\0';
			demoName[0] = '\0';
			demoTick = 0;
			currentDemoTick = 0;
			currentDemoTickValid = false;
			demoTickRate = 100.0f;
			demoTickFraction = 0.0f;
			demoClockTime = 0.0f;
			outputPath[0] = '\0';
			prefix[0] = '\0';
			configName[0] = '\0';
			takeRename[0] = '\0';
			Q_strncpy( hlaeCampathFile, "campath.xml", sizeof( hlaeCampathFile ) );
			Q_strncpy( hlaeCamioFile, "camera.cam", sizeof( hlaeCamioFile ) );
			Q_strncpy( hlaeAgrFile, "afxGameRecord.agr", sizeof( hlaeAgrFile ) );
			Q_strncpy( hlaeBvhFile, "camera.bvh", sizeof( hlaeBvhFile ) );
			Q_strncpy( hlaeInputMemName, "view", sizeof( hlaeInputMemName ) );
			Q_strncpy( hlaeInputMemFile, "mirv_input.xml", sizeof( hlaeInputMemFile ) );
			hlaeCampathKeyframe = 0;
			hlaeFov = 90.0f;
			hlaeBvhFps = 30.0f;
			hlaeInputDirectInitialized = false;
			hlaeInputPosition[0] = hlaeInputPosition[1] = hlaeInputPosition[2] = 0.0f;
			hlaeInputAngles[0] = hlaeInputAngles[1] = hlaeInputAngles[2] = 0.0f;
			hlaeInputDirectFov = 90.0f;
			hlaeInputWhileGui = false;
			hlaeInputWhileGuiActive = false;
			hlaeInputCursorAnchorX = 0;
			hlaeInputCursorAnchorY = 0;
			hlaeInputCursorAnchorValid = false;
			hlaeInputMouseSource = 0;
			hlaeCampathOffsetValue = 0.0f;
			hlaeCampathOffsetInitialized = false;
			mainWindowX = mainWindowY = mainWindowWidth = mainWindowHeight = 0.0f;
			takeRootSnapshot[0] = '\0';
			consoleCommand[0] = '\0';
			commandFilter[0] = '\0';
			lastError[0] = '\0';
			previousMouseEnable[0] = '\0';
		}
	};

	GuiState g_Gui;
	volatile LONG g_GuiVisible = FALSE;
	volatile LONG g_bArtStatisticsOverlayEnabled = TRUE;
	IDirect3DDevice9 *g_pGuiDevice = NULL;
	HWND g_hGameWindow = NULL;
	WNDPROC g_pOriginalWndProc = NULL;
	void *g_pResetTarget = NULL;
	void *g_pPresentTarget = NULL;
	void *g_pEndSceneTarget = NULL;
	void *g_pSwapChainPresentTarget = NULL;
	void *g_pGetCursorPosTarget = NULL;
	void *g_pSetCursorPosTarget = NULL;
	void *g_pClipCursorTarget = NULL;
	void *g_pSetCursorTarget = NULL;
	ResetFn g_pOriginalReset = NULL;
	PresentFn g_pOriginalPresent = NULL;
	EndSceneFn g_pOriginalEndScene = NULL;
	SwapChainPresentFn g_pOriginalSwapChainPresent = NULL;
	GetCursorPosFn g_pOriginalGetCursorPos = NULL;
	SetCursorPosFn g_pOriginalSetCursorPos = NULL;
	ClipCursorFn g_pOriginalClipCursor = NULL;
	SetCursorFn g_pOriginalSetCursor = NULL;
	bool g_bMinHookInitialized = false;
	bool g_bResetHookCreated = false;
	bool g_bPresentHookCreated = false;
	bool g_bEndSceneHookCreated = false;
	bool g_bSwapChainPresentHookCreated = false;
	bool g_bGetCursorPosHookCreated = false;
	bool g_bSetCursorPosHookCreated = false;
	bool g_bClipCursorHookCreated = false;
	bool g_bSetCursorHookCreated = false;
	volatile LONG g_nEndSceneHookCalls = 0;
	volatile LONG g_nPrimaryEndSceneFrames = 0;
	volatile LONG g_nPresentHookCalls = 0;
	volatile LONG g_nSwapChainPresentHookCalls = 0;
	volatile LONG g_nPresentFallbackFrames = 0;
	volatile LONG g_nGuiRenderedFrames = 0;
	volatile LONG g_nPresentNesting = 0;
	volatile LONG g_nPresentFallbackGuard = 0;
	volatile LONG g_nGuiFrameActive = 0;
	volatile LONG g_bWindowCloseCleanupStarted = FALSE;
	volatile LONG g_bPlayerChamsEnabled = FALSE;
	volatile LONG g_bViewmodelChamsEnabled = FALSE;
	volatile LONG g_bSkyboxChamsEnabled = FALSE;
	volatile LONG g_bSkyboxChamsRefreshPending = FALSE;
	volatile LONG g_bSkyboxChamsUpdateActive = FALSE;
	volatile LONG g_bPlayerChamsThroughWalls = FALSE;
	volatile LONG g_bPlayersPassThroughWalls = FALSE;
	volatile LONG g_bPlayersPassWorldWeapons = TRUE;
	volatile LONG g_bViewmodelVisible = TRUE;
	volatile LONG g_bPlayersVisible = TRUE;
	volatile LONG g_bNoFlashEnabled = FALSE;
	volatile LONG g_bNoSmokeEnabled = FALSE;
	volatile LONG g_bNoSmokeRefreshPending = FALSE;
	volatile LONG g_bNoSmokeUpdateActive = FALSE;
	volatile LONG g_bForceRenderLodEnabled = FALSE;
	volatile LONG g_bForceRenderLodCallbackActive = FALSE;
	volatile LONG g_nForcedRenderLodValue = -7;
	volatile LONG g_nForcedRenderLodOverrideCount = 0;
	ConVar *g_pRenderLodConVar = NULL;
	int g_nRenderLodRestoreValue = 0;
	bool g_bRenderLodRestoreValueValid = false;
	bool g_bRenderLodCvarHookInstalled = false;
	bool g_bRenderLodMissingLogged = false;
	volatile LONG g_bObjectIdPassActive = FALSE;
	volatile LONG g_bObjectIdUpdateActive = FALSE;
	volatile LONG g_nObjectIdViewmodelRed = 255;
	volatile LONG g_nObjectIdViewmodelGreen = 128;
	volatile LONG g_nObjectIdViewmodelBlue = 0;
	volatile LONG g_nObjectIdPlayersRed = 0;
	volatile LONG g_nObjectIdPlayersGreen = 0;
	volatile LONG g_nObjectIdPlayersBlue = 255;
	volatile LONG g_nObjectIdWorldRed = 255;
	volatile LONG g_nObjectIdWorldGreen = 255;
	volatile LONG g_nObjectIdWorldBlue = 0;
	volatile LONG g_nObjectIdSkyboxRed = 255;
	volatile LONG g_nObjectIdSkyboxGreen = 0;
	volatile LONG g_nObjectIdSkyboxBlue = 0;
	volatile LONG g_nPlayerChamsRed = 255;
	volatile LONG g_nPlayerChamsGreen = 72;
	volatile LONG g_nPlayerChamsBlue = 72;
	volatile LONG g_nViewmodelChamsRed = 72;
	volatile LONG g_nViewmodelChamsGreen = 160;
	volatile LONG g_nViewmodelChamsBlue = 255;
	volatile LONG g_nSkyboxChamsRed = 120;
	volatile LONG g_nSkyboxChamsGreen = 150;
	volatile LONG g_nSkyboxChamsBlue = 220;
	IMaterial *g_pFlatChamsMaterial = NULL;

	struct MaterialVectorState
	{
		IMaterialVar *pVar;
		float value[4];
		int components;
	};

	struct SkyboxMaterialState
	{
		IMaterial *pMaterial;
		float red;
		float green;
		float blue;
	};
	std::vector<SkyboxMaterialState> g_SkyboxMaterialStates;
	char g_szSkyboxChamsLevelName[MAX_PATH] = "";
	char g_szSkyboxChamsSkyName[128] = "";

	struct SmokeMaterialState
	{
		IMaterial *pMaterial;
		bool noDraw;
	};
	std::vector<SmokeMaterialState> g_SmokeMaterialStates;
	char g_szNoSmokeLevelName[MAX_PATH] = "";
	int g_nNoSmokeKnownMaterialCount = -1;

	ClientClass *g_pFlashNetvarClass = NULL;
	int g_nFlashDurationOffset = 0;
	int g_nFlashMaxAlphaOffset = 0;
	int g_nFlashBangTimeOffset = 0;
	bool g_bFlashNetvarsResolved = false;
	bool g_bFlashNetvarsAvailable = false;
	bool g_bFlashNetvarFailureLogged = false;

	IVModelInfo *g_pGuiModelInfo = NULL;
	int g_nCommandDispatchDepth = 0;

	void MaintainSkyboxChamsMaterials();
	void RestoreSkyboxChamsMaterials();
	void MaintainNoFlashState();
	void MaintainNoSmokeMaterials();
	void RestoreNoSmokeMaterials();
	void MaintainForcedRenderLod();
	bool InstallRenderLodCvarHook();
	void SetForceRenderLodEnabled( bool enabled );
	void RemoveD3D9Hooks();
	bool EnsureDefaultConfig();


	// -------------------------------------------------------------------------
	// General parsing, command dispatch, and error reporting
	// -------------------------------------------------------------------------
	bool IsTruthy( const char *pValue )
	{
		return pValue && ( !Q_stricmp( pValue, "on" ) || !Q_stricmp( pValue, "1" ) ||
			!Q_stricmp( pValue, "true" ) || !Q_stricmp( pValue, "yes" ) );
	}

	bool IsFalsy( const char *pValue )
	{
		return pValue && ( !Q_stricmp( pValue, "off" ) || !Q_stricmp( pValue, "0" ) ||
			!Q_stricmp( pValue, "false" ) || !Q_stricmp( pValue, "no" ) );
	}

	bool ParseSignedInteger( const char *pValue, int &value )
	{
		if ( !pValue || !pValue[0] )
			return false;
		char *pEnd = NULL;
		errno = 0;
		const long parsed = strtol( pValue, &pEnd, 10 );
		if ( pEnd == pValue || !pEnd || *pEnd != '\0' || errno == ERANGE ||
			parsed < INT_MIN || parsed > INT_MAX )
		{
			return false;
		}
		value = static_cast<int>( parsed );
		return true;
	}

	void SetError( const char *pFormat, ... )
	{
		va_list args;
		va_start( args, pFormat );
		_vsnprintf_s( g_Gui.lastError, sizeof( g_Gui.lastError ), _TRUNCATE, pFormat, args );
		va_end( args );
		LogMessage( "GUI ERROR: %s", g_Gui.lastError );
	}

	void ClearError()
	{
		g_Gui.lastError[0] = '\0';
	}

	void AddCommandHistory( const char *pCommand )
	{
		if ( !pCommand || !pCommand[0] )
			return;

		g_Gui.commandHistory.push_back( pCommand );
		while ( static_cast<int>( g_Gui.commandHistory.size() ) > kMaxCommandHistory )
			g_Gui.commandHistory.erase( g_Gui.commandHistory.begin() );
	}

	void IssueCommandV( bool printSeparator, const char *pFormat, va_list args )
	{
		ClearError();
		if ( !g_pEngine )
		{
			SetError( "engine command interface is not ready" );
			return;
		}

		char command[1024];
		_vsnprintf_s( command, sizeof( command ), _TRUNCATE, pFormat, args );
		if ( !command[0] )
			return;

		AddCommandHistory( command );
		LogMessage( "GUI COMMAND: %s", command );
		++g_nCommandDispatchDepth;
		g_pEngine->ExecuteClientCmd( command );
		--g_nCommandDispatchDepth;
		if ( printSeparator && g_nCommandDispatchDepth == 0 && g_pGameConsole && g_pGameConsole->IsConsoleVisible() )
			ArtConsoleMessage( "============================================\n" );
	}

	void IssueCommand( const char *pFormat, ... )
	{
		va_list args;
		va_start( args, pFormat );
		IssueCommandV( false, pFormat, args );
		va_end( args );
	}

	void IssueCommandWithSeparator( const char *pFormat, ... )
	{
		va_list args;
		va_start( args, pFormat );
		IssueCommandV( true, pFormat, args );
		va_end( args );
	}

	bool IsSafeConfigName( const char *pName )
	{
		if ( !pName || !pName[0] || strlen( pName ) >= sizeof( g_Gui.configName ) )
			return false;

		for ( const unsigned char *p = reinterpret_cast<const unsigned char *>( pName ); *p; ++p )
		{
			if ( !( ( *p >= 'a' && *p <= 'z' ) || ( *p >= 'A' && *p <= 'Z' ) ||
				( *p >= '0' && *p <= '9' ) || *p == '_' || *p == '-' ) )
				return false;
		}
		return true;
	}

	bool IsSafeQuotedArgument( const char *pValue )
	{
		if ( !pValue )
			return false;
		for ( const char *p = pValue; *p; ++p )
		{
			if ( *p == '"' || *p == ';' || *p == '\r' || *p == '\n' )
				return false;
		}
		return true;
	}

	bool ContainsInsensitive( const char *pText, const char *pNeedle )
	{
		if ( !pNeedle || !pNeedle[0] )
			return true;
		if ( !pText )
			return false;

		const size_t needleLength = strlen( pNeedle );
		for ( const char *p = pText; *p; ++p )
		{
			if ( !_strnicmp( p, pNeedle, needleLength ) )
				return true;
		}
		return false;
	}

	// -------------------------------------------------------------------------
	// Demo-proof r_lod override
	// -------------------------------------------------------------------------
	ConVar *ResolveRenderLodConVar()
	{
		if ( g_pRenderLodConVar )
			return g_pRenderLodConVar;
		if ( !g_pCvar )
			return NULL;

		ConVar *pVar = g_pCvar->FindVar( "r_lod" );
		if ( pVar )
		{
			if ( pVar != g_pRenderLodConVar )
				LogMessage( "R_LOD CVAR RESOLVED: ptr=%p value=%d", pVar, pVar->GetInt() );
			g_pRenderLodConVar = pVar;
			g_bRenderLodMissingLogged = false;
		}
		else if ( !g_bRenderLodMissingLogged )
		{
			g_bRenderLodMissingLogged = true;
			LogMessage( "R_LOD CVAR NOT FOUND: force override remains armed and will retry" );
		}
		return pVar;
	}

	void ApplyForcedRenderLod( const char *pReason )
	{
		if ( !InterlockedCompareExchange( &g_bForceRenderLodEnabled, FALSE, FALSE ) ||
			InterlockedCompareExchange( &g_bArtGuiTerminating, FALSE, FALSE ) )
		{
			return;
		}

		ConVar *pVar = ResolveRenderLodConVar();
		if ( !pVar )
			return;

		if ( !g_bRenderLodRestoreValueValid )
		{
			g_nRenderLodRestoreValue = pVar->GetInt();
			g_bRenderLodRestoreValueValid = true;
		}
		const int forcedValue = static_cast<int>( InterlockedCompareExchange(
			&g_nForcedRenderLodValue, 0, 0 ) );
		if ( pVar->GetInt() == forcedValue )
			return;
		if ( InterlockedCompareExchange( &g_bForceRenderLodCallbackActive, TRUE, FALSE ) != FALSE )
			return;

		const int previousValue = pVar->GetInt();
		pVar->SetValue( forcedValue );
		InterlockedExchange( &g_bForceRenderLodCallbackActive, FALSE );
		const LONG overrideCount = InterlockedIncrement( &g_nForcedRenderLodOverrideCount );
		if ( overrideCount <= 8 || overrideCount % 1000 == 0 )
		{
			LogMessage( "R_LOD FORCE APPLY: reason='%s' requested=%d forced=%d resulting=%d count=%ld",
				pReason ? pReason : "unknown", previousValue, forcedValue, pVar->GetInt(), overrideCount );
		}
	}

	void ArtRenderLodCvarChanged( ConVar *pVar, const char *pOldString )
	{
		if ( !pVar || !InterlockedCompareExchange( &g_bForceRenderLodEnabled, FALSE, FALSE ) ||
			InterlockedCompareExchange( &g_bArtGuiTerminating, FALSE, FALSE ) ||
			InterlockedCompareExchange( &g_bForceRenderLodCallbackActive, FALSE, FALSE ) )
		{
			return;
		}

		const char *pName = pVar->GetName();
		if ( pVar != g_pRenderLodConVar && ( !pName || Q_stricmp( pName, "r_lod" ) ) )
			return;

		g_pRenderLodConVar = pVar;
		const int forcedValue = static_cast<int>( InterlockedCompareExchange(
			&g_nForcedRenderLodValue, 0, 0 ) );
		if ( pVar->GetInt() == forcedValue )
			return;

		if ( InterlockedCompareExchange( &g_bForceRenderLodCallbackActive, TRUE, FALSE ) != FALSE )
			return;
		const int requestedValue = pVar->GetInt();
		if ( !g_bRenderLodRestoreValueValid )
		{
			g_nRenderLodRestoreValue = requestedValue;
			g_bRenderLodRestoreValueValid = true;
		}
		char oldValue[64];
		Q_strncpy( oldValue, pOldString ? pOldString : "", sizeof( oldValue ) );
		pVar->SetValue( forcedValue );
		InterlockedExchange( &g_bForceRenderLodCallbackActive, FALSE );

		const LONG overrideCount = InterlockedIncrement( &g_nForcedRenderLodOverrideCount );
		if ( overrideCount <= 8 || overrideCount % 1000 == 0 )
		{
			LogMessage( "R_LOD CVAR HOOK: old='%s' requested=%d forced=%d resulting=%d count=%ld",
				oldValue, requestedValue, forcedValue, pVar->GetInt(), overrideCount );
		}
	}

	bool InstallRenderLodCvarHook()
	{
		if ( g_bRenderLodCvarHookInstalled )
			return true;
		if ( !g_pCvar )
			return false;

		ResolveRenderLodConVar();
		g_pCvar->InstallGlobalChangeCallback( ArtRenderLodCvarChanged );
		g_bRenderLodCvarHookInstalled = true;
		LogMessage( "R_LOD CVAR HOOK INSTALLED: target=%p default_enabled=0 default_value=-7",
			g_pRenderLodConVar );
		return true;
	}

	void SetForceRenderLodEnabled( bool enabled )
	{
		if ( enabled )
		{
			ConVar *pVar = ResolveRenderLodConVar();
			if ( !InterlockedCompareExchange( &g_bForceRenderLodEnabled, FALSE, FALSE ) )
			{
				g_bRenderLodRestoreValueValid = pVar != NULL;
				if ( pVar )
					g_nRenderLodRestoreValue = pVar->GetInt();
				InterlockedExchange( &g_nForcedRenderLodOverrideCount, 0 );
			}
			InterlockedExchange( &g_bForceRenderLodEnabled, TRUE );
			ApplyForcedRenderLod( "enabled" );
			return;
		}

		const bool wasEnabled = InterlockedExchange( &g_bForceRenderLodEnabled, FALSE ) != FALSE;
		if ( wasEnabled && g_bRenderLodRestoreValueValid )
		{
			ConVar *pVar = ResolveRenderLodConVar();
			if ( pVar )
			{
				const int forcedValue = pVar->GetInt();
				pVar->SetValue( g_nRenderLodRestoreValue );
				LogMessage( "R_LOD FORCE DISABLED: forced=%d restored=%d resulting=%d",
					forcedValue, g_nRenderLodRestoreValue, pVar->GetInt() );
			}
		}
		g_bRenderLodRestoreValueValid = false;
	}

	void SetForcedRenderLodValue( int value )
	{
		InterlockedExchange( &g_nForcedRenderLodValue, value );
		ApplyForcedRenderLod( "value_changed" );
	}

	void MaintainForcedRenderLod()
	{
		// The global callback catches normal ConVar writes, including demo cvar
		// updates. This render-time check is a fallback for direct engine writes.
		ApplyForcedRenderLod( "pre_render" );
	}

	// -------------------------------------------------------------------------
	// Flash and smoke suppression
	// -------------------------------------------------------------------------
	bool FindGuiRecvPropOffset( RecvTable *pTable, const char *pName, int baseOffset,
		int recursionDepth, int &result )
	{
		if ( !pTable || !pName || recursionDepth > 16 )
			return false;
		for ( int i = 0; i < pTable->GetNumProps(); ++i )
		{
			RecvProp *pProp = pTable->GetProp( i );
			if ( !pProp )
				continue;
			const char *pPropName = pProp->GetName();
			if ( pPropName && !Q_stricmp( pPropName, pName ) )
			{
				result = baseOffset + pProp->GetOffset();
				return true;
			}
			if ( pProp->GetType() == DPT_DataTable && pProp->GetDataTable() &&
				FindGuiRecvPropOffset( pProp->GetDataTable(), pName,
					baseOffset + pProp->GetOffset(), recursionDepth + 1, result ) )
			{
				return true;
			}
		}
		return false;
	}

	bool ResolveFlashNetvars( ClientClass *pClass )
	{
		if ( !pClass || !pClass->m_pRecvTable )
			return false;
		if ( g_pFlashNetvarClass == pClass && g_bFlashNetvarsResolved )
			return g_bFlashNetvarsAvailable;

		g_pFlashNetvarClass = pClass;
		g_nFlashDurationOffset = 0;
		g_nFlashMaxAlphaOffset = 0;
		g_nFlashBangTimeOffset = 0;
		const bool durationFound = FindGuiRecvPropOffset( pClass->m_pRecvTable,
			"m_flFlashDuration", 0, 0, g_nFlashDurationOffset );
		const bool maxAlphaFound = FindGuiRecvPropOffset( pClass->m_pRecvTable,
			"m_flFlashMaxAlpha", 0, 0, g_nFlashMaxAlphaOffset );
		const bool bangTimeFound = FindGuiRecvPropOffset( pClass->m_pRecvTable,
			"m_flFlashBangTime", 0, 0, g_nFlashBangTimeOffset );
		g_bFlashNetvarsResolved = true;
		g_bFlashNetvarsAvailable = durationFound || maxAlphaFound || bangTimeFound;
		if ( g_bFlashNetvarsAvailable )
		{
			g_bFlashNetvarFailureLogged = false;
			LogMessage( "NOFLASH NETVARS: class='%s' duration=%d alpha=%d bang_time=%d",
				pClass->GetName(), durationFound ? g_nFlashDurationOffset : -1,
				maxAlphaFound ? g_nFlashMaxAlphaOffset : -1,
				bangTimeFound ? g_nFlashBangTimeOffset : -1 );
		}
		else if ( !g_bFlashNetvarFailureLogged )
		{
			g_bFlashNetvarFailureLogged = true;
			LogMessage( "NOFLASH UNAVAILABLE: flash receive properties were not found on class='%s'",
				pClass->GetName() );
		}
		return g_bFlashNetvarsAvailable;
	}

	void MaintainNoFlashState()
	{
		if ( !InterlockedCompareExchange( &g_bNoFlashEnabled, FALSE, FALSE ) ||
			!g_pEngine || !g_pEntityList )
		{
			return;
		}

		const int maxClients = g_pEngine->GetMaxClients();
		for ( int entityIndex = 1; entityIndex <= maxClients; ++entityIndex )
		{
			IClientNetworkable *pNetworkable = g_pEntityList->GetClientNetworkable( entityIndex );
			ClientClass *pClass = pNetworkable ? pNetworkable->GetClientClass() : NULL;
			const char *pClassName = pClass ? pClass->GetName() : NULL;
			if ( !pClassName || Q_stricmp( pClassName, "CCSPlayer" ) || !ResolveFlashNetvars( pClass ) )
				continue;

			unsigned char *pBase = static_cast<unsigned char *>( pNetworkable->GetDataTableBasePtr() );
			if ( !pBase )
				continue;
			if ( g_nFlashDurationOffset > 0 )
				*reinterpret_cast<float *>( pBase + g_nFlashDurationOffset ) = 0.0f;
			if ( g_nFlashMaxAlphaOffset > 0 )
				*reinterpret_cast<float *>( pBase + g_nFlashMaxAlphaOffset ) = 0.0f;
			if ( g_nFlashBangTimeOffset > 0 )
				*reinterpret_cast<float *>( pBase + g_nFlashBangTimeOffset ) = 0.0f;
		}
	}

	bool IsSmokeParticleMaterial( IMaterial *pMaterial )
	{
		if ( !pMaterial || pMaterial->IsErrorMaterial() )
			return false;
		const char *pName = pMaterial->GetName();
		const char *pGroup = pMaterial->GetTextureGroupName();
		if ( !ContainsInsensitive( pName, "smoke" ) )
			return false;
		return ContainsInsensitive( pGroup, "particle" ) ||
			ContainsInsensitive( pName, "particle/" ) ||
			ContainsInsensitive( pName, "particles/" );
	}

	void RestoreNoSmokeMaterials()
	{
		for ( size_t i = 0; i < g_SmokeMaterialStates.size(); ++i )
		{
			SmokeMaterialState &state = g_SmokeMaterialStates[i];
			if ( !state.pMaterial )
				continue;
			state.pMaterial->SetMaterialVarFlag( MATERIAL_VAR_NO_DRAW, state.noDraw );
			state.pMaterial->DecrementReferenceCount();
		}
		if ( !g_SmokeMaterialStates.empty() )
			LogMessage( "NOSMOKE RESTORED: materials=%d", static_cast<int>( g_SmokeMaterialStates.size() ) );
		g_SmokeMaterialStates.clear();
		g_nNoSmokeKnownMaterialCount = -1;
	}

	void ScanNoSmokeMaterials()
	{
		if ( !g_pMaterials )
			return;
		const MaterialHandle_t invalid = g_pMaterials->InvalidMaterial();
		for ( MaterialHandle_t handle = g_pMaterials->FirstMaterial(); handle != invalid;
			handle = g_pMaterials->NextMaterial( handle ) )
		{
			IMaterial *pMaterial = g_pMaterials->GetMaterial( handle );
			if ( !IsSmokeParticleMaterial( pMaterial ) )
				continue;
			SmokeMaterialState state;
			state.pMaterial = pMaterial;
			state.noDraw = pMaterial->GetMaterialVarFlag( MATERIAL_VAR_NO_DRAW );
			pMaterial->IncrementReferenceCount();
			pMaterial->SetMaterialVarFlag( MATERIAL_VAR_NO_DRAW, true );
			g_SmokeMaterialStates.push_back( state );
		}
		g_nNoSmokeKnownMaterialCount = g_pMaterials->GetNumMaterials();
		LogMessage( "NOSMOKE APPLIED: particle_materials=%d total_materials=%d",
			static_cast<int>( g_SmokeMaterialStates.size() ), g_nNoSmokeKnownMaterialCount );
	}

	void MaintainNoSmokeMaterials()
	{
		if ( !g_pMaterials || !g_pEngine )
			return;
		if ( InterlockedCompareExchange( &g_bNoSmokeUpdateActive, TRUE, FALSE ) != FALSE )
			return;

		const bool enabled = InterlockedCompareExchange( &g_bNoSmokeEnabled, FALSE, FALSE ) != FALSE;
		const char *pLevelName = g_pEngine->GetLevelName();
		if ( !pLevelName ) pLevelName = "";
		const bool levelChanged = Q_stricmp( g_szNoSmokeLevelName, pLevelName ) != 0;
		const int materialCount = g_pMaterials->GetNumMaterials();
		const bool materialCountChanged = g_nNoSmokeKnownMaterialCount >= 0 &&
			materialCount != g_nNoSmokeKnownMaterialCount;

		if ( !enabled )
		{
			if ( !g_SmokeMaterialStates.empty() )
				RestoreNoSmokeMaterials();
			Q_strncpy( g_szNoSmokeLevelName, pLevelName, sizeof( g_szNoSmokeLevelName ) );
			InterlockedExchange( &g_bNoSmokeRefreshPending, FALSE );
			InterlockedExchange( &g_bNoSmokeUpdateActive, FALSE );
			return;
		}

		if ( levelChanged || materialCountChanged ||
			InterlockedExchange( &g_bNoSmokeRefreshPending, FALSE ) != FALSE ||
			g_nNoSmokeKnownMaterialCount < 0 )
		{
			if ( !g_SmokeMaterialStates.empty() )
				RestoreNoSmokeMaterials();
			Q_strncpy( g_szNoSmokeLevelName, pLevelName, sizeof( g_szNoSmokeLevelName ) );
			ScanNoSmokeMaterials();
		}

		for ( size_t i = 0; i < g_SmokeMaterialStates.size(); ++i )
		{
			IMaterial *pMaterial = g_SmokeMaterialStates[i].pMaterial;
			if ( pMaterial && !pMaterial->GetMaterialVarFlag( MATERIAL_VAR_NO_DRAW ) )
				pMaterial->SetMaterialVarFlag( MATERIAL_VAR_NO_DRAW, true );
		}
		InterlockedExchange( &g_bNoSmokeUpdateActive, FALSE );
	}

	// -------------------------------------------------------------------------
	// Theme, color, and input-binding helpers
	// -------------------------------------------------------------------------
	int ClampColorByte( int value )
	{
		return value < 0 ? 0 : ( value > 255 ? 255 : value );
	}

	int ColorToByte( float value )
	{
		if ( value < 0.0f ) value = 0.0f;
		if ( value > 1.0f ) value = 1.0f;
		return static_cast<int>( value * 255.0f + 0.5f );
	}

	ImVec4 GuiColor( const float color[4] )
	{
		return ImVec4( color[0], color[1], color[2], color[3] );
	}

	ImVec4 MixColor( const ImVec4 &a, const ImVec4 &b, float amount )
	{
		return ImVec4( a.x + ( b.x - a.x ) * amount, a.y + ( b.y - a.y ) * amount,
			a.z + ( b.z - a.z ) * amount, a.w + ( b.w - a.w ) * amount );
	}

	void SetColor( float target[4], float red, float green, float blue, float alpha = 1.0f )
	{
		target[0] = red;
		target[1] = green;
		target[2] = blue;
		target[3] = alpha;
	}

	bool ApplyThemePreset( const char *pName )
	{
		if ( !pName )
			return false;

		SetColor( g_Gui.windowColor, 0.035f, 0.038f, 0.055f, 200.0f / 255.0f );
		SetColor( g_Gui.panelColor, 0.052f, 0.056f, 0.078f, 150.0f / 255.0f );
		SetColor( g_Gui.sidebarColor, 0.025f, 0.027f, 0.040f, 0.92f );
		SetColor( g_Gui.textColor, 0.92f, 0.93f, 0.97f, 1.0f );
		SetColor( g_Gui.mutedTextColor, 0.56f, 0.59f, 0.68f, 1.0f );
		SetColor( g_Gui.controlColor, 0.090f, 0.095f, 0.135f, 0.96f );

		if ( !Q_stricmp( pName, "purple" ) )
			SetColor( g_Gui.accentColor, 0.50f, 0.28f, 0.82f, 1.0f );
		else if ( !Q_stricmp( pName, "blue" ) )
			SetColor( g_Gui.accentColor, 0.16f, 0.48f, 0.92f, 1.0f );
		else if ( !Q_stricmp( pName, "green" ) )
			SetColor( g_Gui.accentColor, 0.14f, 0.68f, 0.42f, 1.0f );
		else if ( !Q_stricmp( pName, "orange" ) || !Q_stricmp( pName, "default" ) )
		{
			SetColor( g_Gui.accentColor, 209.0f / 255.0f, 114.0f / 255.0f, 71.0f / 255.0f, 1.0f );
			SetColor( g_Gui.windowColor, 14.0f / 255.0f, 10.0f / 255.0f, 9.0f / 255.0f, 200.0f / 255.0f );
			SetColor( g_Gui.panelColor, 20.0f / 255.0f, 15.0f / 255.0f, 13.0f / 255.0f, 150.0f / 255.0f );
			SetColor( g_Gui.sidebarColor, 10.0f / 255.0f, 8.0f / 255.0f, 6.0f / 255.0f, 235.0f / 255.0f );
			SetColor( g_Gui.textColor, 235.0f / 255.0f, 237.0f / 255.0f, 247.0f / 255.0f, 1.0f );
			SetColor( g_Gui.mutedTextColor, 143.0f / 255.0f, 150.0f / 255.0f, 173.0f / 255.0f, 1.0f );
			SetColor( g_Gui.controlColor, 34.0f / 255.0f, 27.0f / 255.0f, 23.0f / 255.0f, 245.0f / 255.0f );
		}
		else if ( !Q_stricmp( pName, "red" ) )
			SetColor( g_Gui.accentColor, 0.88f, 0.22f, 0.28f, 1.0f );
		else if ( !Q_stricmp( pName, "mono" ) || !Q_stricmp( pName, "gray" ) )
		{
			SetColor( g_Gui.accentColor, 0.62f, 0.64f, 0.70f, 1.0f );
			SetColor( g_Gui.windowColor, 0.045f, 0.047f, 0.052f, 200.0f / 255.0f );
			SetColor( g_Gui.panelColor, 0.070f, 0.073f, 0.080f, 150.0f / 255.0f );
			SetColor( g_Gui.sidebarColor, 0.034f, 0.036f, 0.040f, 0.92f );
			SetColor( g_Gui.controlColor, 0.100f, 0.103f, 0.112f, 0.96f );
		}
		else
			return false;

		if ( !Q_stricmp( pName, "orange" ) || !Q_stricmp( pName, "default" ) )
		{
			SetColor( g_Gui.borderColor, 88.0f / 255.0f, 52.0f / 255.0f, 35.0f / 255.0f, 217.0f / 255.0f );
			SetColor( g_Gui.selectionColor, 181.0f / 255.0f, 34.0f / 255.0f, 0.0f, 235.0f / 255.0f );
		}
		else
		{
			const ImVec4 accent = GuiColor( g_Gui.accentColor );
			const ImVec4 panel = GuiColor( g_Gui.panelColor );
			const ImVec4 border = MixColor( panel, accent, 0.36f );
			const ImVec4 selection = MixColor( panel, accent, 0.68f );
			SetColor( g_Gui.borderColor, border.x, border.y, border.z, 0.85f );
			SetColor( g_Gui.selectionColor, selection.x, selection.y, selection.z, 0.92f );
		}
		return true;
	}

	struct ToggleKeyName
	{
		const char *name;
		int key;
	};

	static const ToggleKeyName kToggleKeyNames[] =
	{
		{ "PAGEUP", VK_PRIOR }, { "PGUP", VK_PRIOR }, { "PAGEDOWN", VK_NEXT }, { "PGDN", VK_NEXT },
		{ "INSERT", VK_INSERT }, { "INS", VK_INSERT }, { "DELETE", VK_DELETE }, { "DEL", VK_DELETE },
		{ "HOME", VK_HOME }, { "END", VK_END }, { "PAUSE", VK_PAUSE }, { "SCROLLLOCK", VK_SCROLL },
		{ "F1", VK_F1 }, { "F2", VK_F2 }, { "F3", VK_F3 }, { "F4", VK_F4 },
		{ "F5", VK_F5 }, { "F6", VK_F6 }, { "F7", VK_F7 }, { "F8", VK_F8 },
		{ "F9", VK_F9 }, { "F10", VK_F10 }, { "F11", VK_F11 }, { "F12", VK_F12 }
	};

	bool IsModifierKey( int key )
	{
		return key == VK_SHIFT || key == VK_LSHIFT || key == VK_RSHIFT ||
			key == VK_CONTROL || key == VK_LCONTROL || key == VK_RCONTROL ||
			key == VK_MENU || key == VK_LMENU || key == VK_RMENU;
	}

	int ReadActiveToggleModifiers()
	{
		int modifiers = TOGGLE_MODIFIER_NONE;
		if ( GetKeyState( VK_SHIFT ) & 0x8000 ) modifiers |= TOGGLE_MODIFIER_SHIFT;
		if ( GetKeyState( VK_CONTROL ) & 0x8000 ) modifiers |= TOGGLE_MODIFIER_CONTROL;
		if ( GetKeyState( VK_MENU ) & 0x8000 ) modifiers |= TOGGLE_MODIFIER_ALT;
		return modifiers;
	}

	bool ParseSingleToggleKey( const char *pValue, int &key )
	{
		if ( !pValue || !pValue[0] )
			return false;

		char *pEnd = NULL;
		const long numeric = strtol( pValue, &pEnd, 10 );
		if ( pEnd && *pEnd == '\0' && numeric >= 1 && numeric <= 255 )
		{
			key = static_cast<int>( numeric );
			return true;
		}

		if ( pValue[1] == '\0' )
		{
			const unsigned char character = static_cast<unsigned char>( pValue[0] );
			if ( ( character >= 'a' && character <= 'z' ) || ( character >= 'A' && character <= 'Z' ) ||
				( character >= '0' && character <= '9' ) )
			{
				key = character >= 'a' && character <= 'z' ? character - 'a' + 'A' : character;
				return true;
			}
		}

		for ( int i = 0; i < ARRAYSIZE( kToggleKeyNames ); ++i )
		{
			if ( !Q_stricmp( pValue, kToggleKeyNames[i].name ) )
			{
				key = kToggleKeyNames[i].key;
				return true;
			}
		}
		return false;
	}

	bool ParseToggleBinding( const char *pValue, int &key, int &modifiers )
	{
		if ( !pValue || !pValue[0] )
			return false;

		char binding[96];
		Q_strncpy( binding, pValue, sizeof( binding ) );
		char *pWrite = binding;
		for ( const char *pRead = binding; *pRead; ++pRead )
		{
			if ( *pRead != ' ' && *pRead != '\t' )
				*pWrite++ = *pRead;
		}
		*pWrite = '\0';

		modifiers = TOGGLE_MODIFIER_NONE;
		key = 0;
		char *pContext = NULL;
		for ( char *pToken = strtok_s( binding, "+", &pContext ); pToken; pToken = strtok_s( NULL, "+", &pContext ) )
		{
			if ( !Q_stricmp( pToken, "SHIFT" ) ) modifiers |= TOGGLE_MODIFIER_SHIFT;
			else if ( !Q_stricmp( pToken, "CTRL" ) || !Q_stricmp( pToken, "CONTROL" ) ) modifiers |= TOGGLE_MODIFIER_CONTROL;
			else if ( !Q_stricmp( pToken, "ALT" ) ) modifiers |= TOGGLE_MODIFIER_ALT;
			else
			{
				if ( key != 0 || !ParseSingleToggleKey( pToken, key ) || IsModifierKey( key ) )
					return false;
			}
		}
		return key != 0;
	}

	void GetToggleKeyName( int key, char *pOutput, size_t outputBytes )
	{
		if ( !pOutput || outputBytes == 0 )
			return;

		for ( int i = 0; i < ARRAYSIZE( kToggleKeyNames ); ++i )
		{
			if ( kToggleKeyNames[i].key == key )
			{
				Q_strncpy( pOutput, kToggleKeyNames[i].name, static_cast<int>( outputBytes ) );
				return;
			}
		}

		if ( ( key >= 'A' && key <= 'Z' ) || ( key >= '0' && key <= '9' ) )
		{
			pOutput[0] = static_cast<char>( key );
			pOutput[1] = '\0';
			return;
		}

		Q_snprintf( pOutput, outputBytes, "VK_%d", key );
	}

	void GetToggleBindingName( int key, int modifiers, char *pOutput, size_t outputBytes, bool displayStyle )
	{
		if ( !pOutput || outputBytes == 0 )
			return;
		pOutput[0] = '\0';

		char keyName[32];
		GetToggleKeyName( key, keyName, sizeof( keyName ) );
		const char *pJoin = displayStyle ? " + " : "+";
		if ( modifiers & TOGGLE_MODIFIER_CONTROL )
			Q_strncat( pOutput, displayStyle ? "Ctrl" : "CTRL", static_cast<int>( outputBytes ) );
		if ( modifiers & TOGGLE_MODIFIER_SHIFT )
		{
			if ( pOutput[0] ) Q_strncat( pOutput, pJoin, static_cast<int>( outputBytes ) );
			Q_strncat( pOutput, displayStyle ? "Shift" : "SHIFT", static_cast<int>( outputBytes ) );
		}
		if ( modifiers & TOGGLE_MODIFIER_ALT )
		{
			if ( pOutput[0] ) Q_strncat( pOutput, pJoin, static_cast<int>( outputBytes ) );
			Q_strncat( pOutput, displayStyle ? "Alt" : "ALT", static_cast<int>( outputBytes ) );
		}
		if ( pOutput[0] ) Q_strncat( pOutput, pJoin, static_cast<int>( outputBytes ) );
		Q_strncat( pOutput, keyName, static_cast<int>( outputBytes ) );
	}

	struct HlaeInputHoldKeyName
	{
		const char *name;
		int key;
	};

	static const HlaeInputHoldKeyName kHlaeInputHoldKeyNames[] =
	{
		{ "LMB", VK_LBUTTON }, { "MOUSE1", VK_LBUTTON },
		{ "RMB", VK_RBUTTON }, { "MOUSE2", VK_RBUTTON },
		{ "MMB", VK_MBUTTON }, { "MOUSE3", VK_MBUTTON },
		{ "MOUSE4", VK_XBUTTON1 }, { "MOUSE5", VK_XBUTTON2 },
		{ "SHIFT", VK_SHIFT }, { "CTRL", VK_CONTROL }, { "CONTROL", VK_CONTROL },
		{ "ALT", VK_MENU }, { "SPACE", VK_SPACE }, { "TAB", VK_TAB },
		{ "CAPSLOCK", VK_CAPITAL }
	};

	bool ParseHlaeInputHoldKey( const char *pValue, int &key )
	{
		if ( !pValue || !pValue[0] )
			return false;

		for ( int i = 0; i < ARRAYSIZE( kHlaeInputHoldKeyNames ); ++i )
		{
			if ( !Q_stricmp( pValue, kHlaeInputHoldKeyNames[i].name ) )
			{
				key = kHlaeInputHoldKeyNames[i].key;
				return true;
			}
		}

		int parsedKey = 0;
		if ( ParseSingleToggleKey( pValue, parsedKey ) )
		{
			key = parsedKey;
			return true;
		}
		return false;
	}

	void GetHlaeInputHoldKeyName( int key, char *pOutput, size_t outputBytes )
	{
		if ( !pOutput || outputBytes == 0 )
			return;
		for ( int i = 0; i < ARRAYSIZE( kHlaeInputHoldKeyNames ); ++i )
		{
			if ( kHlaeInputHoldKeyNames[i].key == key )
			{
				Q_strncpy( pOutput, kHlaeInputHoldKeyNames[i].name,
					static_cast<int>( outputBytes ) );
				return;
			}
		}
		GetToggleKeyName( key, pOutput, outputBytes );
	}

	int HlaeInputMouseMessageKey( UINT message, WPARAM wParam )
	{
		switch ( message )
		{
		case WM_LBUTTONDOWN:
		case WM_LBUTTONDBLCLK:
			return VK_LBUTTON;
		case WM_RBUTTONDOWN:
		case WM_RBUTTONDBLCLK:
			return VK_RBUTTON;
		case WM_MBUTTONDOWN:
		case WM_MBUTTONDBLCLK:
			return VK_MBUTTON;
		case WM_XBUTTONDOWN:
		case WM_XBUTTONDBLCLK:
			return HIWORD( wParam ) == XBUTTON1 ? VK_XBUTTON1 : VK_XBUTTON2;
		default:
			return 0;
		}
	}

	void AppendFormat( std::string &output, const char *pFormat, ... )
	{
		char buffer[2048];
		va_list args;
		va_start( args, pFormat );
		_vsnprintf_s( buffer, sizeof( buffer ), _TRUNCATE, pFormat, args );
		va_end( args );
		output += buffer;
	}

	// -------------------------------------------------------------------------
	// Config and take-file management
	// -------------------------------------------------------------------------
	void BuildConfigDirectory( char *pOutput, size_t outputBytes )
	{
		const char *pGameDirectory = g_pEngine ? g_pEngine->GetGameDirectory() : NULL;
		Q_snprintf( pOutput, outputBytes, "%s\\cfg\\art_gui", pGameDirectory ? pGameDirectory : "." );
	}

	void BuildConfigPath( const char *pName, char *pOutput, size_t outputBytes )
	{
		char directory[MAX_PATH];
		BuildConfigDirectory( directory, sizeof( directory ) );
		Q_snprintf( pOutput, outputBytes, "%s\\%s.cfg", directory, pName );
	}

	bool EnsureConfigDirectory()
	{
		char cfgDirectory[MAX_PATH];
		BuildConfigDirectory( cfgDirectory, sizeof( cfgDirectory ) );

		char parent[MAX_PATH];
		Q_strncpy( parent, cfgDirectory, sizeof( parent ) );
		char *pSlash = strrchr( parent, '\\' );
		if ( pSlash )
		{
			*pSlash = '\0';
			CreateDirectoryA( parent, NULL );
		}

		if ( CreateDirectoryA( cfgDirectory, NULL ) || GetLastError() == ERROR_ALREADY_EXISTS )
			return true;

		SetError( "could not create %s (Win32 error %lu)", cfgDirectory, GetLastError() );
		return false;
	}

	bool BuildConfiguredOutputDirectory( char *pOutput, size_t outputBytes )
	{
		if ( !pOutput || outputBytes == 0 )
			return false;

		if ( g_bRecordBaseAbsolute )
		{
			Q_strncpy( pOutput, g_szRecordBase, static_cast<int>( outputBytes ) );
		}
		else
		{
			const char *pGameDirectory = g_pEngine ? g_pEngine->GetGameDirectory() : NULL;
			if ( !pGameDirectory || !pGameDirectory[0] )
				return false;
			Q_snprintf( pOutput, static_cast<int>( outputBytes ), "%s\\%s", pGameDirectory, g_szRecordBase );
		}
		Q_FixSlashes( pOutput, '\\' );
		return pOutput[0] != '\0';
	}

	bool CanonicalizeArtPath( const char *pInput, char *pOutput, size_t outputBytes )
	{
		if ( !pInput || !pInput[0] || !pOutput || outputBytes < 4 )
			return false;
		const DWORD written = GetFullPathNameA(
			pInput, static_cast<DWORD>( outputBytes ), pOutput, NULL );
		if ( written == 0 || written >= outputBytes )
			return false;
		Q_FixSlashes( pOutput, '\\' );
		size_t length = strlen( pOutput );
		while ( length > 3 && ( pOutput[length - 1] == '\\' || pOutput[length - 1] == '/' ) )
			pOutput[--length] = '\0';
		return true;
	}

	bool BuildSafeTakePath( const char *pName, char *pOutput, size_t outputBytes )
	{
		if ( !IsSafeTakeName( pName ) )
			return false;
		char configuredRoot[MAX_PATH];
		char canonicalRoot[MAX_PATH];
		if ( !BuildConfiguredOutputDirectory( configuredRoot, sizeof( configuredRoot ) ) ||
			!CanonicalizeArtPath( configuredRoot, canonicalRoot, sizeof( canonicalRoot ) ) )
			return false;

		char child[MAX_PATH];
		const int written = Q_snprintf( child, sizeof( child ), "%s\\%s", canonicalRoot, pName );
		if ( written < 0 || written >= static_cast<int>( sizeof( child ) ) ||
			!CanonicalizeArtPath( child, pOutput, outputBytes ) )
			return false;

		char parent[MAX_PATH];
		Q_strncpy( parent, pOutput, sizeof( parent ) );
		char *pSlash = strrchr( parent, '\\' );
		if ( !pSlash )
			return false;
		*pSlash = '\0';
		return !_stricmp( parent, canonicalRoot ) && _stricmp( pOutput, canonicalRoot );
	}

	void AccumulateTakeFootage( const char *pDirectory, int depth,
		unsigned long &files, unsigned __int64 &bytes )
	{
		if ( !pDirectory || depth > 4 )
			return;
		char pattern[MAX_PATH];
		const int written = Q_snprintf( pattern, sizeof( pattern ), "%s\\*", pDirectory );
		if ( written < 0 || written >= static_cast<int>( sizeof( pattern ) ) )
			return;
		WIN32_FIND_DATAA findData;
		HANDLE hFind = FindFirstFileA( pattern, &findData );
		if ( hFind == INVALID_HANDLE_VALUE )
			return;
		do
		{
			if ( !strcmp( findData.cFileName, "." ) || !strcmp( findData.cFileName, ".." ) )
				continue;
			if ( findData.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT )
				continue;
			char child[MAX_PATH];
			const int childWritten = Q_snprintf(
				child, sizeof( child ), "%s\\%s", pDirectory, findData.cFileName );
			if ( childWritten < 0 || childWritten >= static_cast<int>( sizeof( child ) ) )
				continue;
			if ( findData.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY )
			{
				AccumulateTakeFootage( child, depth + 1, files, bytes );
			}
			else
			{
				const char *pExtension = strrchr( findData.cFileName, '.' );
				if ( !pExtension || Q_stricmp( pExtension, ".tga" ) )
					continue;
				++files;
				bytes += ( static_cast<unsigned __int64>( findData.nFileSizeHigh ) << 32 ) |
					findData.nFileSizeLow;
			}
		}
		while ( FindNextFileA( hFind, &findData ) );
		FindClose( hFind );
	}

	void RefreshTakeList()
	{
		std::string previousName;
		if ( g_Gui.selectedTake >= 0 &&
			g_Gui.selectedTake < static_cast<int>( g_Gui.takes.size() ) )
			previousName = g_Gui.takes[g_Gui.selectedTake].name;
		g_Gui.takes.clear();
		g_Gui.selectedTake = -1;
		g_Gui.refreshTakes = false;

		char configuredRoot[MAX_PATH];
		if ( !BuildConfiguredOutputDirectory( configuredRoot, sizeof( configuredRoot ) ) ||
			!CanonicalizeArtPath( configuredRoot, g_Gui.takeRootSnapshot,
				sizeof( g_Gui.takeRootSnapshot ) ) )
			return;
		char pattern[MAX_PATH];
		const int written = Q_snprintf(
			pattern, sizeof( pattern ), "%s\\*", g_Gui.takeRootSnapshot );
		if ( written < 0 || written >= static_cast<int>( sizeof( pattern ) ) )
			return;

		WIN32_FIND_DATAA findData;
		HANDLE hFind = FindFirstFileA( pattern, &findData );
		if ( hFind == INVALID_HANDLE_VALUE )
			return;
		do
		{
			if ( !( findData.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY ) ||
				( findData.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT ) ||
				!strcmp( findData.cFileName, "." ) || !strcmp( findData.cFileName, ".." ) ||
				!IsSafeTakeName( findData.cFileName ) )
				continue;
			char safePath[MAX_PATH];
			if ( !BuildSafeTakePath( findData.cFileName, safePath, sizeof( safePath ) ) )
				continue;
			TakeEntry entry;
			entry.name = findData.cFileName;
			entry.absolutePath = safePath;
			entry.files = 0;
			entry.bytes = 0;
			entry.modified = findData.ftLastWriteTime;
			AccumulateTakeFootage( safePath, 0, entry.files, entry.bytes );
			g_Gui.takes.push_back( entry );
		}
		while ( FindNextFileA( hFind, &findData ) );
		FindClose( hFind );

		std::sort( g_Gui.takes.begin(), g_Gui.takes.end(),
			[]( const TakeEntry &left, const TakeEntry &right )
			{
				const LONG timeOrder = CompareFileTime( &left.modified, &right.modified );
				return timeOrder == 0 ? left.name < right.name : timeOrder > 0;
			} );
		for ( size_t i = 0; i < g_Gui.takes.size(); ++i )
		{
			if ( ( previousName.empty() && i == 0 ) ||
				( !previousName.empty() &&
					!_stricmp( previousName.c_str(), g_Gui.takes[i].name.c_str() ) ) )
			{
				g_Gui.selectedTake = static_cast<int>( i );
				Q_strncpy( g_Gui.takeRename, g_Gui.takes[i].name.c_str(), sizeof( g_Gui.takeRename ) );
				break;
			}
		}
	}

	bool ResolveSelectedTake( char *pOutput, size_t outputBytes )
	{
		if ( g_Gui.selectedTake < 0 ||
			g_Gui.selectedTake >= static_cast<int>( g_Gui.takes.size() ) )
			return false;
		const TakeEntry &entry = g_Gui.takes[g_Gui.selectedTake];
		if ( !BuildSafeTakePath( entry.name.c_str(), pOutput, outputBytes ) ||
			_stricmp( pOutput, entry.absolutePath.c_str() ) )
			return false;
		const DWORD attributes = GetFileAttributesA( pOutput );
		return attributes != INVALID_FILE_ATTRIBUTES &&
			( attributes & FILE_ATTRIBUTE_DIRECTORY ) &&
			!( attributes & FILE_ATTRIBUTE_REPARSE_POINT );
	}

	bool OpenSelectedTake()
	{
		char path[MAX_PATH];
		if ( !ResolveSelectedTake( path, sizeof( path ) ) )
		{
			SetError( "selected take is no longer a safe folder under the current output root" );
			return false;
		}
		const HINSTANCE result = ShellExecuteA( NULL, "open", path, NULL, NULL, SW_SHOWNORMAL );
		if ( reinterpret_cast<INT_PTR>( result ) <= 32 )
		{
			SetError( "could not open selected take (ShellExecute error %d)",
				static_cast<int>( reinterpret_cast<INT_PTR>( result ) ) );
			return false;
		}
		return true;
	}

	bool RenameSelectedTake()
	{
		if ( InterlockedCompareExchange( &g_bRecording, FALSE, FALSE ) )
		{
			SetError( "stop recording before renaming a take" );
			return false;
		}
		if ( !IsSafeTakeName( g_Gui.takeRename ) )
		{
			SetError( "take names may contain only letters, numbers, '_' and '-'" );
			return false;
		}
		char source[MAX_PATH];
		char destination[MAX_PATH];
		if ( !ResolveSelectedTake( source, sizeof( source ) ) ||
			!BuildSafeTakePath( g_Gui.takeRename, destination, sizeof( destination ) ) )
		{
			SetError( "selected take failed the output-root safety check" );
			return false;
		}
		if ( !_stricmp( source, destination ) )
			return true;
		if ( GetFileAttributesA( destination ) != INVALID_FILE_ATTRIBUTES )
		{
			SetError( "a take named %s already exists", g_Gui.takeRename );
			return false;
		}
		const std::string previousName = g_Gui.takes[g_Gui.selectedTake].name;
		if ( !MoveFileExA( source, destination, 0 ) )
		{
			SetError( "could not rename take (Win32 error %lu)", GetLastError() );
			return false;
		}
		char previousManifest[MAX_PATH];
		char renamedManifest[MAX_PATH];
		const int previousManifestLength = Q_snprintf( previousManifest,
			sizeof( previousManifest ), "%s\\%s.json", destination, previousName.c_str() );
		const int renamedManifestLength = Q_snprintf( renamedManifest,
			sizeof( renamedManifest ), "%s\\%s.json", destination, g_Gui.takeRename );
		if ( previousManifestLength >= 0 &&
			previousManifestLength < static_cast<int>( sizeof( previousManifest ) ) &&
			renamedManifestLength >= 0 &&
			renamedManifestLength < static_cast<int>( sizeof( renamedManifest ) ) &&
			GetFileAttributesA( previousManifest ) != INVALID_FILE_ATTRIBUTES &&
			GetFileAttributesA( renamedManifest ) == INVALID_FILE_ATTRIBUTES )
		{
			if ( !MoveFileExA( previousManifest, renamedManifest, 0 ) )
				LogMessage( "TAKE JSON RENAME WARNING: source='%s' destination='%s' error=%lu",
					previousManifest, renamedManifest, GetLastError() );
		}
		if ( !_stricmp( g_ArtRecordingStats.takeAbsolutePath, source ) )
		{
			Q_strncpy( g_ArtRecordingStats.takeAbsolutePath, destination,
				sizeof( g_ArtRecordingStats.takeAbsolutePath ) );
			Q_strncpy( g_ArtRecordingStats.takeDisplayPath, destination,
				sizeof( g_ArtRecordingStats.takeDisplayPath ) );
			Q_strncpy( g_ArtRecordingStats.takeName, g_Gui.takeRename,
				sizeof( g_ArtRecordingStats.takeName ) );
			WriteArtTakeManifest( true );
		}
		LogMessage( "TAKE RENAMED: source='%s' destination='%s'", source, destination );
		g_Gui.refreshTakes = true;
		return true;
	}

	bool RecycleSelectedTake()
	{
		if ( InterlockedCompareExchange( &g_bRecording, FALSE, FALSE ) )
		{
			SetError( "stop recording before recycling a take" );
			return false;
		}
		char path[MAX_PATH + 2] = {};
		if ( !ResolveSelectedTake( path, MAX_PATH ) )
		{
			SetError( "selected take failed the output-root safety check" );
			return false;
		}
		const std::string name = g_Gui.takes[g_Gui.selectedTake].name;
		path[strlen( path ) + 1] = '\0';
		SHFILEOPSTRUCTA operation = {};
		operation.hwnd = g_hGameWindow;
		operation.wFunc = FO_DELETE;
		operation.pFrom = path;
		operation.fFlags = FOF_ALLOWUNDO | FOF_NOCONFIRMATION |
			FOF_NOERRORUI | FOF_SILENT;
		const int result = SHFileOperationA( &operation );
		if ( result != 0 || operation.fAnyOperationsAborted )
		{
			SetError( "could not move take to the Recycle Bin (shell error %d)", result );
			return false;
		}
		LogMessage( "TAKE RECYCLED: name='%s' path='%s'", name.c_str(), path );
		g_Gui.refreshTakes = true;
		return true;
	}

	bool OpenConfiguredOutputDirectory()
	{
		char outputDirectory[MAX_PATH];
		if ( !BuildConfiguredOutputDirectory( outputDirectory, sizeof( outputDirectory ) ) )
		{
			SetError( "could not resolve the configured output folder" );
			return false;
		}

		if ( g_pFileSystem )
			g_pFileSystem->CreateDirHierarchy( g_szRecordBase, g_bRecordBaseAbsolute ? NULL : "MOD" );

		const HINSTANCE result = ShellExecuteA( NULL, "open", outputDirectory, NULL, NULL, SW_SHOWNORMAL );
		if ( reinterpret_cast<INT_PTR>( result ) <= 32 )
		{
			SetError( "could not open %s (ShellExecute error %d)", outputDirectory,
				static_cast<int>( reinterpret_cast<INT_PTR>( result ) ) );
			return false;
		}
		LogMessage( "OUTPUT FOLDER OPENED: path='%s'", outputDirectory );
		return true;
	}

	void ResetEverythingToDefaults()
	{
		if ( !EnsureDefaultConfig() )
			return;
		IssueCommand( "exec art_gui/art_default.cfg" );
		g_Gui.refreshDelay = 10;
		ArtConsoleMessage( "art_gui: all recorder and GUI settings reset to defaults.\n" );
	}

	bool EnsureDefaultConfig()
	{
		if ( !EnsureConfigDirectory() )
			return false;

		char path[MAX_PATH];
		BuildConfigPath( "art_default", path, sizeof( path ) );

		// art_default.cfg is the generated built-in reset profile, not a user
		// preset. Rewrite it on GUI installation so defaults added by newer builds
		// replace stale values left by an older injected DLL.
		static const char kDefaultConfig[] =
			"// CS:S V34 ADVANCED RECORDING TOOLS default config\r\n"
			"// Created automatically by the in-game GUI.\r\n\r\n"
			"art_gui_key SHIFT+F3\r\n"
			"art_gui_experimental off\r\n"
			"art_demo_pause_after_recording off\r\n"
			"art_demo_unpause_on_recording off\r\n"
			"art_hlae enabled 1\r\n"
			"art_hlae autoExport agr 0\r\n"
			"art_hlae autoExport camio 0\r\n"
			"art_hlae autoExport bvh 0\r\n"
			"art_hlae autoExport bvhFps 30\r\n"
			"art_hlae_input_while_gui off\r\n"
			"art_hlae_input_hold_key LMB\r\n"
			"mirv_campath draw enabled 0\r\n"
			"mirv_campath draw keyAxis 0\r\n"
			"mirv_campath draw keyCam 1\r\n"
			"mirv_campath draw keyIndex 18\r\n"
			"art_overlay on\r\n"
			"art_gui_theme orange\r\n"
			"art_gui_color accent 209 114 71 255\r\n"
			"art_gui_color window 14 10 9 200\r\n"
			"art_gui_color panel 20 15 13 150\r\n"
			"art_gui_color sidebar 10 8 6 235\r\n"
			"art_gui_color text 235 237 247 255\r\n"
			"art_gui_color muted 143 150 173 255\r\n"
			"art_gui_color border 88 52 35 217\r\n"
			"art_gui_color control 34 27 23 245\r\n"
			"art_gui_color selection 181 34 0 235\r\n"
			"art_validation auto off\r\n"
			"art_validation file_size on\r\n"
			"art_validation dropped_frames on\r\n"
			"art_validation min_size 18\r\n"
			"art_take_json on\r\n"
			"art_queue max_files 16\r\n"
			"art_queue max_mb 256\r\n"
			"art_queue reserve_mb 256\r\n"
			"art_tga_compression auto\r\n"
			"host_framerate 0\r\n"
			"art_preview off\r\n"
			"art_record normal on\r\n"
			"art_record clear off\r\n"
			"art_record clear-noplayers off\r\n"
			"art_record viewmodel off\r\n"
			"art_record depth off\r\n"
			"art_record players off\r\n"
			"art_record objectid off\r\n"
			"art_hud normal on\r\n"
			"art_hud clear on\r\n"
			"art_hud clear-noplayers on\r\n"
			"art_hud viewmodel off\r\n"
			"art_hud depth off\r\n"
			"art_hud players off\r\n"
			"art_hud objectid off\r\n"
			"art_viewmodel_color 0 255 0\r\n"
			"art_players_color 0 255 0\r\n"
			"art_fov default\r\n"
			"art_fov handleZoom enabled 1\r\n"
			"art_fov handleZoom minUnzoomedFov 90\r\n"
			"art_viewmodel_fov default\r\n"
			"art_chams players off\r\n"
			"art_chams viewmodel off\r\n"
			"art_chams skybox off\r\n"
			"art_chams players_through_walls off\r\n"
			"art_players_through_walls off\r\n"
			"art_players_world_weapons on\r\n"
			"art_visible viewmodel on\r\n"
			"art_visible players on\r\n"
			"art_noflash off\r\n"
			"art_nosmoke off\r\n"
			"art_force_r_lod value -7\r\n"
			"art_force_r_lod off\r\n"
			"art_objectid_color viewmodel 255 128 0\r\n"
			"art_objectid_color players 0 0 255\r\n"
			"art_objectid_color world 255 255 0\r\n"
			"art_objectid_color skybox 255 0 0\r\n"
			"art_chams players_color 255 72 72\r\n"
			"art_chams viewmodel_color 72 160 255\r\n"
			"art_chams skybox_color 120 150 220\r\n"
			"art_depth_start 150\r\n"
			"art_depth_end 800\r\n"
			"art_record path default\r\n"
			"art_prefix default\r\n"
			"art_debug off\r\n";

		HANDLE hFile = CreateFileA( path, GENERIC_WRITE, FILE_SHARE_READ, NULL, CREATE_ALWAYS,
			FILE_ATTRIBUTE_NORMAL, NULL );
		if ( hFile == INVALID_HANDLE_VALUE )
		{
			SetError( "could not write default config %s (Win32 error %lu)", path, GetLastError() );
			return false;
		}

		DWORD written = 0;
		const DWORD size = static_cast<DWORD>( sizeof( kDefaultConfig ) - 1 );
		const BOOL ok = WriteFile( hFile, kDefaultConfig, size, &written, NULL );
		CloseHandle( hFile );
		if ( !ok || written != size )
		{
			SetError( "incomplete write to default config %s", path );
			return false;
		}

		LogMessage( "CONFIG DEFAULT CREATED: path='%s' bytes=%lu", path, written );
		g_Gui.refreshConfigs = true;
		return true;
	}

	void RefreshConfigList()
	{
		g_Gui.configs.clear();
		g_Gui.selectedConfig = -1;
		g_Gui.refreshConfigs = false;

		if ( !EnsureConfigDirectory() )
			return;

		char directory[MAX_PATH];
		BuildConfigDirectory( directory, sizeof( directory ) );
		char pattern[MAX_PATH];
		Q_snprintf( pattern, sizeof( pattern ), "%s\\*.cfg", directory );

		WIN32_FIND_DATAA findData;
		HANDLE hFind = FindFirstFileA( pattern, &findData );
		if ( hFind == INVALID_HANDLE_VALUE )
			return;

		do
		{
			if ( findData.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY )
				continue;

			std::string name = findData.cFileName;
			const size_t extension = name.rfind( ".cfg" );
			if ( extension != std::string::npos )
				name.erase( extension );
			if ( IsSafeConfigName( name.c_str() ) )
				g_Gui.configs.push_back( name );
		}
		while ( FindNextFileA( hFind, &findData ) );
		FindClose( hFind );

		std::sort( g_Gui.configs.begin(), g_Gui.configs.end() );
		if ( !g_Gui.configs.empty() )
		{
			g_Gui.selectedConfig = 0;
			Q_strncpy( g_Gui.configName, g_Gui.configs[0].c_str(), sizeof( g_Gui.configName ) );
		}
	}

	std::string BuildCurrentConfig()
	{
		const LONG preview = InterlockedCompareExchange( &g_nPreviewPass, ART_PREVIEW_NONE, ART_PREVIEW_NONE );
		const LONG recordMask = InterlockedCompareExchange( &g_nRecordMask, 0, 0 );
		const LONG hudMask = InterlockedCompareExchange( &g_nHudMask, 0, 0 );
		ConVar *pHostFramerate = g_pCvar ? g_pCvar->FindVar( "host_framerate" ) : NULL;

		std::string cfg;
		AppendFormat( cfg, "// CS:S V34 ADVANCED RECORDING TOOLS GUI config\r\n" );
		AppendFormat( cfg, "// Generated by art_config save\r\n\r\n" );
		char toggleBinding[96];
		GetToggleBindingName( g_Gui.toggleKey, g_Gui.toggleModifiers, toggleBinding, sizeof( toggleBinding ), false );
		AppendFormat( cfg, "art_gui_key %s\r\n", toggleBinding );
		AppendFormat( cfg, "art_gui_experimental %s\r\n",
			g_Gui.experimentalOptionsEnabled ? "on" : "off" );
		AppendFormat( cfg, "art_demo_pause_after_recording %s\r\n",
			g_Gui.autoPauseDemoAfterRecording ? "on" : "off" );
		AppendFormat( cfg, "art_demo_unpause_on_recording %s\r\n",
			g_Gui.autoResumeDemoOnRecordingStart ? "on" : "off" );
		AppendFormat( cfg, "art_hlae_input_while_gui %s\r\n",
			g_Gui.hlaeInputWhileGui ? "on" : "off" );
		char hlaeInputHoldKey[32];
		GetHlaeInputHoldKeyName( g_Gui.hlaeInputHoldKey,
			hlaeInputHoldKey, sizeof( hlaeInputHoldKey ) );
		AppendFormat( cfg, "art_hlae_input_hold_key %s\r\n", hlaeInputHoldKey );
		AppendFormat( cfg, "art_overlay %s\r\n",
			InterlockedCompareExchange( &g_bArtStatisticsOverlayEnabled, FALSE, FALSE ) ? "on" : "off" );
		AppendFormat( cfg, "art_validation auto %s\r\n",
			InterlockedCompareExchange( &g_ArtValidationOptions.autoValidate, FALSE, FALSE ) ? "on" : "off" );
		AppendFormat( cfg, "art_validation file_size %s\r\n",
			InterlockedCompareExchange( &g_ArtValidationOptions.checkFileSize, FALSE, FALSE ) ? "on" : "off" );
		AppendFormat( cfg, "art_validation dropped_frames %s\r\n",
			InterlockedCompareExchange( &g_ArtValidationOptions.checkDroppedFrames, FALSE, FALSE ) ? "on" : "off" );
		AppendFormat( cfg, "art_validation min_size %ld\r\n",
			InterlockedCompareExchange( &g_ArtValidationOptions.minimumFileBytes, 0, 0 ) );
		AppendFormat( cfg, "art_take_json %s\r\n",
			InterlockedCompareExchange( &g_bArtTakeManifestEnabled, FALSE, FALSE ) ? "on" : "off" );
		AppendFormat( cfg, "art_queue max_files %ld\r\n",
			InterlockedCompareExchange( &g_ArtQueueOptions.maxFiles, 0, 0 ) );
		AppendFormat( cfg, "art_queue max_mb %ld\r\n",
			InterlockedCompareExchange( &g_ArtQueueOptions.maxMegabytes, 0, 0 ) );
		AppendFormat( cfg, "art_queue reserve_mb %ld\r\n",
			InterlockedCompareExchange( &g_ArtQueueOptions.reserveMegabytes, 0, 0 ) );
		AppendFormat( cfg, "art_tga_compression %s\r\n",
			ArtTgaCompressionModeName( InterlockedCompareExchange(
				&g_nArtTgaCompressionMode, 0, 0 ) ) );
		AppendFormat( cfg, "art_gui_color accent %d %d %d %d\r\n",
			ColorToByte( g_Gui.accentColor[0] ), ColorToByte( g_Gui.accentColor[1] ),
			ColorToByte( g_Gui.accentColor[2] ), ColorToByte( g_Gui.accentColor[3] ) );
		AppendFormat( cfg, "art_gui_color window %d %d %d %d\r\n",
			ColorToByte( g_Gui.windowColor[0] ), ColorToByte( g_Gui.windowColor[1] ),
			ColorToByte( g_Gui.windowColor[2] ), ColorToByte( g_Gui.windowColor[3] ) );
		AppendFormat( cfg, "art_gui_color panel %d %d %d %d\r\n",
			ColorToByte( g_Gui.panelColor[0] ), ColorToByte( g_Gui.panelColor[1] ),
			ColorToByte( g_Gui.panelColor[2] ), ColorToByte( g_Gui.panelColor[3] ) );
		AppendFormat( cfg, "art_gui_color sidebar %d %d %d %d\r\n",
			ColorToByte( g_Gui.sidebarColor[0] ), ColorToByte( g_Gui.sidebarColor[1] ),
			ColorToByte( g_Gui.sidebarColor[2] ), ColorToByte( g_Gui.sidebarColor[3] ) );
		AppendFormat( cfg, "art_gui_color text %d %d %d %d\r\n",
			ColorToByte( g_Gui.textColor[0] ), ColorToByte( g_Gui.textColor[1] ),
			ColorToByte( g_Gui.textColor[2] ), ColorToByte( g_Gui.textColor[3] ) );
		AppendFormat( cfg, "art_gui_color muted %d %d %d %d\r\n",
			ColorToByte( g_Gui.mutedTextColor[0] ), ColorToByte( g_Gui.mutedTextColor[1] ),
			ColorToByte( g_Gui.mutedTextColor[2] ), ColorToByte( g_Gui.mutedTextColor[3] ) );
		AppendFormat( cfg, "art_gui_color border %d %d %d %d\r\n",
			ColorToByte( g_Gui.borderColor[0] ), ColorToByte( g_Gui.borderColor[1] ),
			ColorToByte( g_Gui.borderColor[2] ), ColorToByte( g_Gui.borderColor[3] ) );
		AppendFormat( cfg, "art_gui_color control %d %d %d %d\r\n",
			ColorToByte( g_Gui.controlColor[0] ), ColorToByte( g_Gui.controlColor[1] ),
			ColorToByte( g_Gui.controlColor[2] ), ColorToByte( g_Gui.controlColor[3] ) );
		AppendFormat( cfg, "art_gui_color selection %d %d %d %d\r\n",
			ColorToByte( g_Gui.selectionColor[0] ), ColorToByte( g_Gui.selectionColor[1] ),
			ColorToByte( g_Gui.selectionColor[2] ), ColorToByte( g_Gui.selectionColor[3] ) );
		if ( pHostFramerate )
			AppendFormat( cfg, "host_framerate %g\r\n", pHostFramerate->GetFloat() );
		AppendFormat( cfg, "art_preview off\r\n" );
		if ( preview != ART_PREVIEW_NONE )
			AppendFormat( cfg, "art_preview %s\r\n", PreviewName( preview ) );

		static const struct PassDefinition
		{
			const char *name;
			LONG bit;
		} passes[] =
		{
			{ "normal", ART_RECORD_NORMAL },
			{ "clear", ART_RECORD_CLEAR },
			{ "clear-noplayers", ART_RECORD_CLEAR_NOPLAYERS },
			{ "viewmodel", ART_RECORD_VIEWMODEL },
			{ "depth", ART_RECORD_DEPTH },
			{ "players", ART_RECORD_PLAYERS },
			{ "objectid", ART_RECORD_OBJECTID }
		};

		for ( int i = 0; i < ARRAYSIZE( passes ); ++i )
			AppendFormat( cfg, "art_record %s %s\r\n", passes[i].name, ( recordMask & passes[i].bit ) ? "on" : "off" );
		for ( int i = 0; i < ARRAYSIZE( passes ); ++i )
			AppendFormat( cfg, "art_hud %s %s\r\n", passes[i].name, ( hudMask & passes[i].bit ) ? "on" : "off" );

		AppendFormat( cfg, "art_viewmodel_color %d %d %d\r\n",
			g_nViewmodelBackgroundRed, g_nViewmodelBackgroundGreen, g_nViewmodelBackgroundBlue );
		AppendFormat( cfg, "art_players_color %d %d %d\r\n",
			g_nPlayersBackgroundRed, g_nPlayersBackgroundGreen, g_nPlayersBackgroundBlue );
		ArtHlaeStatus hlaeStatus;
		GetArtHlaeStatus( hlaeStatus );
		AppendFormat( cfg, "art_hlae enabled %d\r\n", hlaeStatus.enabled ? 1 : 0 );
		AppendFormat( cfg, "art_hlae autoExport agr %d\r\n",
			hlaeStatus.autoExportAgr ? 1 : 0 );
		AppendFormat( cfg, "art_hlae autoExport camio %d\r\n",
			hlaeStatus.autoExportCamio ? 1 : 0 );
		AppendFormat( cfg, "art_hlae autoExport bvh %d\r\n",
			hlaeStatus.autoExportBvh ? 1 : 0 );
		AppendFormat( cfg, "art_hlae autoExport bvhFps %g\r\n",
			hlaeStatus.autoExportBvhFps );
		AppendFormat( cfg, "mirv_campath draw enabled %d\r\n",
			hlaeStatus.campathDraw ? 1 : 0 );
		AppendFormat( cfg, "mirv_campath draw keyAxis %d\r\n",
			hlaeStatus.campathDrawKeyAxis ? 1 : 0 );
		AppendFormat( cfg, "mirv_campath draw keyCam %d\r\n",
			hlaeStatus.campathDrawKeyCam ? 1 : 0 );
		AppendFormat( cfg, "mirv_campath draw keyIndex %g\r\n",
			hlaeStatus.campathDrawKeyIndex );
		if ( g_Gui.globalFovDefault )
			AppendFormat( cfg, "art_fov default\r\n" );
		else
			AppendFormat( cfg, "art_fov %g\r\n", g_flGlobalFov );
		AppendFormat( cfg, "art_fov handleZoom enabled %d\r\n",
			InterlockedCompareExchange( &g_bGlobalFovHandleZoom, FALSE, FALSE ) ? 1 : 0 );
		AppendFormat( cfg, "art_fov handleZoom minUnzoomedFov %g\r\n", g_flGlobalFovMinUnzoomedFov );
		AppendFormat( cfg, "art_viewmodel_fov %g\r\n", g_flViewmodelFov );
		AppendFormat( cfg, "art_chams players %s\r\n",
			InterlockedCompareExchange( &g_bPlayerChamsEnabled, FALSE, FALSE ) ? "on" : "off" );
		AppendFormat( cfg, "art_chams viewmodel %s\r\n",
			InterlockedCompareExchange( &g_bViewmodelChamsEnabled, FALSE, FALSE ) ? "on" : "off" );
		AppendFormat( cfg, "art_chams skybox %s\r\n",
			InterlockedCompareExchange( &g_bSkyboxChamsEnabled, FALSE, FALSE ) ? "on" : "off" );
		AppendFormat( cfg, "art_chams players_through_walls %s\r\n",
			InterlockedCompareExchange( &g_bPlayerChamsThroughWalls, FALSE, FALSE ) ? "on" : "off" );
		AppendFormat( cfg, "art_players_through_walls %s\r\n",
			InterlockedCompareExchange( &g_bPlayersPassThroughWalls, FALSE, FALSE ) ? "on" : "off" );
		AppendFormat( cfg, "art_players_world_weapons %s\r\n",
			InterlockedCompareExchange( &g_bPlayersPassWorldWeapons, FALSE, FALSE ) ? "on" : "off" );
		AppendFormat( cfg, "art_visible viewmodel %s\r\n",
			InterlockedCompareExchange( &g_bViewmodelVisible, FALSE, FALSE ) ? "on" : "off" );
		AppendFormat( cfg, "art_visible players %s\r\n",
			InterlockedCompareExchange( &g_bPlayersVisible, FALSE, FALSE ) ? "on" : "off" );
		AppendFormat( cfg, "art_noflash %s\r\n",
			InterlockedCompareExchange( &g_bNoFlashEnabled, FALSE, FALSE ) ? "on" : "off" );
		AppendFormat( cfg, "art_nosmoke %s\r\n",
			InterlockedCompareExchange( &g_bNoSmokeEnabled, FALSE, FALSE ) ? "on" : "off" );
		AppendFormat( cfg, "art_force_r_lod value %ld\r\n",
			InterlockedCompareExchange( &g_nForcedRenderLodValue, 0, 0 ) );
		AppendFormat( cfg, "art_force_r_lod %s\r\n",
			InterlockedCompareExchange( &g_bForceRenderLodEnabled, FALSE, FALSE ) ? "on" : "off" );
		AppendFormat( cfg, "art_objectid_color viewmodel %ld %ld %ld\r\n",
			InterlockedCompareExchange( &g_nObjectIdViewmodelRed, 0, 0 ),
			InterlockedCompareExchange( &g_nObjectIdViewmodelGreen, 0, 0 ),
			InterlockedCompareExchange( &g_nObjectIdViewmodelBlue, 0, 0 ) );
		AppendFormat( cfg, "art_objectid_color players %ld %ld %ld\r\n",
			InterlockedCompareExchange( &g_nObjectIdPlayersRed, 0, 0 ),
			InterlockedCompareExchange( &g_nObjectIdPlayersGreen, 0, 0 ),
			InterlockedCompareExchange( &g_nObjectIdPlayersBlue, 0, 0 ) );
		AppendFormat( cfg, "art_objectid_color world %ld %ld %ld\r\n",
			InterlockedCompareExchange( &g_nObjectIdWorldRed, 0, 0 ),
			InterlockedCompareExchange( &g_nObjectIdWorldGreen, 0, 0 ),
			InterlockedCompareExchange( &g_nObjectIdWorldBlue, 0, 0 ) );
		AppendFormat( cfg, "art_objectid_color skybox %ld %ld %ld\r\n",
			InterlockedCompareExchange( &g_nObjectIdSkyboxRed, 0, 0 ),
			InterlockedCompareExchange( &g_nObjectIdSkyboxGreen, 0, 0 ),
			InterlockedCompareExchange( &g_nObjectIdSkyboxBlue, 0, 0 ) );
		AppendFormat( cfg, "art_chams players_color %ld %ld %ld\r\n",
			InterlockedCompareExchange( &g_nPlayerChamsRed, 0, 0 ),
			InterlockedCompareExchange( &g_nPlayerChamsGreen, 0, 0 ),
			InterlockedCompareExchange( &g_nPlayerChamsBlue, 0, 0 ) );
		AppendFormat( cfg, "art_chams viewmodel_color %ld %ld %ld\r\n",
			InterlockedCompareExchange( &g_nViewmodelChamsRed, 0, 0 ),
			InterlockedCompareExchange( &g_nViewmodelChamsGreen, 0, 0 ),
			InterlockedCompareExchange( &g_nViewmodelChamsBlue, 0, 0 ) );
		AppendFormat( cfg, "art_chams skybox_color %ld %ld %ld\r\n",
			InterlockedCompareExchange( &g_nSkyboxChamsRed, 0, 0 ),
			InterlockedCompareExchange( &g_nSkyboxChamsGreen, 0, 0 ),
			InterlockedCompareExchange( &g_nSkyboxChamsBlue, 0, 0 ) );
		AppendFormat( cfg, "art_depth_start %g\r\n", art_depth_start.GetFloat() );
		AppendFormat( cfg, "art_depth_end %g\r\n", art_depth_end.GetFloat() );

		if ( !g_bRecordBaseAbsolute && !Q_stricmp( g_szRecordBase, "art" ) )
			AppendFormat( cfg, "art_record path default\r\n" );
		else if ( IsSafeQuotedArgument( g_szRecordBase ) )
			AppendFormat( cfg, "art_record path \"%s\"\r\n", g_szRecordBase );

		if ( g_szCapturePrefix[0] )
			AppendFormat( cfg, "art_prefix %s\r\n", g_szCapturePrefix );
		else
			AppendFormat( cfg, "art_prefix default\r\n" );

		AppendFormat( cfg, "art_debug %s\r\n", IsDebugLoggingEnabled() ? "on" : "off" );
		return cfg;
	}

	bool SaveConfig( const char *pName )
	{
		ClearError();
		if ( !IsSafeConfigName( pName ) )
		{
			SetError( "config names may contain only letters, numbers, '_' and '-'" );
			return false;
		}
		if ( !EnsureConfigDirectory() )
			return false;

		char path[MAX_PATH];
		BuildConfigPath( pName, path, sizeof( path ) );
		const std::string content = BuildCurrentConfig();

		HANDLE hFile = CreateFileA( path, GENERIC_WRITE, FILE_SHARE_READ, NULL, CREATE_ALWAYS,
			FILE_ATTRIBUTE_NORMAL, NULL );
		if ( hFile == INVALID_HANDLE_VALUE )
		{
			SetError( "could not write %s (Win32 error %lu)", path, GetLastError() );
			return false;
		}

		DWORD written = 0;
		const BOOL ok = WriteFile( hFile, content.data(), static_cast<DWORD>( content.size() ), &written, NULL );
		CloseHandle( hFile );
		if ( !ok || written != static_cast<DWORD>( content.size() ) )
		{
			SetError( "incomplete write to %s", path );
			return false;
		}

		ArtConsoleMessage( "art_config: saved '%s'.\n", pName );
		LogMessage( "CONFIG SAVE: name='%s' path='%s' bytes=%lu", pName, path, written );
		g_Gui.refreshConfigs = true;
		return true;
	}

	bool LoadConfig( const char *pName )
	{
		ClearError();
		if ( !IsSafeConfigName( pName ) )
		{
			SetError( "invalid config name" );
			return false;
		}

		char path[MAX_PATH];
		BuildConfigPath( pName, path, sizeof( path ) );
		if ( GetFileAttributesA( path ) == INVALID_FILE_ATTRIBUTES )
		{
			SetError( "config '%s' does not exist", pName );
			return false;
		}

		ArtConsoleMessage( "art_config: loading '%s'.\n", pName );
		IssueCommand( "exec art_gui/%s.cfg", pName );
		g_Gui.refreshDelay = 10;
		return true;
	}

	bool DeleteConfig( const char *pName )
	{
		ClearError();
		if ( !IsSafeConfigName( pName ) )
		{
			SetError( "invalid config name" );
			return false;
		}

		char path[MAX_PATH];
		BuildConfigPath( pName, path, sizeof( path ) );
		if ( !DeleteFileA( path ) )
		{
			SetError( "could not delete '%s' (Win32 error %lu)", pName, GetLastError() );
			return false;
		}

		ArtConsoleMessage( "art_config: deleted '%s'.\n", pName );
		LogMessage( "CONFIG DELETE: name='%s' path='%s'", pName, path );
		g_Gui.refreshConfigs = true;
		return true;
	}

	void PrintConfigList()
	{
		RefreshConfigList();
		if ( g_Gui.configs.empty() )
		{
			ArtConsoleMessage( "art_config: no configs in cfg/art_gui.\n" );
			return;
		}

		ArtConsoleMessage( "art_config: %d config(s):\n", static_cast<int>( g_Gui.configs.size() ) );
		for ( size_t i = 0; i < g_Gui.configs.size(); ++i )
			ArtConsoleMessage( "  %s\n", g_Gui.configs[i].c_str() );
	}

	// -------------------------------------------------------------------------
	// Window input ownership and mirv_input passthrough
	// -------------------------------------------------------------------------
	bool GuiOwnsMouse()
	{
		return !InterlockedCompareExchange( &g_bArtGuiTerminating, FALSE, FALSE ) && IsArtGuiVisible() && g_Gui.imguiReady;
	}

	void ForceSoftwareCursor()
	{
		if ( !GuiOwnsMouse() )
			return;

		ImGui::GetIO().MouseDrawCursor = true;
		ReleaseCapture();
		if ( g_pOriginalClipCursor )
			g_pOriginalClipCursor( NULL );
		if ( g_pOriginalSetCursor )
			g_pOriginalSetCursor( NULL );
	}

	void StoreAndDisableGameMouse()
	{
		if ( !g_pCvar )
			return;

		ConVar *pMouseEnable = g_pCvar->FindVar( "cl_mouseenable" );
		if ( !pMouseEnable )
			return;
		if ( !g_Gui.mouseStateStored )
		{
			Q_strncpy( g_Gui.previousMouseEnable, pMouseEnable->GetString(), sizeof( g_Gui.previousMouseEnable ) );
			g_Gui.mouseStateStored = true;
		}
		pMouseEnable->SetValue( "0" );
	}

	void RestoreGameMouse()
	{
		if ( g_pCvar && g_Gui.mouseStateStored )
		{
			ConVar *pMouseEnable = g_pCvar->FindVar( "cl_mouseenable" );
			if ( pMouseEnable )
				pMouseEnable->SetValue( g_Gui.previousMouseEnable[0] ? g_Gui.previousMouseEnable : "1" );
		}
		g_Gui.mouseStateStored = false;
		g_Gui.previousMouseEnable[0] = '\0';
	}

	void UpdateMouseCaptureMode()
	{
		if ( !GuiOwnsMouse() )
			return;

		if ( !g_Gui.mouseStateStored )
			StoreAndDisableGameMouse();
		ForceSoftwareCursor();
	}

	bool IsKeyboardMessage( UINT message )
	{
		return message == WM_KEYDOWN || message == WM_KEYUP || message == WM_SYSKEYDOWN ||
			message == WM_SYSKEYUP || message == WM_CHAR;
	}

	bool IsMouseMessage( UINT message )
	{
		return ( message >= WM_MOUSEFIRST && message <= WM_MOUSELAST ) || message == WM_INPUT;
	}

	void ShutdownImGui();
	void EnterPassiveWindowShutdown( HWND hWnd );
	bool IsPointInsideMainGuiWindow( LONG x, LONG y );

	bool IsHlaeInputHoldKeyDown()
	{
		return 0 != g_Gui.hlaeInputHoldKey &&
			( GetAsyncKeyState( g_Gui.hlaeInputHoldKey ) & 0x8000 ) != 0;
	}

	bool IsHlaeInputHoldMouseButton( int key )
	{
		return key == VK_LBUTTON || key == VK_RBUTTON || key == VK_MBUTTON ||
			key == VK_XBUTTON1 || key == VK_XBUTTON2;
	}

	void SynchronizeHlaeInputCursorAnchor()
	{
		POINT cursor;
		const BOOL cursorRead = g_pOriginalGetCursorPos ?
			g_pOriginalGetCursorPos( &cursor ) : GetCursorPos( &cursor );
		if ( cursorRead )
			NotifyArtHlaeCursorWarp( cursor.x, cursor.y );
	}

	void FilterHlaeInputHoldKeyFromRawInput( RAWINPUT &rawInput )
	{
		if ( rawInput.header.dwType == RIM_TYPEKEYBOARD )
		{
			if ( rawInput.data.keyboard.VKey == static_cast<USHORT>( g_Gui.hlaeInputHoldKey ) )
			{
				rawInput.data.keyboard.MakeCode = 0;
				rawInput.data.keyboard.VKey = 0;
				rawInput.data.keyboard.Message = 0;
				rawInput.data.keyboard.ExtraInformation = 0;
			}
			return;
		}
		if ( rawInput.header.dwType != RIM_TYPEMOUSE )
			return;

		USHORT buttonMask = 0;
		switch ( g_Gui.hlaeInputHoldKey )
		{
		case VK_LBUTTON: buttonMask = RI_MOUSE_LEFT_BUTTON_DOWN | RI_MOUSE_LEFT_BUTTON_UP; break;
		case VK_RBUTTON: buttonMask = RI_MOUSE_RIGHT_BUTTON_DOWN | RI_MOUSE_RIGHT_BUTTON_UP; break;
		case VK_MBUTTON: buttonMask = RI_MOUSE_MIDDLE_BUTTON_DOWN | RI_MOUSE_MIDDLE_BUTTON_UP; break;
		case VK_XBUTTON1: buttonMask = RI_MOUSE_BUTTON_4_DOWN | RI_MOUSE_BUTTON_4_UP; break;
		case VK_XBUTTON2: buttonMask = RI_MOUSE_BUTTON_5_DOWN | RI_MOUSE_BUTTON_5_UP; break;
		default: break;
		}
		rawInput.data.mouse.usButtonFlags &= ~buttonMask;
	}

	bool IsHlaeInputHoldMouseMessage( UINT message, WPARAM wParam )
	{
		switch ( g_Gui.hlaeInputHoldKey )
		{
		case VK_LBUTTON:
			return message == WM_LBUTTONDOWN || message == WM_LBUTTONUP ||
				message == WM_LBUTTONDBLCLK;
		case VK_RBUTTON:
			return message == WM_RBUTTONDOWN || message == WM_RBUTTONUP ||
				message == WM_RBUTTONDBLCLK;
		case VK_MBUTTON:
			return message == WM_MBUTTONDOWN || message == WM_MBUTTONUP ||
				message == WM_MBUTTONDBLCLK;
		case VK_XBUTTON1:
			return ( message == WM_XBUTTONDOWN || message == WM_XBUTTONUP ||
				message == WM_XBUTTONDBLCLK ) && HIWORD( wParam ) == XBUTTON1;
		case VK_XBUTTON2:
			return ( message == WM_XBUTTONDOWN || message == WM_XBUTTONUP ||
				message == WM_XBUTTONDBLCLK ) && HIWORD( wParam ) == XBUTTON2;
		default:
			return false;
		}
	}

	WPARAM FilterHlaeInputHoldMouseState( WPARAM wParam )
	{
		switch ( g_Gui.hlaeInputHoldKey )
		{
		case VK_LBUTTON: return wParam & ~static_cast<WPARAM>( MK_LBUTTON );
		case VK_RBUTTON: return wParam & ~static_cast<WPARAM>( MK_RBUTTON );
		case VK_MBUTTON: return wParam & ~static_cast<WPARAM>( MK_MBUTTON );
		case VK_XBUTTON1: return wParam & ~static_cast<WPARAM>( MK_XBUTTON1 );
		case VK_XBUTTON2: return wParam & ~static_cast<WPARAM>( MK_XBUTTON2 );
		default: return wParam;
		}
	}

	bool IsCursorOutsideMainGuiWindow()
	{
		if ( !g_hGameWindow || g_Gui.mainWindowWidth <= 0.0f ||
			g_Gui.mainWindowHeight <= 0.0f )
			return false;
		POINT cursor;
		const BOOL cursorRead = g_pOriginalGetCursorPos ?
			g_pOriginalGetCursorPos( &cursor ) : GetCursorPos( &cursor );
		if ( !cursorRead || !ScreenToClient( g_hGameWindow, &cursor ) )
			return false;
		return !IsPointInsideMainGuiWindow( cursor.x, cursor.y );
	}

	void ReleaseHlaeInputPassthroughControls()
	{
		static const WPARAM keys[] =
		{
			'W', 'S', 'A', 'D', 'R', 'F', 'X', 'Z',
			VK_NUMPAD8, VK_NUMPAD2, VK_NUMPAD4, VK_NUMPAD6,
			VK_NUMPAD9, VK_NUMPAD3, VK_NUMPAD1, VK_NUMPAD7,
			VK_NUMPAD5, VK_NUMPAD0, VK_DECIMAL,
			VK_DOWN, VK_UP, VK_LEFT, VK_RIGHT,
			VK_ADD, VK_SUBTRACT, VK_CONTROL
		};
		for ( int i = 0; i < ARRAYSIZE( keys ); ++i )
			SupplyArtHlaeKeyEvent( false, keys[i], 0 );

		if ( !IsHlaeInputHoldMouseButton( g_Gui.hlaeInputHoldKey ) )
		{
			SupplyArtHlaeKeyEvent( false, g_Gui.hlaeInputHoldKey, 0 );
		}

		WPARAM mouseWParam = 0;
		LPARAM mouseLParam = 0;
		SupplyArtHlaeMouseEvent( WM_LBUTTONUP, mouseWParam, mouseLParam );
		SupplyArtHlaeMouseEvent( WM_RBUTTONUP, mouseWParam, mouseLParam );
		SupplyArtHlaeMouseEvent( WM_MBUTTONUP, mouseWParam, mouseLParam );
		mouseWParam = MAKEWPARAM( 0, XBUTTON1 );
		SupplyArtHlaeMouseEvent( WM_XBUTTONUP, mouseWParam, mouseLParam );
		mouseWParam = MAKEWPARAM( 0, XBUTTON2 );
		SupplyArtHlaeMouseEvent( WM_XBUTTONUP, mouseWParam, mouseLParam );
	}

	void ResetHlaeInputPassthroughCursorAnchor()
	{
		g_Gui.hlaeInputCursorAnchorX = 0;
		g_Gui.hlaeInputCursorAnchorY = 0;
		g_Gui.hlaeInputCursorAnchorValid = false;
		g_Gui.hlaeInputMouseSource = 0;
	}

	bool CaptureHlaeInputPassthroughCursorAnchor()
	{
		POINT cursor;
		const BOOL cursorRead = g_pOriginalGetCursorPos ?
			g_pOriginalGetCursorPos( &cursor ) : GetCursorPos( &cursor );
		if ( !cursorRead )
		{
			ResetHlaeInputPassthroughCursorAnchor();
			return false;
		}

		g_Gui.hlaeInputCursorAnchorX = cursor.x;
		g_Gui.hlaeInputCursorAnchorY = cursor.y;
		g_Gui.hlaeInputCursorAnchorValid = true;
		NotifyArtHlaeCursorWarp( cursor.x, cursor.y );
		return true;
	}

	bool ProcessHlaeInputPassthroughMouseMove()
	{
		if ( !g_Gui.hlaeInputWhileGuiActive )
			return false;

		POINT cursor;
		const BOOL cursorRead = g_pOriginalGetCursorPos ?
			g_pOriginalGetCursorPos( &cursor ) : GetCursorPos( &cursor );
		if ( !cursorRead )
			return true;

		// Once a genuine WM_INPUT stream is observed, keep using it for this hold
		// session and consume legacy movement without generating a duplicate delta.
		// Keep the visible cursor parked at the outside-GUI activation point.
		if ( g_Gui.hlaeInputMouseSource == 2 )
		{
			if ( g_Gui.hlaeInputCursorAnchorValid &&
				( cursor.x != g_Gui.hlaeInputCursorAnchorX ||
					cursor.y != g_Gui.hlaeInputCursorAnchorY ) &&
				g_pOriginalSetCursorPos )
			{
				if ( g_pOriginalSetCursorPos( g_Gui.hlaeInputCursorAnchorX,
					g_Gui.hlaeInputCursorAnchorY ) )
				{
					NotifyArtHlaeCursorWarp( g_Gui.hlaeInputCursorAnchorX,
						g_Gui.hlaeInputCursorAnchorY );
				}
			}
			return true;
		}

		if ( !g_Gui.hlaeInputCursorAnchorValid )
		{
			g_Gui.hlaeInputCursorAnchorX = cursor.x;
			g_Gui.hlaeInputCursorAnchorY = cursor.y;
			g_Gui.hlaeInputCursorAnchorValid = true;
			return true;
		}

		LONG deltaX = cursor.x - g_Gui.hlaeInputCursorAnchorX;
		LONG deltaY = cursor.y - g_Gui.hlaeInputCursorAnchorY;
		if ( deltaX || deltaY )
		{
			g_Gui.hlaeInputMouseSource = 1;
			// The normal CS:S v34 input path does not reliably emit WM_INPUT while the
			// ImGui window owns the mouse. Convert ordinary cursor movement into the
			// same relative RAWMOUSE deltas consumed by original AdvancedFX MirvInput.
			const LONG maxDelta = 2048;
			if ( deltaX < -maxDelta ) deltaX = -maxDelta;
			if ( deltaX > maxDelta ) deltaX = maxDelta;
			if ( deltaY < -maxDelta ) deltaY = -maxDelta;
			if ( deltaY > maxDelta ) deltaY = maxDelta;
			SupplyArtHlaeRawMouseDelta( deltaX, deltaY );

			const BOOL warped = g_pOriginalSetCursorPos ?
				g_pOriginalSetCursorPos( g_Gui.hlaeInputCursorAnchorX,
					g_Gui.hlaeInputCursorAnchorY ) : FALSE;
			if ( warped )
			{
				NotifyArtHlaeCursorWarp( g_Gui.hlaeInputCursorAnchorX,
					g_Gui.hlaeInputCursorAnchorY );
			}
			else
			{
				// Keep deltas incremental if cursor warping is unavailable.
				g_Gui.hlaeInputCursorAnchorX = cursor.x;
				g_Gui.hlaeInputCursorAnchorY = cursor.y;
			}
		}

		return true;
	}

	void SetHlaeInputWhileGuiActive( bool active )
	{
		if ( g_Gui.hlaeInputWhileGuiActive == active )
			return;

		if ( active )
		{
			// Activate first so MirvInput is no longer suspended, then clear any stale
			// key/button state. The configured hold key is only a gate and must never
			// become a mirv_input movement button itself.
			g_Gui.hlaeInputWhileGuiActive = true;
			ReleaseHlaeInputPassthroughControls();
			CaptureHlaeInputPassthroughCursorAnchor();
		}
		else
		{
			// Release while passthrough is still active; otherwise the AdvancedFX
			// suspend predicate would discard the synthetic key-up events.
			ReleaseHlaeInputPassthroughControls();
			g_Gui.hlaeInputWhileGuiActive = false;
			ResetHlaeInputPassthroughCursorAnchor();
			SynchronizeHlaeInputCursorAnchor();
		}
	}

	void RefreshHlaeInputWhileGuiActive( bool guiVisible )
	{
		const bool active = guiVisible && g_Gui.hlaeInputWhileGui &&
			IsArtHlaeInputActive() && IsHlaeInputHoldKeyDown() &&
			( g_Gui.hlaeInputWhileGuiActive || IsCursorOutsideMainGuiWindow() );
		SetHlaeInputWhileGuiActive( active );
	}

	LRESULT CALLBACK ArtGuiWndProc( HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam )
	{
		if ( message == WM_CLOSE )
		{
			const WNDPROC pOriginalWndProc = g_pOriginalWndProc;
			EnterPassiveWindowShutdown( hWnd );

			// A close-button/Alt+F4 exit is explicitly an abrupt exit path. After all
			// hooks have been restored while the engine is still valid, terminate hl2.exe
			// without running the fragile late Source/D3D DLL-detach sequence that produced
			// the recurring Application Error dialog. Windows reclaims all process resources.
			FlushLog();
			if ( TerminateProcess( GetCurrentProcess(), 0 ) )
				return 0;

			// Extremely unlikely fallback if TerminateProcess is denied.
			return pOriginalWndProc ? CallWindowProcA( pOriginalWndProc, hWnd, message, wParam, lParam )
				: DefWindowProcA( hWnd, message, wParam, lParam );
		}

		if ( message == WM_QUERYENDSESSION || message == WM_ENDSESSION ||
			message == WM_DESTROY || message == WM_NCDESTROY )
		{
			const WNDPROC pOriginalWndProc = g_pOriginalWndProc;
			EnterPassiveWindowShutdown( hWnd );
			return pOriginalWndProc ? CallWindowProcA( pOriginalWndProc, hWnd, message, wParam, lParam )
				: DefWindowProcA( hWnd, message, wParam, lParam );
		}

		if ( InterlockedCompareExchange( &g_bArtGuiTerminating, FALSE, FALSE ) )
			return g_pOriginalWndProc ? CallWindowProcA( g_pOriginalWndProc, hWnd, message, wParam, lParam )
				: DefWindowProcA( hWnd, message, wParam, lParam );

		if ( message == WM_SETFOCUS )
			SupplyArtHlaeFocus( true );
		else if ( message == WM_KILLFOCUS )
		{
			SetHlaeInputWhileGuiActive( false );
			SupplyArtHlaeFocus( false );
		}

		const bool guiVisible = IsArtGuiVisible();
		RefreshHlaeInputWhileGuiActive( guiVisible );

		const bool routeHlaeInput = !guiVisible || g_Gui.hlaeInputWhileGuiActive;
		bool hlaeRawInputConsumed = false;
		if ( routeHlaeInput && message == WM_INPUT )
		{
			RAWINPUT rawInput;
			UINT rawInputBytes = sizeof( rawInput );
			if ( GetRawInputData( reinterpret_cast<HRAWINPUT>( lParam ), RID_INPUT,
				&rawInput, &rawInputBytes, sizeof( RAWINPUTHEADER ) ) == sizeof( rawInput ) )
			{
				if ( g_Gui.hlaeInputWhileGuiActive &&
					rawInput.header.dwType == RIM_TYPEMOUSE )
				{
					const bool rawHasMotion = rawInput.data.mouse.lLastX != 0 ||
						rawInput.data.mouse.lLastY != 0;
					if ( rawHasMotion && g_Gui.hlaeInputMouseSource == 1 )
					{
						// A stable legacy WM_MOUSEMOVE stream is already active. Consume the
						// duplicate raw movement so rotation is applied exactly once.
						hlaeRawInputConsumed = IsArtHlaeInputActive();
					}
					else
					{
						if ( rawHasMotion )
							g_Gui.hlaeInputMouseSource = 2;
						FilterHlaeInputHoldKeyFromRawInput( rawInput );
						hlaeRawInputConsumed = SupplyArtHlaeRawInput( rawInput );
					}
				}
				else
				{
					if ( g_Gui.hlaeInputWhileGuiActive )
						FilterHlaeInputHoldKeyFromRawInput( rawInput );
					hlaeRawInputConsumed = SupplyArtHlaeRawInput( rawInput );
				}
			}
		}

		const bool keyMessage = message == WM_KEYDOWN || message == WM_KEYUP ||
			message == WM_SYSKEYDOWN || message == WM_SYSKEYUP;
		const bool keyPressed = message == WM_KEYDOWN || message == WM_SYSKEYDOWN;
		const bool keyReleased = message == WM_KEYUP || message == WM_SYSKEYUP;
		const int key = static_cast<int>( wParam );

		if ( g_Gui.waitingForHlaeInputHoldKey && message == WM_CHAR )
			return 0;

		if ( g_Gui.waitingForHlaeInputHoldKey )
		{
			int capturedKey = 0;
			if ( keyMessage && keyPressed && !( lParam & ( 1L << 30 ) ) )
				capturedKey = key;
			else
				capturedKey = HlaeInputMouseMessageKey( message, wParam );

			if ( capturedKey )
			{
				if ( capturedKey != VK_ESCAPE )
					g_Gui.hlaeInputHoldKey = capturedKey;
				g_Gui.waitingForHlaeInputHoldKey = false;
				SetHlaeInputWhileGuiActive( false );
				return 0;
			}
		}

		if ( g_Gui.waitingForToggleKey && message == WM_CHAR )
			return 0;

		if ( g_Gui.waitingForToggleKey && keyMessage )
		{
			if ( keyPressed && !( lParam & ( 1L << 30 ) ) )
			{
				if ( key == VK_ESCAPE )
					g_Gui.waitingForToggleKey = false;
				else if ( !IsModifierKey( key ) )
				{
					g_Gui.toggleKey = key;
					g_Gui.toggleModifiers = ReadActiveToggleModifiers();
					g_Gui.waitingForToggleKey = false;
				}
			}
			return 0;
		}

		if ( keyMessage && key == g_Gui.toggleKey && ReadActiveToggleModifiers() == g_Gui.toggleModifiers )
		{
			if ( keyPressed && !( lParam & ( 1L << 30 ) ) )
				SetArtGuiVisible( !IsArtGuiVisible() );
			return 0;
		}

		if ( IsArtGuiVisible() && keyMessage && key == VK_ESCAPE )
		{
			if ( keyReleased )
			{
				SetHlaeInputWhileGuiActive( false );
				SetArtGuiVisible( false );
			}
			return 0;
		}

		if ( routeHlaeInput )
		{
			if ( g_Gui.hlaeInputWhileGuiActive && message == WM_MOUSEMOVE &&
				ProcessHlaeInputPassthroughMouseMove() )
				return 0;
			if ( hlaeRawInputConsumed )
				return 0;
			if ( keyMessage )
			{
				if ( g_Gui.hlaeInputWhileGuiActive && key == g_Gui.hlaeInputHoldKey )
					return 0;
				if ( SupplyArtHlaeKeyEvent( keyPressed, wParam, lParam ) )
					return 0;
			}
			if ( message == WM_CHAR &&
				SupplyArtHlaeCharEvent( wParam, lParam ) )
				return 0;
			if ( message >= WM_MOUSEFIRST && message <= WM_MOUSELAST )
			{
				if ( g_Gui.hlaeInputWhileGuiActive &&
					IsHlaeInputHoldMouseMessage( message, wParam ) )
				{
					return 0;
				}
				WPARAM hlaeWParam = g_Gui.hlaeInputWhileGuiActive ?
					FilterHlaeInputHoldMouseState( wParam ) : wParam;
				LPARAM hlaeLParam = lParam;
				const bool consumed = SupplyArtHlaeMouseEvent(
					message, hlaeWParam, hlaeLParam );
				if ( consumed || guiVisible )
					return 0;
			}
		}

		if ( guiVisible && g_Gui.imguiReady )
		{
			if ( message == WM_SETCURSOR )
			{
				if ( g_pOriginalSetCursor )
					g_pOriginalSetCursor( NULL );
				return TRUE;
			}

			ImGui_ImplWin32_WndProcHandler( hWnd, message, wParam, lParam );
			const ImGuiIO &io = ImGui::GetIO();
			const bool consoleVisible = g_pGameConsole && g_pGameConsole->IsConsoleVisible();
			const bool captureKeyboard = !consoleVisible || io.WantTextInput || io.WantCaptureKeyboard;
			const bool captureMouse = !consoleVisible || io.WantCaptureMouse;
			if ( ( IsKeyboardMessage( message ) && captureKeyboard ) ||
				( IsMouseMessage( message ) && captureMouse ) )
				return 1;
		}

		return g_pOriginalWndProc ? CallWindowProcA( g_pOriginalWndProc, hWnd, message, wParam, lParam )
			: DefWindowProcA( hWnd, message, wParam, lParam );
	}

	// -------------------------------------------------------------------------
	// Dear ImGui setup and reusable controls
	// -------------------------------------------------------------------------
	void ApplyThemeColors()
	{
		ImGuiStyle &style = ImGui::GetStyle();
		ImVec4 *colors = style.Colors;
		const ImVec4 text = GuiColor( g_Gui.textColor );
		const ImVec4 muted = GuiColor( g_Gui.mutedTextColor );
		const ImVec4 window = GuiColor( g_Gui.windowColor );
		const ImVec4 panel = GuiColor( g_Gui.panelColor );
		const ImVec4 accent = GuiColor( g_Gui.accentColor );
		const ImVec4 border = GuiColor( g_Gui.borderColor );
		const ImVec4 control = GuiColor( g_Gui.controlColor );
		const ImVec4 selection = GuiColor( g_Gui.selectionColor );
		const ImVec4 brightAccent = MixColor( accent, text, 0.22f );

		colors[ImGuiCol_Text] = text;
		colors[ImGuiCol_TextDisabled] = muted;
		colors[ImGuiCol_WindowBg] = window;
		colors[ImGuiCol_ChildBg] = panel;
		colors[ImGuiCol_PopupBg] = MixColor( window, panel, 0.55f );
		colors[ImGuiCol_Border] = border;
		colors[ImGuiCol_BorderShadow] = ImVec4( 0, 0, 0, 0 );
		colors[ImGuiCol_FrameBg] = control;
		colors[ImGuiCol_FrameBgHovered] = MixColor( control, accent, 0.48f );
		colors[ImGuiCol_FrameBgActive] = MixColor( control, accent, 0.70f );
		colors[ImGuiCol_TitleBg] = window;
		colors[ImGuiCol_TitleBgActive] = MixColor( window, accent, 0.16f );
		colors[ImGuiCol_MenuBarBg] = GuiColor( g_Gui.sidebarColor );
		colors[ImGuiCol_Button] = control;
		colors[ImGuiCol_ButtonHovered] = MixColor( control, accent, 0.62f );
		colors[ImGuiCol_ButtonActive] = selection;
		colors[ImGuiCol_Header] = selection;
		colors[ImGuiCol_HeaderHovered] = MixColor( selection, accent, 0.48f );
		colors[ImGuiCol_HeaderActive] = accent;
		colors[ImGuiCol_CheckMark] = brightAccent;
		colors[ImGuiCol_SliderGrab] = accent;
		colors[ImGuiCol_SliderGrabActive] = brightAccent;
		colors[ImGuiCol_Separator] = border;
		colors[ImGuiCol_SeparatorHovered] = accent;
		colors[ImGuiCol_SeparatorActive] = brightAccent;
		colors[ImGuiCol_ScrollbarBg] = MixColor( window, control, 0.50f );
		colors[ImGuiCol_ScrollbarGrab] = MixColor( control, muted, 0.30f );
		colors[ImGuiCol_ScrollbarGrabHovered] = MixColor( control, accent, 0.48f );
		colors[ImGuiCol_ScrollbarGrabActive] = accent;
		colors[ImGuiCol_TableHeaderBg] = selection;
		colors[ImGuiCol_TableBorderStrong] = border;
		colors[ImGuiCol_TableBorderLight] = MixColor( panel, border, 0.65f );
		colors[ImGuiCol_TextSelectedBg] = ImVec4( accent.x, accent.y, accent.z, 0.35f );
		colors[ImGuiCol_NavHighlight] = brightAccent;
		colors[ImGuiCol_ResizeGrip] = ImVec4( accent.x, accent.y, accent.z, 0.30f );
		colors[ImGuiCol_ResizeGripHovered] = ImVec4( accent.x, accent.y, accent.z, 0.75f );
		colors[ImGuiCol_ResizeGripActive] = ImVec4( brightAccent.x, brightAccent.y, brightAccent.z, 0.95f );
	}

	void ApplyStyle()
	{
		ImGuiStyle &style = ImGui::GetStyle();
		style.WindowRounding = 13.0f;
		style.ChildRounding = 10.0f;
		style.FrameRounding = 7.0f;
		style.PopupRounding = 9.0f;
		style.ScrollbarRounding = 10.0f;
		style.GrabRounding = 7.0f;
		style.TabRounding = 7.0f;
		style.WindowPadding = ImVec2( 14.0f, 14.0f );
		style.FramePadding = ImVec2( 10.0f, 7.0f );
		style.ItemSpacing = ImVec2( 9.0f, 8.0f );
		style.ItemInnerSpacing = ImVec2( 8.0f, 6.0f );
		style.CellPadding = ImVec2( 9.0f, 7.0f );
		style.IndentSpacing = 20.0f;
		style.ScrollbarSize = 14.0f;
		style.GrabMinSize = 11.0f;
		style.WindowBorderSize = 1.0f;
		style.ChildBorderSize = 1.0f;
		style.FrameBorderSize = 0.0f;
		ApplyThemeColors();
	}

	void SyncTextFieldsFromGame()
	{
		Q_strncpy( g_Gui.outputPath, g_szRecordBase, sizeof( g_Gui.outputPath ) );
		Q_strncpy( g_Gui.prefix, g_szCapturePrefix, sizeof( g_Gui.prefix ) );
		Q_strncpy( g_Gui.ffmpegPath, g_szArtFfmpegPath, sizeof( g_Gui.ffmpegPath ) );
		Q_strncpy( g_Gui.ffmpegCustomArgs, g_szArtFfmpegCustomArgs, sizeof( g_Gui.ffmpegCustomArgs ) );
		ConVar *pHost = g_pCvar ? g_pCvar->FindVar( "host_framerate" ) : NULL;
		g_Gui.hostFramerate = pHost ? pHost->GetInt() : 0;
	}

	void ApplyOutputPathField()
	{
		if ( !g_Gui.outputPath[0] || !Q_stricmp( g_Gui.outputPath, "default" ) )
			IssueCommand( "art_record path default" );
		else if ( IsSafeQuotedArgument( g_Gui.outputPath ) )
			IssueCommand( "art_record path \"%s\"", g_Gui.outputPath );
		else
			SetError( "path cannot contain quotes, semicolons, or line breaks" );
		g_Gui.refreshTakes = true;
	}

	void ApplyPrefixField()
	{
		if ( !g_Gui.prefix[0] || !Q_stricmp( g_Gui.prefix, "default" ) )
			IssueCommand( "art_prefix default" );
		else if ( IsSafeConfigName( g_Gui.prefix ) )
			IssueCommand( "art_prefix %s", g_Gui.prefix );
		else
			SetError( "prefix may contain only letters, numbers, '_' and '-'" );
	}

	void ApplyFfmpegPathField()
	{
		if ( !g_Gui.ffmpegPath[0] || !Q_stricmp( g_Gui.ffmpegPath, "default" ) )
			IssueCommand( "art_ffmpeg_path default" );
		else if ( IsSafeQuotedArgument( g_Gui.ffmpegPath ) )
			IssueCommand( "art_ffmpeg_path \"%s\"", g_Gui.ffmpegPath );
		else
			SetError( "FFmpeg path cannot contain quotes or semicolons" );
	}

	void ApplyFfmpegCustomArgsField()
	{
		if ( g_Gui.ffmpegCustomArgs[0] )
			IssueCommand( "art_ffmpeg_custom %s", g_Gui.ffmpegCustomArgs );
	}

	HWND ResolveGameWindow( HWND hFocusWindow )
	{
		if ( !hFocusWindow || !IsWindow( hFocusWindow ) )
			return NULL;

		HWND hRoot = GetAncestor( hFocusWindow, GA_ROOT );
		if ( hRoot )
			hFocusWindow = hRoot;

		DWORD processId = 0;
		GetWindowThreadProcessId( hFocusWindow, &processId );
		if ( processId != GetCurrentProcessId() )
			return NULL;

		char className[64] = "";
		GetClassNameA( hFocusWindow, className, sizeof( className ) );
		return !Q_stricmp( className, "Valve001" ) ? hFocusWindow : NULL;
	}

	bool InitializeImGui( IDirect3DDevice9 *pDevice )
	{
		if ( InterlockedCompareExchange( &g_bArtGuiTerminating, FALSE, FALSE ) )
			return false;
		if ( g_Gui.imguiReady )
			return true;

		D3DDEVICE_CREATION_PARAMETERS creation;
		ZeroMemory( &creation, sizeof( creation ) );
		if ( FAILED( pDevice->GetCreationParameters( &creation ) ) )
		{
			SetError( "could not read D3D9 device creation parameters" );
			return false;
		}

		g_hGameWindow = ResolveGameWindow( creation.hFocusWindow );
		if ( !g_hGameWindow )
			return false;
		g_pGuiDevice = pDevice;
		g_pGuiDevice->AddRef();

		IMGUI_CHECKVERSION();
		ImGui::CreateContext();
		ImGuiIO &io = ImGui::GetIO();
		io.IniFilename = NULL;
		io.LogFilename = NULL;
		io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
		io.ConfigFlags |= ImGuiConfigFlags_NoMouseCursorChange;
		io.ConfigWindowsMoveFromTitleBarOnly = true;
		io.MouseDrawCursor = IsArtGuiVisible();
		ApplyStyle();

		const bool win32Ready = ImGui_ImplWin32_Init( g_hGameWindow );
		const bool dx9Ready = win32Ready && ImGui_ImplDX9_Init( pDevice );
		if ( !win32Ready || !dx9Ready )
		{
			SetError( "GUI backend initialization failed (win32=%d dx9=%d)",
				win32Ready ? 1 : 0, dx9Ready ? 1 : 0 );
			if ( dx9Ready ) ImGui_ImplDX9_Shutdown();
			if ( win32Ready ) ImGui_ImplWin32_Shutdown();
			ImGui::DestroyContext();
			g_pGuiDevice->Release();
			g_pGuiDevice = NULL;
			g_hGameWindow = NULL;
			return false;
		}

		SetLastError( ERROR_SUCCESS );
		g_pOriginalWndProc = reinterpret_cast<WNDPROC>( SetWindowLongPtrA( g_hGameWindow, GWLP_WNDPROC,
			reinterpret_cast<LONG_PTR>( ArtGuiWndProc ) ) );
		if ( !g_pOriginalWndProc && GetLastError() != ERROR_SUCCESS )
		{
			SetError( "SetWindowLongPtrA failed (Win32 error %lu)", GetLastError() );
			ImGui_ImplDX9_Shutdown();
			ImGui_ImplWin32_Shutdown();
			ImGui::DestroyContext();
			g_pGuiDevice->Release();
			g_pGuiDevice = NULL;
			g_hGameWindow = NULL;
			return false;
		}

		SyncTextFieldsFromGame();
		g_Gui.imguiReady = true;
		LogMessage( "GUI BACKEND READY: hwnd=%p device=%p", g_hGameWindow, pDevice );
		if ( IsArtGuiVisible() )
			UpdateMouseCaptureMode();
		return true;
	}

	void ShutdownImGui()
	{
		RestoreGameMouse();
		if ( !g_Gui.imguiReady )
			return;

		if ( g_hGameWindow && g_pOriginalWndProc )
		{
			const WNDPROC current = reinterpret_cast<WNDPROC>( GetWindowLongPtrA( g_hGameWindow, GWLP_WNDPROC ) );
			if ( current == ArtGuiWndProc )
				SetWindowLongPtrA( g_hGameWindow, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>( g_pOriginalWndProc ) );
			g_pOriginalWndProc = NULL;
		}
		ImGui_ImplDX9_Shutdown();
		ImGui_ImplWin32_Shutdown();
		ImGui::DestroyContext();
		if ( g_pGuiDevice )
		{
			g_pGuiDevice->Release();
			g_pGuiDevice = NULL;
		}
		g_hGameWindow = NULL;
		g_Gui.imguiReady = false;
	}

	void EnterPassiveWindowShutdown( HWND hWnd )
	{
		if ( InterlockedCompareExchange( &g_bWindowCloseCleanupStarted, TRUE, FALSE ) != FALSE )
			return;

		// WM_CLOSE is the last reliable point at which engine.dll, client.dll, d3d9.dll,
		// their vtables, and the game window are all still alive. Switch every callback
		// to passthrough first, restore the original window procedure, and synchronously
		// remove our hooks before forwarding WM_CLOSE to Source. Leaving detours installed
		// until process teardown allowed late render calls to jump through stale original
		// pointers and produced the recurring hl2.exe memory-read dialog.
		BeginArtGuiTermination();
		InterlockedExchange( &g_GuiVisible, FALSE );
		InterlockedExchange( &g_bRecording, FALSE );
		ShutdownArtHlae();

		g_Gui.mouseStateStored = false;
		g_Gui.previousMouseEnable[0] = '\0';
		ReleaseCapture();

		if ( hWnd && g_pOriginalWndProc )
		{
			const WNDPROC current = reinterpret_cast<WNDPROC>( GetWindowLongPtrA( hWnd, GWLP_WNDPROC ) );
			if ( current == ArtGuiWndProc )
				SetWindowLongPtrA( hWnd, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>( g_pOriginalWndProc ) );
		}

		// These operations run before CallWindowProcA forwards WM_CLOSE, outside DllMain
		// and before Source starts unloading modules. The terminating flag prevents new
		// GUI/render work while MinHook and the Source vtable slots are restored.
		RemoveD3D9Hooks();
		RemoveViewRenderHook();
		RemoveModelRenderHook();
		RemoveClientModeFovHook();

		g_Gui.installed = false;
		g_hGameWindow = NULL;
		g_pOriginalWndProc = NULL;
		LogMessage( "WINDOW CLOSE PREPARED: wndproc_restored=1 d3d_hooks_removed=1 source_hooks_removed=1" );
		FlushLog();
	}

	void StatusBadge( bool active, const char *pActive, const char *pInactive )
	{
		const ImVec4 color = active ? ImVec4( 0.31f, 0.92f, 0.55f, 1.0f ) : ImVec4( 0.55f, 0.57f, 0.64f, 1.0f );
		ImGui::TextColored( color, "%s", active ? pActive : pInactive );
	}

	void TextUnformattedWrapped( const char *pText )
	{
		ImGui::PushTextWrapPos( 0.0f );
		ImGui::TextUnformatted( pText ? pText : "" );
		ImGui::PopTextWrapPos();
	}

	void TextUnformattedSingleLine( const char *pText )
	{
		// Used only inside fixed-width table cells where wrapping would desynchronize
		// the label and control rows across adjacent columns.
		ImGui::Text( "%s", pText ? pText : "" );
	}

	void TextDisabledWrapped( const char *pFormat, ... )
	{
		va_list arguments;
		va_start( arguments, pFormat );
		ImGui::PushStyleColor( ImGuiCol_Text, ImGui::GetStyleColorVec4( ImGuiCol_TextDisabled ) );
		ImGui::PushTextWrapPos( 0.0f );
		ImGui::TextV( pFormat, arguments );
		ImGui::PopTextWrapPos();
		ImGui::PopStyleColor();
		va_end( arguments );
	}

	void HoverExplanation( const char *pFormat, ... )
	{
		if ( !ImGui::IsItemHovered() )
			return;
		char buffer[768];
		va_list arguments;
		va_start( arguments, pFormat );
		Q_vsnprintf( buffer, sizeof( buffer ), pFormat, arguments );
		va_end( arguments );
		ImGui::BeginTooltip();
		ImGui::PushTextWrapPos( ImGui::GetFontSize() * 32.0f );
		ImGui::TextWrapped( "%s", buffer );
		ImGui::PopTextWrapPos();
		ImGui::EndTooltip();
	}

	const char *VisibleImGuiLabel( const char *pLabel, char *pBuffer, size_t bufferBytes )
	{
		if ( !pBuffer || !bufferBytes )
			return "control";
		pBuffer[0] = '\0';
		if ( !pLabel || !pLabel[0] )
		{
			Q_strncpy( pBuffer, "control", bufferBytes );
			return pBuffer;
		}
		const char *pHidden = strstr( pLabel, "##" );
		const size_t length = pHidden ? static_cast<size_t>( pHidden - pLabel ) : strlen( pLabel );
		const size_t copyLength = length < bufferBytes - 1 ? length : bufferBytes - 1;
		memcpy( pBuffer, pLabel, copyLength );
		pBuffer[copyLength] = '\0';
		if ( !pBuffer[0] ) Q_strncpy( pBuffer, "control", bufferBytes );
		return pBuffer;
	}

	void HlaeSameLineIfFits( float nextItemWidth )
	{
		const float spacing = ImGui::GetStyle().ItemSpacing.x;
		if ( ImGui::GetContentRegionAvail().x >= nextItemWidth + spacing )
			ImGui::SameLine();
	}

	bool HlaeToggleButton( const char *pInactiveLabel, const char *pActiveLabel,
		bool active, const ImVec2 &size )
	{
		if ( active )
		{
			ImGui::PushStyleColor( ImGuiCol_Button, ImVec4( 0.72f, 0.08f, 0.06f, 1.0f ) );
			ImGui::PushStyleColor( ImGuiCol_ButtonHovered, ImVec4( 0.90f, 0.12f, 0.08f, 1.0f ) );
			ImGui::PushStyleColor( ImGuiCol_ButtonActive, ImVec4( 0.58f, 0.04f, 0.03f, 1.0f ) );
		}
		const bool clicked = ImGui::Button( active ? pActiveLabel : pInactiveLabel, size );
		if ( active ) ImGui::PopStyleColor( 3 );
		return clicked;
	}

	bool IsPointInsideMainGuiWindow( LONG x, LONG y )
	{
		return g_Gui.mainWindowWidth > 0.0f && g_Gui.mainWindowHeight > 0.0f &&
			x >= static_cast<LONG>( g_Gui.mainWindowX ) &&
			y >= static_cast<LONG>( g_Gui.mainWindowY ) &&
			x < static_cast<LONG>( g_Gui.mainWindowX + g_Gui.mainWindowWidth ) &&
			y < static_cast<LONG>( g_Gui.mainWindowY + g_Gui.mainWindowHeight );
	}

	// -------------------------------------------------------------------------
	// Demo browser and spectator controls
	// -------------------------------------------------------------------------
	bool DemoPathLess( const std::string &left, const std::string &right )
	{
		return _stricmp( left.c_str(), right.c_str() ) < 0;
	}

	bool HasDemoExtension( const char *pName )
	{
		if ( !pName )
			return false;
		const size_t length = strlen( pName );
		return length > 4 && !_stricmp( pName + length - 4, ".dem" );
	}

	void CollectDemoFiles( const char *pGameDirectory, const char *pRelativeDirectory, int depth )
	{
		if ( !pGameDirectory || depth > 4 )
			return;

		char directory[MAX_PATH];
		if ( pRelativeDirectory && pRelativeDirectory[0] )
			Q_snprintf( directory, sizeof( directory ), "%s\\%s", pGameDirectory, pRelativeDirectory );
		else
			Q_strncpy( directory, pGameDirectory, sizeof( directory ) );

		char pattern[MAX_PATH];
		Q_snprintf( pattern, sizeof( pattern ), "%s\\*", directory );
		WIN32_FIND_DATAA findData;
		HANDLE hFind = FindFirstFileA( pattern, &findData );
		if ( hFind == INVALID_HANDLE_VALUE )
			return;

		do
		{
			if ( !strcmp( findData.cFileName, "." ) || !strcmp( findData.cFileName, ".." ) )
				continue;
			if ( findData.dwFileAttributes & ( FILE_ATTRIBUTE_HIDDEN | FILE_ATTRIBUTE_SYSTEM ) )
				continue;

			char relative[MAX_PATH];
			if ( pRelativeDirectory && pRelativeDirectory[0] )
				Q_snprintf( relative, sizeof( relative ), "%s/%s", pRelativeDirectory, findData.cFileName );
			else
				Q_strncpy( relative, findData.cFileName, sizeof( relative ) );

			if ( findData.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY )
			{
				CollectDemoFiles( pGameDirectory, relative, depth + 1 );
				continue;
			}
			if ( HasDemoExtension( findData.cFileName ) )
				g_Gui.demos.push_back( relative );
		}
		while ( FindNextFileA( hFind, &findData ) );
		FindClose( hFind );
	}

	void RefreshDemoList()
	{
		g_Gui.demos.clear();
		g_Gui.selectedDemo = -1;
		g_Gui.refreshDemos = false;
		const char *pGameDirectory = g_pEngine ? g_pEngine->GetGameDirectory() : NULL;
		if ( !pGameDirectory || !pGameDirectory[0] )
		{
			SetError( "could not resolve the game directory for demo scanning" );
			return;
		}
		CollectDemoFiles( pGameDirectory, "", 0 );
		std::sort( g_Gui.demos.begin(), g_Gui.demos.end(), DemoPathLess );
		if ( !g_Gui.demos.empty() )
			g_Gui.selectedDemo = 0;
	}

	void RefreshDemoPlayerList()
	{
		const int previousEntity = g_Gui.selectedDemoPlayer >= 0 &&
			g_Gui.selectedDemoPlayer < static_cast<int>( g_Gui.demoPlayers.size() ) ?
			g_Gui.demoPlayers[g_Gui.selectedDemoPlayer].entityIndex : -1;
		g_Gui.demoPlayers.clear();
		g_Gui.selectedDemoPlayer = -1;

		if ( !g_pEngine || !g_pEngine->IsPlayingDemo() )
			return;

		const int maxClients = g_pEngine->GetMaxClients();
		for ( int entityIndex = 1; entityIndex <= maxClients; ++entityIndex )
		{
			player_info_t info;
			ZeroMemory( &info, sizeof( info ) );
			if ( !g_pEngine->GetPlayerInfo( entityIndex, &info ) || !info.name[0] || info.ishltv )
				continue;

			DemoPlayerEntry entry;
			entry.entityIndex = entityIndex;
			entry.userId = info.userID;
			entry.name = info.name;
			g_Gui.demoPlayers.push_back( entry );
		}

		for ( size_t i = 0; i < g_Gui.demoPlayers.size(); ++i )
		{
			if ( g_Gui.demoPlayers[i].entityIndex == previousEntity )
			{
				g_Gui.selectedDemoPlayer = static_cast<int>( i );
				break;
			}
		}
		if ( g_Gui.selectedDemoPlayer < 0 && !g_Gui.demoPlayers.empty() )
			g_Gui.selectedDemoPlayer = 0;
	}

	unsigned long DemoPlayerRosterSignature()
	{
		unsigned long hash = 2166136261u;
		for ( size_t i = 0; i < g_Gui.demoPlayers.size(); ++i )
		{
			const DemoPlayerEntry &entry = g_Gui.demoPlayers[i];
			hash ^= static_cast<unsigned long>( entry.entityIndex );
			hash *= 16777619u;
			hash ^= static_cast<unsigned long>( entry.userId );
			hash *= 16777619u;
			for ( const unsigned char *p = reinterpret_cast<const unsigned char *>( entry.name.c_str() ); *p; ++p )
			{
				hash ^= *p;
				hash *= 16777619u;
			}
		}
		return hash;
	}

	void ScheduleDemoPlayerAutoRefresh( bool clearExistingPlayers )
	{
		if ( clearExistingPlayers )
		{
			g_Gui.demoPlayers.clear();
			g_Gui.selectedDemoPlayer = -1;
		}
		g_Gui.refreshDemoPlayers = true;
		g_Gui.demoPlayerRefreshAttempts = 0;
		g_Gui.demoPlayerStableScans = 0;
		g_Gui.demoPlayerLastScanCount = -1;
		g_Gui.demoPlayerLastSignature = 0;
		g_Gui.nextDemoPlayerRefreshAt = GetTickCount() + kDemoPlayerRefreshInitialDelayMs;
	}

	void StopDemoPlayerAutoRefresh()
	{
		g_Gui.refreshDemoPlayers = false;
		g_Gui.demoPlayerRefreshAttempts = 0;
		g_Gui.demoPlayerStableScans = 0;
		g_Gui.demoPlayerLastScanCount = -1;
		g_Gui.demoPlayerLastSignature = 0;
		g_Gui.nextDemoPlayerRefreshAt = 0;
	}

	void MaintainHiddenSpectatorPanels();

	void UpdateDemoPlayerAutoRefresh()
	{
		if ( !g_Gui.refreshDemoPlayers || !g_pEngine || !g_pEngine->IsPlayingDemo() )
			return;

		const DWORD now = GetTickCount();
		if ( static_cast<LONG>( now - g_Gui.nextDemoPlayerRefreshAt ) < 0 )
			return;

		RefreshDemoPlayerList();
		++g_Gui.demoPlayerRefreshAttempts;
		MaintainHiddenSpectatorPanels();

		const int playerCount = static_cast<int>( g_Gui.demoPlayers.size() );
		const unsigned long signature = DemoPlayerRosterSignature();
		if ( playerCount > 0 && playerCount == g_Gui.demoPlayerLastScanCount &&
			signature == g_Gui.demoPlayerLastSignature )
		{
			++g_Gui.demoPlayerStableScans;
		}
		else
		{
			g_Gui.demoPlayerStableScans = 0;
		}
		g_Gui.demoPlayerLastScanCount = playerCount;
		g_Gui.demoPlayerLastSignature = signature;

		const bool rosterStable = g_Gui.demoPlayerRefreshAttempts >= kDemoPlayerRefreshMinimumAttempts &&
			g_Gui.demoPlayerStableScans >= 2;
		const bool retryLimitReached =
			g_Gui.demoPlayerRefreshAttempts >= kDemoPlayerRefreshMaximumAttempts;
		if ( rosterStable || retryLimitReached )
		{
			g_Gui.refreshDemoPlayers = false;
			LogMessage( "DEMO PLAYER AUTO-REFRESH COMPLETE: players=%d attempts=%d stable=%d limit=%d",
				playerCount, g_Gui.demoPlayerRefreshAttempts, g_Gui.demoPlayerStableScans,
				retryLimitReached ? 1 : 0 );
			return;
		}

		g_Gui.nextDemoPlayerRefreshAt = now + kDemoPlayerRefreshIntervalMs;
	}

	const DemoPlayerEntry *SelectedDemoPlayer()
	{
		return g_Gui.selectedDemoPlayer >= 0 &&
			g_Gui.selectedDemoPlayer < static_cast<int>( g_Gui.demoPlayers.size() ) ?
			&g_Gui.demoPlayers[g_Gui.selectedDemoPlayer] : NULL;
	}

	void EnableGuiExecutionForCommand( const char *pName )
	{
		if ( !g_pCvar || !pName || !pName[0] )
			return;
		for ( const ConCommandBase *pBase = g_pCvar->GetCommands(); pBase; pBase = pBase->GetNext() )
		{
			const char *pExistingName = pBase->GetName();
			if ( pExistingName && !_stricmp( pExistingName, pName ) )
			{
				const_cast<ConCommandBase *>( pBase )->AddFlags( FCVAR_CLIENTCMD_CAN_EXECUTE );
				return;
			}
		}
	}

	void ExecuteGuiMaintenanceCommand( const char *pCommand )
	{
		if ( !g_pEngine || !pCommand || !pCommand[0] )
			return;
		++g_nCommandDispatchDepth;
		g_pEngine->ExecuteClientCmd( pCommand );
		--g_nCommandDispatchDepth;
	}

	void MaintainHiddenSpectatorPanels()
	{
		if ( !g_Gui.spectatorUiHidden )
			return;

		EnableGuiExecutionForCommand( "hidepanel" );
		EnableGuiExecutionForCommand( "spec_menu" );
		ExecuteGuiMaintenanceCommand( "hidepanel specgui" );
		ExecuteGuiMaintenanceCommand( "hidepanel specmenu" );
		ExecuteGuiMaintenanceCommand( "spec_menu 0" );
	}

	void SetSpectatorUiHidden( bool hidden )
	{
		g_Gui.spectatorUiHidden = hidden;
		if ( hidden )
		{
			EnableGuiExecutionForCommand( "hidepanel" );
			EnableGuiExecutionForCommand( "spec_menu" );
			// specgui is the passive top/bottom bars; specmenu is CS:S's
			// active default camera selector. Leave cl_drawhud untouched.
			IssueCommand( "hidepanel specgui" );
			IssueCommand( "hidepanel specmenu" );
			IssueCommand( "spec_menu 0" );
		}
		else
		{
			// Restore the passive bars without forcing the active camera menu open.
			EnableGuiExecutionForCommand( "showpanel" );
			IssueCommand( "showpanel specgui" );
		}
	}

	void ApplyDemoSpectatorMode( int mode, bool selectPlayer )
	{
		if ( !g_pEngine || !g_pEngine->IsPlayingDemo() )
		{
			SetError( "load a demo before changing the spectator camera" );
			return;
		}

		// CS:S v34 observer modes used by the capture controls:
		// 3 = first person, 4 = third person, 5 = free cam.
		if ( mode != 3 && mode != 4 && mode != 5 )
			mode = 3;
		const bool shouldSelectPlayer = selectPlayer && mode != 5;
		const DemoPlayerEntry *pPlayer = SelectedDemoPlayer();
		if ( shouldSelectPlayer && !pPlayer )
		{
			SetError( "select a demo player first" );
			return;
		}

		g_Gui.spectatorMode = mode;
		EnableGuiExecutionForCommand( "spec_autodirector" );
		EnableGuiExecutionForCommand( "spec_player" );
		EnableGuiExecutionForCommand( "spec_mode" );

		// spec_player resolves a player predicate (the exact quoted name below).
		IssueCommand( "spec_autodirector 0" );
		IssueCommand( "spec_mode %d", g_Gui.spectatorMode );
		if ( shouldSelectPlayer && pPlayer )
		{
			char safeName[128];
			Q_strncpy( safeName, pPlayer->name.c_str(), sizeof( safeName ) );
			for ( char *p = safeName; *p; ++p )
			{
				if ( *p == '\"' || *p == '\n' || *p == '\r' || *p == ';' ) *p = '\'';
			}
			// Legacy CS:S resolves spec_player through its player-command predicate.
			// Pass the exact quoted player name; an entity slot is not a valid target
			// on this branch. Apply the requested camera mode after target selection.
			IssueCommand( "spec_player \"%s\"", safeName );
			IssueCommand( "spec_mode %d", g_Gui.spectatorMode );
		}
		MaintainHiddenSpectatorPanels();
	}

	void SpectateSelectedDemoPlayer()
	{
		ApplyDemoSpectatorMode( g_Gui.spectatorMode, true );
	}


#pragma pack( push, 1 )
	struct SourceDemoHeader
	{
		char fileStamp[8];
		int demoProtocol;
		int networkProtocol;
		char serverName[260];
		char clientName[260];
		char mapName[260];
		char gameDirectory[260];
		float playbackTime;
		int playbackTicks;
		int playbackFrames;
		int signonLength;
	};
#pragma pack( pop )

	bool ResolveDemoDiskPath( const char *pDemoName, char *pOutput, size_t outputBytes )
	{
		if ( !pDemoName || !pDemoName[0] || !pOutput || outputBytes == 0 )
			return false;
		const char *pGameDirectory = g_pEngine ? g_pEngine->GetGameDirectory() : NULL;
		if ( !pGameDirectory || !pGameDirectory[0] )
			return false;

		char normalized[MAX_PATH];
		Q_strncpy( normalized, pDemoName, sizeof( normalized ) );
		for ( char *p = normalized; *p; ++p )
			if ( *p == '/' ) *p = '\\';

		const bool absolutePath =
			( normalized[0] && normalized[1] == ':' ) ||
			( normalized[0] == '\\' && normalized[1] == '\\' );
		if ( absolutePath )
		{
			if ( !HasDemoExtension( normalized ) )
				Q_snprintf( pOutput, outputBytes, "%s.dem", normalized );
			else
				Q_strncpy( pOutput, normalized, outputBytes );
		}
		else if ( !HasDemoExtension( normalized ) )
			Q_snprintf( pOutput, outputBytes, "%s\\%s.dem", pGameDirectory, normalized );
		else
			Q_snprintf( pOutput, outputBytes, "%s\\%s", pGameDirectory, normalized );
		return true;
	}

	void LoadDemoTimingMetadata( const char *pDemoName )
	{
		g_Gui.demoTickRate = 100.0f;
		char path[MAX_PATH];
		if ( !ResolveDemoDiskPath( pDemoName, path, sizeof( path ) ) )
			return;

		HANDLE hFile = CreateFileA( path, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE,
			NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL );
		if ( hFile == INVALID_HANDLE_VALUE )
			return;
		SourceDemoHeader header;
		ZeroMemory( &header, sizeof( header ) );
		DWORD bytesRead = 0;
		const BOOL readOk = ReadFile( hFile, &header, sizeof( header ), &bytesRead, NULL );
		CloseHandle( hFile );
		if ( !readOk || bytesRead != sizeof( header ) || memcmp( header.fileStamp, "HL2DEMO", 7 ) != 0 ||
			header.playbackTime <= 0.0f || header.playbackTicks <= 0 )
			return;

		const float rate = header.playbackTicks / header.playbackTime;
		if ( rate >= 10.0f && rate <= 1000.0f )
			g_Gui.demoTickRate = rate;
	}

	void ResetDemoTickTracking( int tick, bool valid )
	{
		g_Gui.currentDemoTick = tick < 0 ? 0 : tick;
		g_Gui.currentDemoTickValid = valid;
		g_Gui.demoTickFraction = 0.0f;
		g_Gui.demoClockTime = g_pEngine ? g_pEngine->Time() : 0.0f;
	}

	static int s_nPendingBackwardSeekTick = -1;
	static DWORD s_dwPendingBackwardSeekTimeout = 0;
	static DWORD s_dwBackwardSeekStartTime = 0;
	static bool s_bRewindingToZero = false;

	void UpdateDemoTickTracking()
	{
		const bool playingDemo = g_pEngine && g_pEngine->IsPlayingDemo();
		const bool pausedDemo = playingDemo && g_pEngine->IsPaused();

		if ( !playingDemo )
		{
			g_Gui.currentDemoTickValid = false;
			g_Gui.demoClockTime = g_pEngine ? g_pEngine->Time() : 0.0f;
			g_Gui.demoWasPlaying = false;
			return;
		}

		const float now = g_pEngine->Time();
		const DWORD nowMs = GetTickCount();
		const bool demoJustStarted = !g_Gui.demoWasPlaying;
		g_Gui.demoWasPlaying = true;

		if ( demoJustStarted || !g_Gui.currentDemoTickValid )
		{
			ScheduleDemoPlayerAutoRefresh( true );
			MaintainHiddenSpectatorPanels();
			g_Gui.demoClockTime = now;
			g_Gui.demoTickFraction = 0.0f;
			ResetDemoTickTracking( 0, true );
		}

		if ( s_bRewindingToZero )
		{
			if ( nowMs > s_dwPendingBackwardSeekTimeout )
			{
				s_bRewindingToZero = false;
				s_nPendingBackwardSeekTick = -1;
			}
			else if ( ( nowMs - s_dwBackwardSeekStartTime >= 100 ) && pausedDemo )
			{
				s_bRewindingToZero = false;
				const int target = s_nPendingBackwardSeekTick;
				s_nPendingBackwardSeekTick = -1;
				if ( target > 0 )
				{
					IssueCommand( "demo_gototick %d 0 1", target );
				}
				ResetDemoTickTracking( target, true );
				g_Gui.demoClockTime = now;
				return;
			}
			g_Gui.demoClockTime = now;
			return;
		}

		if ( !pausedDemo )
		{
			const float delta = now - g_Gui.demoClockTime;
			if ( delta >= 0.0f && delta <= 0.25f )
			{
				g_Gui.demoTickFraction += delta * g_Gui.demoTickRate;
				const int advancedTicks = static_cast<int>( g_Gui.demoTickFraction );
				if ( advancedTicks > 0 )
				{
					g_Gui.currentDemoTick += advancedTicks;
					g_Gui.demoTickFraction -= advancedTicks;
				}
			}
		}
		g_Gui.demoClockTime = now;
	}

	void LoadDemoFromField()
	{
		if ( !g_Gui.demoName[0] )
		{
			SetError( "enter a demo name or path" );
			return;
		}
		if ( !IsSafeQuotedArgument( g_Gui.demoName ) )
		{
			SetError( "demo name cannot contain quotes, semicolons, or line breaks" );
			return;
		}
		s_bRewindingToZero = false;
		s_nPendingBackwardSeekTick = -1;
		LoadDemoTimingMetadata( g_Gui.demoName );
		ScheduleDemoPlayerAutoRefresh( true );
		g_Gui.demoTick = 0;
		ResetDemoTickTracking( 0, true );
		IssueCommand( "playdemo \"%s\"", g_Gui.demoName );
	}

	void SeekDemoTick( int tick )
	{
		if ( tick < 0 ) tick = 0;
		g_Gui.demoTick = tick;

		const bool isPlaying = g_pEngine && g_pEngine->IsPlayingDemo();
		const int curTick = g_Gui.currentDemoTickValid ? g_Gui.currentDemoTick : 0;

		if ( isPlaying && g_Gui.currentDemoTickValid && tick < curTick )
		{
			s_nPendingBackwardSeekTick = tick;
			s_bRewindingToZero = true;
			s_dwBackwardSeekStartTime = GetTickCount();
			s_dwPendingBackwardSeekTimeout = s_dwBackwardSeekStartTime + 15000;
			ResetDemoTickTracking( tick, true );
			IssueCommand( "demo_gototick 0 0 1" );
		}
		else
		{
			s_bRewindingToZero = false;
			s_nPendingBackwardSeekTick = -1;
			ResetDemoTickTracking( tick, true );
			IssueCommand( "demo_gototick %d 0 1", g_Gui.demoTick );
		}
	}

	void ToggleDemoPlayback( bool paused )
	{
		if ( !g_pEngine || !g_pEngine->IsPlayingDemo() )
			return;

		if ( paused )
		{
			// Restart local tick tracking from the engine clock when playback resumes.
			g_Gui.demoClockTime = g_pEngine->Time();
			IssueCommand( "demo_resume" );
		}
		else
		{
			IssueCommand( "demo_pause" );
		}
	}

	void JumpDemoTick( int delta )
	{
		long long base = g_Gui.currentDemoTickValid ? g_Gui.currentDemoTick : g_Gui.demoTick;
		long long target = base + delta;
		if ( target < 0 ) target = 0;
		if ( target > 2147483647LL ) target = 2147483647LL;
		SeekDemoTick( static_cast<int>( target ) );
	}


	// -------------------------------------------------------------------------
	// Main GUI pages and help popups
	// -------------------------------------------------------------------------
	void DrawCapturePage()
	{
		const bool recording = InterlockedCompareExchange( &g_bRecording, FALSE, FALSE ) != FALSE;
		const bool validating = IsArtValidationRunning();
		TextUnformattedWrapped( "Capture session" );
		ImGui::Separator();
		ImGui::Spacing();

		ImGui::BeginChild( "capture_card", ImVec2( 0, 0 ),
			ImGuiChildFlags_Borders | ImGuiChildFlags_AutoResizeY );
		StatusBadge( recording, "RECORDING", "IDLE" );
		ImGui::SameLine();
		TextDisabledWrapped( "Frame %d", g_nFrame );
		ImGui::SameLine();
		const LONG currentOutputMode = InterlockedCompareExchange( &g_nArtOutputMode, 0, 0 );
		if ( currentOutputMode == ART_OUTPUT_MODE_FFMPEG )
		{
			const LONG currentPreset = InterlockedCompareExchange( &g_nArtFfmpegPreset, 0, 0 );
			ImGui::TextColored( ImVec4( 0.35f, 0.80f, 1.00f, 1.0f ), "[FFmpeg: %s]", GetArtFfmpegPresetName( currentPreset ) );
		}
		else
		{
			ImGui::TextColored( ImVec4( 0.70f, 0.70f, 0.70f, 1.0f ), "[TGA Frames]" );
		}
		TextDisabledWrapped( "The menu does not show on recorded footage." );
		ImGui::Spacing();
		ImGui::SetNextItemWidth( -1 );
		ImGui::InputTextWithHint( "##take", "Optional take name", g_Gui.takeName, sizeof( g_Gui.takeName ) );
		ImGui::Spacing();
		if ( !recording )
		{
			ImGui::BeginDisabled( validating );
			if ( ImGui::Button( "Start recording", ImVec2( 170, 38 ) ) )
			{
				if ( !g_Gui.takeName[0] )
					IssueCommand( "art_toggle" );
				else if ( IsSafeConfigName( g_Gui.takeName ) )
					IssueCommand( "art_toggle %s", g_Gui.takeName );
				else
					SetError( "take name may contain only letters, numbers, '_' and '-'" );
			}
			ImGui::EndDisabled();
		}
		else
		{
			ImGui::PushStyleColor( ImGuiCol_Button, ImVec4( 0.72f, 0.08f, 0.06f, 1.0f ) );
			ImGui::PushStyleColor( ImGuiCol_ButtonHovered, ImVec4( 0.90f, 0.12f, 0.08f, 1.0f ) );
			ImGui::PushStyleColor( ImGuiCol_ButtonActive, ImVec4( 0.58f, 0.04f, 0.03f, 1.0f ) );
			if ( ImGui::Button( "Stop recording", ImVec2( 170, 38 ) ) )
				IssueCommand( "art_toggle" );
			ImGui::PopStyleColor( 3 );
		}
		ImGui::SameLine();
		if ( ImGui::Button( "Help", ImVec2( 85, 38 ) ) )
		{
			g_Gui.showCaptureHelp = true;
			IssueCommandWithSeparator( "art_help" );
		}
		ImGui::SameLine();
		ImGui::BeginGroup();
		ImGui::TextColored( GuiColor( g_Gui.mutedTextColor ), "host_framerate" );
		const float hostFramerateControlHeight = ImGui::GetFrameHeight();
		ImGui::SetNextItemWidth( 72 );
		if ( ImGui::InputInt( "##host_framerate", &g_Gui.hostFramerate, 0, 0 ) )
		{
			if ( g_Gui.hostFramerate < 0 ) g_Gui.hostFramerate = 0;
			if ( g_Gui.hostFramerate > 2000 ) g_Gui.hostFramerate = 2000;
			IssueCommand( "host_framerate %d", g_Gui.hostFramerate );
		}
		HoverExplanation( "Sets deterministic demo playback and capture timing. 0 uses real time." );
		static const int hostFrameratePresets[] = { 0, 150, 300, 600, 2000 };
		for ( int i = 0; i < ARRAYSIZE( hostFrameratePresets ); ++i )
		{
			ImGui::SameLine();
			char label[32];
			Q_snprintf( label, sizeof( label ), "%d##capture_host_framerate_%d",
				hostFrameratePresets[i], i );
			if ( ImGui::Button( label, ImVec2( hostFrameratePresets[i] >= 1000 ? 52.0f : 44.0f, hostFramerateControlHeight ) ) )
			{
				g_Gui.hostFramerate = hostFrameratePresets[i];
				IssueCommand( "host_framerate %d", g_Gui.hostFramerate );
			}
			HoverExplanation( "Set host_framerate to %d.", hostFrameratePresets[i] );
		}
		ImGui::EndGroup();
		if ( validating )
		{
			ImGui::TextColored( ImVec4( 1.0f, 0.58f, 0.20f, 1.0f ),
				"Validation: %s (%ld/%ld files)",
				ArtValidationPhaseName( InterlockedCompareExchange(
					&g_ArtValidationProgress.phase, 0, 0 ) ),
				InterlockedCompareExchange( &g_ArtValidationProgress.completedFiles, 0, 0 ),
				InterlockedCompareExchange( &g_ArtValidationProgress.totalFiles, 0, 0 ) );
		}

		ImGui::EndChild();

		ImGui::Spacing();
		ImGui::BeginChild( "capture_demo", ImVec2( 0, 0 ),
			ImGuiChildFlags_Borders | ImGuiChildFlags_AutoResizeY );
		TextUnformattedWrapped( "Demo playback" );
		if ( g_Gui.refreshDemos )
			RefreshDemoList();
		UpdateDemoTickTracking();

		const bool playingDemo = g_pEngine && g_pEngine->IsPlayingDemo();
		const bool pausedDemo = playingDemo && g_pEngine->IsPaused();
		UpdateDemoPlayerAutoRefresh();
		ImGui::Text( "Current tick: %s", g_Gui.currentDemoTickValid ? "" : "not available" );
		if ( g_Gui.currentDemoTickValid )
		{
			ImGui::SameLine();
			ImGui::TextColored( GuiColor( g_Gui.accentColor ), "%d", g_Gui.currentDemoTick );
		}
		ImGui::SameLine();
		StatusBadge( pausedDemo, "PAUSED", playingDemo ? "PLAYING" : "NO DEMO" );

		ImGui::BeginDisabled( !playingDemo );
		if ( ImGui::Button( pausedDemo ? "Resume" : "Pause", ImVec2( 120, 0 ) ) )
			ToggleDemoPlayback( pausedDemo );
		ImGui::EndDisabled();
		HoverExplanation( pausedDemo ?
			"Resume demo playback." : "Pause demo playback." );

		const char *pSelectedDemo = g_Gui.selectedDemo >= 0 &&
			g_Gui.selectedDemo < static_cast<int>( g_Gui.demos.size() ) ?
			g_Gui.demos[g_Gui.selectedDemo].c_str() : "No demos found";
		ImGui::SetNextItemWidth( -110 );
		if ( ImGui::BeginCombo( "Available demos", pSelectedDemo ) )
		{
			for ( size_t i = 0; i < g_Gui.demos.size(); ++i )
			{
				const bool selected = g_Gui.selectedDemo == static_cast<int>( i );
				if ( ImGui::Selectable( g_Gui.demos[i].c_str(), selected ) )
				{
					g_Gui.selectedDemo = static_cast<int>( i );
					Q_strncpy( g_Gui.demoName, g_Gui.demos[i].c_str(), sizeof( g_Gui.demoName ) );
				}
				if ( selected ) ImGui::SetItemDefaultFocus();
			}
			ImGui::EndCombo();
		}
		ImGui::SameLine();
		if ( ImGui::Button( "Refresh", ImVec2( 100, 0 ) ) )
			RefreshDemoList();

		const char *pDemoSuggestion = g_Gui.selectedDemo >= 0 &&
			g_Gui.selectedDemo < static_cast<int>( g_Gui.demos.size() ) ?
			g_Gui.demos[g_Gui.selectedDemo].c_str() :
			( !g_Gui.demos.empty() ? g_Gui.demos[0].c_str() : "example.dem" );
		char demoHint[MAX_PATH + 32];
		Q_snprintf( demoHint, sizeof( demoHint ), "Demo name or path, e.g. %s", pDemoSuggestion );
		ImGui::SetNextItemWidth( -110 );
		ImGui::InputTextWithHint( "##demo_name", demoHint, g_Gui.demoName, sizeof( g_Gui.demoName ) );
		ImGui::SameLine();
		if ( ImGui::Button( "Load demo", ImVec2( 100, 0 ) ) )
		{
			if ( !g_Gui.demoName[0] && g_Gui.selectedDemo >= 0 &&
				g_Gui.selectedDemo < static_cast<int>( g_Gui.demos.size() ) )
			{
				Q_strncpy( g_Gui.demoName, g_Gui.demos[g_Gui.selectedDemo].c_str(), sizeof( g_Gui.demoName ) );
			}
			LoadDemoFromField();
		}

		ImGui::SetNextItemWidth( 190 );
		ImGui::DragInt( "Target tick##demo_tick", &g_Gui.demoTick, 1.0f, 0, INT_MAX );
		if ( g_Gui.demoTick < 0 ) g_Gui.demoTick = 0;
		ImGui::SameLine();
		if ( ImGui::Button( "Go to tick", ImVec2( 100, 0 ) ) )
			SeekDemoTick( g_Gui.demoTick );

		if ( ImGui::Button( "-1000", ImVec2( 72, 0 ) ) ) JumpDemoTick( -1000 );
		ImGui::SameLine();
		if ( ImGui::Button( "-100", ImVec2( 72, 0 ) ) ) JumpDemoTick( -100 );
		ImGui::SameLine();
		if ( ImGui::Button( "+100", ImVec2( 72, 0 ) ) ) JumpDemoTick( 100 );
		ImGui::SameLine();
		if ( ImGui::Button( "+1000", ImVec2( 72, 0 ) ) ) JumpDemoTick( 1000 );

		ImGui::Spacing();
		bool autoResume = g_Gui.autoResumeDemoOnRecordingStart;
		if ( ImGui::Checkbox( "Unpause demo on recording start", &autoResume ) )
		{
			IssueCommand( "art_demo_unpause_on_recording %s", autoResume ? "on" : "off" );
		}

		ImGui::Separator();
		TextUnformattedWrapped( "Spectator" );
		const DemoPlayerEntry *pSelectedPlayer = SelectedDemoPlayer();
		const char *pSelectedPlayerName = pSelectedPlayer ? pSelectedPlayer->name.c_str() :
			( playingDemo ? "No players found" : "Load a demo first" );
		ImGui::SetNextItemWidth( -110 );
		if ( ImGui::BeginCombo( "Player##demo_spectator", pSelectedPlayerName ) )
		{
			for ( size_t i = 0; i < g_Gui.demoPlayers.size(); ++i )
			{
				char playerLabel[160];
				Q_snprintf( playerLabel, sizeof( playerLabel ), "%s  [slot %d]",
					g_Gui.demoPlayers[i].name.c_str(), g_Gui.demoPlayers[i].entityIndex );
				const bool selected = g_Gui.selectedDemoPlayer == static_cast<int>( i );
				if ( ImGui::Selectable( playerLabel, selected ) )
					g_Gui.selectedDemoPlayer = static_cast<int>( i );
				if ( selected ) ImGui::SetItemDefaultFocus();
			}
			ImGui::EndCombo();
		}
		ImGui::SameLine();
		if ( ImGui::Button( "Refresh players", ImVec2( 100, 0 ) ) )
		{
			StopDemoPlayerAutoRefresh();
			RefreshDemoPlayerList();
		}
		if ( playingDemo && g_Gui.refreshDemoPlayers )
		{
			ImGui::TextColored( GuiColor( g_Gui.accentColor ),
				"Auto-refreshing players... scan %d/%d",
				g_Gui.demoPlayerRefreshAttempts, kDemoPlayerRefreshMaximumAttempts );
		}

		if ( ImGui::Button( "Spectate selected", ImVec2( 145, 0 ) ) )
			SpectateSelectedDemoPlayer();
		ImGui::SameLine();
		const bool firstPerson = g_Gui.spectatorMode == 3;
		if ( firstPerson ) ImGui::PushStyleColor( ImGuiCol_Button, GuiColor( g_Gui.selectionColor ) );
		if ( ImGui::Button( "First person", ImVec2( 120, 0 ) ) )
		{
			g_Gui.spectatorMode = 3;
			if ( playingDemo ) ApplyDemoSpectatorMode( 3, SelectedDemoPlayer() != NULL );
		}
		if ( firstPerson ) ImGui::PopStyleColor();
		ImGui::SameLine();
		const bool thirdPerson = g_Gui.spectatorMode == 4;
		if ( thirdPerson ) ImGui::PushStyleColor( ImGuiCol_Button, GuiColor( g_Gui.selectionColor ) );
		if ( ImGui::Button( "Third person", ImVec2( 120, 0 ) ) )
		{
			g_Gui.spectatorMode = 4;
			if ( playingDemo ) ApplyDemoSpectatorMode( 4, SelectedDemoPlayer() != NULL );
		}
		if ( thirdPerson ) ImGui::PopStyleColor();
		ImGui::SameLine();
		const bool freeCamera = g_Gui.spectatorMode == 5;
		if ( freeCamera ) ImGui::PushStyleColor( ImGuiCol_Button, GuiColor( g_Gui.selectionColor ) );
		if ( ImGui::Button( "Free cam", ImVec2( 105, 0 ) ) )
		{
			g_Gui.spectatorMode = 5;
			if ( playingDemo ) ApplyDemoSpectatorMode( 5, false );
		}
		if ( freeCamera ) ImGui::PopStyleColor();
		HoverExplanation( "Use build-4044 Free Look mode for unrestricted spectator movement." );

		if ( g_Gui.spectatorUiHidden )
			ImGui::PushStyleColor( ImGuiCol_Button, GuiColor( g_Gui.selectionColor ) );
		if ( ImGui::Button( g_Gui.spectatorUiHidden ? "Show spectator bars" : "Hide spectator bars",
			ImVec2( 170, 0 ) ) )
		{
			SetSpectatorUiHidden( !g_Gui.spectatorUiHidden );
		}
		if ( g_Gui.spectatorUiHidden )
			ImGui::PopStyleColor();
		ImGui::SameLine();
		TextDisabledWrapped( "Bars and the default CS:S camera menu." );

		TextDisabledWrapped( "Go to tick and jump buttons pause playback at the destination." );
		TextDisabledWrapped( "Current tick is tracked from the demo header and engine playback clock." );
		TextDisabledWrapped( "Scans .dem files under cstrike and its subfolders." );
		ImGui::EndChild();

	}

	void DrawPassesPage()
	{
		struct PassRow { const char *label; const char *command; LONG bit; };
		static const PassRow rows[] =
		{
			{ "Normal", "normal", ART_RECORD_NORMAL },
			{ "Clear", "clear", ART_RECORD_CLEAR },
			{ "Clear - no players", "clear-noplayers", ART_RECORD_CLEAR_NOPLAYERS },
			{ "Viewmodel", "viewmodel", ART_RECORD_VIEWMODEL },
			{ "Players", "players", ART_RECORD_PLAYERS },
			{ "ObjectID", "objectid", ART_RECORD_OBJECTID },
			{ "Depth", "depth", ART_RECORD_DEPTH }
		};

		LONG recordMask = InterlockedCompareExchange( &g_nRecordMask, 0, 0 );
		LONG hudMask = InterlockedCompareExchange( &g_nHudMask, 0, 0 );
		TextUnformattedWrapped( "Pass recording" );
		ImGui::Separator();
		ImGui::Spacing();
		const ImGuiTableFlags tableFlags = ImGuiTableFlags_BordersInnerH | ImGuiTableFlags_RowBg |
			ImGuiTableFlags_SizingFixedFit;
		if ( ImGui::BeginTable( "pass_table", 3, tableFlags ) )
		{
			ImGui::TableSetupColumn( "PASS", ImGuiTableColumnFlags_WidthFixed, 170.0f );
			ImGui::TableSetupColumn( "RECORD", ImGuiTableColumnFlags_WidthFixed, 150.0f );
			ImGui::TableSetupColumn( "HUD", ImGuiTableColumnFlags_WidthStretch );
			ImGui::TableHeadersRow();

			for ( int i = 0; i < ARRAYSIZE( rows ); ++i )
			{
				ImGui::TableNextRow();
				ImGui::TableSetColumnIndex( 0 );
				TextUnformattedWrapped( rows[i].label );
				ImGui::TableSetColumnIndex( 1 );
				bool record = ( recordMask & rows[i].bit ) != 0;
				char recordId[64]; Q_snprintf( recordId, sizeof( recordId ), "##record_%s", rows[i].command );
				if ( ImGui::Checkbox( recordId, &record ) )
					IssueCommand( "art_record %s %s", rows[i].command, record ? "on" : "off" );
				ImGui::TableSetColumnIndex( 2 );
				bool hud = ( hudMask & rows[i].bit ) != 0;
				char hudId[64]; Q_snprintf( hudId, sizeof( hudId ), "##hud_%s", rows[i].command );
				if ( ImGui::Checkbox( hudId, &hud ) )
					IssueCommand( "art_hud %s %s", rows[i].command, hud ? "on" : "off" );
			}
			ImGui::EndTable();
		}
		ImGui::Spacing();
		if ( ImGui::Button( "Enable all recording" ) ) IssueCommand( "art_record all on" );
		ImGui::SameLine();
		if ( ImGui::Button( "Disable all recording" ) ) IssueCommand( "art_record all off" );
		ImGui::SameLine();
		if ( ImGui::Button( "HUD off for all" ) ) IssueCommand( "art_hud all off" );
		ImGui::Spacing();
		bool playersThroughWalls = InterlockedCompareExchange( &g_bPlayersPassThroughWalls, FALSE, FALSE ) != FALSE;
		if ( ImGui::Checkbox( "Players pass: render players through walls", &playersThroughWalls ) )
			IssueCommand( "art_players_through_walls %s", playersThroughWalls ? "on" : "off" );
		TextDisabledWrapped( "Off keeps normal world-depth occlusion; on forces player-pass models in front of world geometry." );
		bool playersWorldWeapons = InterlockedCompareExchange( &g_bPlayersPassWorldWeapons, FALSE, FALSE ) != FALSE;
		if ( ImGui::Checkbox( "Players pass: world weapon models", &playersWorldWeapons ) )
			IssueCommand( "art_players_world_weapons %s", playersWorldWeapons ? "on" : "off" );
		TextDisabledWrapped( "Controls third-person and dropped weapon models in the players pass; off leaves them in the keyed world." );

		ImGui::Spacing();
		TextUnformattedWrapped( "Pass colors and depth" );
		ImGui::Separator();
		float viewmodelBackgroundColor[3] =
		{
			g_nViewmodelBackgroundRed / 255.0f,
			g_nViewmodelBackgroundGreen / 255.0f,
			g_nViewmodelBackgroundBlue / 255.0f
		};
		ImGui::SetNextItemWidth( 330 );
		if ( ImGui::ColorEdit3( "Viewmodel background", viewmodelBackgroundColor,
			ImGuiColorEditFlags_DisplayRGB | ImGuiColorEditFlags_Uint8 ) )
		{
			IssueCommand( "art_viewmodel_color %d %d %d",
				static_cast<int>( viewmodelBackgroundColor[0] * 255.0f + 0.5f ),
				static_cast<int>( viewmodelBackgroundColor[1] * 255.0f + 0.5f ),
				static_cast<int>( viewmodelBackgroundColor[2] * 255.0f + 0.5f ) );
		}
		float playersBackgroundColor[3] =
		{
			g_nPlayersBackgroundRed / 255.0f,
			g_nPlayersBackgroundGreen / 255.0f,
			g_nPlayersBackgroundBlue / 255.0f
		};
		ImGui::SetNextItemWidth( 330 );
		if ( ImGui::ColorEdit3( "Players background", playersBackgroundColor,
			ImGuiColorEditFlags_DisplayRGB | ImGuiColorEditFlags_Uint8 ) )
		{
			IssueCommand( "art_players_color %d %d %d",
				static_cast<int>( playersBackgroundColor[0] * 255.0f + 0.5f ),
				static_cast<int>( playersBackgroundColor[1] * 255.0f + 0.5f ),
				static_cast<int>( playersBackgroundColor[2] * 255.0f + 0.5f ) );
		}
		float depthStart = art_depth_start.GetFloat();
		float depthEnd = art_depth_end.GetFloat();
		ImGui::SetNextItemWidth( 330 );
		if ( ImGui::DragFloat( "Depth start", &depthStart, 5.0f, 0.0f, 100000.0f, "%.0f" ) )
			IssueCommand( "art_depth_start %.3f", depthStart );
		ImGui::SetNextItemWidth( 330 );
		if ( ImGui::DragFloat( "Depth end", &depthEnd, 5.0f, 0.0f, 100000.0f, "%.0f" ) )
			IssueCommand( "art_depth_end %.3f", depthEnd );
		TextDisabledWrapped( "Viewmodel and Players use independent chroma colors. Depth fades from black nearby to white far away." );

		ImGui::Spacing();
		TextUnformattedWrapped( "ObjectID colors" );
		ImGui::Separator();
		struct ObjectIdColorRow
		{
			const char *label;
			const char *command;
			volatile LONG *red;
			volatile LONG *green;
			volatile LONG *blue;
		};
		static const ObjectIdColorRow objectIdRows[] =
		{
			{ "Viewmodel##objectid", "viewmodel", &g_nObjectIdViewmodelRed, &g_nObjectIdViewmodelGreen, &g_nObjectIdViewmodelBlue },
			{ "Players##objectid", "players", &g_nObjectIdPlayersRed, &g_nObjectIdPlayersGreen, &g_nObjectIdPlayersBlue },
			{ "World##objectid", "world", &g_nObjectIdWorldRed, &g_nObjectIdWorldGreen, &g_nObjectIdWorldBlue },
			{ "Skybox##objectid", "skybox", &g_nObjectIdSkyboxRed, &g_nObjectIdSkyboxGreen, &g_nObjectIdSkyboxBlue }
		};
		for ( int i = 0; i < ARRAYSIZE( objectIdRows ); ++i )
		{
			float objectColor[3] =
			{
				InterlockedCompareExchange( objectIdRows[i].red, 0, 0 ) / 255.0f,
				InterlockedCompareExchange( objectIdRows[i].green, 0, 0 ) / 255.0f,
				InterlockedCompareExchange( objectIdRows[i].blue, 0, 0 ) / 255.0f
			};
			ImGui::SetNextItemWidth( 330 );
			if ( ImGui::ColorEdit3( objectIdRows[i].label, objectColor, ImGuiColorEditFlags_DisplayRGB | ImGuiColorEditFlags_Uint8 ) )
			{
				IssueCommand( "art_objectid_color %s %d %d %d", objectIdRows[i].command,
					static_cast<int>( objectColor[0] * 255.0f + 0.5f ),
					static_cast<int>( objectColor[1] * 255.0f + 0.5f ),
					static_cast<int>( objectColor[2] * 255.0f + 0.5f ) );
			}
		}
		TextDisabledWrapped( "The ObjectID pass assigns one exact RGB category color to viewmodel, players, world models/BSP/decals, and skybox." );
	}


	void DrawVisualsPage()
	{
		static const char *previewNames[] = { "Off", "Normal", "Clear", "Clear - no players", "Viewmodel", "Depth", "Players", "ObjectID" };
		LONG preview = InterlockedCompareExchange( &g_nPreviewPass, ART_PREVIEW_NONE, ART_PREVIEW_NONE );
		int previewIndex = static_cast<int>( preview );

		TextUnformattedWrapped( "Preview and render controls" );
		ImGui::Separator();
		ImGui::Spacing();
		ImGui::SetNextItemWidth( 240 );
		if ( ImGui::Combo( "Live preview", &previewIndex, previewNames, ARRAYSIZE( previewNames ) ) )
		{
			static const char *previewCommands[] = { "off", "normal", "clear", "clear-noplayers", "viewmodel", "depth", "players", "objectid" };
			IssueCommand( "art_preview %s", previewCommands[previewIndex] );
		}
		ImGui::SameLine();
		if ( ImGui::Button( "Next pass##preview" ) )
			IssueCommand( "art_preview_next" );

		float globalFov = g_flGlobalFov;
		ImGui::SetNextItemWidth( 330 );
		const bool globalFovChanged = ImGui::DragFloat(
			"Global FOV", &globalFov, 0.1f, 1.0f, 179.0f, "%.2f" );
		ImGui::SameLine();
		if ( ImGui::SmallButton( "Default##global_fov" ) )
			IssueCommand( "art_fov default" );
		if ( globalFovChanged )
		{
			if ( globalFov < 1.0f ) globalFov = 1.0f;
			if ( globalFov > 179.0f ) globalFov = 179.0f;
			IssueCommand( "art_fov %.3f", globalFov );
		}
		bool preserveZoom = InterlockedCompareExchange( &g_bGlobalFovHandleZoom, FALSE, FALSE ) != FALSE;
		if ( ImGui::Checkbox( "Preserve scoped / zoom FOV", &preserveZoom ) )
			IssueCommand( "art_fov handleZoom enabled %d", preserveZoom ? 1 : 0 );
		if ( preserveZoom && g_Gui.experimentalOptionsEnabled )
		{
			float minUnzoomedFov = g_flGlobalFovMinUnzoomedFov;
			ImGui::SetNextItemWidth( 330 );
			if ( ImGui::SliderFloat( "Minimum unzoomed FOV (experimental)", &minUnzoomedFov, 1.0f, 179.0f, "%.1f" ) )
				IssueCommand( "art_fov handleZoom minUnzoomedFov %.3f", minUnzoomedFov );
			ImGui::TextWrapped( "Defines the boundary between an unzoomed camera and a scoped camera. When the game FOV falls below this value, the global FOV override is skipped so the weapon scope keeps its original zoom." );
		}

		float viewmodelFov = g_flViewmodelFov;
		ImGui::SetNextItemWidth( 330 );
		const bool viewmodelFovChanged = ImGui::DragFloat(
			"Viewmodel FOV", &viewmodelFov, 0.1f, 1.0f, 179.0f, "%.2f" );
		ImGui::SameLine();
		if ( ImGui::SmallButton( "Default##viewmodel_fov" ) )
			IssueCommand( "art_viewmodel_fov default" );
		if ( viewmodelFovChanged )
		{
			if ( viewmodelFov < 1.0f ) viewmodelFov = 1.0f;
			if ( viewmodelFov > 179.0f ) viewmodelFov = 179.0f;
			IssueCommand( "art_viewmodel_fov %.3f", viewmodelFov );
		}
		TextDisabledWrapped( "Global FOV overrides both the camera and viewmodel FOV. The separate viewmodel value is used only while Global FOV is set to Default." );

		ImGui::Spacing();
		ImGui::BeginChild( "visibility_controls", ImVec2( 0, 105 ), ImGuiChildFlags_Borders );
		TextUnformattedWrapped( "Visibility" );
		bool viewmodelVisible = InterlockedCompareExchange( &g_bViewmodelVisible, FALSE, FALSE ) != FALSE;
		bool playersVisible = InterlockedCompareExchange( &g_bPlayersVisible, FALSE, FALSE ) != FALSE;
		if ( ImGui::Checkbox( "Viewmodel visible", &viewmodelVisible ) )
			IssueCommand( "art_visible viewmodel %s", viewmodelVisible ? "on" : "off" );
		ImGui::SameLine();
		if ( ImGui::Checkbox( "Players visible", &playersVisible ) )
			IssueCommand( "art_visible players %s", playersVisible ? "on" : "off" );
		TextDisabledWrapped( "Checked means the model category is visible." );
		ImGui::EndChild();

		ImGui::Spacing();
		ImGui::BeginChild( "visual_removals", ImVec2( 0, 0 ),
			ImGuiChildFlags_Borders | ImGuiChildFlags_AutoResizeY );
		TextUnformattedWrapped( "Visual overrides" );
		bool noFlash = InterlockedCompareExchange( &g_bNoFlashEnabled, FALSE, FALSE ) != FALSE;
		bool noSmoke = InterlockedCompareExchange( &g_bNoSmokeEnabled, FALSE, FALSE ) != FALSE;
		if ( ImGui::Checkbox( "No flash", &noFlash ) )
			IssueCommand( "art_noflash %s", noFlash ? "on" : "off" );
		ImGui::SameLine();
		if ( ImGui::Checkbox( "No smoke", &noSmoke ) )
			IssueCommand( "art_nosmoke %s", noSmoke ? "on" : "off" );

		bool forceRenderLod = InterlockedCompareExchange( &g_bForceRenderLodEnabled, FALSE, FALSE ) != FALSE;
		int forcedRenderLod = static_cast<int>( InterlockedCompareExchange( &g_nForcedRenderLodValue, 0, 0 ) );
		if ( ImGui::Checkbox( "Force r_lod", &forceRenderLod ) )
			IssueCommand( "art_force_r_lod %s", forceRenderLod ? "on" : "off" );
		ImGui::SameLine();
		ImGui::SetNextItemWidth( 140 );
		if ( ImGui::InputInt( "r_lod value", &forcedRenderLod ) )
			IssueCommand( "art_force_r_lod value %d", forcedRenderLod );
		TextDisabledWrapped( "No flash and no smoke affect live view and capture. Force r_lod blocks demo cvar updates; default value is -7." );
		ImGui::EndChild();

		ImGui::Spacing();
		const float flatChamsHeight =
			ImGui::GetFrameHeightWithSpacing() * 6.0f +
			ImGui::GetTextLineHeightWithSpacing() * 2.0f +
			ImGui::GetStyle().WindowPadding.y * 2.0f;
		ImGui::BeginChild( "flat_chams", ImVec2( 0, flatChamsHeight ), ImGuiChildFlags_Borders );
		TextUnformattedWrapped( "Flat chams" );
		bool playerChams = InterlockedCompareExchange( &g_bPlayerChamsEnabled, FALSE, FALSE ) != FALSE;
		if ( ImGui::Checkbox( "Players", &playerChams ) )
			IssueCommand( "art_chams players %s", playerChams ? "on" : "off" );
		ImGui::SameLine();
		bool playerChamsThroughWalls = InterlockedCompareExchange( &g_bPlayerChamsThroughWalls, FALSE, FALSE ) != FALSE;
		ImGui::BeginDisabled( !playerChams );
		if ( ImGui::Checkbox( "Through walls##player_chams", &playerChamsThroughWalls ) )
			IssueCommand( "art_chams players_through_walls %s", playerChamsThroughWalls ? "on" : "off" );
		ImGui::EndDisabled();
		float playerColor[3] =
		{
			InterlockedCompareExchange( &g_nPlayerChamsRed, 0, 0 ) / 255.0f,
			InterlockedCompareExchange( &g_nPlayerChamsGreen, 0, 0 ) / 255.0f,
			InterlockedCompareExchange( &g_nPlayerChamsBlue, 0, 0 ) / 255.0f
		};
		ImGui::SetNextItemWidth( 330 );
		if ( ImGui::ColorEdit3( "Player color", playerColor, ImGuiColorEditFlags_DisplayRGB | ImGuiColorEditFlags_Uint8 ) )
			IssueCommand( "art_chams players_color %d %d %d",
				static_cast<int>( playerColor[0] * 255.0f + 0.5f ),
				static_cast<int>( playerColor[1] * 255.0f + 0.5f ),
				static_cast<int>( playerColor[2] * 255.0f + 0.5f ) );

		bool viewmodelChams = InterlockedCompareExchange( &g_bViewmodelChamsEnabled, FALSE, FALSE ) != FALSE;
		if ( ImGui::Checkbox( "Viewmodel", &viewmodelChams ) )
			IssueCommand( "art_chams viewmodel %s", viewmodelChams ? "on" : "off" );
		float viewmodelColor[3] =
		{
			InterlockedCompareExchange( &g_nViewmodelChamsRed, 0, 0 ) / 255.0f,
			InterlockedCompareExchange( &g_nViewmodelChamsGreen, 0, 0 ) / 255.0f,
			InterlockedCompareExchange( &g_nViewmodelChamsBlue, 0, 0 ) / 255.0f
		};
		ImGui::SetNextItemWidth( 330 );
		if ( ImGui::ColorEdit3( "Viewmodel color", viewmodelColor, ImGuiColorEditFlags_DisplayRGB | ImGuiColorEditFlags_Uint8 ) )
			IssueCommand( "art_chams viewmodel_color %d %d %d",
				static_cast<int>( viewmodelColor[0] * 255.0f + 0.5f ),
				static_cast<int>( viewmodelColor[1] * 255.0f + 0.5f ),
				static_cast<int>( viewmodelColor[2] * 255.0f + 0.5f ) );


		bool skyboxChams = InterlockedCompareExchange( &g_bSkyboxChamsEnabled, FALSE, FALSE ) != FALSE;
		if ( ImGui::Checkbox( "Skybox", &skyboxChams ) )
			IssueCommand( "art_chams skybox %s", skyboxChams ? "on" : "off" );
		float skyboxColor[3] =
		{
			InterlockedCompareExchange( &g_nSkyboxChamsRed, 0, 0 ) / 255.0f,
			InterlockedCompareExchange( &g_nSkyboxChamsGreen, 0, 0 ) / 255.0f,
			InterlockedCompareExchange( &g_nSkyboxChamsBlue, 0, 0 ) / 255.0f
		};
		ImGui::SetNextItemWidth( 330 );
		if ( ImGui::ColorEdit3( "Skybox color", skyboxColor, ImGuiColorEditFlags_DisplayRGB | ImGuiColorEditFlags_Uint8 ) )
			IssueCommand( "art_chams skybox_color %d %d %d",
				static_cast<int>( skyboxColor[0] * 255.0f + 0.5f ),
				static_cast<int>( skyboxColor[1] * 255.0f + 0.5f ),
				static_cast<int>( skyboxColor[2] * 255.0f + 0.5f ) );
		TextDisabledWrapped( "Skybox chams are independent from player and viewmodel chams." );
		ImGui::EndChild();

	}

	void DrawOutputPage()
	{
		if ( g_Gui.refreshTakes )
			RefreshTakeList();
		const bool recording = InterlockedCompareExchange( &g_bRecording, FALSE, FALSE ) != FALSE;
		TextUnformattedWrapped( "Output naming" );
		ImGui::Separator();
		ImGui::Spacing();

		ImGui::BeginDisabled( recording );
		ImGui::SetNextItemWidth( -1 );
		const bool outputPathSubmitted = ImGui::InputTextWithHint( "##path",
			"Output path", g_Gui.outputPath, sizeof( g_Gui.outputPath ),
			ImGuiInputTextFlags_EnterReturnsTrue );
		const bool outputPathFinished = ImGui::IsItemDeactivatedAfterEdit();
		if ( outputPathSubmitted || outputPathFinished )
			ApplyOutputPathField();
		TextDisabledWrapped(
			"Applied automatically after editing. Relative paths are resolved under cstrike; absolute and UNC paths are supported." );

		// Reserve space for the visible label so it stays beside the field instead of being clipped.
		static const char *pPrefixLabel = "Prefix";
		float prefixFieldWidth = ImGui::GetContentRegionAvail().x
			- ImGui::CalcTextSize( pPrefixLabel ).x - ImGui::GetStyle().ItemSpacing.x;
		if ( prefixFieldWidth < 120.0f )
			prefixFieldWidth = 120.0f;
		ImGui::SetNextItemWidth( prefixFieldWidth );
		const bool prefixSubmitted = ImGui::InputTextWithHint( "##prefix", "default",
			g_Gui.prefix, sizeof( g_Gui.prefix ), ImGuiInputTextFlags_EnterReturnsTrue );
		const bool prefixFinished = ImGui::IsItemDeactivatedAfterEdit();
		ImGui::SameLine();
		ImGui::AlignTextToFramePadding();
		TextUnformattedSingleLine( pPrefixLabel );
		if ( prefixSubmitted || prefixFinished )
			ApplyPrefixField();
		ImGui::TextWrapped( "Prefix is optional text added before every pass filename. Example: prefix 'shot01' creates shot01_normal_0000.tga; Default creates normal_0000.tga." );
		ImGui::EndDisabled();

		if ( recording )
			ImGui::TextColored( ImVec4( 1.0f, 0.65f, 0.30f, 1.0f ), "Stop recording before changing path or prefix." );
		ImGui::Spacing();
		if ( ImGui::Button( "Open output folder", ImVec2( 170, 34 ) ) )
			IssueCommand( "art_open_folder" );
		ImGui::SameLine();
		if ( ImGui::Button( "Refresh fields from game", ImVec2( 190, 34 ) ) )
		{
			SyncTextFieldsFromGame();
			RefreshTakeList();
		}

		ImGui::Spacing();
		TextUnformattedWrapped( "Take metadata" );
		ImGui::Separator();
		bool takeJsonEnabled = InterlockedCompareExchange(
			&g_bArtTakeManifestEnabled, FALSE, FALSE ) != FALSE;
		if ( ImGui::Checkbox( "Generate [take].json", &takeJsonEnabled ) )
			IssueCommand( "art_take_json %s", takeJsonEnabled ? "on" : "off" );
		TextDisabledWrapped( "Writes import-ready take, sequence, pass, color, timing, queue, and validation metadata." );
		char manifestPath[MAX_PATH];
		FormatArtTakeManifestPath( manifestPath, sizeof( manifestPath ) );
		ImGui::TextWrapped( "Latest: %s",
			manifestPath[0] ? manifestPath : "no take recorded this session" );
		ImGui::BeginDisabled( recording || !manifestPath[0] );
		if ( ImGui::Button( "Write / update latest JSON", ImVec2( 205, 30 ) ) )
			IssueCommandWithSeparator( "art_take_json write" );
		ImGui::EndDisabled();
		if ( recording )
			TextDisabledWrapped( "The current take uses the metadata setting captured when recording started." );

		ImGui::Spacing();
		TextUnformattedWrapped( "Output format & video encoding" );
		ImGui::Separator();
		ImGui::Spacing();

		LONG outputMode = InterlockedCompareExchange( &g_nArtOutputMode, 0, 0 );
		int modeSelection = ( outputMode == ART_OUTPUT_MODE_FFMPEG ) ? 1 : 0;
		static const char *outputModeNames[] = { "Image sequences (TGA frames)", "Direct video stream (FFmpeg)" };
		ImGui::SetNextItemWidth( 320 );
		ImGui::BeginDisabled( recording );
		if ( ImGui::Combo( "Capture format", &modeSelection, outputModeNames, ARRAYSIZE( outputModeNames ) ) )
		{
			IssueCommand( "art_output_mode %s", modeSelection == 1 ? "ffmpeg" : "tga" );
		}
		ImGui::EndDisabled();

		if ( modeSelection == 1 )
		{
			ImGui::Spacing();
			char resolvedPath[MAX_PATH];
			bool foundOnDisk = false;
			ResolveArtFfmpegExecutablePath( resolvedPath, sizeof( resolvedPath ), &foundOnDisk );

			TextUnformattedWrapped( "FFmpeg executable:" );
			ImGui::SameLine();
			if ( foundOnDisk )
				ImGui::TextColored( ImVec4( 0.25f, 0.90f, 0.35f, 1.0f ), "[FOUND ON DISK]" );
			else
				ImGui::TextColored( ImVec4( 1.0f, 0.45f, 0.30f, 1.0f ), "[NOT FOUND ON DISK]" );

			ImGui::BeginDisabled( recording );
			ImGui::SetNextItemWidth( ImGui::GetContentRegionAvail().x - 130.0f );
			const bool pathSubmitted = ImGui::InputTextWithHint( "##ffmpeg_path", "ffmpeg.exe or full path",
				g_Gui.ffmpegPath, sizeof( g_Gui.ffmpegPath ), ImGuiInputTextFlags_EnterReturnsTrue );
			if ( pathSubmitted || ImGui::IsItemDeactivatedAfterEdit() )
				ApplyFfmpegPathField();
			ImGui::SameLine();
			if ( ImGui::Button( "Auto-detect", ImVec2( 120, 0 ) ) )
			{
				IssueCommand( "art_ffmpeg_path default" );
				SyncTextFieldsFromGame();
			}
			ImGui::EndDisabled();

			TextDisabledWrapped( "Active executable: %s", resolvedPath[0] ? resolvedPath : "unknown" );
			TextDisabledWrapped( "Note: You can place ffmpeg.exe next to the loader / DLL, in the game folder (cstrike), or system PATH." );

			ImGui::Spacing();
			LONG currentPreset = InterlockedCompareExchange( &g_nArtFfmpegPreset, 0, 0 );
			int presetSelection = ( currentPreset >= 0 && currentPreset < GetArtFfmpegPresetCount() ) ? static_cast<int>( currentPreset ) : 0;

			const char *presetNames[ART_FFMPEG_PRESET_COUNT];
			for ( int i = 0; i < GetArtFfmpegPresetCount(); ++i )
				presetNames[i] = GetArtFfmpegPresetInfo( i )->displayName;

			ImGui::SetNextItemWidth( 420 );
			ImGui::BeginDisabled( recording );
			if ( ImGui::Combo( "Encoding preset", &presetSelection, presetNames, ARRAYSIZE( presetNames ) ) )
			{
				IssueCommand( "art_ffmpeg_preset %s", GetArtFfmpegPresetInfo( presetSelection )->name );
			}
			ImGui::EndDisabled();

			const ArtFfmpegPresetInfo *pPresetInfo = GetArtFfmpegPresetInfo( presetSelection );
			ImGui::TextColored( ImVec4( 0.85f, 0.85f, 0.85f, 1.0f ), "%s", pPresetInfo->description );

			if ( presetSelection == ART_FFMPEG_PRESET_CUSTOM )
			{
				ImGui::Spacing();
				TextUnformattedWrapped( "Custom arguments template:" );
				ImGui::BeginDisabled( recording );
				ImGui::SetNextItemWidth( -1 );
				const bool customSubmitted = ImGui::InputText( "##ffmpeg_custom",
					g_Gui.ffmpegCustomArgs, sizeof( g_Gui.ffmpegCustomArgs ), ImGuiInputTextFlags_EnterReturnsTrue );
				if ( customSubmitted || ImGui::IsItemDeactivatedAfterEdit() )
					ApplyFfmpegCustomArgsField();
				ImGui::EndDisabled();
				TextDisabledWrapped( "Supported tags: {WIDTH}, {HEIGHT}, {FPS}, {PASS}, {OUTPUT_FILE}, {TAKE_DIR}, {TAKE_NAME}, {FFMPEG}" );
			}

			ImGui::Spacing();
			if ( ImGui::Button( "Test FFmpeg setup", ImVec2( 170, 32 ) ) )
			{
				RunArtFfmpegTest( g_Gui.ffmpegTestResult );
				g_Gui.ffmpegTestPerformed = true;
			}
			if ( g_Gui.ffmpegTestPerformed )
			{
				ImGui::SameLine();
				if ( g_Gui.ffmpegTestResult.success )
					ImGui::TextColored( ImVec4( 0.25f, 0.90f, 0.35f, 1.0f ), "%s", g_Gui.ffmpegTestResult.message );
				else
					ImGui::TextColored( ImVec4( 1.0f, 0.35f, 0.35f, 1.0f ), "%s", g_Gui.ffmpegTestResult.message );
			}
		}

		if ( modeSelection == 0 )
		{
			ImGui::Spacing();
			TextUnformattedWrapped( "Capture pipeline safety (TGA mode)" );
			ImGui::Separator();
			LONG compressionMode = InterlockedCompareExchange( &g_nArtTgaCompressionMode, 0, 0 );
			int compressionSelection = compressionMode >= ART_TGA_COMPRESSION_OFF &&
				compressionMode <= ART_TGA_COMPRESSION_RLE ? static_cast<int>( compressionMode ) :
				ART_TGA_COMPRESSION_AUTO;
			static const char *compressionNames[] = { "Off (uncompressed)", "Auto (smaller result)", "RLE (always)" };
			ImGui::SetNextItemWidth( 245 );
			if ( ImGui::Combo( "TGA compression", &compressionSelection,
				compressionNames, ARRAYSIZE( compressionNames ) ) )
				IssueCommand( "art_tga_compression %s",
					ArtTgaCompressionModeName( compressionSelection ) );

			int maxFiles = static_cast<int>( InterlockedCompareExchange(
				&g_ArtQueueOptions.maxFiles, 0, 0 ) );
			int maxMegabytes = static_cast<int>( InterlockedCompareExchange(
				&g_ArtQueueOptions.maxMegabytes, 0, 0 ) );
			int reserveMegabytes = static_cast<int>( InterlockedCompareExchange(
				&g_ArtQueueOptions.reserveMegabytes, 0, 0 ) );
			ImGui::SetNextItemWidth( 125 );
			if ( ImGui::InputInt( "Maximum queued files", &maxFiles ) )
			{
				if ( maxFiles < 1 ) maxFiles = 1;
				if ( maxFiles > 512 ) maxFiles = 512;
				IssueCommand( "art_queue max_files %d", maxFiles );
			}
			ImGui::SetNextItemWidth( 125 );
			if ( ImGui::InputInt( "Maximum queued MiB", &maxMegabytes ) )
			{
				if ( maxMegabytes < 16 ) maxMegabytes = 16;
				if ( maxMegabytes > 1024 ) maxMegabytes = 1024;
				IssueCommand( "art_queue max_mb %d", maxMegabytes );
			}
			ImGui::SetNextItemWidth( 125 );
			if ( ImGui::InputInt( "Required virtual-memory reserve MiB", &reserveMegabytes ) )
			{
				if ( reserveMegabytes < 64 ) reserveMegabytes = 64;
				if ( reserveMegabytes > 1024 ) reserveMegabytes = 1024;
				IssueCommand( "art_queue reserve_mb %d", reserveMegabytes );
			}
			char pendingQueueBytes[48];
			FormatArtByteCount( g_ArtPipelineStats.pendingBytes,
				pendingQueueBytes, sizeof( pendingQueueBytes ) );
			ImGui::Text( "Pending upper bound: %lu files | %s | take flushes: %lu",
				g_ArtPipelineStats.pendingFiles, pendingQueueBytes, g_ArtPipelineStats.takeFlushes );
			if ( ImGui::Button( "Flush queued writes", ImVec2( 170, 30 ) ) )
				IssueCommandWithSeparator( "art_queue flush" );
			ImGui::SameLine();
			if ( ImGui::Button( "Restore safety defaults", ImVec2( 180, 30 ) ) )
				IssueCommand( "art_queue default" );
			TextDisabledWrapped( "ART pauses when a queue limit or memory reserve is reached, then continues safely." );
		}

		ImGui::Spacing();
		TextUnformattedWrapped( "Takes" );
		ImGui::Separator();
		ImGui::BeginChild( "take_list", ImVec2( 285, 255 ), ImGuiChildFlags_Borders );
		for ( size_t i = 0; i < g_Gui.takes.size(); ++i )
		{
			const bool selected = g_Gui.selectedTake == static_cast<int>( i );
			if ( ImGui::Selectable( g_Gui.takes[i].name.c_str(), selected ) )
			{
				g_Gui.selectedTake = static_cast<int>( i );
				Q_strncpy( g_Gui.takeRename, g_Gui.takes[i].name.c_str(),
					sizeof( g_Gui.takeRename ) );
			}
		}
		if ( g_Gui.takes.empty() )
			TextDisabledWrapped( "No take folders found" );
		ImGui::EndChild();
		ImGui::SameLine();
		ImGui::BeginGroup();
		static char recycleConfirmation[64] = {};
		const bool hasTake = g_Gui.selectedTake >= 0 &&
			g_Gui.selectedTake < static_cast<int>( g_Gui.takes.size() );
		if ( hasTake )
		{
			const TakeEntry &take = g_Gui.takes[g_Gui.selectedTake];
			char takeBytes[48];
			FormatArtByteCount( take.bytes, takeBytes, sizeof( takeBytes ) );
			ImGui::TextWrapped( "%s", take.name.c_str() );
			ImGui::Text( "%lu files | %s", take.files, takeBytes );
			ImGui::TextWrapped( "%s", take.absolutePath.c_str() );
		}
		else
		{
			TextDisabledWrapped( "Select a take to manage it." );
		}
		ImGui::BeginDisabled( !hasTake );
		if ( ImGui::Button( "Open selected", ImVec2( 135, 30 ) ) )
			OpenSelectedTake();
		ImGui::SameLine();
		if ( ImGui::Button( "Copy path", ImVec2( 105, 30 ) ) && hasTake )
			ImGui::SetClipboardText( g_Gui.takes[g_Gui.selectedTake].absolutePath.c_str() );
		ImGui::EndDisabled();
		ImGui::SetNextItemWidth( 245 );
		ImGui::InputTextWithHint( "##take_rename", "New take name",
			g_Gui.takeRename, sizeof( g_Gui.takeRename ) );
		ImGui::BeginDisabled( recording || !hasTake );
		if ( ImGui::Button( "Rename", ImVec2( 115, 30 ) ) )
		{
			if ( RenameSelectedTake() )
				recycleConfirmation[0] = '\0';
		}
		ImGui::SameLine();
		const bool deleteArmed = hasTake &&
			!Q_stricmp( recycleConfirmation, g_Gui.takes[g_Gui.selectedTake].name.c_str() );
		if ( ImGui::Button( deleteArmed ? "Confirm recycle" : "Recycle take", ImVec2( 125, 30 ) ) )
		{
			if ( deleteArmed )
			{
				if ( RecycleSelectedTake() )
					recycleConfirmation[0] = '\0';
			}
			else if ( hasTake )
			{
				Q_strncpy( recycleConfirmation,
					g_Gui.takes[g_Gui.selectedTake].name.c_str(),
					sizeof( recycleConfirmation ) );
			}
		}
		ImGui::EndDisabled();
		if ( deleteArmed )
			ImGui::TextColored( ImVec4( 1.0f, 0.30f, 0.18f, 1.0f ),
				"Click Confirm recycle to move this folder to the Recycle Bin." );
		if ( recording )
			TextDisabledWrapped( "Stop recording before renaming or recycling takes." );
		if ( ImGui::Button( "Refresh takes", ImVec2( 135, 30 ) ) )
			RefreshTakeList();
		ImGui::EndGroup();
	}


	std::string MaskNames( LONG mask )
	{
		static const struct MaskName { LONG bit; const char *name; } names[] =
		{
			{ ART_RECORD_NORMAL, "normal" },
			{ ART_RECORD_CLEAR, "clear" },
			{ ART_RECORD_CLEAR_NOPLAYERS, "clear-noplayers" },
			{ ART_RECORD_VIEWMODEL, "viewmodel" },
			{ ART_RECORD_DEPTH, "depth" },
			{ ART_RECORD_PLAYERS, "players" },
			{ ART_RECORD_OBJECTID, "objectid" }
		};
		std::string result;
		for ( int i = 0; i < ARRAYSIZE( names ); ++i )
		{
			if ( !( mask & names[i].bit ) ) continue;
			if ( !result.empty() ) result += ", ";
			result += names[i].name;
		}
		return result.empty() ? "none" : result;
	}

	struct HlaeInputTableValueSpec
	{
		const char *label;
		const char *command;
		double currentValue;
		float speed;
		float minimum;
		float maximum;
		const char *format;
	};

	void DrawHlaeInputTableRow( const HlaeInputTableValueSpec *pValues, int valueCount )
	{
		if ( !pValues || valueCount <= 0 )
			return;

		// Labels and controls use separate explicit table rows. This avoids ImGui's
		// per-cell cursor-height accumulation shifting later columns down by one line.
		ImGui::TableNextRow();
		for ( int i = 0; i < valueCount; ++i )
		{
			ImGui::TableSetColumnIndex( i );
			char visibleLabel[128];
			VisibleImGuiLabel( pValues[i].label, visibleLabel, sizeof( visibleLabel ) );
			ImGui::AlignTextToFramePadding();
			TextUnformattedSingleLine( visibleLabel );
			HoverExplanation( "Adjust %s through the original AdvancedFX mirv_input command.",
				visibleLabel );
		}

		ImGui::TableNextRow();
		for ( int i = 0; i < valueCount; ++i )
		{
			ImGui::TableSetColumnIndex( i );
			char visibleLabel[128];
			VisibleImGuiLabel( pValues[i].label, visibleLabel, sizeof( visibleLabel ) );
			float value = static_cast<float>( pValues[i].currentValue );
			ImGui::PushID( pValues[i].label );
			ImGui::SetNextItemWidth( -1 );
			if ( ImGui::DragFloat( "##value", &value, pValues[i].speed,
				pValues[i].minimum, pValues[i].maximum,
				pValues[i].format ? pValues[i].format : "%.3f" ) )
			{
				IssueCommand( pValues[i].command, value );
			}
			HoverExplanation( "Adjust %s through the original AdvancedFX mirv_input command.",
				visibleLabel );
			ImGui::PopID();
		}
	}

	void DrawHlaePage()
	{
		static bool s_HlaePageFirstFrame = true;
		const bool firstFrame = s_HlaePageFirstFrame;
		if ( firstFrame )
			LogMessage( "HLAE GUI OPEN: begin" );

		ArtHlaeStatus status;
		GetArtHlaeStatus( status );
		if ( firstFrame )
			LogMessage( "HLAE GUI OPEN: status ready enabled=%d hook=%d keys=%u",
				status.enabled ? 1 : 0, status.v34HookReady ? 1 : 0,
				static_cast<unsigned int>( status.campathKeyframes ) );
		if ( !ImGui::IsAnyItemActive() )
			g_Gui.hlaeBvhFps = static_cast<float>( status.autoExportBvhFps );

		TextUnformattedWrapped( "HLAE" );
		HoverExplanation( "AdvancedFX camera and game-recording features for the CS:S -afxV34 engine path." );
		HlaeSameLineIfFits( 100.0f );
		StatusBadge( status.enabled, "ENABLED", "DISABLED" );
		HoverExplanation( "Global state of all mirv_* features provided by ART." );
		HlaeSameLineIfFits( 150.0f );
		StatusBadge( status.v34HookReady, "V34 HOOK READY", "V34 HOOK FAILED" );
		HoverExplanation(
			"Shows whether ART is connected to VCLIENTENGINETOOLS001::SetupEngineView "
			"and VClient013::FrameStageNotify, the hooks used by AdvancedFX -afxV34." );
		ImGui::Separator();
		ImGui::Spacing();

		ImGui::BeginChild( "hlae_master", ImVec2( 0, 0 ),
			ImGuiChildFlags_Borders | ImGuiChildFlags_AutoResizeY );
		bool enabled = status.enabled;
		if ( ImGui::Checkbox( "Enable HLAE features globally", &enabled ) )
			IssueCommand( "art_hlae enabled %d", enabled ? 1 : 0 );
		HoverExplanation(
			"Master switch for every mirv_* camera, input, import/export and AGR feature. "
			"Disabling it also closes active HLAE output files." );
		TextDisabledWrapped(
			"AdvancedFX command names and CAM, BVH and AGR file formats are preserved. "
			"Absolute paths are used directly. Relative files use the active ART take, "
			"or the Output page folder while ART is idle." );
		ImGui::Separator();
		TextUnformattedWrapped( "Auto-export with ART takes" );
		HoverExplanation(
			"Starts selected HLAE writers after an ART take folder is created and finalizes "
			"only those writers when that take stops." );
		bool autoExportAgr = status.autoExportAgr;
		if ( ImGui::Checkbox( "AGR##auto_export", &autoExportAgr ) )
			IssueCommand( "art_hlae autoExport agr %d", autoExportAgr ? 1 : 0 );
		HoverExplanation( "Automatically create afxGameRecord.agr in every ART take." );
		HlaeSameLineIfFits( 120.0f );
		bool autoExportCamio = status.autoExportCamio;
		if ( ImGui::Checkbox( "CAMIO##auto_export", &autoExportCamio ) )
			IssueCommand( "art_hlae autoExport camio %d", autoExportCamio ? 1 : 0 );
		HoverExplanation( "Automatically create camera.cam in every ART take." );
		HlaeSameLineIfFits( 105.0f );
		bool autoExportBvh = status.autoExportBvh;
		if ( ImGui::Checkbox( "BVH##auto_export", &autoExportBvh ) )
			IssueCommand( "art_hlae autoExport bvh %d", autoExportBvh ? 1 : 0 );
		HoverExplanation( "Automatically create camera.bvh in every ART take using the BVH FPS field below." );
		ImGui::EndChild();

		ImGui::BeginDisabled( !status.enabled );
		ImGui::Spacing();
		ImGui::BeginChild( "hlae_campath", ImVec2( 0, 0 ),
			ImGuiChildFlags_Borders | ImGuiChildFlags_AutoResizeY );
		if ( firstFrame )
			LogMessage( "HLAE GUI OPEN: master ready" );
		TextUnformattedWrapped( "mirv_campath" );
		HoverExplanation(
			"Original AdvancedFX camera-path command core, timing and interpolation. "
			"The enabled path overrides position, rotation and FOV during demo playback." );
		ImGui::Text( "Keyframes: %u", static_cast<unsigned int>( status.campathKeyframes ) );
		HlaeSameLineIfFits( 125.0f );
		StatusBadge( status.campathCanEval, "PATH READY", "MORE KEYS NEEDED" );
		HoverExplanation( "Whether the selected interpolation modes have enough keyframes to evaluate." );
		if ( status.campathKeyframes )
			TextDisabledWrapped( "Path time %.3f | keyframe range %.3f - %.3f",
				status.campathCurrentTime, status.campathLowerBound,
				status.campathUpperBound );
		if ( !status.campathCanEval )
			TextDisabledWrapped(
				"Cubic position/FOV and sCubic rotation require four keyframes. "
				"Linear position/FOV and sLinear rotation work with two." );

		bool campathEnabled = status.campathEnabled;
		if ( ImGui::Checkbox( "Enabled##campath", &campathEnabled ) )
			IssueCommand( "mirv_campath enabled %d", campathEnabled ? 1 : 0 );
		HoverExplanation( "Enable or disable evaluation of the current camera path." );
		HlaeSameLineIfFits( 180.0f );
		bool campathHold = status.campathHold;
		if ( ImGui::Checkbox( "Hold interpolation", &campathHold ) )
			IssueCommand( "mirv_campath hold %d", campathHold ? 1 : 0 );
		HoverExplanation( "Hold the first or last keyframe outside the path range." );
		HlaeSameLineIfFits( 135.0f );
		bool campathDraw = status.campathDraw;
		if ( ImGui::Checkbox( "Draw path", &campathDraw ) )
			IssueCommand( "mirv_campath draw enabled %d", campathDraw ? 1 : 0 );
		HoverExplanation(
			"Draw the sampled interpolation line, keyframe axes, cameras and current "
			"interpolated camera in the in-game overlay." );

		static const char *doubleInterpNames[] = { "default", "linear", "cubic" };
		static const char *rotationInterpNames[] = { "default", "sLinear", "sCubic" };
		int positionInterp = status.campathPositionInterp;
		int rotationInterp = status.campathRotationInterp;
		int fovInterp = status.campathFovInterp;
		if ( ImGui::BeginTable( "campath_interpolation", 3,
			ImGuiTableFlags_SizingStretchSame ) )
		{
			ImGui::TableNextRow();
			ImGui::TableSetColumnIndex( 0 );
			ImGui::AlignTextToFramePadding();
			TextUnformattedSingleLine( "Position interpolation" );
			HoverExplanation( "Select default/cubic or linear interpolation for X, Y and Z." );
			ImGui::TableSetColumnIndex( 1 );
			ImGui::AlignTextToFramePadding();
			TextUnformattedSingleLine( "Rotation interpolation" );
			HoverExplanation( "Select quaternion sLinear or sCubic rotation interpolation." );
			ImGui::TableSetColumnIndex( 2 );
			ImGui::AlignTextToFramePadding();
			TextUnformattedSingleLine( "FOV interpolation" );
			HoverExplanation( "Select default/cubic or linear field-of-view interpolation." );

			ImGui::TableNextRow();
			ImGui::TableSetColumnIndex( 0 );
			ImGui::SetNextItemWidth( -1 );
			if ( ImGui::Combo( "##campath_position_interp", &positionInterp,
				doubleInterpNames, ARRAYSIZE( doubleInterpNames ) ) )
				IssueCommand( "mirv_campath edit interp position %s",
					doubleInterpNames[positionInterp] );
			HoverExplanation( "Select default/cubic or linear interpolation for X, Y and Z." );
			ImGui::TableSetColumnIndex( 1 );
			ImGui::SetNextItemWidth( -1 );
			if ( ImGui::Combo( "##campath_rotation_interp", &rotationInterp,
				rotationInterpNames, ARRAYSIZE( rotationInterpNames ) ) )
				IssueCommand( "mirv_campath edit interp rotation %s",
					rotationInterpNames[rotationInterp] );
			HoverExplanation( "Select quaternion sLinear or sCubic rotation interpolation." );
			ImGui::TableSetColumnIndex( 2 );
			ImGui::SetNextItemWidth( -1 );
			if ( ImGui::Combo( "##campath_fov_interp", &fovInterp,
				doubleInterpNames, ARRAYSIZE( doubleInterpNames ) ) )
				IssueCommand( "mirv_campath edit interp fov %s",
					doubleInterpNames[fovInterp] );
			HoverExplanation( "Select default/cubic or linear field-of-view interpolation." );
			ImGui::EndTable();
		}

		bool drawKeyAxis = status.campathDrawKeyAxis;
		if ( ImGui::Checkbox( "Keyframe axes", &drawKeyAxis ) )
			IssueCommand( "mirv_campath draw keyAxis %d", drawKeyAxis ? 1 : 0 );
		HoverExplanation( "Draw the original 36-unit world-space X/Y/Z axes." );
		HlaeSameLineIfFits( 175.0f );
		bool drawKeyCam = status.campathDrawKeyCam;
		if ( ImGui::Checkbox( "Keyframe cameras", &drawKeyCam ) )
			IssueCommand( "mirv_campath draw keyCam %d", drawKeyCam ? 1 : 0 );
		HoverExplanation( "Draw wireframe cameras using each keyframe rotation and FOV." );
		HlaeSameLineIfFits( 245.0f );
		float drawKeyIndex = status.campathDrawKeyIndex;
		ImGui::SetNextItemWidth( 130 );
		if ( ImGui::DragFloat( "Index size##campath", &drawKeyIndex,
			0.5f, 0.0f, 48.0f, "%.1f" ) )
			IssueCommand( "mirv_campath draw keyIndex %.3f", drawKeyIndex );
		HoverExplanation( "Keyframe index text height; original HLAE default is 18." );

		if ( !g_Gui.hlaeCampathOffsetInitialized )
		{
			g_Gui.hlaeCampathOffsetValue = static_cast<float>( status.campathOffset );
			g_Gui.hlaeCampathOffsetInitialized = true;
		}
		TextUnformattedWrapped( "Time offset" );
		ImGui::SetNextItemWidth( 330 );
		ImGui::DragFloat( "##campath_time_offset", &g_Gui.hlaeCampathOffsetValue,
			0.01f, -3600.0f, 3600.0f, "%.4f" );
		HoverExplanation(
			"Absolute path offset in seconds. The value is committed on release. The GUI "
			"resets AdvancedFX's relative offset first, preventing cumulative slider drift." );
		const bool offsetActive = ImGui::IsItemActive();
		const bool offsetCommitted = ImGui::IsItemDeactivatedAfterEdit();
		if ( offsetCommitted )
		{
			IssueCommand( "mirv_campath offset none" );
			if ( fabsf( g_Gui.hlaeCampathOffsetValue ) > 0.000001f )
				IssueCommand( "mirv_campath offset %.6f", g_Gui.hlaeCampathOffsetValue );
			g_Gui.hlaeCampathOffsetInitialized = false;
		}
		HlaeSameLineIfFits( 115.0f );
		if ( ImGui::Button( "Reset offset", ImVec2( 105, 0 ) ) )
		{
			IssueCommand( "mirv_campath offset none" );
			g_Gui.hlaeCampathOffsetInitialized = false;
		}
		HoverExplanation( "Set the campath offset to zero." );
		HlaeSameLineIfFits( 185.0f );
		if ( ImGui::Button( "Start at current time", ImVec2( 175, 0 ) ) )
		{
			IssueCommand( "mirv_campath offset current" );
			g_Gui.hlaeCampathOffsetInitialized = false;
		}
		HoverExplanation( "Align the first keyframe with the current AdvancedFX client/demo time." );
		if ( !offsetActive && !offsetCommitted &&
			fabs( static_cast<double>( g_Gui.hlaeCampathOffsetValue ) - status.campathOffset ) > 0.0001 )
		{
			g_Gui.hlaeCampathOffsetValue = static_cast<float>( status.campathOffset );
		}

		UpdateDemoTickTracking();
		const bool playingDemo = g_pEngine && g_pEngine->IsPlayingDemo();
		const bool pausedDemo = playingDemo && g_pEngine->IsPaused();
		StatusBadge( pausedDemo, "DEMO PAUSED", playingDemo ? "DEMO PLAYING" : "NO DEMO" );
		HoverExplanation( "Current Source demo playback state." );
		HlaeSameLineIfFits( 135.0f );
		ImGui::BeginDisabled( !playingDemo );
		if ( ImGui::Button( pausedDemo ? "Resume demo" : "Pause demo", ImVec2( 125, 30 ) ) )
			ToggleDemoPlayback( pausedDemo );
		ImGui::EndDisabled();
		HoverExplanation( pausedDemo ?
			"Resume demo playback and resynchronize ART's tick tracker." :
			"Pause demo playback without leaving the campath section." );
		HlaeSameLineIfFits( 260.0f );
		ImGui::SetNextItemWidth( 150 );
		ImGui::DragInt( "Target tick##campath_demo_tick", &g_Gui.demoTick,
			1.0f, 0, INT_MAX );
		if ( g_Gui.demoTick < 0 ) g_Gui.demoTick = 0;
		HoverExplanation( "Drag or type the demo tick used by Go to tick." );
		HlaeSameLineIfFits( 115.0f );
		if ( ImGui::Button( "Go to tick##campath", ImVec2( 105, 30 ) ) )
			SeekDemoTick( g_Gui.demoTick );
		HoverExplanation( "Seek to the target tick and pause when Source reaches it." );

		if ( ImGui::Button( "Add keyframe", ImVec2( 125, 30 ) ) )
			IssueCommand( "mirv_campath add" );
		HoverExplanation( "Add the final rendered camera at the original HLAE path time." );
		HlaeSameLineIfFits( 135.0f );
		if ( ImGui::Button( "Print keyframes", ImVec2( 125, 30 ) ) )
			IssueCommandWithSeparator( "mirv_campath print" );
		HoverExplanation( "Print original HLAE tick, demo-time and game-time diagnostics." );
		HlaeSameLineIfFits( 125.0f );
		if ( ImGui::Button( "Command help##campath", ImVec2( 115, 30 ) ) )
			g_Gui.showHlaeCampathHelp = true;
		HoverExplanation( "Open an in-game mirv_campath command reference window." );

		if ( status.campathKeyframes == 0 )
			g_Gui.hlaeCampathKeyframe = 0;
		else if ( g_Gui.hlaeCampathKeyframe >= static_cast<int>( status.campathKeyframes ) )
			g_Gui.hlaeCampathKeyframe = static_cast<int>( status.campathKeyframes ) - 1;
		ImGui::BeginDisabled( status.campathKeyframes == 0 );
		ImGui::SetNextItemWidth( 95 );
		ImGui::DragInt( "Index##campath_remove", &g_Gui.hlaeCampathKeyframe,
			1.0f, 0, status.campathKeyframes ?
				static_cast<int>( status.campathKeyframes ) - 1 : 0 );
		HoverExplanation( "Choose the zero-based keyframe index to remove." );
		HlaeSameLineIfFits( 145.0f );
		if ( ImGui::Button( "Remove keyframe", ImVec2( 135, 30 ) ) )
			IssueCommand( "mirv_campath remove %d", g_Gui.hlaeCampathKeyframe );
		HoverExplanation( "Remove the selected keyframe index." );
		ImGui::EndDisabled();
		HlaeSameLineIfFits( 85.0f );
		if ( ImGui::Button( "Clear##campath", ImVec2( 75, 30 ) ) )
			IssueCommand( "mirv_campath clear" );
		HoverExplanation( "Clear selected keyframes, or all keyframes when none are selected." );

		ImGui::SetNextItemWidth( -1 );
		ImGui::InputTextWithHint( "##hlae_campath_file", "campath.xml",
			g_Gui.hlaeCampathFile, sizeof( g_Gui.hlaeCampathFile ) );
		HoverExplanation(
			"AdvancedFX campath XML. Absolute paths are used directly; relative files use the active ART take or the Output page folder." );
		if ( ImGui::Button( "Save campath", ImVec2( 120, 30 ) ) )
		{
			if ( IsSafeQuotedArgument( g_Gui.hlaeCampathFile ) )
				IssueCommand( "mirv_campath save \"%s\"", g_Gui.hlaeCampathFile );
			else SetError( "HLAE paths cannot contain quotes, semicolons, or line breaks" );
		}
		HoverExplanation( "Save the current keyframes, interpolation, hold and offset to XML." );
		HlaeSameLineIfFits( 130.0f );
		if ( ImGui::Button( "Load campath", ImVec2( 120, 30 ) ) )
		{
			if ( IsSafeQuotedArgument( g_Gui.hlaeCampathFile ) )
				IssueCommand( "mirv_campath load \"%s\"", g_Gui.hlaeCampathFile );
			else SetError( "HLAE paths cannot contain quotes, semicolons, or line breaks" );
		}
		HoverExplanation( "Load an AdvancedFX campath XML file." );
		ImGui::EndChild();

		ImGui::Spacing();
		ImGui::BeginChild( "hlae_input", ImVec2( 0, 0 ),
			ImGuiChildFlags_Borders | ImGuiChildFlags_AutoResizeY );
		if ( firstFrame )
			LogMessage( "HLAE GUI OPEN: campath ready" );
		TextUnformattedWrapped( "mirv_input" );
		HoverExplanation(
			"The original AdvancedFX MirvInput command core and key/mouse event handling, "
			"applied at VCLIENTENGINETOOLS001::SetupEngineView." );
		if ( ImGui::BeginTable( "mirv_input_header", 3,
			ImGuiTableFlags_SizingStretchProp | ImGuiTableFlags_PadOuterX ) )
		{
			ImGui::TableSetupColumn( "State", ImGuiTableColumnFlags_WidthFixed, 175.0f );
			ImGui::TableSetupColumn( "Camera", ImGuiTableColumnFlags_WidthFixed, 175.0f );
			ImGui::TableSetupColumn( "Help", ImGuiTableColumnFlags_WidthStretch );
			ImGui::TableNextRow();
			ImGui::TableSetColumnIndex( 0 );
			StatusBadge( status.inputCamera, "CAMERA INPUT ACTIVE", "CAMERA INPUT INACTIVE" );
			HoverExplanation( "Whether original mirv_input camera-control mode is active." );
			ImGui::TableSetColumnIndex( 1 );
			if ( HlaeToggleButton( "Start camera##mirv_input_camera",
				"End camera##mirv_input_camera", status.inputCamera,
				ImVec2( ImGui::GetContentRegionAvail().x, 30 ) ) )
			{
				IssueCommand( status.inputCamera ? "mirv_input end" : "mirv_input camera" );
			}
			HoverExplanation( status.inputCamera ?
				"End original HLAE camera-control mode." :
				"Start mirv_input from the camera selected by Offset mode." );
			ImGui::TableSetColumnIndex( 2 );
			if ( ImGui::Button( "Command help##mirv_input",
				ImVec2( ImGui::GetContentRegionAvail().x, 30 ) ) )
				g_Gui.showHlaeInputHelp = true;
			HoverExplanation( "Open an in-game mirv_input command reference window." );
			ImGui::EndTable();
		}

		char hlaeInputHoldKeyName[32];
		GetHlaeInputHoldKeyName( g_Gui.hlaeInputHoldKey,
			hlaeInputHoldKeyName, sizeof( hlaeInputHoldKeyName ) );
		if ( ImGui::BeginTable( "mirv_input_gui_passthrough", 4,
			ImGuiTableFlags_SizingStretchProp | ImGuiTableFlags_PadOuterX ) )
		{
			ImGui::TableSetupColumn( "Enable", ImGuiTableColumnFlags_WidthStretch );
			ImGui::TableSetupColumn( "Key", ImGuiTableColumnFlags_WidthFixed, 155.0f );
			ImGui::TableSetupColumn( "Change", ImGuiTableColumnFlags_WidthFixed, 145.0f );
			ImGui::TableSetupColumn( "Reset", ImGuiTableColumnFlags_WidthFixed, 90.0f );
			ImGui::TableNextRow();
			ImGui::TableSetColumnIndex( 0 );
			bool inputWhileGui = g_Gui.hlaeInputWhileGui;
			if ( ImGui::Checkbox( "Use mirv_input while GUI is open", &inputWhileGui ) )
				IssueCommand( "art_hlae_input_while_gui %s", inputWhileGui ? "on" : "off" );
			HoverExplanation(
				"When enabled, hold the configured key while the mouse cursor is outside the ART "
				"window. Mouse and keyboard input is routed to mirv_input while the GUI stays visible." );
			ImGui::TableSetColumnIndex( 1 );
			TextDisabledWrapped( "Hold key: %s", hlaeInputHoldKeyName );
			HoverExplanation( "The key or mouse button that temporarily gives control to mirv_input." );
			ImGui::TableSetColumnIndex( 2 );
			if ( ImGui::Button( g_Gui.waitingForHlaeInputHoldKey ?
				"Press key..." : "Change hold key",
				ImVec2( ImGui::GetContentRegionAvail().x, 30 ) ) )
			{
				g_Gui.waitingForHlaeInputHoldKey = true;
			}
			HoverExplanation( "Capture any keyboard key or mouse button. Escape cancels." );
			ImGui::TableSetColumnIndex( 3 );
			if ( ImGui::Button( "LMB", ImVec2( ImGui::GetContentRegionAvail().x, 30 ) ) )
			{
				g_Gui.hlaeInputHoldKey = VK_LBUTTON;
				g_Gui.waitingForHlaeInputHoldKey = false;
				SetHlaeInputWhileGuiActive( false );
			}
			HoverExplanation( "Reset the mirv_input GUI hold key to left mouse button." );
			ImGui::EndTable();
		}
		if ( g_Gui.waitingForHlaeInputHoldKey )
			TextDisabledWrapped( "Press a keyboard key or mouse button. Escape cancels." );
		if ( g_Gui.hlaeInputWhileGuiActive )
		{
			StatusBadge( true, "GUI INPUT PASSTHROUGH ACTIVE", "" );
			HoverExplanation( "Release %s or move the cursor over the ART window to return input to the GUI.",
				hlaeInputHoldKeyName );
		}

		if ( !g_Gui.hlaeInputDirectInitialized && status.inputHasCameraData )
		{
			g_Gui.hlaeInputPosition[0] = static_cast<float>( status.inputCameraX );
			g_Gui.hlaeInputPosition[1] = static_cast<float>( status.inputCameraY );
			g_Gui.hlaeInputPosition[2] = static_cast<float>( status.inputCameraZ );
			g_Gui.hlaeInputAngles[0] = static_cast<float>( status.inputCameraPitch );
			g_Gui.hlaeInputAngles[1] = static_cast<float>( status.inputCameraYaw );
			g_Gui.hlaeInputAngles[2] = static_cast<float>( status.inputCameraRoll );
			g_Gui.hlaeInputDirectFov = static_cast<float>( status.inputCameraFov );
			g_Gui.hlaeInputDirectInitialized = true;
		}

		ImGui::SetNextItemOpen( false, ImGuiCond_Once );
		const bool directStateOpen = ImGui::CollapsingHeader( "Direct view state" );
		HoverExplanation( "Expand direct position, angle and FOV editing. Closed by default." );
		if ( directStateOpen )
		{
			if ( ImGui::Button( "Use current output", ImVec2( 150, 28 ) ) &&
				status.inputHasCameraData )
			{
				g_Gui.hlaeInputPosition[0] = static_cast<float>( status.inputCameraX );
				g_Gui.hlaeInputPosition[1] = static_cast<float>( status.inputCameraY );
				g_Gui.hlaeInputPosition[2] = static_cast<float>( status.inputCameraZ );
				g_Gui.hlaeInputAngles[0] = static_cast<float>( status.inputCameraPitch );
				g_Gui.hlaeInputAngles[1] = static_cast<float>( status.inputCameraYaw );
				g_Gui.hlaeInputAngles[2] = static_cast<float>( status.inputCameraRoll );
				g_Gui.hlaeInputDirectFov = static_cast<float>( status.inputCameraFov );
				g_Gui.hlaeInputDirectInitialized = true;
			}
			HoverExplanation( "Copy the final camera currently produced by the game and all active overrides." );

			TextUnformattedWrapped( "Position X / Y / Z" );
			ImGui::SetNextItemWidth( -1 );
			ImGui::DragFloat3( "##mirv_input_direct_position",
				g_Gui.hlaeInputPosition, 1.0f, 0.0f, 0.0f, "%.3f" );
			HoverExplanation( "Edit direct world-space camera position." );
			TextUnformattedWrapped( "Angles pitch / yaw / roll" );
			ImGui::SetNextItemWidth( -1 );
			ImGui::DragFloat3( "##mirv_input_direct_angles",
				g_Gui.hlaeInputAngles, 0.25f, 0.0f, 0.0f, "%.3f" );
			HoverExplanation(
				"Edit original mirv_input angle command order (Rx, Ry, Rz), shown as Source pitch, yaw and roll." );
			TextUnformattedWrapped( "FOV" );
			ImGui::SetNextItemWidth( 260 );
			ImGui::DragFloat( "##mirv_input_direct_fov", &g_Gui.hlaeInputDirectFov,
				0.25f, 0.001f, 179.999f, "%.3f" );
			HoverExplanation( "Edit the FOV value used by Apply FOV or Apply real FOV." );

			if ( ImGui::Button( "Apply position", ImVec2( 125, 28 ) ) )
				IssueCommand( "mirv_input position %.6f %.6f %.6f",
					g_Gui.hlaeInputPosition[0], g_Gui.hlaeInputPosition[1],
					g_Gui.hlaeInputPosition[2] );
			HoverExplanation( "Apply only the edited world position." );
			HlaeSameLineIfFits( 125.0f );
			if ( ImGui::Button( "Apply angles", ImVec2( 115, 28 ) ) )
				IssueCommand( "mirv_input angles %.6f %.6f %.6f",
					g_Gui.hlaeInputAngles[0], g_Gui.hlaeInputAngles[1],
					g_Gui.hlaeInputAngles[2] );
			HoverExplanation( "Apply only the edited camera angles." );
			HlaeSameLineIfFits( 110.0f );
			if ( ImGui::Button( "Apply FOV", ImVec2( 100, 28 ) ) )
				IssueCommand( "mirv_input fov %.6f", g_Gui.hlaeInputDirectFov );
			HoverExplanation( "Apply the edited internal HLAE FOV." );
			HlaeSameLineIfFits( 135.0f );
			if ( ImGui::Button( "Apply real FOV", ImVec2( 125, 28 ) ) )
				IssueCommand( "mirv_input fov real %.6f", g_Gui.hlaeInputDirectFov );
			HoverExplanation( "Convert the edited real/display FOV into HLAE's internal FOV." );
			HlaeSameLineIfFits( 115.0f );
			if ( ImGui::Button( "Apply all", ImVec2( 105, 28 ) ) )
			{
				IssueCommand( "mirv_input position %.6f %.6f %.6f",
					g_Gui.hlaeInputPosition[0], g_Gui.hlaeInputPosition[1],
					g_Gui.hlaeInputPosition[2] );
				IssueCommand( "mirv_input angles %.6f %.6f %.6f",
					g_Gui.hlaeInputAngles[0], g_Gui.hlaeInputAngles[1],
					g_Gui.hlaeInputAngles[2] );
				IssueCommand( "mirv_input fov %.6f", g_Gui.hlaeInputDirectFov );
			}
			HoverExplanation( "Apply position, angles and internal HLAE FOV through the original parser." );
		}

		ImGui::SetNextItemOpen( false, ImGuiCond_Once );
		const bool memoryOpen = ImGui::CollapsingHeader( "View-state memory" );
		HoverExplanation( "Expand named mirv_input view-state store/use/save/load controls. Closed by default." );
		if ( memoryOpen )
		{
			TextUnformattedWrapped( "State name" );
			ImGui::SetNextItemWidth( 260 );
			ImGui::InputTextWithHint( "##mirv_input_mem_name", "view name",
				g_Gui.hlaeInputMemName, sizeof( g_Gui.hlaeInputMemName ) );
			HoverExplanation( "Name used by mirv_input mem store/use/remove." );
			if ( ImGui::Button( "Store", ImVec2( 80, 28 ) ) )
			{
				if ( g_Gui.hlaeInputMemName[0] && IsSafeQuotedArgument( g_Gui.hlaeInputMemName ) )
					IssueCommand( "mirv_input mem store \"%s\"", g_Gui.hlaeInputMemName );
				else SetError( "mirv_input state names cannot be empty or contain quotes, semicolons, or line breaks" );
			}
			HoverExplanation( "Store current origin, angles and FOV under the selected name." );
			HlaeSameLineIfFits( 90.0f );
			if ( ImGui::Button( "Use all", ImVec2( 80, 28 ) ) )
			{
				if ( g_Gui.hlaeInputMemName[0] && IsSafeQuotedArgument( g_Gui.hlaeInputMemName ) )
					IssueCommand( "mirv_input mem use \"%s\"", g_Gui.hlaeInputMemName );
				else SetError( "mirv_input state names cannot be empty or contain quotes, semicolons, or line breaks" );
			}
			HoverExplanation( "Restore all values from the named state." );
			HlaeSameLineIfFits( 85.0f );
			if ( ImGui::Button( "Origin", ImVec2( 75, 28 ) ) )
			{
				if ( g_Gui.hlaeInputMemName[0] && IsSafeQuotedArgument( g_Gui.hlaeInputMemName ) )
					IssueCommand( "mirv_input mem use \"%s\" origin", g_Gui.hlaeInputMemName );
				else SetError( "mirv_input state names cannot be empty or contain quotes, semicolons, or line breaks" );
			}
			HoverExplanation( "Restore only the named state's origin." );
			HlaeSameLineIfFits( 85.0f );
			if ( ImGui::Button( "Angles", ImVec2( 75, 28 ) ) )
			{
				if ( g_Gui.hlaeInputMemName[0] && IsSafeQuotedArgument( g_Gui.hlaeInputMemName ) )
					IssueCommand( "mirv_input mem use \"%s\" angles", g_Gui.hlaeInputMemName );
				else SetError( "mirv_input state names cannot be empty or contain quotes, semicolons, or line breaks" );
			}
			HoverExplanation( "Restore only the named state's angles." );
			HlaeSameLineIfFits( 75.0f );
			if ( ImGui::Button( "FOV##mem", ImVec2( 65, 28 ) ) )
			{
				if ( g_Gui.hlaeInputMemName[0] && IsSafeQuotedArgument( g_Gui.hlaeInputMemName ) )
					IssueCommand( "mirv_input mem use \"%s\" fov", g_Gui.hlaeInputMemName );
				else SetError( "mirv_input state names cannot be empty or contain quotes, semicolons, or line breaks" );
			}
			HoverExplanation( "Restore only the named state's FOV." );
			HlaeSameLineIfFits( 90.0f );
			if ( ImGui::Button( "Remove##mem", ImVec2( 80, 28 ) ) )
			{
				if ( g_Gui.hlaeInputMemName[0] && IsSafeQuotedArgument( g_Gui.hlaeInputMemName ) )
					IssueCommand( "mirv_input mem remove \"%s\"", g_Gui.hlaeInputMemName );
				else SetError( "mirv_input state names cannot be empty or contain quotes, semicolons, or line breaks" );
			}
			HoverExplanation( "Remove the named view state." );

			if ( ImGui::Button( "Print states", ImVec2( 105, 28 ) ) )
				IssueCommandWithSeparator( "mirv_input mem print" );
			HoverExplanation( "Print all named view states to the Source console." );
			HlaeSameLineIfFits( 115.0f );
			if ( ImGui::Button( "Clear states", ImVec2( 105, 28 ) ) )
				IssueCommand( "mirv_input mem clear" );
			HoverExplanation( "Remove every named view state." );

			TextUnformattedWrapped( "Memory XML" );
			ImGui::SetNextItemWidth( -1 );
			ImGui::InputTextWithHint( "##mirv_input_mem_file", "mirv_input.xml",
				g_Gui.hlaeInputMemFile, sizeof( g_Gui.hlaeInputMemFile ) );
			HoverExplanation(
				"AdvancedFX XML for named view-state persistence. Absolute paths are used directly; relative files use the active ART take or the Output page folder." );
			if ( ImGui::Button( "Save XML", ImVec2( 100, 28 ) ) )
			{
				if ( IsSafeQuotedArgument( g_Gui.hlaeInputMemFile ) )
					IssueCommand( "mirv_input mem save \"%s\"", g_Gui.hlaeInputMemFile );
				else SetError( "HLAE paths cannot contain quotes, semicolons, or line breaks" );
			}
			HoverExplanation( "Save all named view states to the selected XML file." );
			HlaeSameLineIfFits( 110.0f );
			if ( ImGui::Button( "Load XML", ImVec2( 100, 28 ) ) )
			{
				if ( IsSafeQuotedArgument( g_Gui.hlaeInputMemFile ) )
					IssueCommand( "mirv_input mem load \"%s\"", g_Gui.hlaeInputMemFile );
				else SetError( "HLAE paths cannot contain quotes, semicolons, or line breaks" );
			}
			HoverExplanation( "Load named view states from the selected XML file." );
		}

		bool mouseMoveSupport = status.inputMouseMoveSupport;
		if ( ImGui::Checkbox( "Mouse move support", &mouseMoveSupport ) )
			IssueCommand( "mirv_input cfg mouseMoveSupport %d", mouseMoveSupport ? 1 : 0 );
		HoverExplanation(
			"Original HLAE mode: hold left mouse to move forward/sideways, right mouse "
			"to move vertically/sideways, and use the wheel for FOV." );
		HlaeSameLineIfFits( 190.0f );
		bool rotLocalSpace = status.inputRotLocalSpace;
		if ( ImGui::Checkbox( "Local-space rotation", &rotLocalSpace ) )
			IssueCommand( "mirv_input cfg rotLocalSpace %d", rotLocalSpace ? 1 : 0 );
		HoverExplanation( "Rotate in camera-local space exactly like original mirv_input." );

		static const char *offsetModeNames[] = { "last", "ownLast", "game", "current" };
		int offsetMode = status.inputOffsetMode;
		if ( offsetMode < 0 || ARRAYSIZE( offsetModeNames ) <= offsetMode ) offsetMode = 0;
		TextUnformattedWrapped( "Offset mode" );
		ImGui::SetNextItemWidth( 230 );
		if ( ImGui::Combo( "##mirv_input_offset_mode", &offsetMode, offsetModeNames,
			ARRAYSIZE( offsetModeNames ) ) )
			IssueCommand( "mirv_input cfg offsetMode %s", offsetModeNames[offsetMode] );
		HoverExplanation(
			"last = final previous output; ownLast = previous mirv_input output; "
			"game = game camera; current = camera after preceding overrides." );

		if ( ImGui::BeginTable( "mirv_input_core", 3,
			ImGuiTableFlags_SizingStretchSame ) )
		{
			const HlaeInputTableValueSpec values[] =
			{
				{ "Mouse sensitivity##mirv_input", "mirv_input cfg msens %.6f",
					status.inputMouseSensitivity, 0.005f, 0.0f, 10.0f, "%.3f" },
				{ "Keyboard sensitivity##mirv_input", "mirv_input cfg ksens %.6f",
					status.inputKeyboardSensitivity, 0.05f, 0.0f, 100.0f, "%.3f" },
				{ "Speed step factor##mirv_input", "mirv_input cfg stepFactor %.6f",
					status.inputStepFactor, 0.05f, 1.0f, 8.0f, "%.3f" }
			};
			DrawHlaeInputTableRow( values, ARRAYSIZE( values ) );
			ImGui::EndTable();
		}

		ImGui::SetNextItemOpen( false, ImGuiCond_Once );
		const bool smoothingOpen = ImGui::CollapsingHeader( "Smoothing" );
		HoverExplanation( "Expand mirv_input smoothing configuration. Closed by default." );
		if ( smoothingOpen )
		{
			bool smoothing = status.inputSmooth;
			if ( ImGui::Checkbox( "Enabled##mirv_input_smooth", &smoothing ) )
				IssueCommand( "mirv_input cfg smooth enabled %d", smoothing ? 1 : 0 );
			HoverExplanation( "Enable exponential smoothing for camera position, rotation and FOV." );
			HlaeSameLineIfFits( 210.0f );
			bool shortestPath = status.inputSmoothRotShortestPath;
			if ( ImGui::Checkbox( "Shortest rotation path", &shortestPath ) )
				IssueCommand( "mirv_input cfg smooth rotShortestPath %d",
					shortestPath ? 1 : 0 );
			HoverExplanation( "Use the shortest quaternion rotation path while smoothing." );
			if ( ImGui::BeginTable( "mirv_input_smoothing", 3,
				ImGuiTableFlags_SizingStretchSame ) )
			{
				const HlaeInputTableValueSpec values[] =
				{
					{ "Position half-time", "mirv_input cfg smooth halfTimeVec %.6f",
						status.inputSmoothHalfTimeVec, 0.01f, 0.0f, 10.0f, "%.3f" },
					{ "Rotation half-time", "mirv_input cfg smooth halfTimeAng %.6f",
						status.inputSmoothHalfTimeAng, 0.01f, 0.0f, 10.0f, "%.3f" },
					{ "FOV half-time", "mirv_input cfg smooth halfTimeFov %.6f",
						status.inputSmoothHalfTimeFov, 0.01f, 0.0f, 10.0f, "%.3f" }
				};
				DrawHlaeInputTableRow( values, ARRAYSIZE( values ) );
				ImGui::EndTable();
			}
		}

		ImGui::SetNextItemOpen( false, ImGuiCond_Once );
		const bool keyboardSpeedsOpen = ImGui::CollapsingHeader( "Keyboard speeds" );
		HoverExplanation( "Expand all original mirv_input keyboard movement/rotation/FOV speeds. Closed by default." );
		if ( keyboardSpeedsOpen )
		{
			if ( ImGui::BeginTable( "mirv_input_keyboard_speeds", 2,
				ImGuiTableFlags_SizingStretchSame ) )
			{
				const HlaeInputTableValueSpec row0[] =
				{
					{ "Forward (W / Num8)", "mirv_input cfg kForwardSpeed %.6f", status.inputKeyboardForwardSpeed, 1.0f, 0.0f, 10000.0f, "%.3f" },
					{ "Backward (S / Num2)", "mirv_input cfg kBackwardSpeed %.6f", status.inputKeyboardBackwardSpeed, 1.0f, 0.0f, 10000.0f, "%.3f" }
				};
				const HlaeInputTableValueSpec row1[] =
				{
					{ "Left (A / Num4)", "mirv_input cfg kLeftSpeed %.6f", status.inputKeyboardLeftSpeed, 1.0f, 0.0f, 10000.0f, "%.3f" },
					{ "Right (D / Num6)", "mirv_input cfg kRightSpeed %.6f", status.inputKeyboardRightSpeed, 1.0f, 0.0f, 10000.0f, "%.3f" }
				};
				const HlaeInputTableValueSpec row2[] =
				{
					{ "Up (R / Num9)", "mirv_input cfg kUpSpeed %.6f", status.inputKeyboardUpSpeed, 1.0f, 0.0f, 10000.0f, "%.3f" },
					{ "Down (F / Num3)", "mirv_input cfg kDownSpeed %.6f", status.inputKeyboardDownSpeed, 1.0f, 0.0f, 10000.0f, "%.3f" }
				};
				const HlaeInputTableValueSpec row3[] =
				{
					{ "Pitch + (Down arrow)", "mirv_input cfg kPitchPositiveSpeed %.6f", status.inputKeyboardPitchPositiveSpeed, 1.0f, 0.0f, 10000.0f, "%.3f" },
					{ "Pitch - (Up arrow)", "mirv_input cfg kPitchNegativeSpeed %.6f", status.inputKeyboardPitchNegativeSpeed, 1.0f, 0.0f, 10000.0f, "%.3f" }
				};
				const HlaeInputTableValueSpec row4[] =
				{
					{ "Yaw + (Left arrow)", "mirv_input cfg kYawPositiveSpeed %.6f", status.inputKeyboardYawPositiveSpeed, 1.0f, 0.0f, 10000.0f, "%.3f" },
					{ "Yaw - (Right arrow)", "mirv_input cfg kYawNegativeSpeed %.6f", status.inputKeyboardYawNegativeSpeed, 1.0f, 0.0f, 10000.0f, "%.3f" }
				};
				const HlaeInputTableValueSpec row5[] =
				{
					{ "Roll + (X / Num decimal)", "mirv_input cfg kRollPositiveSpeed %.6f", status.inputKeyboardRollPositiveSpeed, 1.0f, 0.0f, 10000.0f, "%.3f" },
					{ "Roll - (Z / Num0)", "mirv_input cfg kRollNegativeSpeed %.6f", status.inputKeyboardRollNegativeSpeed, 1.0f, 0.0f, 10000.0f, "%.3f" }
				};
				const HlaeInputTableValueSpec row6[] =
				{
					{ "FOV + (Page Down / Num1)", "mirv_input cfg kFovPositiveSpeed %.6f", status.inputKeyboardFovPositiveSpeed, 1.0f, 0.0f, 10000.0f, "%.3f" },
					{ "FOV - (Page Up / Num7)", "mirv_input cfg kFovNegativeSpeed %.6f", status.inputKeyboardFovNegativeSpeed, 1.0f, 0.0f, 10000.0f, "%.3f" }
				};
				DrawHlaeInputTableRow( row0, ARRAYSIZE( row0 ) );
				DrawHlaeInputTableRow( row1, ARRAYSIZE( row1 ) );
				DrawHlaeInputTableRow( row2, ARRAYSIZE( row2 ) );
				DrawHlaeInputTableRow( row3, ARRAYSIZE( row3 ) );
				DrawHlaeInputTableRow( row4, ARRAYSIZE( row4 ) );
				DrawHlaeInputTableRow( row5, ARRAYSIZE( row5 ) );
				DrawHlaeInputTableRow( row6, ARRAYSIZE( row6 ) );
				ImGui::EndTable();
			}
		}

		ImGui::SetNextItemOpen( false, ImGuiCond_Once );
		const bool mouseSpeedsOpen = ImGui::CollapsingHeader( "Mouse speeds" );
		HoverExplanation( "Expand all original mirv_input mouse movement/rotation/FOV speeds. Closed by default." );
		if ( mouseSpeedsOpen )
		{
			if ( ImGui::BeginTable( "mirv_input_mouse_speeds", 2,
				ImGuiTableFlags_SizingStretchSame ) )
			{
				const HlaeInputTableValueSpec row0[] =
				{
					{ "Yaw", "mirv_input cfg mYawSpeed %.6f", status.inputMouseYawSpeed, 1.0f, 0.0f, 10000.0f, "%.3f" },
					{ "Pitch", "mirv_input cfg mPitchSpeed %.6f", status.inputMousePitchSpeed, 1.0f, 0.0f, 10000.0f, "%.3f" }
				};
				const HlaeInputTableValueSpec row1[] =
				{
					{ "FOV +", "mirv_input cfg mFovPositiveSpeed %.6f", status.inputMouseFovPositiveSpeed, 1.0f, 0.0f, 10000.0f, "%.3f" },
					{ "FOV -", "mirv_input cfg mFovNegativeSpeed %.6f", status.inputMouseFovNegativeSpeed, 1.0f, 0.0f, 10000.0f, "%.3f" }
				};
				const HlaeInputTableValueSpec row2[] =
				{
					{ "Forward", "mirv_input cfg mForwardSpeed %.6f", status.inputMouseForwardSpeed, 1.0f, 0.0f, 10000.0f, "%.3f" },
					{ "Backward", "mirv_input cfg mBackSpeed %.6f", status.inputMouseBackwardSpeed, 1.0f, 0.0f, 10000.0f, "%.3f" }
				};
				const HlaeInputTableValueSpec row3[] =
				{
					{ "Left", "mirv_input cfg mLeftSpeed %.6f", status.inputMouseLeftSpeed, 1.0f, 0.0f, 10000.0f, "%.3f" },
					{ "Right", "mirv_input cfg mRightSpeed %.6f", status.inputMouseRightSpeed, 1.0f, 0.0f, 10000.0f, "%.3f" }
				};
				const HlaeInputTableValueSpec row4[] =
				{
					{ "Up", "mirv_input cfg mUpSpeed %.6f", status.inputMouseUpSpeed, 1.0f, 0.0f, 10000.0f, "%.3f" },
					{ "Down", "mirv_input cfg mDownSpeed %.6f", status.inputMouseDownSpeed, 1.0f, 0.0f, 10000.0f, "%.3f" }
				};
				DrawHlaeInputTableRow( row0, ARRAYSIZE( row0 ) );
				DrawHlaeInputTableRow( row1, ARRAYSIZE( row1 ) );
				DrawHlaeInputTableRow( row2, ARRAYSIZE( row2 ) );
				DrawHlaeInputTableRow( row3, ARRAYSIZE( row3 ) );
				DrawHlaeInputTableRow( row4, ARRAYSIZE( row4 ) );
				ImGui::EndTable();
			}
		}


		TextDisabledWrapped(
			"Original controls: Escape ends; Home/Num5 resets view and speed; Num +/- "
			"changes speed; Ctrl passes the next key through. By default input pauses while "
			"the ART menu is open. Enable GUI passthrough above, place the cursor outside the "
			"window, and hold the configured key to control mirv_input." );
		ImGui::EndChild();

		ImGui::Spacing();
		ImGui::BeginChild( "hlae_camio", ImVec2( 0, 0 ),
			ImGuiChildFlags_Borders | ImGuiChildFlags_AutoResizeY );
		if ( firstFrame )
			LogMessage( "HLAE GUI OPEN: input ready" );
		TextUnformattedWrapped( "mirv_camio" );
		HoverExplanation(
			"Imports or exports the AdvancedFX CAM v2 format, including camera time, "
			"position, rotation and aspect-corrected FOV." );
		StatusBadge( status.camioExporting, "CAM EXPORT ACTIVE", "CAM EXPORT IDLE" );
		HoverExplanation( "Current AdvancedFX CAM writer state." );
		HlaeSameLineIfFits( 165.0f );
		StatusBadge( status.camioImporting, "CAM IMPORT ACTIVE", "CAM IMPORT IDLE" );
		HoverExplanation( "Current AdvancedFX CAM reader state." );
		ImGui::SetNextItemWidth( -1 );
		ImGui::InputTextWithHint( "##hlae_camio_file", "camera.cam",
			g_Gui.hlaeCamioFile, sizeof( g_Gui.hlaeCamioFile ) );
		HoverExplanation(
			"CAM path. Absolute paths are used directly; relative paths use the active ART take or the Output page folder." );
		if ( HlaeToggleButton( "Start CAM export##camio_export",
			"Stop CAM export##camio_export", status.camioExporting, ImVec2( 160, 34 ) ) )
		{
			if ( status.camioExporting )
				IssueCommand( "mirv_camio export end" );
			else if ( IsSafeQuotedArgument( g_Gui.hlaeCamioFile ) )
				IssueCommand( "mirv_camio export start \"%s\"", g_Gui.hlaeCamioFile );
			else SetError( "HLAE paths cannot contain quotes, semicolons, or line breaks" );
		}
		HoverExplanation( status.camioExporting ?
			"Stop and finalize the active AdvancedFX CAM export." :
			"Start writing final -afxV34 camera samples to an AdvancedFX CAM file." );
		HlaeSameLineIfFits( 170.0f );
		if ( HlaeToggleButton( "Start CAM import##camio_import",
			"Stop CAM import##camio_import", status.camioImporting, ImVec2( 160, 34 ) ) )
		{
			if ( status.camioImporting )
				IssueCommand( "mirv_camio import end" );
			else if ( IsSafeQuotedArgument( g_Gui.hlaeCamioFile ) )
				IssueCommand( "mirv_camio import start \"%s\"", g_Gui.hlaeCamioFile );
			else SetError( "HLAE paths cannot contain quotes, semicolons, or line breaks" );
		}
		HoverExplanation( status.camioImporting ?
			"Stop applying the active CAM import." :
			"Load a CAM file and apply its camera samples during playback." );
		if ( status.camioExportPath[0] )
			TextDisabledWrapped( "Export file: %s", status.camioExportPath );
		if ( status.camioImportPath[0] )
			TextDisabledWrapped( "Import file: %s", status.camioImportPath );
		ImGui::EndChild();

		ImGui::Spacing();
		ImGui::BeginChild( "hlae_agr", ImVec2( 0, 0 ),
			ImGuiChildFlags_Borders | ImGuiChildFlags_AutoResizeY );
		if ( firstFrame )
			LogMessage( "HLAE GUI OPEN: camio ready" );
		TextUnformattedWrapped( "mirv_agr" );
		HoverExplanation(
			"Records AdvancedFX GameRecord v6 entity and camera data. Enable recording "
			"mode before loading a demo for complete entity state." );
		bool agrEnabled = status.agrEnabled;
		if ( ImGui::Checkbox( "Recording mode enabled", &agrEnabled ) )
			IssueCommand( "mirv_agr enabled %d", agrEnabled ? 1 : 0 );
		HoverExplanation(
			"Toggle Source tool recording mode. Enable before loading the demo so recordable entities are registered." );
		HlaeSameLineIfFits( 110.0f );
		StatusBadge( status.agrRecording, "RECORDING", "IDLE" );
		HoverExplanation( "Whether an AGR v6 file is currently being written." );

		bool recordCamera = status.agrRecordCamera;
		if ( ImGui::Checkbox( "Camera##agr", &recordCamera ) )
			IssueCommand( "mirv_agr recordCamera %d", recordCamera ? 1 : 0 );
		HoverExplanation( "Write the final -afxV34 camera and real FOV into each AGR frame." );
		HlaeSameLineIfFits( 100.0f );
		bool recordPlayers = status.agrRecordPlayers;
		if ( ImGui::Checkbox( "Players##agr", &recordPlayers ) )
			IssueCommand( "mirv_agr recordPlayers %d", recordPlayers ? 1 : 0 );
		HoverExplanation( "Record player and ragdoll entity state." );
		HlaeSameLineIfFits( 105.0f );
		bool recordWeapons = status.agrRecordWeapons;
		if ( ImGui::Checkbox( "Weapons##agr", &recordWeapons ) )
			IssueCommand( "mirv_agr recordWeapons %d", recordWeapons ? 1 : 0 );
		HoverExplanation( "Record weapon and breakable-prop entity state." );
		HlaeSameLineIfFits( 120.0f );
		bool recordProjectiles = status.agrRecordProjectiles;
		if ( ImGui::Checkbox( "Projectiles##agr", &recordProjectiles ) )
			IssueCommand( "mirv_agr recordProjectiles %d", recordProjectiles ? 1 : 0 );
		HoverExplanation( "Record grenade/projectile entity state." );
		HlaeSameLineIfFits( 145.0f );
		bool recordInvisible = status.agrRecordInvisible;
		if ( ImGui::Checkbox( "Record invisible##agr", &recordInvisible ) )
			IssueCommand( "mirv_agr recordInvisible %d", recordInvisible ? 1 : 0 );
		HoverExplanation( "Include invisible entities; this can retain data most scenes do not need." );
		HlaeSameLineIfFits( 90.0f );
		bool agrDebug = status.agrDebug;
		if ( ImGui::Checkbox( "Debug##agr", &agrDebug ) )
			IssueCommand( "mirv_agr debug %d", agrDebug ? 1 : 0 );
		HoverExplanation( "Print AGR entity classification and recording diagnostics to the console." );

		if ( ImGui::BeginTable( "agr_entity_filters", 2,
			ImGuiTableFlags_SizingStretchSame ) )
		{
			// Keep labels and controls on separate rows so both numeric fields share
			// the same baseline even when the panel becomes narrow.
			ImGui::TableNextRow();
			ImGui::TableSetColumnIndex( 0 );
			ImGui::AlignTextToFramePadding();
			TextUnformattedSingleLine( "Player cameras" );
			HoverExplanation( "0 disables player cameras, -1 records all, or enter one player entity index." );
			ImGui::TableSetColumnIndex( 1 );
			ImGui::AlignTextToFramePadding();
			TextUnformattedSingleLine( "Viewmodels" );
			HoverExplanation( "0 disables viewmodels, -1 records all, or enter one player entity index." );

			ImGui::TableNextRow();
			ImGui::TableSetColumnIndex( 0 );
			int playerCameras = status.agrRecordPlayerCameras;
			ImGui::SetNextItemWidth( -1 );
			if ( ImGui::DragInt( "##agr_player_cameras", &playerCameras, 1.0f, -1, 64 ) )
				IssueCommand( "mirv_agr recordPlayerCameras %d", playerCameras );
			HoverExplanation( "0 disables player cameras, -1 records all, or enter one player entity index." );

			ImGui::TableSetColumnIndex( 1 );
			int viewModels = status.agrRecordViewModels;
			ImGui::SetNextItemWidth( -1 );
			if ( ImGui::DragInt( "##agr_viewmodels", &viewModels, 1.0f, -1, 64 ) )
				IssueCommand( "mirv_agr recordViewModels %d", viewModels );
			HoverExplanation( "0 disables viewmodels, -1 records all, or enter one player entity index." );
			ImGui::EndTable();
		}
		ImGui::SetNextItemWidth( -1 );
		ImGui::InputTextWithHint( "##hlae_agr_file", "afxGameRecord.agr",
			g_Gui.hlaeAgrFile, sizeof( g_Gui.hlaeAgrFile ) );
		HoverExplanation(
			"AGR path. Absolute paths are used directly; relative paths use the active ART take or the Output page folder." );
		if ( HlaeToggleButton( "Start AGR##agr_toggle", "Stop AGR##agr_toggle",
			status.agrRecording, ImVec2( 145, 34 ) ) )
		{
			if ( status.agrRecording )
				IssueCommand( "mirv_agr stop" );
			else if ( IsSafeQuotedArgument( g_Gui.hlaeAgrFile ) )
				IssueCommand( "mirv_agr start \"%s\"", g_Gui.hlaeAgrFile );
			else SetError( "HLAE paths cannot contain quotes, semicolons, or line breaks" );
		}
		HoverExplanation( status.agrRecording ?
			"Finish the current AGR frame and close the file." :
			"Start an AdvancedFX GameRecord v6 file using the selected entity filters." );
		if ( status.agrPath[0] )
			TextDisabledWrapped( "AGR file: %s", status.agrPath );
		ImGui::EndChild();

		ImGui::Spacing();
		ImGui::BeginChild( "hlae_bvh", ImVec2( 0, 0 ),
			ImGuiChildFlags_Borders | ImGuiChildFlags_AutoResizeY );
		if ( firstFrame )
			LogMessage( "HLAE GUI OPEN: agr ready" );
		TextUnformattedWrapped( "mirv_camexport / mirv_camimport" );
		HoverExplanation(
			"Legacy AdvancedFX BVH camera export/import. Coordinate conversion and channel order match HLAE MdtCam files." );
		StatusBadge( status.camexportRecording, "BVH EXPORT ACTIVE", "BVH EXPORT IDLE" );
		HoverExplanation( "Current MdtCam BVH writer state." );
		HlaeSameLineIfFits( 170.0f );
		StatusBadge( status.camimportActive, "BVH IMPORT ACTIVE", "BVH IMPORT IDLE" );
		HoverExplanation( "Current MdtCam BVH reader state." );

		TextUnformattedWrapped( "BVH file" );
		ImGui::SetNextItemWidth( -1 );
		ImGui::InputTextWithHint( "##hlae_bvh_file", "camera.bvh",
			g_Gui.hlaeBvhFile, sizeof( g_Gui.hlaeBvhFile ) );
		HoverExplanation(
			"BVH path used by camera export and import. Absolute paths are used directly; relative paths use the active ART take or the Output page folder." );
		TextUnformattedWrapped( "BVH FPS" );
		ImGui::SetNextItemWidth( 180 );
		if ( ImGui::DragFloat( "##hlae_bvh_fps", &g_Gui.hlaeBvhFps,
			1.0f, 0.1f, 2000.0f, "%.2f" ) )
			IssueCommand( "art_hlae autoExport bvhFps %.6f", g_Gui.hlaeBvhFps );
		HoverExplanation( "Frame rate written into manual and automatic BVH exports." );

		if ( HlaeToggleButton( "Start BVH export##bvh_export",
			"Stop BVH export##bvh_export", status.camexportRecording, ImVec2( 160, 34 ) ) )
		{
			if ( status.camexportRecording )
				IssueCommand( "mirv_camexport stop" );
			else if ( IsSafeQuotedArgument( g_Gui.hlaeBvhFile ) )
				IssueCommand( "mirv_camexport start \"%s\" %.6f",
					g_Gui.hlaeBvhFile, g_Gui.hlaeBvhFps );
			else SetError( "HLAE paths cannot contain quotes, semicolons, or line breaks" );
		}
		HoverExplanation( status.camexportRecording ?
			"Finalize the BVH frame count and close the active export." :
			"Begin exporting the final -afxV34 camera to an MdtCam BVH file." );
		HlaeSameLineIfFits( 115.0f );
		if ( ImGui::Button( "Time info", ImVec2( 100, 34 ) ) )
			IssueCommandWithSeparator( "mirv_camexport timeinfo" );
		HoverExplanation( "Print the current interpolated client/demo time used by HLAE cameras." );

		if ( HlaeToggleButton( "Start BVH import##bvh_import",
			"Stop BVH import##bvh_import", status.camimportActive, ImVec2( 160, 34 ) ) )
		{
			if ( status.camimportActive )
				IssueCommand( "mirv_camimport stop" );
			else if ( IsSafeQuotedArgument( g_Gui.hlaeBvhFile ) )
				IssueCommand( "mirv_camimport start \"%s\"", g_Gui.hlaeBvhFile );
			else SetError( "HLAE paths cannot contain quotes, semicolons, or line breaks" );
		}
		HoverExplanation( status.camimportActive ?
			"Stop the active BVH camera import." :
			"Load and begin applying camera motion from the selected BVH file." );
		HlaeSameLineIfFits( 155.0f );
		if ( ImGui::Button( "Base time: current", ImVec2( 145, 34 ) ) )
			IssueCommand( "mirv_camimport basetime current" );
		HoverExplanation( "Align BVH time zero with the current client/demo time." );
		HlaeSameLineIfFits( 165.0f );
		if ( ImGui::Button( "Convert to campath", ImVec2( 155, 34 ) ) )
			IssueCommand( "mirv_camimport toCamPath 1 %.6f", g_Gui.hlaeFov );
		HoverExplanation( "Convert imported BVH samples into mirv_campath keyframes and set suitable interpolation." );
		if ( status.camexportPath[0] )
			TextDisabledWrapped( "Export file: %s", status.camexportPath );
		if ( status.camimportPath[0] )
			TextDisabledWrapped( "Import file: %s", status.camimportPath );
		ImGui::EndChild();

		ImGui::Spacing();
		ImGui::BeginChild( "hlae_fov", ImVec2( 0, 0 ),
			ImGuiChildFlags_Borders | ImGuiChildFlags_AutoResizeY );
		if ( firstFrame )
			LogMessage( "HLAE GUI OPEN: camera import/export ready" );
		TextUnformattedWrapped( "mirv_fov" );
		HoverExplanation( "Overrides final camera FOV at the AdvancedFX -afxV34 SetupEngineView stage." );
		if ( !ImGui::IsAnyItemActive() )
			g_Gui.hlaeFov = static_cast<float>( status.fov );
		TextUnformattedWrapped( "Engine FOV" );
		ImGui::SetNextItemWidth( 300 );
		if ( ImGui::DragFloat( "##hlae_fov", &g_Gui.hlaeFov,
			0.25f, 1.0f, 179.0f, "%.2f" ) )
			IssueCommand( "mirv_fov %.6f", g_Gui.hlaeFov );
		HoverExplanation( "Set horizontal 4:3 engine FOV override from 1 to 179 degrees." );
		HlaeSameLineIfFits( 140.0f );
		if ( ImGui::Button( "Game default", ImVec2( 130, 0 ) ) )
			IssueCommand( "mirv_fov default" );
		HoverExplanation( "Disable mirv_fov and use the game's current camera FOV." );
		bool handleZoom = status.fovHandleZoom;
		if ( ImGui::Checkbox( "Preserve weapon zoom", &handleZoom ) )
			IssueCommand( "mirv_fov handleZoom enabled %d", handleZoom ? 1 : 0 );
		HoverExplanation( "Do not apply the override while the game FOV is below the zoom threshold." );
		HlaeSameLineIfFits( 320.0f );
		float minUnzoomed = static_cast<float>( status.fovMinUnzoomed );
		ImGui::SetNextItemWidth( 190 );
		if ( ImGui::DragFloat( "Minimum unzoomed FOV", &minUnzoomed,
			0.25f, 1.0f, 179.0f, "%.2f" ) )
			IssueCommand( "mirv_fov handleZoom minUnzoomedFov %.6f", minUnzoomed );
		HoverExplanation( "Game FOV values below this threshold are treated as weapon zoom." );
		ImGui::EndChild();
		ImGui::EndDisabled();

		if ( firstFrame )
		{
			LogMessage( "HLAE GUI OPEN: render complete" );
			s_HlaePageFirstFrame = false;
		}
	}
	void DrawValidationInfoSection( bool recording )
	{
		ImGui::BeginChild( "info_validation", ImVec2( 0, 0 ),
			ImGuiChildFlags_Borders | ImGuiChildFlags_AutoResizeY );
		TextUnformattedWrapped( "Recorded-file validation" );
		bool autoValidate = InterlockedCompareExchange(
			&g_ArtValidationOptions.autoValidate, FALSE, FALSE ) != FALSE;
		if ( ImGui::Checkbox( "Automatically validate after recording", &autoValidate ) )
			IssueCommand( "art_validation auto %s", autoValidate ? "on" : "off" );
		bool checkFileSize = InterlockedCompareExchange(
			&g_ArtValidationOptions.checkFileSize, FALSE, FALSE ) != FALSE;
		if ( ImGui::Checkbox( "Check file size", &checkFileSize ) )
			IssueCommand( "art_validation file_size %s", checkFileSize ? "on" : "off" );
		ImGui::SameLine();
		bool checkDroppedFrames = InterlockedCompareExchange(
			&g_ArtValidationOptions.checkDroppedFrames, FALSE, FALSE ) != FALSE;
		if ( ImGui::Checkbox( "Check missing / dropped frames", &checkDroppedFrames ) )
			IssueCommand( "art_validation dropped_frames %s", checkDroppedFrames ? "on" : "off" );
		int minimumBytes = static_cast<int>( InterlockedCompareExchange(
			&g_ArtValidationOptions.minimumFileBytes, 0, 0 ) );
		ImGui::SetNextItemWidth( 150 );
		if ( ImGui::InputInt( "Minimum TGA bytes", &minimumBytes ) )
		{
			if ( minimumBytes < 18 ) minimumBytes = 18;
			if ( minimumBytes > 1073741824 ) minimumBytes = 1073741824;
			IssueCommand( "art_validation min_size %d", minimumBytes );
		}
		const bool validationRunning = IsArtValidationRunning();
		ImGui::BeginDisabled( recording || validationRunning ||
			!g_ArtRecordingStats.takeDisplayPath[0] );
		if ( ImGui::Button( "Validate latest take", ImVec2( 175, 32 ) ) )
			IssueCommand( "art_validation" );
		ImGui::EndDisabled();
		ImGui::SameLine();
		if ( ImGui::Button( "Print result", ImVec2( 125, 32 ) ) )
			IssueCommandWithSeparator( "art_validation status" );
		if ( validationRunning )
		{
			const LONG completed = InterlockedCompareExchange(
				&g_ArtValidationProgress.completedFiles, 0, 0 );
			const LONG total = InterlockedCompareExchange(
				&g_ArtValidationProgress.totalFiles, 0, 0 );
			char progressLabel[96];
			if ( total > 0 )
				Q_snprintf( progressLabel, sizeof( progressLabel ), "%ld / %ld files", completed, total );
			else
				Q_strncpy( progressLabel, "Discovering files...", sizeof( progressLabel ) );
			ImGui::ProgressBar( GetArtValidationProgressFraction(),
				ImVec2( -1.0f, 22.0f ), progressLabel );
			TextDisabledWrapped( "%s | elapsed %.2f seconds | recording start waits for completion",
				ArtValidationPhaseName( InterlockedCompareExchange(
					&g_ArtValidationProgress.phase, 0, 0 ) ),
				GetArtValidationElapsedMs() / 1000.0f );
		}
		else if ( g_ArtValidationResult.hasResult )
		{
			ImGui::TextColored( g_ArtValidationResult.passed ?
				ImVec4( 0.35f, 0.90f, 0.46f, 1.0f ) : ImVec4( 1.0f, 0.25f, 0.18f, 1.0f ),
				"%s: %lu scanned / %lu expected", g_ArtValidationResult.passed ? "PASS" : "FAIL",
				g_ArtValidationResult.scannedFiles, g_ArtValidationResult.expectedFiles );
			ImGui::TextWrapped( "Missing %lu | unexpected %lu | gaps %lu | undersized %lu | bad headers %lu | damaged pixel data %lu | dimension mismatches %lu | directory errors %lu",
				g_ArtValidationResult.missingFiles, g_ArtValidationResult.unexpectedFiles,
				g_ArtValidationResult.sequenceGaps, g_ArtValidationResult.undersizedFiles,
				g_ArtValidationResult.invalidHeaders, g_ArtValidationResult.invalidPixelData,
				g_ArtValidationResult.inconsistentDimensions, g_ArtValidationResult.directoryErrors );
		}
		else
		{
			TextDisabledWrapped( "No validation has been run for the latest take." );
		}
		ImGui::EndChild();
	}

	void DrawInfoPage()
	{
		if ( g_Gui.resetInfoScroll )
		{
			ImGui::SetScrollY( 0.0f );
			g_Gui.resetInfoScroll = false;
		}
		if ( g_Gui.refreshConfigs ) RefreshConfigList();
		const bool recording = InterlockedCompareExchange( &g_bRecording, FALSE, FALSE ) != FALSE;
		const LONG recordMask = InterlockedCompareExchange( &g_nRecordMask, 0, 0 );
		const LONG hudMask = InterlockedCompareExchange( &g_nHudMask, 0, 0 );
		const LONG preview = InterlockedCompareExchange( &g_nPreviewPass, ART_PREVIEW_NONE, ART_PREVIEW_NONE );
		const std::string recordPasses = MaskNames( recordMask );
		const std::string hudPasses = MaskNames( hudMask );
		ConVar *pHostFramerate = g_pCvar ? g_pCvar->FindVar( "host_framerate" ) : NULL;

		TextUnformattedWrapped( "Information" );
		ImGui::Separator();
		ImGui::Spacing();

		DrawValidationInfoSection( recording );

		ImGui::Spacing();
		ImGui::BeginChild( "info_recording", ImVec2( 0, 168 ), ImGuiChildFlags_Borders );
		TextUnformattedWrapped( "Recording" );
		ImGui::Text( "Recorder state: %s", recording ? "recording" : "idle" );
		ImGui::Text( "Frame: %d", g_nFrame );
		ImGui::Text( "Recorded passes: %s", recordPasses.c_str() );
		ImGui::Text( "HUD passes: %s", hudPasses.c_str() );
		static const char *previewNames[] = { "off", "normal", "clear", "clear-noplayers", "viewmodel", "depth", "players", "objectid" };
		const char *pPreviewName = preview >= 0 && preview < ARRAYSIZE( previewNames ) ?
			previewNames[preview] : "unknown";
		ImGui::Text( "Preview: %s", pPreviewName );
		ImGui::Text( "host_framerate: %g", pHostFramerate ? pHostFramerate->GetFloat() : 0.0f );
		ImGui::Text( "Global FOV: %s", InterlockedCompareExchange( &g_bGlobalFovOverride, FALSE, FALSE ) ? "camera + viewmodel override enabled" : "game default" );
		ImGui::Text( "Viewmodel FOV: %.1f", g_flViewmodelFov );
		ImGui::EndChild();

		ImGui::Spacing();
		ImGui::BeginChild( "info_files", ImVec2( 0, 174 ), ImGuiChildFlags_Borders );
		TextUnformattedWrapped( "Files and paths" );
		char configDirectory[MAX_PATH];
		BuildConfigDirectory( configDirectory, sizeof( configDirectory ) );
		ImGui::TextWrapped( "Game directory: %s", g_pEngine ? g_pEngine->GetGameDirectory() : "unavailable" );
		ImGui::TextWrapped( "Output base: %s%s", g_szRecordBase,
			g_bRecordBaseAbsolute ? " (absolute)" : " (relative to the game directory)" );
		ImGui::TextWrapped( "Capture prefix: %s", g_szCapturePrefix[0] ? g_szCapturePrefix : "default" );
		ImGui::TextWrapped( "Config directory: %s", configDirectory );
		ImGui::TextWrapped( "Diagnostic log: %s", GetLogPath() );
		ImGui::Text( "Discovered configs: %d", static_cast<int>( g_Gui.configs.size() ) );
		if ( ImGui::Button( "Refresh config list" ) )
			RefreshConfigList();
		ImGui::EndChild();

		ImGui::Spacing();
		ImGui::SetNextItemOpen( false, ImGuiCond_Once );
		if ( !ImGui::CollapsingHeader( "Recorded footage statistics" ) )
			return;
		ImGui::BeginChild( "info_statistics", ImVec2( 0, 0 ),
			ImGuiChildFlags_Borders | ImGuiChildFlags_AutoResizeY );
		char takeBytes[48];
		char sessionBytes[48];
		FormatArtByteCount( g_ArtRecordingStats.takeBytes, takeBytes, sizeof( takeBytes ) );
		FormatArtByteCount( g_ArtRecordingStats.sessionBytes, sessionBytes, sizeof( sessionBytes ) );
		ImGui::TextWrapped( "Current/latest take: %s",
			g_ArtRecordingStats.takeDisplayPath[0] ? g_ArtRecordingStats.takeDisplayPath : "none this session" );
		ImGui::Text( "Take: %lu frames | %lu files | %s | %.2f seconds%s",
			g_ArtRecordingStats.takeFrames, g_ArtRecordingStats.takeFiles, takeBytes,
			GetArtRecordingElapsedMs() / 1000.0f, g_ArtRecordingStats.takeAborted ? " | ABORTED" : "" );
		ImGui::Text( "Capture: %dx%d | host_framerate %g | resolution changes %lu",
			g_ArtRecordingStats.takeWidth, g_ArtRecordingStats.takeHeight,
			g_ArtRecordingStats.takeHostFramerate,
			g_ArtRecordingStats.takeResolutionChanges );
		char manifestPath[MAX_PATH];
		FormatArtTakeManifestPath( manifestPath, sizeof( manifestPath ) );
		ImGui::TextWrapped( "Take JSON: %s | %s",
			g_ArtRecordingStats.takeManifestEnabled ? "enabled" : "disabled",
			manifestPath[0] ? manifestPath : "not available" );
		ImGui::Text( "Session: %lu started | %lu complete | %lu aborted",
			g_ArtRecordingStats.sessionTakesStarted, g_ArtRecordingStats.sessionTakesCompleted,
			g_ArtRecordingStats.sessionTakesAborted );
		ImGui::Text( "Session footage: %lu frames | %lu files | %s",
			g_ArtRecordingStats.sessionFrames, g_ArtRecordingStats.sessionFiles, sessionBytes );
		static const char *timingNames[ART_TIMING_COUNT] =
			{ "Render", "Read", "Encode", "Write enqueue", "Queue wait" };
		ImGui::Separator();
		TextUnformattedWrapped( "Take pipeline timing (average / maximum / total)" );
		for ( int i = 0; i < ART_TIMING_COUNT; ++i )
		{
			const ArtStageTimingStatistics &timing = g_ArtPipelineStats.stages[i];
			const double average = timing.takeSamples ?
				static_cast<double>( timing.takeTotalMicroseconds ) /
					timing.takeSamples / 1000.0 : 0.0;
			ImGui::Text( "%-14s %.3f / %.3f / %.3f ms (%lu)",
				timingNames[i], average, timing.takeMaxMicroseconds / 1000.0,
				timing.takeTotalMicroseconds / 1000.0, timing.takeSamples );
		}
		char pipelineInputBytes[48];
		char pipelineOutputBytes[48];
		char pendingBytes[48];
		FormatArtByteCount( g_ArtPipelineStats.takeUncompressedBytes,
			pipelineInputBytes, sizeof( pipelineInputBytes ) );
		FormatArtByteCount( g_ArtPipelineStats.takeOutputBytes,
			pipelineOutputBytes, sizeof( pipelineOutputBytes ) );
		FormatArtByteCount( g_ArtPipelineStats.pendingBytes,
			pendingBytes, sizeof( pendingBytes ) );
		ImGui::Text( "TGA: %s | %s -> %s",
			ArtTgaCompressionModeName( InterlockedCompareExchange(
				&g_nArtTgaCompressionMode, 0, 0 ) ),
			pipelineInputBytes, pipelineOutputBytes );
		ImGui::Text( "Queue: %lu pending / %s | %lu flushes | %lu allocation retries | %lu failures",
			g_ArtPipelineStats.pendingFiles, pendingBytes, g_ArtPipelineStats.takeFlushes,
			g_ArtPipelineStats.takeAllocationRetries, g_ArtPipelineStats.takeAllocationFailures );
		ImGui::Separator();
		static const char *passNames[ART_CAPTURE_PASS_COUNT] =
			{ "Normal", "Clear", "Clear - no players", "Viewmodel", "Depth", "Players", "ObjectID" };
		for ( int i = 0; i < ART_CAPTURE_PASS_COUNT; ++i )
		{
			char passBytes[48];
			FormatArtByteCount( g_ArtRecordingStats.passes[i].bytes, passBytes, sizeof( passBytes ) );
			ImGui::Text( "%-18s %lu files | %s", passNames[i],
				g_ArtRecordingStats.passes[i].files, passBytes );
		}
		if ( ImGui::Button( "Print statistics", ImVec2( 160, 30 ) ) )
			IssueCommandWithSeparator( "art_stats" );
		ImGui::EndChild();
	}


	void DrawConfigsPage()
	{
		if ( g_Gui.refreshConfigs )
			RefreshConfigList();

		TextUnformattedWrapped( "Recorder configs" );
		ImGui::Separator();
		ImGui::Spacing();
		ImGui::BeginChild( "config_list", ImVec2( 250, 0 ), ImGuiChildFlags_Borders );
		for ( size_t i = 0; i < g_Gui.configs.size(); ++i )
		{
			const bool selected = g_Gui.selectedConfig == static_cast<int>( i );
			if ( ImGui::Selectable( g_Gui.configs[i].c_str(), selected ) )
			{
				g_Gui.selectedConfig = static_cast<int>( i );
				Q_strncpy( g_Gui.configName, g_Gui.configs[i].c_str(), sizeof( g_Gui.configName ) );
			}
		}
		if ( g_Gui.configs.empty() )
			TextDisabledWrapped( "No saved configs" );
		ImGui::EndChild();
		ImGui::SameLine();
		ImGui::BeginGroup();
		ImGui::SetNextItemWidth( -1 );
		ImGui::InputTextWithHint( "##config_name", "Config name", g_Gui.configName, sizeof( g_Gui.configName ) );
		if ( ImGui::Button( "Save current", ImVec2( 130, 34 ) ) )
		{
			if ( IsSafeConfigName( g_Gui.configName ) )
			{
				IssueCommand( "art_config save %s", g_Gui.configName );
				g_Gui.refreshDelay = 3;
			}
			else SetError( "enter a valid config name" );
		}
		ImGui::SameLine();
		if ( ImGui::Button( "Load", ImVec2( 90, 34 ) ) )
		{
			if ( IsSafeConfigName( g_Gui.configName ) ) IssueCommand( "art_config load %s", g_Gui.configName );
			else SetError( "enter a valid config name" );
		}
		ImGui::SameLine();
		if ( ImGui::Button( "Delete", ImVec2( 90, 34 ) ) )
			ImGui::OpenPopup( "Delete config?" );
		if ( ImGui::BeginPopupModal( "Delete config?", NULL, ImGuiWindowFlags_AlwaysAutoResize ) )
		{
			ImGui::Text( "Delete config '%s'?", g_Gui.configName );
			if ( ImGui::Button( "Delete", ImVec2( 100, 0 ) ) )
			{
				if ( IsSafeConfigName( g_Gui.configName ) )
				{
					IssueCommand( "art_config delete %s", g_Gui.configName );
					g_Gui.refreshDelay = 3;
				}
				else SetError( "enter a valid config name" );
				ImGui::CloseCurrentPopup();
			}
			ImGui::SameLine();
			if ( ImGui::Button( "Cancel", ImVec2( 100, 0 ) ) )
				ImGui::CloseCurrentPopup();
			ImGui::EndPopup();
		}
		ImGui::Spacing();
		if ( ImGui::Button( "Refresh list" ) )
			RefreshConfigList();
		ImGui::TextWrapped( "Configs are plain .cfg files in cstrike/cfg/art_gui and can also be loaded with exec." );
		ImGui::Spacing();
		ImGui::Separator();
		ImGui::BeginDisabled( InterlockedCompareExchange( &g_bRecording, FALSE, FALSE ) != FALSE );
		if ( ImGui::Button( "Reset everything to default", ImVec2( 220, 36 ) ) )
			ImGui::OpenPopup( "Reset everything?" );
		ImGui::EndDisabled();
		if ( ImGui::BeginPopupModal( "Reset everything?", NULL, ImGuiWindowFlags_AlwaysAutoResize ) )
		{
			ImGui::TextWrapped( "Reset all recorder, pass, HUD, FOV, visual-removal, chams, output, GUI theme, and hotkey settings to their built-in defaults? Saved config files will not be deleted." );
			if ( ImGui::Button( "Reset", ImVec2( 110, 0 ) ) )
			{
				ResetEverythingToDefaults();
				ImGui::CloseCurrentPopup();
			}
			ImGui::SameLine();
			if ( ImGui::Button( "Cancel", ImVec2( 110, 0 ) ) )
				ImGui::CloseCurrentPopup();
			ImGui::EndPopup();
		}
		ImGui::EndGroup();
	}

	void DrawConsolePage()
	{
		TextUnformattedWrapped( "Console command bridge" );
		ImGui::Separator();
		ImGui::Spacing();
		ImGui::SetNextItemWidth( -90 );
		const bool enter = ImGui::InputTextWithHint( "##console", "Enter any Source console command", g_Gui.consoleCommand,
			sizeof( g_Gui.consoleCommand ), ImGuiInputTextFlags_EnterReturnsTrue );
		ImGui::SameLine();
		if ( ( ImGui::Button( "Execute" ) || enter ) && g_Gui.consoleCommand[0] )
		{
			IssueCommandWithSeparator( "%s", g_Gui.consoleCommand );
			g_Gui.consoleCommand[0] = '\0';
		}

		ImGui::Spacing();
		TextDisabledWrapped( "Registered art_* commands" );
		ImGui::SetNextItemWidth( -1 );
		ImGui::InputTextWithHint( "##command_filter", "Filter commands", g_Gui.commandFilter,
			sizeof( g_Gui.commandFilter ) );
		ImGui::BeginChild( "command_browser", ImVec2( 0, 225 ), ImGuiChildFlags_Borders );
		int visibleCommands = 0;
		if ( g_pCvar )
		{
			for ( const ConCommandBase *pCommand = g_pCvar->GetCommands(); pCommand; pCommand = pCommand->GetNext() )
			{
				const char *pName = pCommand->GetName();
				if ( !pName || ( _strnicmp( pName, "art_", 4 ) &&
					_strnicmp( pName, "mirv_", 5 ) ) ||
					!ContainsInsensitive( pName, g_Gui.commandFilter ) )
					continue;
				++visibleCommands;
				if ( ImGui::Selectable( pName, false ) )
					Q_strncpy( g_Gui.consoleCommand, pName, sizeof( g_Gui.consoleCommand ) );
				if ( ImGui::IsItemHovered() )
				{
					const char *pHelp = pCommand->GetHelpText();
					if ( pHelp && pHelp[0] )
					{
						ImGui::BeginTooltip();
						ImGui::PushTextWrapPos( ImGui::GetFontSize() * 34.0f );
						// Do not call TextUnformattedWrapped here: it resets the tooltip's
						// explicit wrap width and can collapse help text into a vertical column.
						ImGui::TextWrapped( "%s", pHelp );
						ImGui::PopTextWrapPos();
						ImGui::EndTooltip();
					}
				}
			}
		}
		if ( visibleCommands == 0 )
			TextDisabledWrapped( "No matching art_* commands" );
		ImGui::EndChild();

		ImGui::Spacing();
		ImGui::BeginChild( "command_history", ImVec2( 0, 0 ), ImGuiChildFlags_Borders );
		TextDisabledWrapped( "GUI command history" );
		for ( size_t i = 0; i < g_Gui.commandHistory.size(); ++i )
			TextUnformattedWrapped( g_Gui.commandHistory[i].c_str() );
		ImGui::EndChild();
	}

	void DrawSettingsPage()
	{
		TextUnformattedWrapped( "Interface settings" );
		ImGui::Separator();
		ImGui::Spacing();

		ImGui::BeginChild( "input_settings", ImVec2( 0, 120 ), ImGuiChildFlags_Borders );
		TextUnformattedWrapped( "Input" );
		char keyName[96];
		GetToggleBindingName( g_Gui.toggleKey, g_Gui.toggleModifiers, keyName, sizeof( keyName ), true );
		ImGui::Text( "Toggle key: %s", keyName );
		ImGui::SameLine();
		if ( ImGui::Button( g_Gui.waitingForToggleKey ? "Press a key..." : "Change key", ImVec2( 135, 0 ) ) )
			g_Gui.waitingForToggleKey = true;
		ImGui::SameLine();
		if ( ImGui::Button( "Reset to Shift + F3" ) )
		{
			g_Gui.toggleKey = VK_F3;
			g_Gui.toggleModifiers = TOGGLE_MODIFIER_SHIFT;
			g_Gui.waitingForToggleKey = false;
		}
		if ( g_Gui.waitingForToggleKey )
			TextDisabledWrapped( "Hold modifiers, then press a key. Escape cancels." );
		ImGui::EndChild();

		ImGui::Spacing();
		const float advancedSettingsHeight =
			g_Gui.experimentalOptionsEnabled ? 280.0f : 230.0f;
		ImGui::BeginChild( "advanced_settings", ImVec2( 0, advancedSettingsHeight ), ImGuiChildFlags_Borders );
		TextUnformattedWrapped( "Diagnostics and experimental" );
		bool statisticsOverlay = InterlockedCompareExchange(
			&g_bArtStatisticsOverlayEnabled, FALSE, FALSE ) != FALSE;
		if ( ImGui::Checkbox( "In-game recording statistics overlay", &statisticsOverlay ) )
			IssueCommand( "art_overlay %s", statisticsOverlay ? "on" : "off" );
		TextDisabledWrapped( "Orange while idle, red while recording; excluded from recorded TGAs." );
		ImGui::Separator();
		bool autoResumeDemo = g_Gui.autoResumeDemoOnRecordingStart;
		if ( ImGui::Checkbox( "Unpause demo on recording start", &autoResumeDemo ) )
			IssueCommand( "art_demo_unpause_on_recording %s", autoResumeDemo ? "on" : "off" );
		TextDisabledWrapped( "Automatically executes demo_resume when recording starts." );
		ImGui::Separator();
		bool experimentalOptionsEnabled = g_Gui.experimentalOptionsEnabled;
		if ( ImGui::Checkbox( "Enable experimental options", &experimentalOptionsEnabled ) )
			IssueCommand( "art_gui_experimental %s", experimentalOptionsEnabled ? "on" : "off" );
		TextDisabledWrapped( "Shows unfinished or less-tested controls." );
		if ( g_Gui.experimentalOptionsEnabled )
		{
			bool autoPauseDemoAfterRecording = g_Gui.autoPauseDemoAfterRecording;
			if ( ImGui::Checkbox( "Pause demo after recording stops (experimental)",
				&autoPauseDemoAfterRecording ) )
			{
				IssueCommand( "art_demo_pause_after_recording %s",
					autoPauseDemoAfterRecording ? "on" : "off" );
			}
			TextDisabledWrapped( "When a demo is playing, ART requests demo_pause before flushing queued footage." );
		}
		ImGui::Separator();
		bool debugLogging = IsDebugLoggingEnabled();
		if ( ImGui::Checkbox( "Diagnostic logging", &debugLogging ) )
			IssueCommand( "art_debug %s", debugLogging ? "on" : "off" );
		TextDisabledWrapped( "Log file: %s", GetLogPath() );
		ImGui::EndChild();

		ImGui::Spacing();
		ImGui::BeginChild( "theme_settings", ImVec2( 0, 0 ), ImGuiChildFlags_Borders );
		TextUnformattedWrapped( "Appearance" );
		TextDisabledWrapped( "Presets" );
		if ( ImGui::Button( "Purple" ) ) ApplyThemePreset( "purple" );
		ImGui::SameLine();
		if ( ImGui::Button( "Blue" ) ) ApplyThemePreset( "blue" );
		ImGui::SameLine();
		if ( ImGui::Button( "Green" ) ) ApplyThemePreset( "green" );
		ImGui::SameLine();
		if ( ImGui::Button( "Orange" ) ) ApplyThemePreset( "orange" );
		ImGui::SameLine();
		if ( ImGui::Button( "Red" ) ) ApplyThemePreset( "red" );
		ImGui::SameLine();
		if ( ImGui::Button( "Mono" ) ) ApplyThemePreset( "mono" );

		ImGui::Spacing();
		const ImGuiColorEditFlags colorFlags = ImGuiColorEditFlags_DisplayRGB |
			ImGuiColorEditFlags_Uint8 | ImGuiColorEditFlags_AlphaBar;
		ImGui::SetNextItemWidth( 330 );
		ImGui::ColorEdit4( "Accent", g_Gui.accentColor, colorFlags );
		ImGui::SetNextItemWidth( 330 );
		ImGui::ColorEdit4( "Window background", g_Gui.windowColor, colorFlags );
		ImGui::SetNextItemWidth( 330 );
		ImGui::ColorEdit4( "Panel background", g_Gui.panelColor, colorFlags );
		ImGui::SetNextItemWidth( 330 );
		ImGui::ColorEdit4( "Sidebar background", g_Gui.sidebarColor, colorFlags );
		ImGui::SetNextItemWidth( 330 );
		ImGui::ColorEdit4( "Text", g_Gui.textColor, colorFlags );
		ImGui::SetNextItemWidth( 330 );
		ImGui::ColorEdit4( "Muted text", g_Gui.mutedTextColor, colorFlags );
		ImGui::SetNextItemWidth( 330 );
		ImGui::ColorEdit4( "Borders", g_Gui.borderColor, colorFlags );
		ImGui::SetNextItemWidth( 330 );
		ImGui::ColorEdit4( "Controls", g_Gui.controlColor, colorFlags );
		ImGui::SetNextItemWidth( 330 );
		ImGui::ColorEdit4( "Selection", g_Gui.selectionColor, colorFlags );

		ImGui::Spacing();
		TextDisabledWrapped( "Colors and toggle key are included in art_config saves." );
		ImGui::Spacing();
		ImGui::Separator();
		TextUnformattedWrapped( "About" );
		ImGui::Text( "CS:S V34 ADVANCED RECORDING TOOLS (ART) v%s", V34_ART_VERSION_STRING );
		ImGui::Text( "Build: %s %s", __DATE__, __TIME__ );
		TextUnformattedWrapped( "Created by Contrastniy" );
		if ( ImGui::Button( "YouTube: @Contrastniy", ImVec2( 190, 30 ) ) )
		{
			const HINSTANCE result = ShellExecuteA( NULL, "open",
				"https://www.youtube.com/@Contrastniy", NULL, NULL, SW_SHOWNORMAL );
			if ( reinterpret_cast<INT_PTR>( result ) <= 32 )
				SetError( "could not open YouTube channel (ShellExecute error %d)",
					static_cast<int>( reinterpret_cast<INT_PTR>( result ) ) );
		}
		HoverExplanation( "Open the Contrastniy YouTube channel in the default browser." );
		TextDisabledWrapped( "Counter-Strike: Source v34 build 4044" );
		ApplyThemeColors();
		ImGui::EndChild();
	}

	void DrawHlaeCampathHelpWindow()
	{
		if ( !g_Gui.showHlaeCampathHelp )
			return;

		ImGui::SetNextWindowSize( ImVec2( 780, 610 ), ImGuiCond_FirstUseEver );
		ImGui::SetNextWindowPos( ImVec2( 150, 95 ), ImGuiCond_FirstUseEver );
		if ( !ImGui::Begin( "mirv_campath command help", &g_Gui.showHlaeCampathHelp,
			ImGuiWindowFlags_NoCollapse ) )
		{
			ImGui::End();
			return;
		}

		TextDisabledWrapped( "Original AdvancedFX command grammar used by ART. Commands can be entered in the Source console or through the HLAE panel." );
		ImGui::Separator();
		ImGui::BeginChild( "mirv_campath_help_scroll", ImVec2( 0, -42 ),
			ImGuiChildFlags_Borders );
		TextUnformattedWrapped( "Core path commands" );
		TextUnformattedWrapped( "mirv_campath add    Add the current rendered camera as a keyframe." );
		TextUnformattedWrapped( "mirv_campath enable 0|1    Disable or enable path playback." );
		TextUnformattedWrapped( "mirv_campath print    Print keyframes and current time diagnostics." );
		TextUnformattedWrapped( "mirv_campath remove <index>    Remove one zero-based keyframe." );
		TextUnformattedWrapped( "mirv_campath clear    Clear selected keyframes, or the full path if none are selected." );
		TextUnformattedWrapped( "mirv_campath save <file.xml> / load <file.xml>" );
		ImGui::Spacing();

		TextUnformattedWrapped( "Drawing" );
		TextUnformattedWrapped( "mirv_campath draw enabled 0|1" );
		TextUnformattedWrapped( "mirv_campath draw keyAxis 0|1" );
		TextUnformattedWrapped( "mirv_campath draw keyCam 0|1" );
		TextUnformattedWrapped( "mirv_campath draw keyIndex <height>" );
		ImGui::Spacing();

		TextUnformattedWrapped( "Selection" );
		TextUnformattedWrapped( "mirv_campath select all|none|invert" );
		TextUnformattedWrapped( "mirv_campath select [add] <from> [to]    Select by keyframe ID/time range using the original HLAE syntax." );
		ImGui::Spacing();

		TextUnformattedWrapped( "Editing" );
		TextUnformattedWrapped( "mirv_campath edit start    Align the path or selection to current client/demo time." );
		TextUnformattedWrapped( "mirv_campath edit start abs <seconds>" );
		TextUnformattedWrapped( "mirv_campath edit start delta+<seconds> / delta-<seconds>" );
		TextUnformattedWrapped( "mirv_campath edit duration <seconds>" );
		TextUnformattedWrapped( "mirv_campath edit position current|<x|*> <y|*> <z|*>" );
		TextUnformattedWrapped( "mirv_campath edit angles current|<pitch|*> <yaw|*> <roll|*>" );
		TextUnformattedWrapped( "mirv_campath edit fov current|<degrees>" );
		TextUnformattedWrapped( "mirv_campath edit rotate <pitch> <yaw> <roll>" );
		TextUnformattedWrapped( "mirv_campath edit anchor #<keyframe>|<six anchor values> current|<six destination values>" );
		ImGui::Spacing();

		TextUnformattedWrapped( "Interpolation" );
		TextUnformattedWrapped( "mirv_campath edit interp position <mode>" );
		TextUnformattedWrapped( "mirv_campath edit interp rotation <mode>" );
		TextUnformattedWrapped( "mirv_campath edit interp fov <mode>" );
		TextDisabledWrapped( "Run the command without its final value to print the interpolation modes supported by the bundled AdvancedFX revision." );
		ImGui::EndChild();

		if ( ImGui::Button( "Print full original help", ImVec2( 190, 30 ) ) )
			IssueCommandWithSeparator( "mirv_campath" );
		HoverExplanation( "Print the complete original command usage to the Source console." );
		ImGui::SameLine();
		if ( ImGui::Button( "Close", ImVec2( 100, 30 ) ) )
			g_Gui.showHlaeCampathHelp = false;
		ImGui::End();
	}

	void DrawHlaeInputHelpWindow()
	{
		if ( !g_Gui.showHlaeInputHelp )
			return;

		ImGui::SetNextWindowSize( ImVec2( 820, 640 ), ImGuiCond_FirstUseEver );
		ImGui::SetNextWindowPos( ImVec2( 180, 110 ), ImGuiCond_FirstUseEver );
		if ( !ImGui::Begin( "mirv_input command help", &g_Gui.showHlaeInputHelp,
			ImGuiWindowFlags_NoCollapse ) )
		{
			ImGui::End();
			return;
		}

		TextDisabledWrapped( "Original AdvancedFX free-camera controls. Camera mode must be active for movement and rotation input." );
		ImGui::Separator();
		ImGui::BeginChild( "mirv_input_help_scroll", ImVec2( 0, -42 ),
			ImGuiChildFlags_Borders );
		TextUnformattedWrapped( "Camera and direct view" );
		TextUnformattedWrapped( "mirv_input camera / mirv_input end" );
		TextUnformattedWrapped( "mirv_input position <x> <y> <z>" );
		TextUnformattedWrapped( "mirv_input angles <pitch> <yaw> <roll>" );
		TextUnformattedWrapped( "mirv_input fov <internalFov>" );
		TextUnformattedWrapped( "mirv_input fov real <realHorizontalFov>" );
		ImGui::Spacing();

		TextUnformattedWrapped( "View-state memory" );
		TextUnformattedWrapped( "mirv_input mem store <name>" );
		TextUnformattedWrapped( "mirv_input mem use <name> [origin|angles|fov]" );
		TextUnformattedWrapped( "mirv_input mem remove <name> / print / clear" );
		TextUnformattedWrapped( "mirv_input mem save <file.xml> / load <file.xml>" );
		ImGui::Spacing();

		TextUnformattedWrapped( "Core configuration" );
		TextUnformattedWrapped( "mirv_input cfg offsetMode last|ownLast|game|current" );
		TextUnformattedWrapped( "mirv_input cfg mouseMoveSupport 0|1" );
		TextUnformattedWrapped( "mirv_input cfg msens <value>" );
		TextUnformattedWrapped( "mirv_input cfg ksens <value>" );
		TextUnformattedWrapped( "mirv_input cfg stepFactor <1..8>" );
		TextUnformattedWrapped( "mirv_input cfg rotLocalSpace 0|1" );
		ImGui::Spacing();

		TextUnformattedWrapped( "Smoothing" );
		TextUnformattedWrapped( "mirv_input cfg smooth enabled 0|1" );
		TextUnformattedWrapped( "mirv_input cfg smooth halfTime <seconds>" );
		TextUnformattedWrapped( "mirv_input cfg smooth halfTimeVec|halfTimeAng|halfTimeFov <seconds>" );
		TextUnformattedWrapped( "mirv_input cfg smooth rotShortestPath 0|1" );
		ImGui::Spacing();

		TextUnformattedWrapped( "Keyboard speeds" );
		TextUnformattedWrapped( "kForwardSpeed, kBackwardSpeed, kLeftSpeed, kRightSpeed, kUpSpeed, kDownSpeed" );
		TextUnformattedWrapped( "kPitchPositiveSpeed, kPitchNegativeSpeed, kYawPositiveSpeed, kYawNegativeSpeed" );
		TextUnformattedWrapped( "kRollPositiveSpeed, kRollNegativeSpeed, kFovPositiveSpeed, kFovNegativeSpeed" );
		TextDisabledWrapped( "Syntax: mirv_input cfg <name> <value>. Omit <value> to print the current setting." );
		ImGui::Spacing();

		TextUnformattedWrapped( "Mouse speeds" );
		TextUnformattedWrapped( "mYawSpeed, mPitchSpeed, mFovPositiveSpeed, mFovNegativeSpeed" );
		TextUnformattedWrapped( "mForwardSpeed, mBackSpeed, mLeftSpeed, mRightSpeed, mUpSpeed, mDownSpeed" );
		ImGui::Spacing();

		TextUnformattedWrapped( "Default controls" );
		TextUnformattedWrapped( "W/S/A/D move, R/F move up/down, arrow keys rotate, X/Z roll, Page Down/Page Up change FOV." );
		TextUnformattedWrapped( "Escape ends camera mode. Home or Num5 resets the view and speed. Num +/- changes speed. Ctrl passes the next key through." );
		TextDisabledWrapped( "For control while ART is visible, enable GUI passthrough, move the cursor outside the ART window, and hold the configured activation key." );
		ImGui::EndChild();

		if ( ImGui::Button( "Print full original help", ImVec2( 190, 30 ) ) )
			IssueCommandWithSeparator( "mirv_input" );
		HoverExplanation( "Print the complete original command usage to the Source console." );
		ImGui::SameLine();
		if ( ImGui::Button( "Close", ImVec2( 100, 30 ) ) )
			g_Gui.showHlaeInputHelp = false;
		ImGui::End();
	}

	void DrawCaptureHelpWindow()
	{
		if ( !g_Gui.showCaptureHelp )
			return;

		ImGui::SetNextWindowSize( ImVec2( 620, 470 ), ImGuiCond_FirstUseEver );
		ImGui::SetNextWindowPos( ImVec2( 170, 120 ), ImGuiCond_FirstUseEver );
		if ( !ImGui::Begin( "Capture Help", &g_Gui.showCaptureHelp, ImGuiWindowFlags_NoCollapse ) )
		{
			ImGui::End();
			return;
		}

		TextUnformattedWrapped( "Recording workflow" );
		ImGui::Separator();
		ImGui::BulletText( "Choose the output folder and optional filename prefix in Output." );
		ImGui::BulletText( "Enable the required passes and HUD layers in Passes." );
		ImGui::BulletText( "Set host_framerate. Use 0 for real-time rendering." );
		ImGui::BulletText( "Enter an optional take name and press Start recording." );
		ImGui::BulletText( "Press Stop recording before changing output settings." );

		ImGui::Spacing();
		TextUnformattedWrapped( "Passes" );
		ImGui::Separator();
		ImGui::TextWrapped( "Normal records the complete scene. Clear removes the first-person viewmodel. Clear - no players removes players and player-owned models as well. Viewmodel isolates the first-person model. Players isolates players and, when enabled, world weapon models. Depth records the fog-based depth approximation." );

		ImGui::Spacing();
		TextUnformattedWrapped( "Useful commands" );
		ImGui::Separator();
		TextUnformattedWrapped( "art_start [take]    Start recording" );
		TextUnformattedWrapped( "art_stop            Stop recording" );
		TextUnformattedWrapped( "art_toggle [take]   Toggle recording" );
		TextUnformattedWrapped( "art_status          Print current state" );
		TextUnformattedWrapped( "art_stats           Print footage statistics" );
		TextUnformattedWrapped( "art_validation      Validate latest take" );
		TextUnformattedWrapped( "art_help            Print complete console help" );

		ImGui::Spacing();
		if ( ImGui::Button( "Print full console help", ImVec2( 200, 0 ) ) )
			IssueCommandWithSeparator( "art_help" );
		ImGui::SameLine();
		if ( ImGui::Button( "Close", ImVec2( 100, 0 ) ) )
			g_Gui.showCaptureHelp = false;

		ImGui::End();
	}

	void DrawMainWindow()
	{
		ApplyThemeColors();
		ImGui::SetNextWindowSize( ImVec2( 1220, 760 ), ImGuiCond_FirstUseEver );
		ImGui::SetNextWindowPos( ImVec2( 70, 70 ), ImGuiCond_FirstUseEver );

		bool open = true;
		ImGui::SetNextWindowSizeConstraints( ImVec2( 900, 620 ), ImVec2( FLT_MAX, FLT_MAX ) );
		if ( !ImGui::Begin( V34_ART_PRODUCT_NAME " V" V34_ART_VERSION_STRING, &open, ImGuiWindowFlags_NoCollapse ) )
		{
			ImGui::End();
			if ( !open ) SetArtGuiVisible( false );
			return;
		}

		const ImVec2 mainWindowPosition = ImGui::GetWindowPos();
		const ImVec2 mainWindowSize = ImGui::GetWindowSize();
		g_Gui.mainWindowX = mainWindowPosition.x;
		g_Gui.mainWindowY = mainWindowPosition.y;
		g_Gui.mainWindowWidth = mainWindowSize.x;
		g_Gui.mainWindowHeight = mainWindowSize.y;

		const bool recording = InterlockedCompareExchange( &g_bRecording, FALSE, FALSE ) != FALSE;
		ImGui::SetCursorPosX( ImGui::GetWindowWidth() - 180 );
		StatusBadge( recording, "REC", "READY" );
		ImGui::SameLine();
		TextDisabledWrapped( "#%04d", g_nFrame );
		ImGui::Separator();

		const float sidebarWidth = 174.0f;
		ImGui::PushStyleColor( ImGuiCol_ChildBg, GuiColor( g_Gui.sidebarColor ) );
		ImGui::BeginChild( "sidebar", ImVec2( sidebarWidth, 0 ), ImGuiChildFlags_Borders );
		static const char *pages[] = { "Capture", "Passes", "Visuals", "Output", "Info", "HLAE", "Configs", "Console", "Settings" };
		for ( int i = 0; i < ARRAYSIZE( pages ); ++i )
		{
			if ( ImGui::Selectable( pages[i], g_Gui.selectedPage == i, 0, ImVec2( 0, 38 ) ) )
			{
				g_Gui.selectedPage = i;
				if ( i == 4 )
					g_Gui.resetInfoScroll = true;
			}
		}
		ImGui::SetCursorPosY( ImGui::GetWindowHeight() - 31 );
		char toggleName[96];
		GetToggleBindingName( g_Gui.toggleKey, g_Gui.toggleModifiers, toggleName, sizeof( toggleName ), true );
		TextDisabledWrapped( "Menu: [%s]", toggleName );
		ImGui::EndChild();
		ImGui::PopStyleColor();
		ImGui::SameLine();
		ImGui::BeginChild( "content", ImVec2( 0, 0 ), ImGuiChildFlags_Borders );

		switch ( g_Gui.selectedPage )
		{
		case 0: DrawCapturePage(); break;
		case 1: DrawPassesPage(); break;
		case 2: DrawVisualsPage(); break;
		case 3: DrawOutputPage(); break;
		case 4: DrawInfoPage(); break;
		case 5: DrawHlaePage(); break;
		case 6: DrawConfigsPage(); break;
		case 7: DrawConsolePage(); break;
		case 8: DrawSettingsPage(); break;
		default: break;
		}

		if ( g_Gui.lastError[0] )
		{
			ImGui::SetCursorPosY( ImGui::GetWindowHeight() - 42 );
			ImGui::Separator();
			ImGui::TextColored( ImVec4( 1.0f, 0.36f, 0.40f, 1.0f ), "%s", g_Gui.lastError );
		}
		ImGui::EndChild();
		ImGui::End();

		if ( !open )
			SetArtGuiVisible( false );
	}

	// -------------------------------------------------------------------------
	// Capture-excluded overlays
	// -------------------------------------------------------------------------
	void DrawRecordingStatisticsOverlay()
	{
		const bool recording = InterlockedCompareExchange( &g_bRecording, FALSE, FALSE ) != FALSE;
		const bool validating = IsArtValidationRunning();
		char bytes[48];
		FormatArtByteCount( g_ArtRecordingStats.takeBytes, bytes, sizeof( bytes ) );
		char text[1024];
		if ( recording )
		{
			const DWORD elapsedMs = GetArtRecordingElapsedMs();
			const DWORD elapsedSeconds = elapsedMs / 1000;
			const LONG mask = InterlockedCompareExchange( &g_nRecordMask, 0, 0 );
			const std::string passNames = MaskNames( mask );
			char queueBytes[48];
			FormatArtByteCount( g_ArtPipelineStats.pendingBytes,
				queueBytes, sizeof( queueBytes ) );
			const double captureRate = elapsedMs ?
				static_cast<double>( g_ArtRecordingStats.takeFrames ) * 1000.0 / elapsedMs : 0.0;
			double averageMs[ART_TIMING_COUNT] = {};
			for ( int i = 0; i < ART_TIMING_COUNT; ++i )
			{
				const ArtStageTimingStatistics &timing = g_ArtPipelineStats.stages[i];
				averageMs[i] = timing.takeSamples ?
					static_cast<double>( timing.takeTotalMicroseconds ) /
						timing.takeSamples / 1000.0 : 0.0;
			}
			const double compressionSaved = g_ArtPipelineStats.takeUncompressedBytes ?
				100.0 * ( 1.0 -
					static_cast<double>( g_ArtPipelineStats.takeOutputBytes ) /
					static_cast<double>( g_ArtPipelineStats.takeUncompressedBytes ) ) : 0.0;
			_snprintf_s( text, sizeof( text ), _TRUNCATE,
				"ART RECORDING\nTake: %s\nFrame %04d  |  %lu files  |  %s  |  %.2f fps\n"
				"%02lu:%02lu:%02lu  |  Passes: %s\n"
				"Queue: %lu/%ld files  |  %s/%ld MiB  |  %lu flushes\n"
				"Average ms: render %.2f  read %.2f  encode %.2f  write %.2f  queue %.2f\n"
				"TGA: %s  |  %.1f%% saved  |  allocation retries %lu",
				g_ArtRecordingStats.takeDisplayPath[0] ?
					g_ArtRecordingStats.takeDisplayPath : "<starting>",
				g_nFrame, g_ArtRecordingStats.takeFiles, bytes,
				captureRate, elapsedSeconds / 3600, ( elapsedSeconds / 60 ) % 60,
				elapsedSeconds % 60, passNames.c_str(),
				g_ArtPipelineStats.pendingFiles,
				InterlockedCompareExchange( &g_ArtQueueOptions.maxFiles, 0, 0 ),
				queueBytes,
				InterlockedCompareExchange( &g_ArtQueueOptions.maxMegabytes, 0, 0 ),
				g_ArtPipelineStats.takeFlushes,
				averageMs[ART_TIMING_RENDER], averageMs[ART_TIMING_READ],
				averageMs[ART_TIMING_ENCODE], averageMs[ART_TIMING_WRITE],
				averageMs[ART_TIMING_QUEUE],
				ArtTgaCompressionModeName( InterlockedCompareExchange(
					&g_nArtTgaCompressionMode, 0, 0 ) ),
				compressionSaved, g_ArtPipelineStats.takeAllocationRetries );
		}
		else if ( validating )
		{
			const LONG completed = InterlockedCompareExchange(
				&g_ArtValidationProgress.completedFiles, 0, 0 );
			const LONG total = InterlockedCompareExchange(
				&g_ArtValidationProgress.totalFiles, 0, 0 );
			_snprintf_s( text, sizeof( text ), _TRUNCATE,
				"ART VALIDATING\n%s\n%ld / %ld files  |  %.1f%%  |  %.2f s",
				ArtValidationPhaseName( InterlockedCompareExchange(
					&g_ArtValidationProgress.phase, 0, 0 ) ),
				completed, total, GetArtValidationProgressFraction() * 100.0f,
				GetArtValidationElapsedMs() / 1000.0f );
		}
		else if ( g_ArtRecordingStats.takeDisplayPath[0] )
		{
			const char *pValidation = !g_ArtValidationResult.hasResult ? "not validated" :
				g_ArtValidationResult.passed ? "validation PASS" : "validation FAIL";
			_snprintf_s( text, sizeof( text ), _TRUNCATE,
				"ART READY\nLast take: %lu frames  |  %lu files  |  %s\n%s",
				g_ArtRecordingStats.takeFrames, g_ArtRecordingStats.takeFiles, bytes, pValidation );
		}
		else
		{
			Q_strncpy( text, "ART READY\nNo footage recorded this session.", sizeof( text ) );
		}

		ArtHlaeStatus hlaeStatus;
		GetArtHlaeStatus( hlaeStatus );
		const bool hlaeFileActivity = hlaeStatus.camioExporting ||
			hlaeStatus.camioImporting || hlaeStatus.agrRecording ||
			hlaeStatus.camexportRecording || hlaeStatus.camimportActive;
		const bool hlaeCameraActivity = hlaeStatus.campathEnabled ||
			hlaeStatus.inputCamera || hlaeFileActivity;
		std::string overlayText( text );
		if ( hlaeStatus.enabled && hlaeCameraActivity )
		{
			AppendFormat( overlayText,
				"\nHLAE: campath %s | mirv_input %s",
				hlaeStatus.campathEnabled ? "ON" : "off",
				hlaeStatus.inputCamera ? "ON" : "off" );
			AppendFormat( overlayText,
				"\nCAM exp %s / imp %s | AGR %s | BVH exp %s / imp %s",
				hlaeStatus.camioExporting ? "ON" : "off",
				hlaeStatus.camioImporting ? "ON" : "off",
				hlaeStatus.agrRecording ? "REC" : "off",
				hlaeStatus.camexportRecording ? "ON" : "off",
				hlaeStatus.camimportActive ? "ON" : "off" );
		}

		const ImVec2 position( 13.0f, 13.0f );
		ImDrawList *pDrawList = ImGui::GetForegroundDrawList();
		pDrawList->AddText( ImVec2( position.x + 1.0f, position.y + 1.0f ),
			IM_COL32( 0, 0, 0, 220 ), overlayText.c_str() );
		pDrawList->AddText( position, recording || hlaeFileActivity ?
			IM_COL32( 255, 58, 42, 255 ) : IM_COL32( 255, 145, 32, 255 ),
			overlayText.c_str() );
		if ( validating )
		{
			const ImVec2 textSize = ImGui::CalcTextSize( overlayText.c_str() );
			const ImVec2 barMin( position.x, position.y + textSize.y + 5.0f );
			const ImVec2 barMax( position.x + 285.0f, barMin.y + 7.0f );
			const float fraction = GetArtValidationProgressFraction();
			pDrawList->AddRectFilled( barMin, barMax, IM_COL32( 24, 16, 12, 220 ), 2.0f );
			pDrawList->AddRectFilled( barMin,
				ImVec2( barMin.x + ( barMax.x - barMin.x ) * fraction, barMax.y ),
				IM_COL32( 255, 116, 38, 255 ), 2.0f );
			pDrawList->AddRect( barMin, barMax, IM_COL32( 255, 156, 72, 230 ), 2.0f );
		}
	}

	struct CampathClipPoint
	{
		double x;
		double y;
		double w;
	};

	CampathClipPoint TransformCampathPoint( double x, double y, double z,
		const VMatrix &matrix )
	{
		CampathClipPoint result;
		result.x = matrix[0][0] * x + matrix[0][1] * y +
			matrix[0][2] * z + matrix[0][3];
		result.y = matrix[1][0] * x + matrix[1][1] * y +
			matrix[1][2] * z + matrix[1][3];
		result.w = matrix[3][0] * x + matrix[3][1] * y +
			matrix[3][2] * z + matrix[3][3];
		return result;
	}

	bool ClipCampathScreenLine( ImVec2 &from, ImVec2 &to,
		const ImVec2 &displaySize )
	{
		const float minimumX = -2.0f;
		const float minimumY = -2.0f;
		const float maximumX = displaySize.x + 2.0f;
		const float maximumY = displaySize.y + 2.0f;
		const float dx = to.x - from.x;
		const float dy = to.y - from.y;
		float begin = 0.0f;
		float end = 1.0f;

		const float p[4] = { -dx, dx, -dy, dy };
		const float q[4] =
		{
			from.x - minimumX,
			maximumX - from.x,
			from.y - minimumY,
			maximumY - from.y
		};
		for ( int i = 0; i < 4; ++i )
		{
			if ( fabsf( p[i] ) < 0.000001f )
			{
				if ( q[i] < 0.0f ) return false;
				continue;
			}
			const float ratio = q[i] / p[i];
			if ( p[i] < 0.0f )
			{
				if ( end < ratio ) return false;
				if ( begin < ratio ) begin = ratio;
			}
			else
			{
				if ( ratio < begin ) return false;
				if ( ratio < end ) end = ratio;
			}
		}
		const ImVec2 originalFrom = from;
		from.x = originalFrom.x + dx * begin;
		from.y = originalFrom.y + dy * begin;
		to.x = originalFrom.x + dx * end;
		to.y = originalFrom.y + dy * end;
		return true;
	}

	bool ProjectCampathWorldSegment( double x1, double y1, double z1,
		double x2, double y2, double z2, const VMatrix &matrix,
		const ImVec2 &displaySize, ImVec2 &from, ImVec2 &to )
	{
		static const double kNearW = 0.001;
		CampathClipPoint first = TransformCampathPoint( x1, y1, z1, matrix );
		CampathClipPoint second = TransformCampathPoint( x2, y2, z2, matrix );
		if ( first.w <= kNearW && second.w <= kNearW )
			return false;

		if ( first.w <= kNearW || second.w <= kNearW )
		{
			CampathClipPoint *pBehind = first.w <= kNearW ? &first : &second;
			const CampathClipPoint &front = first.w <= kNearW ? second : first;
			const double denominator = front.w - pBehind->w;
			if ( fabs( denominator ) <= 0.000000001 )
				return false;
			const double fraction = ( kNearW - pBehind->w ) / denominator;
			pBehind->x += ( front.x - pBehind->x ) * fraction;
			pBehind->y += ( front.y - pBehind->y ) * fraction;
			pBehind->w = kNearW;
		}

		from.x = static_cast<float>( displaySize.x * 0.5 *
			( 1.0 + first.x / first.w ) );
		from.y = static_cast<float>( displaySize.y * 0.5 *
			( 1.0 - first.y / first.w ) );
		to.x = static_cast<float>( displaySize.x * 0.5 *
			( 1.0 + second.x / second.w ) );
		to.y = static_cast<float>( displaySize.y * 0.5 *
			( 1.0 - second.y / second.w ) );
		return ClipCampathScreenLine( from, to, displaySize );
	}

	bool ProjectCampathWorldPoint( double x, double y, double z,
		const VMatrix &matrix, const ImVec2 &displaySize, ImVec2 &screen )
	{
		const CampathClipPoint point = TransformCampathPoint( x, y, z, matrix );
		if ( point.w <= 0.001 )
			return false;
		screen.x = static_cast<float>( displaySize.x * 0.5 *
			( 1.0 + point.x / point.w ) );
		screen.y = static_cast<float>( displaySize.y * 0.5 *
			( 1.0 - point.y / point.w ) );
		return screen.x > -256.0f && screen.x < displaySize.x + 256.0f &&
			screen.y > -256.0f && screen.y < displaySize.y + 256.0f;
	}

	bool ProjectCampathPoint( const ArtHlaeCampathDrawPoint &point,
		const VMatrix &matrix, const ImVec2 &displaySize, ImVec2 &screen )
	{
		return ProjectCampathWorldPoint( point.x, point.y, point.z,
			matrix, displaySize, screen );
	}

	unsigned char CampathColorComponent( double value, bool selected )
	{
		if ( value < 0.0 ) value = 0.0;
		if ( 255.0 < value ) value = 255.0;
		const unsigned char component = static_cast<unsigned char>( value );
		return selected ? static_cast<unsigned char>( 255 - component ) : component;
	}

	ImU32 CampathTimeColor( const ArtHlaeCampathDrawPoint &point,
		double currentPathTime )
	{
		const double deltaTime = fabs( currentPathTime - point.time );
		unsigned char r = 255;
		unsigned char g = 0;
		unsigned char b = 0;
		unsigned char a = 128;
		if ( deltaTime < 1.0 )
		{
			const double t = deltaTime;
			r = CampathColorComponent( 255.0 * t, point.selected );
			g = CampathColorComponent( 255.0, point.selected );
			b = CampathColorComponent( 0.0, point.selected );
			a = static_cast<unsigned char>( 127.0 * ( 1.0 - t ) + 128.0 );
		}
		else if ( deltaTime < 2.0 )
		{
			const double t = deltaTime - 1.0;
			r = CampathColorComponent( 255.0, point.selected );
			g = CampathColorComponent( 255.0 * ( 1.0 - t ), point.selected );
			b = CampathColorComponent( 0.0, point.selected );
			a = static_cast<unsigned char>( 64.0 * ( 1.0 - t ) + 128.0 );
		}
		else
		{
			r = CampathColorComponent( 255.0, point.selected );
			g = CampathColorComponent( 0.0, point.selected );
			b = CampathColorComponent( 0.0, point.selected );
		}
		return IM_COL32( r, g, b, a );
	}

	void DrawCampathGradientLine( ImDrawList *pDrawList, const ImVec2 &from,
		const ImVec2 &to, ImU32 fromColor, ImU32 toColor, float width )
	{
		const float dx = to.x - from.x;
		const float dy = to.y - from.y;
		const float lengthSquared = dx * dx + dy * dy;
		if ( lengthSquared <= 0.0001f )
			return;
		const float inverseLength = 1.0f / sqrtf( lengthSquared );
		const float halfWidth = width * 0.5f;
		const ImVec2 normal( -dy * inverseLength * halfWidth,
			dx * inverseLength * halfWidth );
		const ImVec2 uv = ImGui::GetFontTexUvWhitePixel();
		pDrawList->PrimReserve( 6, 4 );
		const ImDrawIdx baseIndex = static_cast<ImDrawIdx>( pDrawList->_VtxCurrentIdx );
		pDrawList->PrimWriteIdx( baseIndex );
		pDrawList->PrimWriteIdx( static_cast<ImDrawIdx>( baseIndex + 1 ) );
		pDrawList->PrimWriteIdx( static_cast<ImDrawIdx>( baseIndex + 2 ) );
		pDrawList->PrimWriteIdx( baseIndex );
		pDrawList->PrimWriteIdx( static_cast<ImDrawIdx>( baseIndex + 2 ) );
		pDrawList->PrimWriteIdx( static_cast<ImDrawIdx>( baseIndex + 3 ) );
		pDrawList->PrimWriteVtx( ImVec2( from.x + normal.x, from.y + normal.y ),
			uv, fromColor );
		pDrawList->PrimWriteVtx( ImVec2( to.x + normal.x, to.y + normal.y ),
			uv, toColor );
		pDrawList->PrimWriteVtx( ImVec2( to.x - normal.x, to.y - normal.y ),
			uv, toColor );
		pDrawList->PrimWriteVtx( ImVec2( from.x - normal.x, from.y - normal.y ),
			uv, fromColor );
	}

	void DrawCampathWorldGradientLine( ImDrawList *pDrawList,
		const VMatrix &matrix, const ImVec2 &displaySize,
		double x1, double y1, double z1, double x2, double y2, double z2,
		ImU32 fromColor, ImU32 toColor, float width )
	{
		ImVec2 from;
		ImVec2 to;
		if ( !ProjectCampathWorldSegment( x1, y1, z1, x2, y2, z2,
			matrix, displaySize, from, to ) )
			return;
		DrawCampathGradientLine( pDrawList, from, to,
			IM_COL32( 0, 0, 0, 180 ), IM_COL32( 0, 0, 0, 180 ), width + 3.0f );
		DrawCampathGradientLine( pDrawList, from, to,
			fromColor, toColor, width );
	}

	void DrawCampathWorldLine( ImDrawList *pDrawList, const VMatrix &matrix,
		const ImVec2 &displaySize, double x1, double y1, double z1,
		double x2, double y2, double z2, ImU32 color, float width )
	{
		DrawCampathWorldGradientLine( pDrawList, matrix, displaySize,
			x1, y1, z1, x2, y2, z2, color, color, width );
	}

	void DrawCampathCamera( ImDrawList *pDrawList, const VMatrix &matrix,
		const ImVec2 &displaySize, const ArtHlaeCampathDrawPoint &point,
		ImU32 color )
	{
		static const double kPi = 3.14159265358979323846;
		static const double kCameraRadius = 18.0;
		double forward[3];
		double right[3];
		double up[3];
		Afx::Math::MakeVectors( point.roll, point.pitch, point.yaw,
			forward, right, up );
		const double fov = point.fov < 1.0f ? 1.0 :
			( 179.0f < point.fov ? 179.0 : point.fov );
		const double a = sin( fov * kPi / 360.0 ) * kCameraRadius;
		const double aspect = displaySize.x > 0.0f ?
			static_cast<double>( displaySize.y ) / displaySize.x : 1.0;
		const double b = a * aspect;

		struct WorldPoint { double x, y, z; };
		const WorldPoint cp = { point.x, point.y, point.z };
		WorldPoint lu = { cp.x + kCameraRadius * forward[0] - a * right[0] + b * up[0],
			cp.y + kCameraRadius * forward[1] - a * right[1] + b * up[1],
			cp.z + kCameraRadius * forward[2] - a * right[2] + b * up[2] };
		WorldPoint ru = { cp.x + kCameraRadius * forward[0] + a * right[0] + b * up[0],
			cp.y + kCameraRadius * forward[1] + a * right[1] + b * up[1],
			cp.z + kCameraRadius * forward[2] + a * right[2] + b * up[2] };
		WorldPoint ld = { cp.x + kCameraRadius * forward[0] - a * right[0] - b * up[0],
			cp.y + kCameraRadius * forward[1] - a * right[1] - b * up[1],
			cp.z + kCameraRadius * forward[2] - a * right[2] - b * up[2] };
		WorldPoint rd = { cp.x + kCameraRadius * forward[0] + a * right[0] - b * up[0],
			cp.y + kCameraRadius * forward[1] + a * right[1] - b * up[1],
			cp.z + kCameraRadius * forward[2] + a * right[2] - b * up[2] };
		WorldPoint mu = { ( lu.x + ru.x ) * 0.5, ( lu.y + ru.y ) * 0.5,
			( lu.z + ru.z ) * 0.5 };
		WorldPoint muu = { mu.x + 0.5 * b * up[0], mu.y + 0.5 * b * up[1],
			mu.z + 0.5 * b * up[2] };

		#define ART_DRAW_CAMPATH_LINE( from, to ) DrawCampathWorldLine( pDrawList, matrix, displaySize, from.x, from.y, from.z, to.x, to.y, to.z, color, 4.0f )
		ART_DRAW_CAMPATH_LINE( cp, ld );
		ART_DRAW_CAMPATH_LINE( cp, rd );
		ART_DRAW_CAMPATH_LINE( cp, lu );
		ART_DRAW_CAMPATH_LINE( cp, ru );
		ART_DRAW_CAMPATH_LINE( ld, rd );
		ART_DRAW_CAMPATH_LINE( rd, ru );
		ART_DRAW_CAMPATH_LINE( ru, lu );
		ART_DRAW_CAMPATH_LINE( lu, ld );
		ART_DRAW_CAMPATH_LINE( lu, muu );
		ART_DRAW_CAMPATH_LINE( ru, muu );
		#undef ART_DRAW_CAMPATH_LINE
	}

	void DrawHlaeCampathOverlay()
	{
		if ( !g_pEngine || !IsArtHlaeCampathDrawingEnabled() )
			return;

		double currentPathTime = 0.0;
		const size_t keyframeCount =
			GetArtHlaeCampathDrawPoints( NULL, 0, currentPathTime );
		if ( !keyframeCount )
			return;
		std::vector<ArtHlaeCampathDrawPoint> keyframes( keyframeCount );
		GetArtHlaeCampathDrawPoints( &keyframes[0], keyframeCount, currentPathTime );

		double trajectoryTime = currentPathTime;
		const size_t trajectoryCount =
			GetArtHlaeCampathTrajectoryPoints( NULL, 0, trajectoryTime );
		std::vector<ArtHlaeCampathDrawPoint> trajectory( trajectoryCount );
		if ( trajectoryCount )
			GetArtHlaeCampathTrajectoryPoints( &trajectory[0], trajectoryCount,
				trajectoryTime );
		currentPathTime = trajectoryTime;

		ArtHlaeStatus status;
		GetArtHlaeStatus( status );
		const VMatrix &matrix = g_pEngine->WorldToScreenMatrix();
		const ImVec2 displaySize = ImGui::GetIO().DisplaySize;
		ImDrawList *pDrawList = ImGui::GetForegroundDrawList();

		for ( size_t i = 1; i < trajectory.size(); ++i )
		{
			const ArtHlaeCampathDrawPoint &from = trajectory[i - 1];
			const ArtHlaeCampathDrawPoint &to = trajectory[i];
			DrawCampathWorldGradientLine( pDrawList, matrix, displaySize,
				from.x, from.y, from.z, to.x, to.y, to.z,
				CampathTimeColor( from, currentPathTime ),
				CampathTimeColor( to, currentPathTime ), 7.0f );
		}

		// When the selected interpolation cannot evaluate yet, still show the
		// keyframe connection so the user can see where additional points are needed.
		if ( trajectory.size() < 2 && keyframes.size() >= 2 )
		{
			for ( size_t i = 1; i < keyframes.size(); ++i )
			{
				const ArtHlaeCampathDrawPoint &from = keyframes[i - 1];
				const ArtHlaeCampathDrawPoint &to = keyframes[i];
				DrawCampathWorldGradientLine( pDrawList, matrix, displaySize,
					from.x, from.y, from.z, to.x, to.y, to.z,
					IM_COL32( 255, 145, 32, 180 ), IM_COL32( 255, 145, 32, 180 ), 4.0f );
			}
		}

		const double axisRadius = 36.0;
		const float indexSize = status.campathDrawKeyIndex < 0.0f ?
			0.0f : status.campathDrawKeyIndex;
		for ( size_t i = 0; i < keyframes.size(); ++i )
		{
			const ArtHlaeCampathDrawPoint &point = keyframes[i];
			const ImU32 color = CampathTimeColor( point, currentPathTime );
			if ( status.campathDrawKeyAxis )
			{
				DrawCampathWorldLine( pDrawList, matrix, displaySize,
					point.x - axisRadius, point.y, point.z,
					point.x + axisRadius, point.y, point.z, color, 4.0f );
				DrawCampathWorldLine( pDrawList, matrix, displaySize,
					point.x, point.y - axisRadius, point.z,
					point.x, point.y + axisRadius, point.z, color, 4.0f );
				DrawCampathWorldLine( pDrawList, matrix, displaySize,
					point.x, point.y, point.z - axisRadius,
					point.x, point.y, point.z + axisRadius, color, 4.0f );
			}
			if ( status.campathDrawKeyCam )
				DrawCampathCamera( pDrawList, matrix, displaySize, point, color );
			if ( indexSize > 0.0f )
			{
				ImVec2 screen;
				if ( ProjectCampathPoint( point, matrix, displaySize, screen ) )
				{
					char label[24];
					_snprintf_s( label, sizeof( label ), _TRUNCATE, "%u",
						static_cast<unsigned int>( i ) );
					pDrawList->AddText( ImGui::GetFont(), indexSize,
						ImVec2( screen.x - ImGui::CalcTextSize( label ).x * 0.5f,
							screen.y - indexSize * 0.5f ), color, label );
				}
			}
		}

		ArtHlaeCampathDrawPoint currentCamera;
		bool campathEnabled = false;
		if ( GetArtHlaeCampathCurrentCamera( currentCamera, campathEnabled ) )
		{
			const unsigned char r = CampathColorComponent( 255, currentCamera.selected );
			const unsigned char g = CampathColorComponent(
				campathEnabled ? 0 : 255, currentCamera.selected );
			const unsigned char b = CampathColorComponent( 255, currentCamera.selected );
			DrawCampathCamera( pDrawList, matrix, displaySize, currentCamera,
				IM_COL32( r, g, b, 128 ) );
		}
	}

	void RenderGuiFrame( IDirect3DDevice9 *pDevice )
	{
		if ( InterlockedCompareExchange( &g_bArtGuiTerminating, FALSE, FALSE ) )
			return;
		if ( InterlockedCompareExchange( &g_nGuiFrameActive, TRUE, FALSE ) != FALSE )
			return;

		const bool mainVisible = IsArtGuiVisible();
		const bool overlayVisible = InterlockedCompareExchange(
			&g_bArtStatisticsOverlayEnabled, FALSE, FALSE ) != FALSE;
		const bool campathVisible = IsArtHlaeCampathDrawingEnabled();
		if ( !mainVisible && !overlayVisible && !campathVisible )
		{
			InterlockedExchange( &g_nGuiFrameActive, FALSE );
			return;
		}

		if ( InterlockedCompareExchange( &g_bArtGuiTerminating, FALSE, FALSE ) ||
			( g_Gui.imguiReady && pDevice != g_pGuiDevice ) ||
			!InitializeImGui( pDevice ) )
		{
			InterlockedExchange( &g_nGuiFrameActive, FALSE );
			return;
		}

		if ( mainVisible && g_Gui.refreshDelay > 0 && --g_Gui.refreshDelay == 0 )
		{
			g_Gui.refreshConfigs = true;
			SyncTextFieldsFromGame();
		}

		if ( mainVisible )
		{
			UpdateMouseCaptureMode();
			RefreshHlaeInputWhileGuiActive( true );
		}
		else
			SetHlaeInputWhileGuiActive( false );
		ImGui::GetIO().MouseDrawCursor = mainVisible && !g_Gui.hlaeInputWhileGuiActive;
		ImGui_ImplDX9_NewFrame();
		ImGui_ImplWin32_NewFrame();
		ImGui::NewFrame();
		if ( mainVisible )
		{
			DrawMainWindow();
			DrawCaptureHelpWindow();
			DrawHlaeCampathHelpWindow();
			DrawHlaeInputHelpWindow();
		}
		if ( overlayVisible )
			DrawRecordingStatisticsOverlay();
		if ( campathVisible )
			DrawHlaeCampathOverlay();
		ImGui::Render();
		ImGui_ImplDX9_RenderDrawData( ImGui::GetDrawData() );
		InterlockedIncrement( &g_nGuiRenderedFrames );
		InterlockedExchange( &g_nGuiFrameActive, FALSE );
	}

	HRESULT WINAPI HookedReset( IDirect3DDevice9 *pDevice, D3DPRESENT_PARAMETERS *pParameters )
	{
		if ( InterlockedCompareExchange( &g_bArtGuiTerminating, FALSE, FALSE ) )
			return g_pOriginalReset ? g_pOriginalReset( pDevice, pParameters ) : D3DERR_INVALIDCALL;
		const bool guiDevice = g_Gui.imguiReady && pDevice == g_pGuiDevice;
		if ( guiDevice )
			ImGui_ImplDX9_InvalidateDeviceObjects();
		const HRESULT result = g_pOriginalReset ? g_pOriginalReset( pDevice, pParameters ) : D3DERR_INVALIDCALL;
		if ( SUCCEEDED( result ) && guiDevice )
			ImGui_ImplDX9_CreateDeviceObjects();
		return result;
	}

	HRESULT WINAPI HookedEndScene( IDirect3DDevice9 *pDevice )
	{
		if ( InterlockedCompareExchange( &g_bArtGuiTerminating, FALSE, FALSE ) )
			return g_pOriginalEndScene ? g_pOriginalEndScene( pDevice ) : D3D_OK;
		UpdateDemoTickTracking();
		if ( InterlockedCompareExchange( &g_nPresentFallbackGuard, 0, 0 ) == 0 )
		{
			InterlockedIncrement( &g_nEndSceneHookCalls );
			RenderGuiFrame( pDevice );
			if ( g_Gui.imguiReady && pDevice == g_pGuiDevice )
				InterlockedIncrement( &g_nPrimaryEndSceneFrames );
		}
		return g_pOriginalEndScene ? g_pOriginalEndScene( pDevice ) : D3D_OK;
	}

	void RenderPresentFallback( IDirect3DDevice9 *pDevice )
	{
		if ( InterlockedCompareExchange( &g_bArtGuiTerminating, FALSE, FALSE ) )
			return;
		if ( !pDevice || InterlockedCompareExchange( &g_nPrimaryEndSceneFrames, 0, 0 ) != 0 )
			return;
		if ( InterlockedCompareExchange( &g_nPresentFallbackGuard, 1, 0 ) != 0 )
			return;

		const HRESULT beginResult = pDevice->BeginScene();
		if ( SUCCEEDED( beginResult ) )
		{
			RenderGuiFrame( pDevice );
			pDevice->EndScene();
			if ( g_Gui.imguiReady && pDevice == g_pGuiDevice )
				InterlockedIncrement( &g_nPresentFallbackFrames );
		}
		InterlockedExchange( &g_nPresentFallbackGuard, 0 );
	}

	HRESULT WINAPI HookedPresent( IDirect3DDevice9 *pDevice, const RECT *pSourceRect,
		const RECT *pDestRect, HWND hDestWindowOverride, const RGNDATA *pDirtyRegion )
	{
		if ( InterlockedCompareExchange( &g_bArtGuiTerminating, FALSE, FALSE ) )
			return g_pOriginalPresent ? g_pOriginalPresent( pDevice, pSourceRect, pDestRect,
				hDestWindowOverride, pDirtyRegion ) : D3D_OK;
		InterlockedIncrement( &g_nPresentHookCalls );
		const bool outermostPresent = InterlockedIncrement( &g_nPresentNesting ) == 1;
		if ( outermostPresent )
			RenderPresentFallback( pDevice );
		const HRESULT result = g_pOriginalPresent ? g_pOriginalPresent( pDevice, pSourceRect, pDestRect,
			hDestWindowOverride, pDirtyRegion ) : D3D_OK;
		InterlockedDecrement( &g_nPresentNesting );
		return result;
	}

	HRESULT WINAPI HookedSwapChainPresent( IDirect3DSwapChain9 *pSwapChain, const RECT *pSourceRect,
		const RECT *pDestRect, HWND hDestWindowOverride, const RGNDATA *pDirtyRegion, DWORD flags )
	{
		if ( InterlockedCompareExchange( &g_bArtGuiTerminating, FALSE, FALSE ) )
			return g_pOriginalSwapChainPresent ? g_pOriginalSwapChainPresent( pSwapChain,
				pSourceRect, pDestRect, hDestWindowOverride, pDirtyRegion, flags ) : D3D_OK;
		InterlockedIncrement( &g_nSwapChainPresentHookCalls );
		const bool outermostPresent = InterlockedIncrement( &g_nPresentNesting ) == 1;
		if ( outermostPresent && pSwapChain )
		{
			IDirect3DDevice9 *pDevice = NULL;
			if ( SUCCEEDED( pSwapChain->GetDevice( &pDevice ) ) && pDevice )
			{
				RenderPresentFallback( pDevice );
				pDevice->Release();
			}
		}
		const HRESULT result = g_pOriginalSwapChainPresent ? g_pOriginalSwapChainPresent( pSwapChain,
			pSourceRect, pDestRect, hDestWindowOverride, pDirtyRegion, flags ) : D3D_OK;
		InterlockedDecrement( &g_nPresentNesting );
		return result;
	}

	BOOL WINAPI HookedGetCursorPos( POINT *pPoint )
	{
		const BOOL result = g_pOriginalGetCursorPos ?
			g_pOriginalGetCursorPos( pPoint ) : FALSE;
		if ( !result || !pPoint ||
			InterlockedCompareExchange( &g_bArtGuiTerminating, FALSE, FALSE ) )
			return result;

		LONG x = pPoint->x;
		LONG y = pPoint->y;
		if ( CaptureArtHlaeCursorPosition( x, y ) )
		{
			pPoint->x = x;
			pPoint->y = y;
		}
		return result;
	}

	BOOL WINAPI HookedSetCursorPos( int x, int y )
	{
		if ( InterlockedCompareExchange( &g_bArtGuiTerminating, FALSE, FALSE ) )
			return g_pOriginalSetCursorPos ? g_pOriginalSetCursorPos( x, y ) : FALSE;
		if ( GuiOwnsMouse() )
			return TRUE;
		const BOOL result = g_pOriginalSetCursorPos ?
			g_pOriginalSetCursorPos( x, y ) : FALSE;
		if ( result )
		{
			POINT actual = { x, y };
			if ( g_pOriginalGetCursorPos )
				g_pOriginalGetCursorPos( &actual );
			NotifyArtHlaeCursorWarp( actual.x, actual.y );
		}
		return result;
	}

	BOOL WINAPI HookedClipCursor( const RECT *pRect )
	{
		if ( InterlockedCompareExchange( &g_bArtGuiTerminating, FALSE, FALSE ) )
			return g_pOriginalClipCursor ? g_pOriginalClipCursor( pRect ) : FALSE;
		if ( GuiOwnsMouse() )
			return g_pOriginalClipCursor ? g_pOriginalClipCursor( NULL ) : TRUE;
		return g_pOriginalClipCursor ? g_pOriginalClipCursor( pRect ) : FALSE;
	}

	HCURSOR WINAPI HookedSetCursor( HCURSOR hCursor )
	{
		if ( InterlockedCompareExchange( &g_bArtGuiTerminating, FALSE, FALSE ) )
			return g_pOriginalSetCursor ? g_pOriginalSetCursor( hCursor ) : NULL;
		if ( GuiOwnsMouse() )
			return g_pOriginalSetCursor ? g_pOriginalSetCursor( NULL ) : NULL;
		return g_pOriginalSetCursor ? g_pOriginalSetCursor( hCursor ) : NULL;
	}


	// -------------------------------------------------------------------------
	// Model classification, material overrides, and Direct3D hooks
	// -------------------------------------------------------------------------
	bool IsPlayerChamsClassName( const char *pName )
	{
		return pName && ( !Q_stricmp( pName, "CCSPlayer" ) || !Q_stricmp( pName, "CCSRagdoll" ) );
	}

	bool IsViewmodelChamsClassName( const char *pName )
	{
		return pName && ( !Q_stricmp( pName, "CPredictedViewModel" ) ||
			!Q_stricmp( pName, "CBaseViewModel" ) || strstr( pName, "ViewModel" ) != NULL );
	}

	const char *GetChamsEntityClassName( IClientNetworkable *pNetworkable )
	{
		ClientClass *pClass = pNetworkable ? pNetworkable->GetClientClass() : NULL;
		return pClass ? pClass->GetName() : NULL;
	}

	bool IsPlayerChamsEntity( int entityIndex )
	{
		if ( !g_pEntityList || entityIndex <= 0 )
			return false;
		if ( g_pEngine && entityIndex <= g_pEngine->GetMaxClients() )
			return true;
		IClientNetworkable *pNetworkable = g_pEntityList->GetClientNetworkable( entityIndex );
		return IsPlayerChamsClassName( GetChamsEntityClassName( pNetworkable ) );
	}

	bool IsViewmodelChamsEntity( int entityIndex )
	{
		if ( !g_pEntityList || entityIndex <= 0 )
			return false;
		IClientNetworkable *pNetworkable = g_pEntityList->GetClientNetworkable( entityIndex );
		return IsViewmodelChamsClassName( GetChamsEntityClassName( pNetworkable ) );
	}

	bool EnsureFlatChamsMaterialDefinition()
	{
		const char *pGameDirectory = g_pEngine ? g_pEngine->GetGameDirectory() : NULL;
		if ( !pGameDirectory || !pGameDirectory[0] )
			return false;

		char materialsDirectory[MAX_PATH];
		char artDirectory[MAX_PATH];
		char materialPath[MAX_PATH];
		Q_snprintf( materialsDirectory, sizeof( materialsDirectory ), "%s\\materials", pGameDirectory );
		Q_snprintf( artDirectory, sizeof( artDirectory ), "%s\\art", materialsDirectory );
		Q_snprintf( materialPath, sizeof( materialPath ), "%s\\flat.vmt", artDirectory );
		CreateDirectoryA( materialsDirectory, NULL );
		CreateDirectoryA( artDirectory, NULL );

		const DWORD attributes = GetFileAttributesA( materialPath );
		if ( attributes != INVALID_FILE_ATTRIBUTES && !( attributes & FILE_ATTRIBUTE_DIRECTORY ) )
			return true;

		static const char materialText[] =
			"\"UnlitGeneric\"\r\n"
			"{\r\n"
			"\t\"$basetexture\" \"vgui/white\"\r\n"
			"\t\"$model\" \"1\"\r\n"
			"\t\"$nocull\" \"1\"\r\n"
			"\t\"$halflambert\" \"1\"\r\n"
			"}\r\n";
		HANDLE hFile = CreateFileA( materialPath, GENERIC_WRITE, FILE_SHARE_READ, NULL,
			CREATE_NEW, FILE_ATTRIBUTE_NORMAL, NULL );
		if ( hFile == INVALID_HANDLE_VALUE )
			return GetLastError() == ERROR_FILE_EXISTS;

		DWORD bytesWritten = 0;
		const BOOL writeOk = WriteFile( hFile, materialText,
			static_cast<DWORD>( sizeof( materialText ) - 1 ), &bytesWritten, NULL );
		CloseHandle( hFile );
		return writeOk && bytesWritten == sizeof( materialText ) - 1;
	}

	IMaterial *ResolveFlatChamsMaterial()
	{
		if ( g_pFlatChamsMaterial )
			return g_pFlatChamsMaterial;
		if ( !g_pMaterials )
			return NULL;

		EnsureFlatChamsMaterialDefinition();
		static const char *materialNames[] =
		{
			"art/flat",
			"debug/debugdrawflat",
			"debug/debugambientcube",
			"models/debug/debugwhite"
		};
		for ( int i = 0; i < ARRAYSIZE( materialNames ); ++i )
		{
			IMaterial *pCandidate = g_pMaterials->FindMaterial( materialNames[i], "Model textures", false );
			if ( pCandidate && !pCandidate->IsErrorMaterial() )
			{
				g_pFlatChamsMaterial = pCandidate;
				LogMessage( "CHAMS MATERIAL READY: '%s'", materialNames[i] );
				return g_pFlatChamsMaterial;
			}
		}
		LogMessage( "CHAMS MATERIAL UNAVAILABLE: no compatible flat model material was found" );
		return NULL;
	}

	bool GetCurrentSkyboxName( char *pOutput, size_t outputBytes )
	{
		if ( !pOutput || outputBytes == 0 )
			return false;
		pOutput[0] = '\0';
		ConVar *pSkyName = g_pCvar ? g_pCvar->FindVar( "sv_skyname" ) : NULL;
		const char *pValue = pSkyName ? pSkyName->GetString() : NULL;
		if ( !pValue || !pValue[0] )
			return false;

		if ( !_strnicmp( pValue, "skybox/", 7 ) )
			pValue += 7;
		Q_strncpy( pOutput, pValue, static_cast<int>( outputBytes ) );
		return pOutput[0] != '\0';
	}

	bool HasSkyboxMaterial( IMaterial *pMaterial )
	{
		for ( size_t i = 0; i < g_SkyboxMaterialStates.size(); ++i )
		{
			if ( g_SkyboxMaterialStates[i].pMaterial == pMaterial )
				return true;
		}
		return false;
	}

	void RestoreSkyboxChamsMaterials()
	{
		for ( size_t i = 0; i < g_SkyboxMaterialStates.size(); ++i )
		{
			SkyboxMaterialState &state = g_SkyboxMaterialStates[i];
			if ( !state.pMaterial )
				continue;
			state.pMaterial->ColorModulate( state.red, state.green, state.blue );
			state.pMaterial->DecrementReferenceCount();
		}
		g_SkyboxMaterialStates.clear();
	}

	void AddSkyboxMaterial( IMaterial *pMaterial, float red, float green, float blue )
	{
		if ( !pMaterial || pMaterial->IsErrorMaterial() || HasSkyboxMaterial( pMaterial ) )
			return;

		SkyboxMaterialState state;
		state.pMaterial = pMaterial;
		pMaterial->GetColorModulation( &state.red, &state.green, &state.blue );
		pMaterial->IncrementReferenceCount();
		pMaterial->ColorModulate( red, green, blue );
		g_SkyboxMaterialStates.push_back( state );
	}

	void ReapplySkyboxChamsTint()
	{
		const float red = InterlockedCompareExchange( &g_nSkyboxChamsRed, 0, 0 ) / 255.0f;
		const float green = InterlockedCompareExchange( &g_nSkyboxChamsGreen, 0, 0 ) / 255.0f;
		const float blue = InterlockedCompareExchange( &g_nSkyboxChamsBlue, 0, 0 ) / 255.0f;

		for ( size_t i = 0; i < g_SkyboxMaterialStates.size(); ++i )
		{
			IMaterial *pMaterial = g_SkyboxMaterialStates[i].pMaterial;
			if ( pMaterial )
				pMaterial->ColorModulate( red, green, blue );
		}
	}

	void ApplySkyboxChamsMaterials()
	{
		if ( !g_pMaterials || !InterlockedCompareExchange( &g_bSkyboxChamsEnabled, FALSE, FALSE ) )
			return;

		char skyName[128];
		if ( !GetCurrentSkyboxName( skyName, sizeof( skyName ) ) )
		{
			LogMessage( "SKYBOX CHAMS WAITING: sv_skyname is unavailable" );
			return;
		}

		const float red = InterlockedCompareExchange( &g_nSkyboxChamsRed, 0, 0 ) / 255.0f;
		const float green = InterlockedCompareExchange( &g_nSkyboxChamsGreen, 0, 0 ) / 255.0f;
		const float blue = InterlockedCompareExchange( &g_nSkyboxChamsBlue, 0, 0 ) / 255.0f;
		static const char *faceSuffixes[] = { "rt", "lf", "bk", "ft", "up", "dn" };

		for ( int i = 0; i < ARRAYSIZE( faceSuffixes ); ++i )
		{
			char materialName[MAX_PATH];
			Q_snprintf( materialName, sizeof( materialName ), "skybox/%s%s", skyName, faceSuffixes[i] );
			IMaterial *pMaterial = g_pMaterials->FindMaterial( materialName, TEXTURE_GROUP_SKYBOX, false );
			AddSkyboxMaterial( pMaterial, red, green, blue );
		}

		Q_strncpy( g_szSkyboxChamsSkyName, skyName, sizeof( g_szSkyboxChamsSkyName ) );
		LogMessage( "SKYBOX CHAMS APPLIED: safe_face_lookup=1 sky='%s' materials=%d color=%ld %ld %ld",
			skyName, static_cast<int>( g_SkyboxMaterialStates.size() ),
			InterlockedCompareExchange( &g_nSkyboxChamsRed, 0, 0 ),
			InterlockedCompareExchange( &g_nSkyboxChamsGreen, 0, 0 ),
			InterlockedCompareExchange( &g_nSkyboxChamsBlue, 0, 0 ) );
	}

	void MaintainSkyboxChamsMaterials()
	{
		if ( InterlockedCompareExchange( &g_bSkyboxChamsUpdateActive, TRUE, FALSE ) != FALSE )
			return;

		const char *pLevelName = g_pEngine ? g_pEngine->GetLevelName() : NULL;
		const char *pCurrentLevel = pLevelName ? pLevelName : "";
		char currentSkyName[128];
		GetCurrentSkyboxName( currentSkyName, sizeof( currentSkyName ) );

		const bool contextChanged = _stricmp( g_szSkyboxChamsLevelName, pCurrentLevel ) != 0 ||
			_stricmp( g_szSkyboxChamsSkyName, currentSkyName ) != 0;
		if ( contextChanged )
		{
			if ( !g_SkyboxMaterialStates.empty() )
				RestoreSkyboxChamsMaterials();
			Q_strncpy( g_szSkyboxChamsLevelName, pCurrentLevel, sizeof( g_szSkyboxChamsLevelName ) );
			Q_strncpy( g_szSkyboxChamsSkyName, currentSkyName, sizeof( g_szSkyboxChamsSkyName ) );
			InterlockedExchange( &g_bSkyboxChamsRefreshPending, TRUE );
		}

		if ( !InterlockedCompareExchange( &g_bSkyboxChamsEnabled, FALSE, FALSE ) )
		{
			if ( !g_SkyboxMaterialStates.empty() )
				RestoreSkyboxChamsMaterials();
			InterlockedExchange( &g_bSkyboxChamsRefreshPending, FALSE );
			InterlockedExchange( &g_bSkyboxChamsUpdateActive, FALSE );
			return;
		}

		if ( InterlockedExchange( &g_bSkyboxChamsRefreshPending, FALSE ) || g_SkyboxMaterialStates.empty() )
		{
			if ( !g_SkyboxMaterialStates.empty() )
				RestoreSkyboxChamsMaterials();
			ApplySkyboxChamsMaterials();
		}
		else
		{
			ReapplySkyboxChamsTint();
		}

		InterlockedExchange( &g_bSkyboxChamsUpdateActive, FALSE );
	}

	struct MaterialOverrideRestore
	{
		IMaterial *pMaterial;
		bool ignoreZ;
		float red;
		float green;
		float blue;
		float alpha;
		MaterialVectorState tintVars[2];
		int tintVarCount;
		bool active;

		MaterialOverrideRestore() : pMaterial( NULL ), ignoreZ( false ), red( 1.0f ), green( 1.0f ),
			blue( 1.0f ), alpha( 1.0f ), tintVarCount( 0 ), active( false ) {}

		void ApplyTintVar( const char *pName, float newRed, float newGreen, float newBlue )
		{
			if ( tintVarCount >= ARRAYSIZE( tintVars ) )
				return;
			bool found = false;
			IMaterialVar *pVar = pMaterial->FindVar( pName, &found, false );
			if ( !found || !pVar || !pVar->IsDefined() || pVar->GetType() != MATERIAL_VAR_TYPE_VECTOR )
				return;
			const int components = pVar->VectorSize();
			if ( components < 3 || components > 4 )
				return;
			MaterialVectorState &saved = tintVars[tintVarCount++];
			saved.pVar = pVar;
			saved.components = components;
			saved.value[0] = saved.value[1] = saved.value[2] = saved.value[3] = 1.0f;
			pVar->GetVecValue( saved.value, components );
			float tint[4] = { newRed, newGreen, newBlue, saved.value[3] };
			pVar->SetVecValue( tint, components );
		}

		bool Apply( IMaterial *pNewMaterial, bool newIgnoreZ, float newRed, float newGreen, float newBlue )
		{
			if ( !pNewMaterial || pNewMaterial->IsErrorMaterial() )
				return false;
			pMaterial = pNewMaterial;
			ignoreZ = pMaterial->GetMaterialVarFlag( MATERIAL_VAR_IGNOREZ );
			pMaterial->GetColorModulation( &red, &green, &blue );
			alpha = pMaterial->GetAlphaModulation();
			tintVarCount = 0;
			pMaterial->SetMaterialVarFlag( MATERIAL_VAR_IGNOREZ, newIgnoreZ );
			ApplyTintVar( "$color", newRed, newGreen, newBlue );
			ApplyTintVar( "$color2", newRed, newGreen, newBlue );
			pMaterial->ColorModulate( newRed, newGreen, newBlue );
			pMaterial->AlphaModulate( 1.0f );
			pMaterial->RecomputeStateSnapshots();
			active = true;
			return true;
		}

		void Restore()
		{
			if ( !active || !pMaterial )
				return;
			pMaterial->SetMaterialVarFlag( MATERIAL_VAR_IGNOREZ, ignoreZ );
			pMaterial->ColorModulate( red, green, blue );
			pMaterial->AlphaModulate( alpha );
			for ( int i = 0; i < tintVarCount; ++i )
			{
				MaterialVectorState &saved = tintVars[i];
				if ( saved.pVar )
					saved.pVar->SetVecValue( saved.value, saved.components );
			}
			pMaterial->RecomputeStateSnapshots();
			active = false;
			pMaterial = NULL;
		}
	};

	struct RenderViewModulationRestore
	{
		float color[3];
		float blend;
		bool active;

		RenderViewModulationRestore() : blend( 1.0f ), active( false )
		{
			color[0] = color[1] = color[2] = 1.0f;
		}

		bool Apply( float red, float green, float blue )
		{
			if ( !g_pRenderView )
				return false;
			g_pRenderView->GetColorModulation( color );
			blend = g_pRenderView->GetBlend();
			const float requested[3] = { red, green, blue };
			g_pRenderView->SetColorModulation( requested );
			g_pRenderView->SetBlend( 1.0f );
			active = true;
			return true;
		}

		void Restore()
		{
			if ( !active || !g_pRenderView )
				return;
			g_pRenderView->SetColorModulation( color );
			g_pRenderView->SetBlend( blend );
			active = false;
		}

		~RenderViewModulationRestore()
		{
			Restore();
		}
	};

	IVModelInfo *ResolveGuiModelInfo()
	{
		if ( g_pGuiModelInfo )
			return g_pGuiModelInfo;
		if ( !g_hEngineModule )
			return NULL;
		typedef void *( __cdecl *GuiCreateInterfaceFn )( const char *, int * );
		GuiCreateInterfaceFn pFactory = reinterpret_cast<GuiCreateInterfaceFn>(
			GetProcAddress( g_hEngineModule, "CreateInterface" ) );
		if ( !pFactory )
			return NULL;
		g_pGuiModelInfo = static_cast<IVModelInfo *>( pFactory( VMODELINFO_CLIENT_INTERFACE_VERSION, NULL ) );
		if ( !g_pGuiModelInfo )
			LogMessage( "MODEL INFO UNAVAILABLE: interface %s", VMODELINFO_CLIENT_INTERFACE_VERSION );
		return g_pGuiModelInfo;
	}

	bool IsViewmodelModel( const model_t *pModel )
	{
		IVModelInfo *pModelInfo = ResolveGuiModelInfo();
		const char *pName = pModelInfo && pModel ? pModelInfo->GetModelName( pModel ) : NULL;
		return pName && ( ContainsInsensitive( pName, "models/weapons/v_" ) ||
			ContainsInsensitive( pName, "/v_" ) );
	}

	bool IsWorldWeaponModelName( const char *pName )
	{
		return pName && ( ContainsInsensitive( pName, "models/weapons/w_" ) ||
			ContainsInsensitive( pName, "models\\weapons\\w_" ) );
	}

	LRESULT CALLBACK DummyWindowProc( HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam )
	{
		return DefWindowProcA( hWnd, message, wParam, lParam );
	}

	bool CreateQueuedHook( void *pTarget, void *pDetour, void **ppOriginal, bool *pCreated, const char *pName )
	{
		const MH_STATUS createStatus = MH_CreateHook( pTarget, pDetour, ppOriginal );
		if ( createStatus != MH_OK )
		{
			SetError( "MinHook could not create %s hook: %s", pName, MH_StatusToString( createStatus ) );
			return false;
		}
		*pCreated = true;

		const MH_STATUS queueStatus = MH_QueueEnableHook( pTarget );
		if ( queueStatus != MH_OK )
		{
			SetError( "MinHook could not queue %s hook: %s", pName, MH_StatusToString( queueStatus ) );
			return false;
		}
		return true;
	}

	void RemoveD3D9Hooks()
	{
		if ( !g_bMinHookInitialized )
			return;

		if ( g_bSetCursorHookCreated )
		{
			MH_DisableHook( g_pSetCursorTarget );
			MH_RemoveHook( g_pSetCursorTarget );
		}
		if ( g_bClipCursorHookCreated )
		{
			MH_DisableHook( g_pClipCursorTarget );
			MH_RemoveHook( g_pClipCursorTarget );
		}
		if ( g_bSetCursorPosHookCreated )
		{
			MH_DisableHook( g_pSetCursorPosTarget );
			MH_RemoveHook( g_pSetCursorPosTarget );
		}
		if ( g_bGetCursorPosHookCreated )
		{
			MH_DisableHook( g_pGetCursorPosTarget );
			MH_RemoveHook( g_pGetCursorPosTarget );
		}

		if ( g_bSwapChainPresentHookCreated )
		{
			MH_DisableHook( g_pSwapChainPresentTarget );
			MH_RemoveHook( g_pSwapChainPresentTarget );
		}
		if ( g_bEndSceneHookCreated )
		{
			MH_DisableHook( g_pEndSceneTarget );
			MH_RemoveHook( g_pEndSceneTarget );
		}
		if ( g_bPresentHookCreated )
		{
			MH_DisableHook( g_pPresentTarget );
			MH_RemoveHook( g_pPresentTarget );
		}
		if ( g_bResetHookCreated )
		{
			MH_DisableHook( g_pResetTarget );
			MH_RemoveHook( g_pResetTarget );
		}
		MH_Uninitialize();

		g_bMinHookInitialized = false;
		g_bResetHookCreated = false;
		g_bPresentHookCreated = false;
		g_bEndSceneHookCreated = false;
		g_bSwapChainPresentHookCreated = false;
		g_bGetCursorPosHookCreated = false;
		g_bSetCursorPosHookCreated = false;
		g_bClipCursorHookCreated = false;
		g_bSetCursorHookCreated = false;
		g_pResetTarget = NULL;
		g_pPresentTarget = NULL;
		g_pEndSceneTarget = NULL;
		g_pSwapChainPresentTarget = NULL;
		g_pGetCursorPosTarget = NULL;
		g_pSetCursorPosTarget = NULL;
		g_pClipCursorTarget = NULL;
		g_pSetCursorTarget = NULL;
		g_pOriginalReset = NULL;
		g_pOriginalPresent = NULL;
		g_pOriginalEndScene = NULL;
		g_pOriginalSwapChainPresent = NULL;
		g_pOriginalGetCursorPos = NULL;
		g_pOriginalSetCursorPos = NULL;
		g_pOriginalClipCursor = NULL;
		g_pOriginalSetCursor = NULL;
	}

	bool InstallD3D9Hooks()
	{
		const char *pClassName = "V34ArtGuiProbe";
		WNDCLASSEXA windowClass;
		ZeroMemory( &windowClass, sizeof( windowClass ) );
		windowClass.cbSize = sizeof( windowClass );
		windowClass.lpfnWndProc = DummyWindowProc;
		windowClass.hInstance = g_hThisModule;
		windowClass.lpszClassName = pClassName;
		RegisterClassExA( &windowClass );

		HWND hWindow = CreateWindowExA( 0, pClassName, pClassName, WS_OVERLAPPEDWINDOW,
			0, 0, 100, 100, NULL, NULL, g_hThisModule, NULL );
		if ( !hWindow )
		{
			SetError( "could not create D3D9 probe window (Win32 error %lu)", GetLastError() );
			UnregisterClassA( pClassName, g_hThisModule );
			return false;
		}

		IDirect3D9 *pD3D = Direct3DCreate9( D3D_SDK_VERSION );
		if ( !pD3D )
		{
			SetError( "Direct3DCreate9 failed" );
			DestroyWindow( hWindow );
			UnregisterClassA( pClassName, g_hThisModule );
			return false;
		}

		D3DPRESENT_PARAMETERS present;
		ZeroMemory( &present, sizeof( present ) );
		present.Windowed = TRUE;
		present.SwapEffect = D3DSWAPEFFECT_DISCARD;
		present.hDeviceWindow = hWindow;
		present.BackBufferFormat = D3DFMT_UNKNOWN;

		IDirect3DDevice9 *pDevice = NULL;
		HRESULT result = pD3D->CreateDevice( D3DADAPTER_DEFAULT, D3DDEVTYPE_HAL, hWindow,
			D3DCREATE_HARDWARE_VERTEXPROCESSING, &present, &pDevice );
		if ( FAILED( result ) )
			result = pD3D->CreateDevice( D3DADAPTER_DEFAULT, D3DDEVTYPE_HAL, hWindow,
				D3DCREATE_MIXED_VERTEXPROCESSING, &present, &pDevice );
		if ( FAILED( result ) )
			result = pD3D->CreateDevice( D3DADAPTER_DEFAULT, D3DDEVTYPE_HAL, hWindow,
				D3DCREATE_SOFTWARE_VERTEXPROCESSING, &present, &pDevice );

		if ( FAILED( result ) || !pDevice )
		{
			SetError( "could not create D3D9 probe device (HRESULT 0x%08lX)", result );
			pD3D->Release();
			DestroyWindow( hWindow );
			UnregisterClassA( pClassName, g_hThisModule );
			return false;
		}

		IDirect3DSwapChain9 *pSwapChain = NULL;
		result = pDevice->GetSwapChain( 0, &pSwapChain );
		if ( FAILED( result ) || !pSwapChain )
		{
			SetError( "could not get D3D9 probe swap chain (HRESULT 0x%08lX)", result );
			pDevice->Release();
			pD3D->Release();
			DestroyWindow( hWindow );
			UnregisterClassA( pClassName, g_hThisModule );
			return false;
		}

		void **pDeviceVtable = *reinterpret_cast<void ***>( pDevice );
		void **pSwapChainVtable = *reinterpret_cast<void ***>( pSwapChain );
		g_pResetTarget = pDeviceVtable[kD3D9ResetIndex];
		g_pPresentTarget = pDeviceVtable[kD3D9PresentIndex];
		g_pEndSceneTarget = pDeviceVtable[kD3D9EndSceneIndex];
		g_pSwapChainPresentTarget = pSwapChainVtable[kD3D9SwapChainPresentIndex];

		HMODULE hUser32 = GetModuleHandleA( "user32.dll" );
		g_pGetCursorPosTarget = hUser32 ? reinterpret_cast<void *>( GetProcAddress( hUser32, "GetCursorPos" ) ) : NULL;
		g_pSetCursorPosTarget = hUser32 ? reinterpret_cast<void *>( GetProcAddress( hUser32, "SetCursorPos" ) ) : NULL;
		g_pClipCursorTarget = hUser32 ? reinterpret_cast<void *>( GetProcAddress( hUser32, "ClipCursor" ) ) : NULL;
		g_pSetCursorTarget = hUser32 ? reinterpret_cast<void *>( GetProcAddress( hUser32, "SetCursor" ) ) : NULL;
		// Model effects are integrated into the recorder's existing DrawModelEx hook.
		if ( !g_pGetCursorPosTarget || !g_pSetCursorPosTarget ||
			!g_pClipCursorTarget || !g_pSetCursorTarget )
		{
			SetError( "could not resolve user32 cursor functions" );
			pSwapChain->Release();
			pDevice->Release();
			pD3D->Release();
			DestroyWindow( hWindow );
			UnregisterClassA( pClassName, g_hThisModule );
			return false;
		}

		const MH_STATUS initializeStatus = MH_Initialize();
		if ( initializeStatus != MH_OK )
		{
			SetError( "MinHook initialization failed: %s", MH_StatusToString( initializeStatus ) );
			pSwapChain->Release();
			pDevice->Release();
			pD3D->Release();
			DestroyWindow( hWindow );
			UnregisterClassA( pClassName, g_hThisModule );
			return false;
		}
		g_bMinHookInitialized = true;

		const bool hooksCreated =
			CreateQueuedHook( g_pResetTarget, reinterpret_cast<void *>( HookedReset ),
				reinterpret_cast<void **>( &g_pOriginalReset ), &g_bResetHookCreated, "Reset" ) &&
			CreateQueuedHook( g_pPresentTarget, reinterpret_cast<void *>( HookedPresent ),
				reinterpret_cast<void **>( &g_pOriginalPresent ), &g_bPresentHookCreated, "device Present" ) &&
			CreateQueuedHook( g_pEndSceneTarget, reinterpret_cast<void *>( HookedEndScene ),
				reinterpret_cast<void **>( &g_pOriginalEndScene ), &g_bEndSceneHookCreated, "EndScene" ) &&
			CreateQueuedHook( g_pSwapChainPresentTarget, reinterpret_cast<void *>( HookedSwapChainPresent ),
				reinterpret_cast<void **>( &g_pOriginalSwapChainPresent ), &g_bSwapChainPresentHookCreated,
				"swap-chain Present" ) &&
			CreateQueuedHook( g_pGetCursorPosTarget, reinterpret_cast<void *>( HookedGetCursorPos ),
				reinterpret_cast<void **>( &g_pOriginalGetCursorPos ), &g_bGetCursorPosHookCreated,
				"GetCursorPos" ) &&
			CreateQueuedHook( g_pSetCursorPosTarget, reinterpret_cast<void *>( HookedSetCursorPos ),
				reinterpret_cast<void **>( &g_pOriginalSetCursorPos ), &g_bSetCursorPosHookCreated,
				"SetCursorPos" ) &&
			CreateQueuedHook( g_pClipCursorTarget, reinterpret_cast<void *>( HookedClipCursor ),
				reinterpret_cast<void **>( &g_pOriginalClipCursor ), &g_bClipCursorHookCreated,
				"ClipCursor" ) &&
			CreateQueuedHook( g_pSetCursorTarget, reinterpret_cast<void *>( HookedSetCursor ),
				reinterpret_cast<void **>( &g_pOriginalSetCursor ), &g_bSetCursorHookCreated,
				"SetCursor" );

		if ( hooksCreated )
		{
			const MH_STATUS applyStatus = MH_ApplyQueued();
			if ( applyStatus != MH_OK )
				SetError( "MinHook could not enable D3D9 hooks: %s", MH_StatusToString( applyStatus ) );
			else
			{
				ClearError();
			}
		}

		pSwapChain->Release();
		pDevice->Release();
		pD3D->Release();
		DestroyWindow( hWindow );
		UnregisterClassA( pClassName, g_hThisModule );

		if ( !hooksCreated || g_Gui.lastError[0] )
		{
			RemoveD3D9Hooks();
			return false;
		}

		LogMessage( "GUI DETOURS READY: reset=%p present=%p end_scene=%p swap_present=%p get_cursor_pos=%p set_cursor_pos=%p clip_cursor=%p set_cursor=%p minhook=%s",
			g_pResetTarget, g_pPresentTarget, g_pEndSceneTarget, g_pSwapChainPresentTarget,
			g_pGetCursorPosTarget, g_pSetCursorPosTarget,
			g_pClipCursorTarget, g_pSetCursorTarget, kMinHookVersionPinned );
		return true;
	}

	// -------------------------------------------------------------------------
	// Console commands and public GUI integration
	// -------------------------------------------------------------------------
	void ArtGui_f()
	{
		if ( !g_Gui.installed )
		{
			ArtConsoleMessage( "art_gui: unavailable: %s.\n",
				g_Gui.lastError[0] ? g_Gui.lastError : "D3D9 hook is not installed" );
			return;
		}

		const int argc = g_pEngine ? g_pEngine->Cmd_Argc() : 0;
		if ( argc <= 1 )
		{
			SetArtGuiVisible( !IsArtGuiVisible() );
			char keyName[96];
			GetToggleBindingName( g_Gui.toggleKey, g_Gui.toggleModifiers, keyName, sizeof( keyName ), true );
			ArtConsoleMessage( "art_gui: %s (backend %s, %s toggles).\n",
				IsArtGuiVisible() ? "on" : "off", g_Gui.imguiReady ? "ready" : "waiting for render frame",
				keyName );
			return;
		}

		const char *pValue = g_pEngine->Cmd_Argv( 1 );
		if ( !Q_stricmp( pValue, "status" ) )
		{
			ArtConsoleMessage( "art_gui: installed=%d visible=%d backend=%d cursor_hooks=%d model_hook=%d endscene_calls=%ld primary_frames=%ld present_calls=%ld swap_present_calls=%ld fallback_frames=%ld rendered_frames=%ld error='%s'.\n",
				g_Gui.installed ? 1 : 0, IsArtGuiVisible() ? 1 : 0, g_Gui.imguiReady ? 1 : 0,
				( g_bGetCursorPosHookCreated && g_bSetCursorPosHookCreated &&
					g_bClipCursorHookCreated && g_bSetCursorHookCreated ) ? 1 : 0,
				IsModelRenderHookReady() ? 1 : 0,
				InterlockedCompareExchange( &g_nEndSceneHookCalls, 0, 0 ),
				InterlockedCompareExchange( &g_nPrimaryEndSceneFrames, 0, 0 ),
				InterlockedCompareExchange( &g_nPresentHookCalls, 0, 0 ),
				InterlockedCompareExchange( &g_nSwapChainPresentHookCalls, 0, 0 ),
				InterlockedCompareExchange( &g_nPresentFallbackFrames, 0, 0 ),
				InterlockedCompareExchange( &g_nGuiRenderedFrames, 0, 0 ),
				g_Gui.lastError[0] ? g_Gui.lastError : "none" );
			return;
		}
		if ( !Q_stricmp( pValue, "toggle" ) )
			SetArtGuiVisible( !IsArtGuiVisible() );
		else if ( IsTruthy( pValue ) )
			SetArtGuiVisible( true );
		else if ( IsFalsy( pValue ) )
			SetArtGuiVisible( false );
		else
			ArtConsoleMessage( "Usage: art_gui [on|off|toggle|status].\n" );
	}


	void PrintGlobalFovUsage()
	{
		ArtConsoleMessage( "Usage: art_fov <1-179|default>.\n" );
		ArtConsoleMessage( "       art_fov handleZoom enabled <0|1>.\n" );
		ArtConsoleMessage( "       art_fov handleZoom minUnzoomedFov <1-179>.\n" );
	}

	void ArtGlobalFov_f()
	{
		const int argc = g_pEngine ? g_pEngine->Cmd_Argc() : 0;
		if ( argc <= 1 )
		{
			ArtConsoleMessage( "art_fov: value=%s%g handleZoom=%d minUnzoomedFov=%g.\n",
				g_Gui.globalFovDefault ? "default / " : "", g_flGlobalFov,
				InterlockedCompareExchange( &g_bGlobalFovHandleZoom, FALSE, FALSE ) ? 1 : 0,
				g_flGlobalFovMinUnzoomedFov );
			PrintGlobalFovUsage();
			return;
		}

		const char *pAction = g_pEngine->Cmd_Argv( 1 );
		if ( !Q_stricmp( pAction, "handleZoom" ) )
		{
			if ( argc == 3 )
			{
				const char *pSetting = g_pEngine->Cmd_Argv( 2 );
				if ( !Q_stricmp( pSetting, "enabled" ) )
					ArtConsoleMessage( "art_fov handleZoom enabled is %d.\n",
						InterlockedCompareExchange( &g_bGlobalFovHandleZoom, FALSE, FALSE ) ? 1 : 0 );
				else if ( !Q_stricmp( pSetting, "minUnzoomedFov" ) )
					ArtConsoleMessage( "art_fov handleZoom minUnzoomedFov is %g.\n",
						g_flGlobalFovMinUnzoomedFov );
				else
					PrintGlobalFovUsage();
				return;
			}
			if ( argc != 4 )
			{
				PrintGlobalFovUsage();
				return;
			}

			const char *pSetting = g_pEngine->Cmd_Argv( 2 );
			const char *pValue = g_pEngine->Cmd_Argv( 3 );
			if ( !Q_stricmp( pSetting, "enabled" ) )
			{
				if ( !IsTruthy( pValue ) && !IsFalsy( pValue ) )
				{
					ArtConsoleMessage( "art_fov handleZoom enabled: expected 0 or 1.\n" );
					return;
				}
				InterlockedExchange( &g_bGlobalFovHandleZoom, IsTruthy( pValue ) ? TRUE : FALSE );
				ArtConsoleMessage( "art_fov handleZoom enabled: %d.\n",
					InterlockedCompareExchange( &g_bGlobalFovHandleZoom, FALSE, FALSE ) ? 1 : 0 );
				return;
			}
			if ( !Q_stricmp( pSetting, "minUnzoomedFov" ) )
			{
				char *pEnd = NULL;
				const double requested = strtod( pValue, &pEnd );
				if ( !pEnd || *pEnd != '\0' || requested < 1.0 || requested > 179.0 )
				{
					ArtConsoleMessage( "art_fov handleZoom minUnzoomedFov: expected 1-179.\n" );
					return;
				}
				g_flGlobalFovMinUnzoomedFov = static_cast<float>( requested );
				ArtConsoleMessage( "art_fov handleZoom minUnzoomedFov: %g.\n",
					g_flGlobalFovMinUnzoomedFov );
				return;
			}
			PrintGlobalFovUsage();
			return;
		}

		if ( argc != 2 )
		{
			PrintGlobalFovUsage();
			return;
		}

		if ( !Q_stricmp( pAction, "default" ) )
		{
			g_Gui.globalFov = 90.0f;
			g_Gui.globalFovDefault = true;
			g_flGlobalFov = 90.0f;
			InterlockedExchange( &g_bGlobalFovOverride, FALSE );
			ArtConsoleMessage( "art_fov: restored the game camera FOV.\n" );
			return;
		}

		char *pEnd = NULL;
		const double requested = strtod( pAction, &pEnd );
		if ( !pEnd || *pEnd != '\0' || requested < 1.0 || requested > 179.0 )
		{
			ArtConsoleMessage( "art_fov: expected a value from 1 to 179, or default.\n" );
			return;
		}

		g_flGlobalFov = static_cast<float>( requested );
		g_Gui.globalFov = g_flGlobalFov;
		g_Gui.globalFovDefault = false;
		InterlockedExchange( &g_bGlobalFovOverride, TRUE );
		ArtConsoleMessage( "art_fov: global FOV changed to %g.\n", g_flGlobalFov );
	}

	float *FindGuiColorTarget( const char *pName )
	{
		if ( !pName ) return NULL;
		if ( !Q_stricmp( pName, "accent" ) ) return g_Gui.accentColor;
		if ( !Q_stricmp( pName, "window" ) || !Q_stricmp( pName, "background" ) ) return g_Gui.windowColor;
		if ( !Q_stricmp( pName, "panel" ) ) return g_Gui.panelColor;
		if ( !Q_stricmp( pName, "sidebar" ) ) return g_Gui.sidebarColor;
		if ( !Q_stricmp( pName, "text" ) ) return g_Gui.textColor;
		if ( !Q_stricmp( pName, "muted" ) || !Q_stricmp( pName, "muted_text" ) ) return g_Gui.mutedTextColor;
		if ( !Q_stricmp( pName, "border" ) || !Q_stricmp( pName, "borders" ) ) return g_Gui.borderColor;
		if ( !Q_stricmp( pName, "control" ) || !Q_stricmp( pName, "controls" ) ) return g_Gui.controlColor;
		if ( !Q_stricmp( pName, "selection" ) || !Q_stricmp( pName, "selected" ) ) return g_Gui.selectionColor;
		return NULL;
	}

	void ArtGuiKey_f()
	{
		const int argc = g_pEngine ? g_pEngine->Cmd_Argc() : 0;
		char keyName[96];
		GetToggleBindingName( g_Gui.toggleKey, g_Gui.toggleModifiers, keyName, sizeof( keyName ), true );
		if ( argc <= 1 )
		{
			ArtConsoleMessage( "art_gui_key is %s. Usage: art_gui_key <[SHIFT+][CTRL+][ALT+]key>.\n",
				keyName );
			return;
		}
		if ( argc != 2 )
		{
			ArtConsoleMessage( "Usage: art_gui_key <[SHIFT+][CTRL+][ALT+]key>.\n" );
			return;
		}

		int key = 0;
		int modifiers = TOGGLE_MODIFIER_NONE;
		if ( !ParseToggleBinding( g_pEngine->Cmd_Argv( 1 ), key, modifiers ) || key == VK_ESCAPE )
		{
			ArtConsoleMessage( "art_gui_key: unsupported binding. Example: SHIFT+F3, PAGEUP, CTRL+INSERT.\n" );
			return;
		}
		g_Gui.toggleKey = key;
		g_Gui.toggleModifiers = modifiers;
		g_Gui.waitingForToggleKey = false;
		GetToggleBindingName( g_Gui.toggleKey, g_Gui.toggleModifiers, keyName, sizeof( keyName ), true );
		ArtConsoleMessage( "art_gui_key: %s.\n", keyName );
	}

	void ArtGuiColor_f()
	{
		const int argc = g_pEngine ? g_pEngine->Cmd_Argc() : 0;
		if ( argc <= 1 )
		{
			ArtConsoleMessage( "Usage: art_gui_color <accent|window|panel|sidebar|text|muted|border|control|selection> [r g b [a]], values 0-255.\n" );
			return;
		}

		const char *pTargetName = g_pEngine->Cmd_Argv( 1 );
		float *pTarget = FindGuiColorTarget( pTargetName );
		if ( !pTarget )
		{
			ArtConsoleMessage( "art_gui_color: unknown target '%s'.\n", pTargetName );
			return;
		}

		if ( argc == 2 )
		{
			ArtConsoleMessage( "art_gui_color %s = %d %d %d %d.\n", pTargetName,
				ColorToByte( pTarget[0] ), ColorToByte( pTarget[1] ), ColorToByte( pTarget[2] ),
				ColorToByte( pTarget[3] ) );
			return;
		}
		if ( argc != 5 && argc != 6 )
		{
			ArtConsoleMessage( "Usage: art_gui_color <accent|window|panel|sidebar|text|muted|border|control|selection> <r> <g> <b> [a].\n" );
			return;
		}

		const int red = ClampColorByte( atoi( g_pEngine->Cmd_Argv( 2 ) ) );
		const int green = ClampColorByte( atoi( g_pEngine->Cmd_Argv( 3 ) ) );
		const int blue = ClampColorByte( atoi( g_pEngine->Cmd_Argv( 4 ) ) );
		const int alpha = argc == 6 ? ClampColorByte( atoi( g_pEngine->Cmd_Argv( 5 ) ) ) : ColorToByte( pTarget[3] );
		SetColor( pTarget, red / 255.0f, green / 255.0f, blue / 255.0f, alpha / 255.0f );
		if ( g_Gui.imguiReady ) ApplyThemeColors();
		ArtConsoleMessage( "art_gui_color %s: %d %d %d %d.\n", pTargetName, red, green, blue, alpha );
	}

	void ArtGuiTheme_f()
	{
		const int argc = g_pEngine ? g_pEngine->Cmd_Argc() : 0;
		if ( argc != 2 || !ApplyThemePreset( g_pEngine->Cmd_Argv( 1 ) ) )
		{
			ArtConsoleMessage( "Usage: art_gui_theme <default|purple|blue|green|orange|red|mono>.\n" );
			return;
		}
		if ( g_Gui.imguiReady ) ApplyThemeColors();
		ArtConsoleMessage( "art_gui_theme: %s.\n", g_pEngine->Cmd_Argv( 1 ) );
	}

	void ArtOverlay_f()
	{
		const int argc = g_pEngine ? g_pEngine->Cmd_Argc() : 0;
		if ( argc <= 1 )
		{
			ArtConsoleMessage( "art_overlay is %s. Usage: art_overlay <on|off>.\n",
				InterlockedCompareExchange( &g_bArtStatisticsOverlayEnabled, FALSE, FALSE ) ? "on" : "off" );
			return;
		}
		if ( argc != 2 || ( !IsTruthy( g_pEngine->Cmd_Argv( 1 ) ) &&
			!IsFalsy( g_pEngine->Cmd_Argv( 1 ) ) ) )
		{
			ArtConsoleMessage( "Usage: art_overlay <on|off>.\n" );
			return;
		}
		InterlockedExchange( &g_bArtStatisticsOverlayEnabled,
			IsTruthy( g_pEngine->Cmd_Argv( 1 ) ) ? TRUE : FALSE );
		ArtConsoleMessage( "art_overlay: %s.\n",
			InterlockedCompareExchange( &g_bArtStatisticsOverlayEnabled, FALSE, FALSE ) ? "on" : "off" );
	}

	void ArtOpenFolder_f()
	{
		const int argc = g_pEngine ? g_pEngine->Cmd_Argc() : 0;
		if ( argc != 1 )
		{
			ArtConsoleMessage( "Usage: art_open_folder.\n" );
			return;
		}
		if ( OpenConfiguredOutputDirectory() )
			ArtConsoleMessage( "art_open_folder: opened the configured ART output folder.\n" );
		else
			ArtConsoleMessage( "art_open_folder: unable to open the configured output folder.\n" );
	}

	void ArtHlaeInputWhileGui_f()
	{
		const int argc = g_pEngine ? g_pEngine->Cmd_Argc() : 0;
		if ( argc <= 1 )
		{
			ArtConsoleMessage(
				"art_hlae_input_while_gui is %s. Usage: art_hlae_input_while_gui <on|off>.\n",
				g_Gui.hlaeInputWhileGui ? "on" : "off" );
			return;
		}
		if ( argc != 2 || ( !IsTruthy( g_pEngine->Cmd_Argv( 1 ) ) &&
			!IsFalsy( g_pEngine->Cmd_Argv( 1 ) ) ) )
		{
			ArtConsoleMessage( "Usage: art_hlae_input_while_gui <on|off>.\n" );
			return;
		}

		g_Gui.hlaeInputWhileGui = IsTruthy( g_pEngine->Cmd_Argv( 1 ) );
		if ( !g_Gui.hlaeInputWhileGui )
			SetHlaeInputWhileGuiActive( false );
		ArtConsoleMessage( "art_hlae_input_while_gui: %s.\n",
			g_Gui.hlaeInputWhileGui ? "on" : "off" );
	}

	void ArtHlaeInputHoldKey_f()
	{
		const int argc = g_pEngine ? g_pEngine->Cmd_Argc() : 0;
		char keyName[32];
		GetHlaeInputHoldKeyName( g_Gui.hlaeInputHoldKey,
			keyName, sizeof( keyName ) );
		if ( argc <= 1 )
		{
			ArtConsoleMessage(
				"art_hlae_input_hold_key is %s. Usage: art_hlae_input_hold_key <key>.\n",
				keyName );
			return;
		}
		if ( argc != 2 )
		{
			ArtConsoleMessage(
				"Usage: art_hlae_input_hold_key <LMB|RMB|MMB|MOUSE4|MOUSE5|key>.\n" );
			return;
		}

		int key = 0;
		if ( !ParseHlaeInputHoldKey( g_pEngine->Cmd_Argv( 1 ), key ) || key == VK_ESCAPE )
		{
			ArtConsoleMessage(
				"art_hlae_input_hold_key: unsupported key. Examples: LMB, RMB, SHIFT, SPACE, F4.\n" );
			return;
		}
		g_Gui.hlaeInputHoldKey = key;
		g_Gui.waitingForHlaeInputHoldKey = false;
		SetHlaeInputWhileGuiActive( false );
		GetHlaeInputHoldKeyName( g_Gui.hlaeInputHoldKey,
			keyName, sizeof( keyName ) );
		ArtConsoleMessage( "art_hlae_input_hold_key: %s.\n", keyName );
	}

	void ArtGuiExperimental_f()
	{
		const int argc = g_pEngine ? g_pEngine->Cmd_Argc() : 0;
		if ( argc <= 1 )
		{
			ArtConsoleMessage( "art_gui_experimental is %s. Usage: art_gui_experimental <on|off>.\n",
				g_Gui.experimentalOptionsEnabled ? "on" : "off" );
			return;
		}
		if ( argc != 2 || ( !IsTruthy( g_pEngine->Cmd_Argv( 1 ) ) && !IsFalsy( g_pEngine->Cmd_Argv( 1 ) ) ) )
		{
			ArtConsoleMessage( "Usage: art_gui_experimental <on|off>.\n" );
			return;
		}

		g_Gui.experimentalOptionsEnabled = IsTruthy( g_pEngine->Cmd_Argv( 1 ) );
		ArtConsoleMessage( "art_gui_experimental: %s.\n",
			g_Gui.experimentalOptionsEnabled ? "on" : "off" );
	}

	void ArtDemoPauseAfterRecording_f()
	{
		const int argc = g_pEngine ? g_pEngine->Cmd_Argc() : 0;
		if ( argc <= 1 )
		{
			ArtConsoleMessage( "art_demo_pause_after_recording is %s. Usage: art_demo_pause_after_recording <on|off>.\n",
				g_Gui.autoPauseDemoAfterRecording ? "on" : "off" );
			return;
		}
		if ( argc != 2 || ( !IsTruthy( g_pEngine->Cmd_Argv( 1 ) ) &&
			!IsFalsy( g_pEngine->Cmd_Argv( 1 ) ) ) )
		{
			ArtConsoleMessage( "Usage: art_demo_pause_after_recording <on|off>.\n" );
			return;
		}

		g_Gui.autoPauseDemoAfterRecording = IsTruthy( g_pEngine->Cmd_Argv( 1 ) );
		ArtConsoleMessage( "art_demo_pause_after_recording: %s%s.\n",
			g_Gui.autoPauseDemoAfterRecording ? "on" : "off",
			g_Gui.experimentalOptionsEnabled ? "" : " (inactive until experimental options are enabled)" );
	}

	void ArtDemoUnpauseOnRecording_f()
	{
		const int argc = g_pEngine ? g_pEngine->Cmd_Argc() : 0;
		if ( argc <= 1 )
		{
			ArtConsoleMessage( "art_demo_unpause_on_recording is %s. Usage: art_demo_unpause_on_recording <on|off>.\n",
				g_Gui.autoResumeDemoOnRecordingStart ? "on" : "off" );
			return;
		}
		if ( argc != 2 || ( !IsTruthy( g_pEngine->Cmd_Argv( 1 ) ) &&
			!IsFalsy( g_pEngine->Cmd_Argv( 1 ) ) ) )
		{
			ArtConsoleMessage( "Usage: art_demo_unpause_on_recording <on|off>.\n" );
			return;
		}

		g_Gui.autoResumeDemoOnRecordingStart = IsTruthy( g_pEngine->Cmd_Argv( 1 ) );
		ArtConsoleMessage( "art_demo_unpause_on_recording: %s.\n",
			g_Gui.autoResumeDemoOnRecordingStart ? "on" : "off" );
	}

	void ArtPlayersThroughWalls_f()
	{
		const int argc = g_pEngine ? g_pEngine->Cmd_Argc() : 0;
		if ( argc <= 1 )
		{
			ArtConsoleMessage( "art_players_through_walls is %s. Usage: art_players_through_walls <on|off>.\n",
				InterlockedCompareExchange( &g_bPlayersPassThroughWalls, FALSE, FALSE ) ? "on" : "off" );
			return;
		}
		if ( argc != 2 || ( !IsTruthy( g_pEngine->Cmd_Argv( 1 ) ) && !IsFalsy( g_pEngine->Cmd_Argv( 1 ) ) ) )
		{
			ArtConsoleMessage( "Usage: art_players_through_walls <on|off>.\n" );
			return;
		}
		InterlockedExchange( &g_bPlayersPassThroughWalls, IsTruthy( g_pEngine->Cmd_Argv( 1 ) ) ? TRUE : FALSE );
		ArtConsoleMessage( "art_players_through_walls: %s.\n",
			InterlockedCompareExchange( &g_bPlayersPassThroughWalls, FALSE, FALSE ) ? "on" : "off" );
	}


	void ArtPlayersWorldWeapons_f()
	{
		const int argc = g_pEngine ? g_pEngine->Cmd_Argc() : 0;
		if ( argc <= 1 )
		{
			ArtConsoleMessage( "art_players_world_weapons is %s. Usage: art_players_world_weapons <on|off>.\n",
				InterlockedCompareExchange( &g_bPlayersPassWorldWeapons, FALSE, FALSE ) ? "on" : "off" );
			return;
		}
		if ( argc != 2 || ( !IsTruthy( g_pEngine->Cmd_Argv( 1 ) ) && !IsFalsy( g_pEngine->Cmd_Argv( 1 ) ) ) )
		{
			ArtConsoleMessage( "Usage: art_players_world_weapons <on|off>.\n" );
			return;
		}
		InterlockedExchange( &g_bPlayersPassWorldWeapons,
			IsTruthy( g_pEngine->Cmd_Argv( 1 ) ) ? TRUE : FALSE );
		ArtConsoleMessage( "art_players_world_weapons: %s.\n",
			InterlockedCompareExchange( &g_bPlayersPassWorldWeapons, FALSE, FALSE ) ? "on" : "off" );
	}

	void ArtVisible_f()
	{
		const int argc = g_pEngine ? g_pEngine->Cmd_Argc() : 0;
		if ( argc <= 1 || !Q_stricmp( g_pEngine->Cmd_Argv( 1 ), "status" ) )
		{
			ArtConsoleMessage( "art_visible: viewmodel=%s players=%s.\n",
				InterlockedCompareExchange( &g_bViewmodelVisible, FALSE, FALSE ) ? "on" : "off",
				InterlockedCompareExchange( &g_bPlayersVisible, FALSE, FALSE ) ? "on" : "off" );
			return;
		}
		if ( argc != 3 || ( !IsTruthy( g_pEngine->Cmd_Argv( 2 ) ) && !IsFalsy( g_pEngine->Cmd_Argv( 2 ) ) ) )
		{
			ArtConsoleMessage( "Usage: art_visible <viewmodel|players> <on|off>.\n" );
			return;
		}
		const char *pTarget = g_pEngine->Cmd_Argv( 1 );
		volatile LONG *pState = NULL;
		if ( !Q_stricmp( pTarget, "viewmodel" ) ) pState = &g_bViewmodelVisible;
		else if ( !Q_stricmp( pTarget, "players" ) ) pState = &g_bPlayersVisible;
		if ( !pState )
		{
			ArtConsoleMessage( "Usage: art_visible <viewmodel|players> <on|off>.\n" );
			return;
		}
		InterlockedExchange( pState, IsTruthy( g_pEngine->Cmd_Argv( 2 ) ) ? TRUE : FALSE );
		ArtConsoleMessage( "art_visible %s: %s.\n", pTarget,
			InterlockedCompareExchange( pState, FALSE, FALSE ) ? "on" : "off" );
	}



	void ArtNoFlash_f()
	{
		const int argc = g_pEngine ? g_pEngine->Cmd_Argc() : 0;
		if ( argc <= 1 )
		{
			ArtConsoleMessage( "art_noflash is %s. Usage: art_noflash <on|off>.\n",
				InterlockedCompareExchange( &g_bNoFlashEnabled, FALSE, FALSE ) ? "on" : "off" );
			return;
		}
		if ( argc != 2 || ( !IsTruthy( g_pEngine->Cmd_Argv( 1 ) ) && !IsFalsy( g_pEngine->Cmd_Argv( 1 ) ) ) )
		{
			ArtConsoleMessage( "Usage: art_noflash <on|off>.\n" );
			return;
		}
		const bool enabled = IsTruthy( g_pEngine->Cmd_Argv( 1 ) );
		InterlockedExchange( &g_bNoFlashEnabled, enabled ? TRUE : FALSE );
		ArtConsoleMessage( "art_noflash: %s.\n", enabled ? "on" : "off" );
	}

	void ArtNoSmoke_f()
	{
		const int argc = g_pEngine ? g_pEngine->Cmd_Argc() : 0;
		if ( argc <= 1 )
		{
			ArtConsoleMessage( "art_nosmoke is %s. Usage: art_nosmoke <on|off>.\n",
				InterlockedCompareExchange( &g_bNoSmokeEnabled, FALSE, FALSE ) ? "on" : "off" );
			return;
		}
		if ( argc != 2 || ( !IsTruthy( g_pEngine->Cmd_Argv( 1 ) ) && !IsFalsy( g_pEngine->Cmd_Argv( 1 ) ) ) )
		{
			ArtConsoleMessage( "Usage: art_nosmoke <on|off>.\n" );
			return;
		}
		const bool enabled = IsTruthy( g_pEngine->Cmd_Argv( 1 ) );
		InterlockedExchange( &g_bNoSmokeEnabled, enabled ? TRUE : FALSE );
		InterlockedExchange( &g_bNoSmokeRefreshPending, TRUE );
		ArtConsoleMessage( "art_nosmoke: %s.\n", enabled ? "on" : "off" );
	}

	void ArtForceRenderLod_f()
	{
		const int argc = g_pEngine ? g_pEngine->Cmd_Argc() : 0;
		if ( argc <= 1 || ( argc == 2 && !Q_stricmp( g_pEngine->Cmd_Argv( 1 ), "status" ) ) )
		{
			ConVar *pVar = ResolveRenderLodConVar();
			ArtConsoleMessage( "art_force_r_lod: %s; value=%ld; current=%s. Usage: art_force_r_lod <on|off|status|value <integer>>.\n",
				InterlockedCompareExchange( &g_bForceRenderLodEnabled, FALSE, FALSE ) ? "on" : "off",
				InterlockedCompareExchange( &g_nForcedRenderLodValue, 0, 0 ),
				pVar ? pVar->GetString() : "<r_lod unavailable>" );
			return;
		}

		const char *pAction = g_pEngine->Cmd_Argv( 1 );
		if ( argc == 2 && ( IsTruthy( pAction ) || IsFalsy( pAction ) ) )
		{
			const bool enabled = IsTruthy( pAction );
			SetForceRenderLodEnabled( enabled );
			ArtConsoleMessage( "art_force_r_lod: %s; value=%ld.\n", enabled ? "on" : "off",
				InterlockedCompareExchange( &g_nForcedRenderLodValue, 0, 0 ) );
			return;
		}

		if ( argc == 3 && !Q_stricmp( pAction, "value" ) )
		{
			int value = 0;
			if ( !ParseSignedInteger( g_pEngine->Cmd_Argv( 2 ), value ) )
			{
				ArtConsoleMessage( "art_force_r_lod: value must be a signed 32-bit integer.\n" );
				return;
			}
			SetForcedRenderLodValue( value );
			ArtConsoleMessage( "art_force_r_lod value: %d%s.\n", value,
				InterlockedCompareExchange( &g_bForceRenderLodEnabled, FALSE, FALSE ) ? " (applied)" : " (stored; override is off)" );
			return;
		}

		ArtConsoleMessage( "Usage: art_force_r_lod <on|off|status|value <integer>>.\n" );
	}

	void ArtObjectIdColor_f()
	{
		const int argc = g_pEngine ? g_pEngine->Cmd_Argc() : 0;
		if ( argc <= 1 )
		{
			ArtConsoleMessage( "art_objectid_color: viewmodel=%ld %ld %ld; players=%ld %ld %ld; world=%ld %ld %ld; skybox=%ld %ld %ld.\n",
				InterlockedCompareExchange( &g_nObjectIdViewmodelRed, 0, 0 ), InterlockedCompareExchange( &g_nObjectIdViewmodelGreen, 0, 0 ), InterlockedCompareExchange( &g_nObjectIdViewmodelBlue, 0, 0 ),
				InterlockedCompareExchange( &g_nObjectIdPlayersRed, 0, 0 ), InterlockedCompareExchange( &g_nObjectIdPlayersGreen, 0, 0 ), InterlockedCompareExchange( &g_nObjectIdPlayersBlue, 0, 0 ),
				InterlockedCompareExchange( &g_nObjectIdWorldRed, 0, 0 ), InterlockedCompareExchange( &g_nObjectIdWorldGreen, 0, 0 ), InterlockedCompareExchange( &g_nObjectIdWorldBlue, 0, 0 ),
				InterlockedCompareExchange( &g_nObjectIdSkyboxRed, 0, 0 ), InterlockedCompareExchange( &g_nObjectIdSkyboxGreen, 0, 0 ), InterlockedCompareExchange( &g_nObjectIdSkyboxBlue, 0, 0 ) );
			return;
		}
		if ( argc != 5 )
		{
			ArtConsoleMessage( "Usage: art_objectid_color <viewmodel|players|world|skybox> <r> <g> <b>.\n" );
			return;
		}
		const char *pTarget = g_pEngine->Cmd_Argv( 1 );
		volatile LONG *pRed = NULL;
		volatile LONG *pGreen = NULL;
		volatile LONG *pBlue = NULL;
		if ( !Q_stricmp( pTarget, "viewmodel" ) )
		{ pRed = &g_nObjectIdViewmodelRed; pGreen = &g_nObjectIdViewmodelGreen; pBlue = &g_nObjectIdViewmodelBlue; }
		else if ( !Q_stricmp( pTarget, "players" ) || !Q_stricmp( pTarget, "player" ) )
		{ pRed = &g_nObjectIdPlayersRed; pGreen = &g_nObjectIdPlayersGreen; pBlue = &g_nObjectIdPlayersBlue; }
		else if ( !Q_stricmp( pTarget, "world" ) )
		{ pRed = &g_nObjectIdWorldRed; pGreen = &g_nObjectIdWorldGreen; pBlue = &g_nObjectIdWorldBlue; }
		else if ( !Q_stricmp( pTarget, "skybox" ) )
		{ pRed = &g_nObjectIdSkyboxRed; pGreen = &g_nObjectIdSkyboxGreen; pBlue = &g_nObjectIdSkyboxBlue; }
		else
		{
			ArtConsoleMessage( "art_objectid_color: unknown category '%s'.\n", pTarget );
			return;
		}
		const int red = ClampColorByte( atoi( g_pEngine->Cmd_Argv( 2 ) ) );
		const int green = ClampColorByte( atoi( g_pEngine->Cmd_Argv( 3 ) ) );
		const int blue = ClampColorByte( atoi( g_pEngine->Cmd_Argv( 4 ) ) );
		InterlockedExchange( pRed, red );
		InterlockedExchange( pGreen, green );
		InterlockedExchange( pBlue, blue );
		ArtConsoleMessage( "art_objectid_color %s: %d %d %d.\n", pTarget, red, green, blue );
	}

	void PrintChamsUsage()
	{
		ArtConsoleMessage( "Usage: art_chams <players|viewmodel|skybox> <on|off>.\n" );
		ArtConsoleMessage( "       art_chams <players_color|viewmodel_color|skybox_color> <r> <g> <b>.\n" );
		ArtConsoleMessage( "       art_chams players_through_walls <on|off>.\n" );
		ArtConsoleMessage( "       art_chams status.\n" );
	}

	void ArtChams_f()
	{
		const int argc = g_pEngine ? g_pEngine->Cmd_Argc() : 0;
		if ( argc <= 1 )
		{
			PrintChamsUsage();
			return;
		}

		const char *pTarget = g_pEngine->Cmd_Argv( 1 );
		if ( !Q_stricmp( pTarget, "status" ) )
		{
			ArtConsoleMessage( "art_chams: players=%s through_walls=%s color=%ld %ld %ld, viewmodel=%s color=%ld %ld %ld, skybox=%s color=%ld %ld %ld, skybox_materials=%d, model_hook=%s.\n",
				InterlockedCompareExchange( &g_bPlayerChamsEnabled, FALSE, FALSE ) ? "on" : "off",
				InterlockedCompareExchange( &g_bPlayerChamsThroughWalls, FALSE, FALSE ) ? "on" : "off",
				InterlockedCompareExchange( &g_nPlayerChamsRed, 0, 0 ),
				InterlockedCompareExchange( &g_nPlayerChamsGreen, 0, 0 ),
				InterlockedCompareExchange( &g_nPlayerChamsBlue, 0, 0 ),
				InterlockedCompareExchange( &g_bViewmodelChamsEnabled, FALSE, FALSE ) ? "on" : "off",
				InterlockedCompareExchange( &g_nViewmodelChamsRed, 0, 0 ),
				InterlockedCompareExchange( &g_nViewmodelChamsGreen, 0, 0 ),
				InterlockedCompareExchange( &g_nViewmodelChamsBlue, 0, 0 ),
				InterlockedCompareExchange( &g_bSkyboxChamsEnabled, FALSE, FALSE ) ? "on" : "off",
				InterlockedCompareExchange( &g_nSkyboxChamsRed, 0, 0 ),
				InterlockedCompareExchange( &g_nSkyboxChamsGreen, 0, 0 ),
				InterlockedCompareExchange( &g_nSkyboxChamsBlue, 0, 0 ),
				static_cast<int>( g_SkyboxMaterialStates.size() ),
				IsModelRenderHookReady() ? "ready" : "unavailable" );
			return;
		}

		if ( !Q_stricmp( pTarget, "players_through_walls" ) )
		{
			if ( argc != 3 || ( !IsTruthy( g_pEngine->Cmd_Argv( 2 ) ) && !IsFalsy( g_pEngine->Cmd_Argv( 2 ) ) ) )
			{
				PrintChamsUsage();
				return;
			}
			InterlockedExchange( &g_bPlayerChamsThroughWalls,
				IsTruthy( g_pEngine->Cmd_Argv( 2 ) ) ? TRUE : FALSE );
			ArtConsoleMessage( "art_chams %s: %s.\n", pTarget,
				InterlockedCompareExchange( &g_bPlayerChamsThroughWalls, FALSE, FALSE ) ? "on" : "off" );
			return;
		}

		if ( !Q_stricmp( pTarget, "skybox" ) )
		{
			if ( argc != 3 || ( !IsTruthy( g_pEngine->Cmd_Argv( 2 ) ) && !IsFalsy( g_pEngine->Cmd_Argv( 2 ) ) ) )
			{
				PrintChamsUsage();
				return;
			}
			const bool enabled = IsTruthy( g_pEngine->Cmd_Argv( 2 ) );
			InterlockedExchange( &g_bSkyboxChamsEnabled, enabled ? TRUE : FALSE );
			InterlockedExchange( &g_bSkyboxChamsRefreshPending, TRUE );
			ArtConsoleMessage( "art_chams skybox: %s.\n", enabled ? "on" : "off" );
			return;
		}

		const bool playerTarget = !Q_stricmp( pTarget, "players" );
		const bool viewmodelTarget = !Q_stricmp( pTarget, "viewmodel" );
		if ( playerTarget || viewmodelTarget )
		{
			if ( argc != 3 || ( !IsTruthy( g_pEngine->Cmd_Argv( 2 ) ) && !IsFalsy( g_pEngine->Cmd_Argv( 2 ) ) ) )
			{
				PrintChamsUsage();
				return;
			}
			const bool enabled = IsTruthy( g_pEngine->Cmd_Argv( 2 ) );
			if ( enabled && !ResolveFlatChamsMaterial() )
			{
				ArtConsoleMessage( "art_chams: flat model material is unavailable; chams were not enabled.\n" );
				return;
			}
			volatile LONG *pState = playerTarget ? &g_bPlayerChamsEnabled : &g_bViewmodelChamsEnabled;
			InterlockedExchange( pState, enabled ? TRUE : FALSE );
			ArtConsoleMessage( "art_chams %s: %s.\n", pTarget, enabled ? "on" : "off" );
			return;
		}

		const bool playerColor = !Q_stricmp( pTarget, "players_color" );
		const bool viewmodelColor = !Q_stricmp( pTarget, "viewmodel_color" );
		const bool skyboxColor = !Q_stricmp( pTarget, "skybox_color" );
		if ( playerColor || viewmodelColor || skyboxColor )
		{
			if ( argc != 5 )
			{
				PrintChamsUsage();
				return;
			}
			const int red = ClampColorByte( atoi( g_pEngine->Cmd_Argv( 2 ) ) );
			const int green = ClampColorByte( atoi( g_pEngine->Cmd_Argv( 3 ) ) );
			const int blue = ClampColorByte( atoi( g_pEngine->Cmd_Argv( 4 ) ) );
			volatile LONG *pRed = playerColor ? &g_nPlayerChamsRed :
				( viewmodelColor ? &g_nViewmodelChamsRed : &g_nSkyboxChamsRed );
			volatile LONG *pGreen = playerColor ? &g_nPlayerChamsGreen :
				( viewmodelColor ? &g_nViewmodelChamsGreen : &g_nSkyboxChamsGreen );
			volatile LONG *pBlue = playerColor ? &g_nPlayerChamsBlue :
				( viewmodelColor ? &g_nViewmodelChamsBlue : &g_nSkyboxChamsBlue );
			InterlockedExchange( pRed, red );
			InterlockedExchange( pGreen, green );
			InterlockedExchange( pBlue, blue );
			if ( skyboxColor ) InterlockedExchange( &g_bSkyboxChamsRefreshPending, TRUE );
			ArtConsoleMessage( "art_chams %s: %d %d %d.\n", pTarget, red, green, blue );
			return;
		}

		PrintChamsUsage();
	}
	void ArtConfig_f()
	{
		const int argc = g_pEngine ? g_pEngine->Cmd_Argc() : 0;
		if ( argc <= 1 )
		{
			ArtConsoleMessage( "Usage: art_config <save|load|delete|list> [name].\n" );
			return;
		}

		const char *pAction = g_pEngine->Cmd_Argv( 1 );
		if ( !Q_stricmp( pAction, "list" ) )
		{
			PrintConfigList();
			return;
		}
		if ( argc != 3 )
		{
			ArtConsoleMessage( "art_config: '%s' requires one config name.\n", pAction );
			return;
		}

		const char *pName = g_pEngine->Cmd_Argv( 2 );
		if ( !Q_stricmp( pAction, "save" ) ) SaveConfig( pName );
		else if ( !Q_stricmp( pAction, "load" ) ) LoadConfig( pName );
		else if ( !Q_stricmp( pAction, "delete" ) ) DeleteConfig( pName );
		else ArtConsoleMessage( "Usage: art_config <save|load|delete|list> [name].\n" );
	}

	ConCommand g_ArtGuiCommand( "art_gui", ArtGui_f,
		"Open or close the CS:S v34 control panel." );
	ConCommand g_ArtGlobalFovCommand( "art_fov", ArtGlobalFov_f,
		"HLAE-style global camera FOV override with optional zoom preservation." );
	ConCommand g_ArtGuiKeyCommand( "art_gui_key", ArtGuiKey_f,
		"Set the keyboard key or modifier chord that toggles the menu." );
	ConCommand g_ArtGuiColorCommand( "art_gui_color", ArtGuiColor_f,
		"Set a menu color using 0-255 RGBA values." );
	ConCommand g_ArtGuiThemeCommand( "art_gui_theme", ArtGuiTheme_f,
		"Apply a preset menu color theme." );
	ConCommand g_ArtOverlayCommand( "art_overlay", ArtOverlay_f,
		"Show or hide the capture-excluded in-game recording statistics overlay." );
	ConCommand g_ArtOpenFolderCommand( "art_open_folder", ArtOpenFolder_f,
		"Open the configured ART recording output folder." );
	ConCommand g_ArtGuiExperimentalCommand( "art_gui_experimental", ArtGuiExperimental_f,
		"Show or hide experimental menu controls." );
	ConCommand g_ArtHlaeInputWhileGuiCommand( "art_hlae_input_while_gui",
		ArtHlaeInputWhileGui_f,
		"Allow mirv_input while the ART GUI is open by holding a configured key outside the window." );
	ConCommand g_ArtHlaeInputHoldKeyCommand( "art_hlae_input_hold_key",
		ArtHlaeInputHoldKey_f,
		"Set the hold key used for mirv_input control while the ART GUI remains open." );
	ConCommand g_ArtDemoPauseAfterRecordingCommand( "art_demo_pause_after_recording",
		ArtDemoPauseAfterRecording_f,
		"Pause demo playback automatically when recording stops (experimental)." );
	ConCommand g_ArtDemoUnpauseOnRecordingCommand( "art_demo_unpause_on_recording",
		ArtDemoUnpauseOnRecording_f,
		"Unpause demo playback automatically when recording starts." );
	ConCommand g_ArtChamsCommand( "art_chams", ArtChams_f,
		"Enable flat colors for players, viewmodel, and skybox." );
	ConCommand g_ArtPlayersThroughWallsCommand( "art_players_through_walls", ArtPlayersThroughWalls_f,
		"Choose whether the players pass renders player models through world geometry." );
	ConCommand g_ArtPlayersWorldWeaponsCommand( "art_players_world_weapons", ArtPlayersWorldWeapons_f,
		"Choose whether world weapon models are isolated with players or left in the keyed world." );
	ConCommand g_ArtVisibleCommand( "art_visible", ArtVisible_f,
		"Set whether viewmodel or player models are visible." );
	ConCommand g_ArtNoFlashCommand( "art_noflash", ArtNoFlash_f,
		"Remove flashbang screen effects from live view and recording." );
	ConCommand g_ArtNoSmokeCommand( "art_nosmoke", ArtNoSmoke_f,
		"Hide smoke particle materials from live view and recording." );
	ConCommand g_ArtForceRenderLodCommand( "art_force_r_lod", ArtForceRenderLod_f,
		"Force r_lod to a chosen integer and block demo cvar updates." );
	ConCommand g_ArtObjectIdColorCommand( "art_objectid_color", ArtObjectIdColor_f,
		"Set ObjectID colors for viewmodel, players, world, and skybox." );
	ConCommand g_ArtConfigCommand( "art_config", ArtConfig_f,
		"Save, load, delete, or list CS:S V34 ADVANCED RECORDING TOOLS configs." );

	bool IsCommandRegistered( const char *pName )
	{
		if ( !g_pCvar || !pName || !pName[0] )
			return false;

		for ( const ConCommandBase *pBase = g_pCvar->GetCommands(); pBase; pBase = pBase->GetNext() )
		{
			const char *pExistingName = pBase->GetName();
			if ( pExistingName && !_stricmp( pExistingName, pName ) )
				return true;
		}

		return false;
	}

	void RegisterGuiCommand( ConCommandBase *pCommand )
	{
		if ( !pCommand || !g_pCvar || IsCommandRegistered( pCommand->GetName() ) )
			return;
		pCommand->AddFlags( FCVAR_PLUGIN | FCVAR_CLIENTCMD_CAN_EXECUTE );
		pCommand->SetNext( NULL );
		g_pCvar->RegisterConCommandBase( pCommand );
		LogMessage( "GUI COMMAND REGISTERED: name='%s'", pCommand->GetName() );
	}

	void EnableGuiExecutionForArtCommands()
	{
		if ( !g_pCvar )
			return;

		for ( const ConCommandBase *pBase = g_pCvar->GetCommands(); pBase; pBase = pBase->GetNext() )
		{
			const char *pName = pBase->GetName();
			if ( pName && ( !_strnicmp( pName, "art_", 4 ) ||
				!_strnicmp( pName, "mirv_", 5 ) ) )
				const_cast<ConCommandBase *>( pBase )->AddFlags( FCVAR_CLIENTCMD_CAN_EXECUTE );
		}
	}
}

bool PauseArtDemoAfterRecordingIfEnabled()
{
	if ( !g_Gui.experimentalOptionsEnabled || !g_Gui.autoPauseDemoAfterRecording ||
		!g_pEngine || !g_pEngine->IsPlayingDemo() )
	{
		return false;
	}
	IssueCommand( "demo_pause" );
	LogMessage( "DEMO AUTO-PAUSE REQUESTED: reason=recording_stopped experimental=1" );
	return true;
}

bool ResumeArtDemoBeforeRecordingIfEnabled()
{
	if ( !g_Gui.autoResumeDemoOnRecordingStart || !g_pEngine || !g_pEngine->IsPlayingDemo() )
		return false;

	IssueCommand( "demo_resume" );
	LogMessage( "DEMO AUTO-RESUME REQUESTED: reason=recording_started" );
	return true;
}


bool BeginArtObjectIdPass()
{
	if ( InterlockedCompareExchange( &g_bArtGuiTerminating, FALSE, FALSE ) || !g_pMaterials )
		return false;
	if ( InterlockedCompareExchange( &g_bObjectIdUpdateActive, TRUE, FALSE ) != FALSE )
		return false;
	if ( InterlockedCompareExchange( &g_bObjectIdPassActive, FALSE, FALSE ) )
	{
		InterlockedExchange( &g_bObjectIdUpdateActive, FALSE );
		return false;
	}
	// Reuse the same flat material that already works for player and viewmodel chams.
	// World geometry is categorized by fog and the skybox is the clear color.
	if ( !ResolveFlatChamsMaterial() )
	{
		InterlockedExchange( &g_bObjectIdUpdateActive, FALSE );
		return false;
	}
	InterlockedExchange( &g_bObjectIdPassActive, TRUE );
	InterlockedExchange( &g_bObjectIdUpdateActive, FALSE );
	LogMessage( "OBJECTID PASS BEGIN: fog_world=1 flat_players_viewmodel=1 clear_skybox=1" );
	return true;
}

void EndArtObjectIdPass()
{
	InterlockedExchange( &g_bObjectIdPassActive, FALSE );
	LogMessage( "OBJECTID PASS END" );
}

void GetArtObjectIdColors( int &worldRed, int &worldGreen, int &worldBlue,
	int &skyboxRed, int &skyboxGreen, int &skyboxBlue )
{
	worldRed = static_cast<int>( InterlockedCompareExchange( &g_nObjectIdWorldRed, 0, 0 ) );
	worldGreen = static_cast<int>( InterlockedCompareExchange( &g_nObjectIdWorldGreen, 0, 0 ) );
	worldBlue = static_cast<int>( InterlockedCompareExchange( &g_nObjectIdWorldBlue, 0, 0 ) );
	skyboxRed = static_cast<int>( InterlockedCompareExchange( &g_nObjectIdSkyboxRed, 0, 0 ) );
	skyboxGreen = static_cast<int>( InterlockedCompareExchange( &g_nObjectIdSkyboxGreen, 0, 0 ) );
	skyboxBlue = static_cast<int>( InterlockedCompareExchange( &g_nObjectIdSkyboxBlue, 0, 0 ) );
}

void GetArtObjectIdCategoryColors( int colors[4][3] )
{
	if ( !colors )
		return;
	colors[0][0] = static_cast<int>( InterlockedCompareExchange( &g_nObjectIdViewmodelRed, 0, 0 ) );
	colors[0][1] = static_cast<int>( InterlockedCompareExchange( &g_nObjectIdViewmodelGreen, 0, 0 ) );
	colors[0][2] = static_cast<int>( InterlockedCompareExchange( &g_nObjectIdViewmodelBlue, 0, 0 ) );
	colors[1][0] = static_cast<int>( InterlockedCompareExchange( &g_nObjectIdPlayersRed, 0, 0 ) );
	colors[1][1] = static_cast<int>( InterlockedCompareExchange( &g_nObjectIdPlayersGreen, 0, 0 ) );
	colors[1][2] = static_cast<int>( InterlockedCompareExchange( &g_nObjectIdPlayersBlue, 0, 0 ) );
	colors[2][0] = static_cast<int>( InterlockedCompareExchange( &g_nObjectIdWorldRed, 0, 0 ) );
	colors[2][1] = static_cast<int>( InterlockedCompareExchange( &g_nObjectIdWorldGreen, 0, 0 ) );
	colors[2][2] = static_cast<int>( InterlockedCompareExchange( &g_nObjectIdWorldBlue, 0, 0 ) );
	colors[3][0] = static_cast<int>( InterlockedCompareExchange( &g_nObjectIdSkyboxRed, 0, 0 ) );
	colors[3][1] = static_cast<int>( InterlockedCompareExchange( &g_nObjectIdSkyboxGreen, 0, 0 ) );
	colors[3][2] = static_cast<int>( InterlockedCompareExchange( &g_nObjectIdSkyboxBlue, 0, 0 ) );
}

void MaintainArtVisualEffectsForRender()
{
	if ( InterlockedCompareExchange( &g_bArtGuiTerminating, FALSE, FALSE ) )
		return;
	MaintainNoFlashState();
	MaintainNoSmokeMaterials();
	MaintainForcedRenderLod();
}

void MaintainArtSkyboxChamsForRender()
{
	if ( InterlockedCompareExchange( &g_bArtGuiTerminating, FALSE, FALSE ) )
		return;
	MaintainSkyboxChamsMaterials();
}

bool AreArtPlayersPassWorldWeaponsEnabled()
{
	return InterlockedCompareExchange( &g_bPlayersPassWorldWeapons, FALSE, FALSE ) != FALSE;
}

bool IsArtWorldWeaponModel( const ModelRenderInfo_t &info )
{
	IVModelInfo *pModelInfo = ResolveGuiModelInfo();
	const char *pModelName = pModelInfo && info.pModel ? pModelInfo->GetModelName( info.pModel ) : NULL;
	if ( IsWorldWeaponModelName( pModelName ) )
		return true;

	IClientNetworkable *pNetworkable = g_pEntityList && info.entity_index > 0 ?
		g_pEntityList->GetClientNetworkable( info.entity_index ) : NULL;
	const char *pClassName = GetChamsEntityClassName( pNetworkable );
	return pClassName && ContainsInsensitive( pClassName, "weapon" ) &&
		!IsViewmodelChamsClassName( pClassName );
}

bool HandleArtGuiDrawModelEx( IVModelRender *pThis, ModelRenderInfo_t &info,
	ArtDrawModelExFn pOriginalDrawModelEx, bool playerPassActive, bool playerPassEntity, int &result )
{
	if ( !pThis || !pOriginalDrawModelEx ||
		InterlockedCompareExchange( &g_bArtGuiTerminating, FALSE, FALSE ) )
		return false;

	const bool playerEntity = playerPassEntity || IsPlayerChamsEntity( info.entity_index );
	const bool viewmodelEntity = !playerEntity &&
		( IsViewmodelChamsEntity( info.entity_index ) || IsViewmodelModel( info.pModel ) );

	if ( playerEntity && !InterlockedCompareExchange( &g_bPlayersVisible, FALSE, FALSE ) )
	{
		result = 0;
		return true;
	}
	if ( viewmodelEntity && !InterlockedCompareExchange( &g_bViewmodelVisible, FALSE, FALSE ) )
	{
		result = 0;
		return true;
	}

	if ( InterlockedCompareExchange( &g_bObjectIdPassActive, FALSE, FALSE ) )
	{
		// World and ordinary map models stay on Source's normal path and are
		// categorized by full-distance fog. Only foreground categories are overridden.
		if ( !( playerEntity || viewmodelEntity ) )
			return false;

		IMaterial *pMaterial = ResolveFlatChamsMaterial();
		if ( !pMaterial || !g_pMaterials )
			return false;

		volatile LONG *pRed = viewmodelEntity ? &g_nObjectIdViewmodelRed : &g_nObjectIdPlayersRed;
		volatile LONG *pGreen = viewmodelEntity ? &g_nObjectIdViewmodelGreen : &g_nObjectIdPlayersGreen;
		volatile LONG *pBlue = viewmodelEntity ? &g_nObjectIdViewmodelBlue : &g_nObjectIdPlayersBlue;
		const float red = InterlockedCompareExchange( pRed, 0, 0 ) / 255.0f;
		const float green = InterlockedCompareExchange( pGreen, 0, 0 ) / 255.0f;
		const float blue = InterlockedCompareExchange( pBlue, 0, 0 ) / 255.0f;

		MaterialOverrideRestore materialRestore;
		if ( !materialRestore.Apply( pMaterial, false, red, green, blue ) )
			return false;
		const MaterialFogMode_t previousFogMode = g_pMaterials->GetFogMode();
		g_pMaterials->FogMode( MATERIAL_FOG_NONE );
		RenderViewModulationRestore renderViewRestore;
		renderViewRestore.Apply( red, green, blue );
		pThis->ForcedMaterialOverride( pMaterial );
		result = pOriginalDrawModelEx( pThis, info );
		pThis->ForcedMaterialOverride( NULL );
		renderViewRestore.Restore();
		materialRestore.Restore();
		g_pMaterials->FogMode( previousFogMode );
		return true;
	}

	if ( viewmodelEntity )
	{
		if ( !InterlockedCompareExchange( &g_bViewmodelChamsEnabled, FALSE, FALSE ) )
			return false;
		IMaterial *pMaterial = g_pFlatChamsMaterial;
		const float red = InterlockedCompareExchange( &g_nViewmodelChamsRed, 0, 0 ) / 255.0f;
		const float green = InterlockedCompareExchange( &g_nViewmodelChamsGreen, 0, 0 ) / 255.0f;
		const float blue = InterlockedCompareExchange( &g_nViewmodelChamsBlue, 0, 0 ) / 255.0f;
		MaterialOverrideRestore restore;
		RenderViewModulationRestore renderViewRestore;
		if ( !restore.Apply( pMaterial, false, red, green, blue ) )
			return false;
		renderViewRestore.Apply( red, green, blue );
		pThis->ForcedMaterialOverride( pMaterial );
		result = pOriginalDrawModelEx( pThis, info );
		pThis->ForcedMaterialOverride( NULL );
		restore.Restore();
		return true;
	}

	if ( !playerEntity )
		return false;

	const bool playerChams = InterlockedCompareExchange( &g_bPlayerChamsEnabled, FALSE, FALSE ) != FALSE;
	const bool passThroughWalls = playerPassActive && playerPassEntity &&
		InterlockedCompareExchange( &g_bPlayersPassThroughWalls, FALSE, FALSE ) != FALSE;
	const bool chamsThroughWalls = playerChams &&
		InterlockedCompareExchange( &g_bPlayerChamsThroughWalls, FALSE, FALSE ) != FALSE;
	if ( !playerChams && !passThroughWalls )
		return false;

	IMaterial *pMaterial = playerChams ? g_pFlatChamsMaterial : NULL;
	MaterialOverrideRestore materialRestore;
	const bool materialApplied = playerChams && materialRestore.Apply( pMaterial, chamsThroughWalls,
		InterlockedCompareExchange( &g_nPlayerChamsRed, 0, 0 ) / 255.0f,
		InterlockedCompareExchange( &g_nPlayerChamsGreen, 0, 0 ) / 255.0f,
		InterlockedCompareExchange( &g_nPlayerChamsBlue, 0, 0 ) / 255.0f );
	if ( playerChams && !materialApplied && !passThroughWalls )
		return false;

	const MaterialFogMode_t previousFogMode = g_pMaterials ? g_pMaterials->GetFogMode() : MATERIAL_FOG_NONE;
	if ( playerPassEntity && g_pMaterials ) g_pMaterials->FogMode( MATERIAL_FOG_NONE );
	if ( passThroughWalls && g_pMaterials ) g_pMaterials->DepthRange( 0.0f, 0.01f );
	RenderViewModulationRestore renderViewRestore;
	if ( materialApplied )
	{
		const float red = InterlockedCompareExchange( &g_nPlayerChamsRed, 0, 0 ) / 255.0f;
		const float green = InterlockedCompareExchange( &g_nPlayerChamsGreen, 0, 0 ) / 255.0f;
		const float blue = InterlockedCompareExchange( &g_nPlayerChamsBlue, 0, 0 ) / 255.0f;
		renderViewRestore.Apply( red, green, blue );
		pThis->ForcedMaterialOverride( pMaterial );
	}

	result = pOriginalDrawModelEx( pThis, info );

	if ( materialApplied ) pThis->ForcedMaterialOverride( NULL );
	renderViewRestore.Restore();
	materialRestore.Restore();
	if ( passThroughWalls && g_pMaterials ) g_pMaterials->DepthRange( 0.0f, 1.0f );
	if ( playerPassEntity && g_pMaterials ) g_pMaterials->FogMode( previousFogMode );
	return true;
}

bool InstallArtGui()
{
	InterlockedExchange( &g_bArtGuiTerminating, FALSE );
	InterlockedExchange( &g_bWindowCloseCleanupStarted, FALSE );
	if ( g_Gui.installed )
		return true;
	if ( !g_pEngine || !g_pCvar )
	{
		SetError( "InstallArtGui called before engine interfaces were ready" );
		return false;
	}

	RegisterGuiCommand( &g_ArtGuiCommand );
	RegisterGuiCommand( &g_ArtGlobalFovCommand );
	RegisterGuiCommand( &g_ArtGuiKeyCommand );
	RegisterGuiCommand( &g_ArtGuiColorCommand );
	RegisterGuiCommand( &g_ArtGuiThemeCommand );
	RegisterGuiCommand( &g_ArtOverlayCommand );
	RegisterGuiCommand( &g_ArtOpenFolderCommand );
	RegisterGuiCommand( &g_ArtGuiExperimentalCommand );
	RegisterGuiCommand( &g_ArtHlaeInputWhileGuiCommand );
	RegisterGuiCommand( &g_ArtHlaeInputHoldKeyCommand );
	RegisterGuiCommand( &g_ArtDemoPauseAfterRecordingCommand );
	RegisterGuiCommand( &g_ArtDemoUnpauseOnRecordingCommand );
	RegisterGuiCommand( &g_ArtChamsCommand );
	RegisterGuiCommand( &g_ArtPlayersThroughWallsCommand );
	RegisterGuiCommand( &g_ArtPlayersWorldWeaponsCommand );
	RegisterGuiCommand( &g_ArtVisibleCommand );
	RegisterGuiCommand( &g_ArtNoFlashCommand );
	RegisterGuiCommand( &g_ArtNoSmokeCommand );
	RegisterGuiCommand( &g_ArtForceRenderLodCommand );
	RegisterGuiCommand( &g_ArtObjectIdColorCommand );
	RegisterGuiCommand( &g_ArtConfigCommand );
	EnableGuiExecutionForArtCommands();
	g_Gui.commandsRegistered = true;
	InstallRenderLodCvarHook();
	EnsureDefaultConfig();
	ResolveGuiModelInfo();
	ResolveFlatChamsMaterial();

	if ( !InstallD3D9Hooks() )
	{
		ArtConsoleMessage( "v34_art: GUI hook failed: %s. Recorder commands remain available.\n",
			g_Gui.lastError[0] ? g_Gui.lastError : "unknown error" );
		return false;
	}

	g_Gui.installed = true;
	char keyName[96];
	GetToggleBindingName( g_Gui.toggleKey, g_Gui.toggleModifiers, keyName, sizeof( keyName ), true );
	ArtConsoleMessage( "art_gui: control panel ready. Press %s or run art_gui toggle.\n", keyName );
	LogMessage( "GUI INSTALL SUCCESS: gui=%s minhook_pinned=%s", kGuiVersion,
		kMinHookVersionPinned );
	return true;
}

void ShutdownArtGui()
{
	SetForceRenderLodEnabled( false );
	if ( !g_Gui.installed && !g_Gui.imguiReady )
		return;
	BeginArtGuiTermination();
	ShutdownArtHlae();
	InterlockedExchange( &g_bSkyboxChamsEnabled, FALSE );
	InterlockedExchange( &g_bSkyboxChamsRefreshPending, FALSE );
	InterlockedExchange( &g_bNoFlashEnabled, FALSE );
	InterlockedExchange( &g_bNoSmokeEnabled, FALSE );
	InterlockedExchange( &g_bNoSmokeRefreshPending, FALSE );
	InterlockedExchange( &g_bObjectIdPassActive, FALSE );
	InterlockedExchange( &g_bObjectIdUpdateActive, FALSE );
	if ( !g_SkyboxMaterialStates.empty() )
		RestoreSkyboxChamsMaterials();
	if ( !g_SmokeMaterialStates.empty() )
		RestoreNoSmokeMaterials();
	InterlockedExchange( &g_GuiVisible, FALSE );
	RemoveD3D9Hooks();
	ShutdownImGui();
	g_pFlatChamsMaterial = NULL;
	g_pGuiModelInfo = NULL;
	g_Gui.installed = false;
	LogMessage( "GUI SHUTDOWN COMPLETE" );
}

bool IsArtGuiVisible()
{
	return !InterlockedCompareExchange( &g_bArtGuiTerminating, FALSE, FALSE ) &&
		InterlockedCompareExchange( &g_GuiVisible, FALSE, FALSE ) != FALSE;
}

bool IsArtGuiMirvInputPassthroughActive()
{
	return g_Gui.hlaeInputWhileGuiActive;
}

void SetArtGuiVisible( bool visible )
{
	if ( visible && InterlockedCompareExchange( &g_bArtGuiTerminating, FALSE, FALSE ) )
		return;
	const bool previous = InterlockedExchange( &g_GuiVisible, visible ? TRUE : FALSE ) != FALSE;
	if ( previous == visible )
		return;
	if ( visible )
	{
		if ( g_Gui.imguiReady )
			UpdateMouseCaptureMode();
	}
	else
	{
		SetHlaeInputWhileGuiActive( false );
		g_Gui.waitingForHlaeInputHoldKey = false;
		if ( g_Gui.imguiReady )
			ImGui::GetIO().MouseDrawCursor = false;
		RestoreGameMouse();
		if ( g_hGameWindow )
			PostMessageA( g_hGameWindow, WM_SETCURSOR, reinterpret_cast<WPARAM>( g_hGameWindow ),
				MAKELPARAM( HTCLIENT, WM_MOUSEMOVE ) );
	}
	LogMessage( "GUI VISIBILITY: %s", visible ? "on" : "off" );
}
}
