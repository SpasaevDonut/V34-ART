// DLL entry point and build-4044 interface initialization.
// Attribution and third-party notices: ../THIRD_PARTY_NOTICES.md.

#include "art_internal.h"


#include "art_gui.h"
#include "art_hlae.h"
// Emergency startup diagnostics: change to true and rebuild to log immediately on injection.
// Keep false for normal releases; logging can still be enabled later with art_debug on.
static const bool kEnableDebugLoggingOnInjection = false;

// memdbgon must be the last include file in a .cpp file.
#include "tier0/memdbgon.h"

namespace art
{
	namespace
	{
		CreateInterfaceFn GetFactory( HMODULE hModule )
		{
			CreateInterfaceFn factory = hModule ? reinterpret_cast<CreateInterfaceFn>(
				GetProcAddress( hModule, CREATEINTERFACE_PROCNAME ) ) : NULL;
			LogMessage( "FACTORY LOOKUP: module=%p export='%s' result=%p last_error=%lu",
				hModule, CREATEINTERFACE_PROCNAME, factory, factory ? 0 : GetLastError() );
			return factory;
		}

		template <typename T>
		T *QueryInterface( CreateInterfaceFn factory, const char *pVersion )
		{
			int returnCode = -999;
			T *pResult = factory ? static_cast<T *>( factory( pVersion, &returnCode ) ) : NULL;
			LogMessage( "INTERFACE QUERY: version='%s' factory=%p result=%p return_code=%d",
				pVersion, factory, pResult, returnCode );
			return pResult;
		}

		void LogModuleInfo( const char *pName, HMODULE hModule )
		{
			char path[MAX_PATH] = "<unavailable>";
			const DWORD length = hModule ? GetModuleFileNameA( hModule, path, sizeof( path ) ) : 0;
			LogMessage( "MODULE: name='%s' handle=%p path='%s' path_length=%lu error=%lu",
				pName, hModule, length ? path : "<GetModuleFileNameA failed>", length,
				length ? 0 : GetLastError() );
		}

		DWORD WINAPI InitializeArt( void * )
		{
			LogMessage( "INITIALIZER THREAD BEGIN: dll=%p host_exe='%s' log='%s'",
				g_hThisModule, GetHostExecutablePath(), GetLogPath() );
			LogModuleInfo( "v34_art.dll", g_hThisModule );

			HMODULE hPinnedModule = NULL;
			if ( !GetModuleHandleExA( GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_PIN,
				reinterpret_cast<LPCSTR>( &g_hThisModule ), &hPinnedModule ) )
			{
				const DWORD error = GetLastError();
				LogMessage( "INITIALIZATION FAILED (code 4): injected module could not be pinned at initializer startup; error=%lu", error );
				FlushLog();
				return 4;
			}
			LogMessage( "MODULE PIN COMPLETE: pinned_handle=%p (performed before game-module discovery)", hPinnedModule );

			HMODULE hEngine = NULL;
			HMODULE hMaterialSystem = NULL;
			HMODULE hFileSystem = NULL;
			HMODULE hGameUI = NULL;
			unsigned long waitIterations = 0;
			for ( ;; )
			{
				g_hClientModule = GetModuleHandleA( "client.dll" );
				hEngine = GetModuleHandleA( "engine.dll" );
				hMaterialSystem = GetModuleHandleA( "materialsystem.dll" );
				hGameUI = GetModuleHandleA( "GameUI.dll" );
				if ( g_hClientModule && hEngine && hMaterialSystem && hGameUI )
					break;

				if ( waitIterations == 0 || waitIterations % 50 == 0 )
				{
					LogMessage( "MODULE WAIT: iteration=%lu client.dll=%p engine.dll=%p materialsystem.dll=%p GameUI.dll=%p; retrying in 100ms",
						waitIterations, g_hClientModule, hEngine, hMaterialSystem, hGameUI );
					FlushLog();
				}
				++waitIterations;
				Sleep( 100 );
			}

			LogMessage( "MODULE WAIT COMPLETE: iterations=%lu", waitIterations );
			g_hEngineModule = hEngine;
			LogModuleInfo( "client.dll", g_hClientModule );
			LogModuleInfo( "engine.dll", hEngine );
			LogModuleInfo( "materialsystem.dll", hMaterialSystem );
			LogModuleInfo( "GameUI.dll", hGameUI );

			CreateInterfaceFn clientFactory = GetFactory( g_hClientModule );
			CreateInterfaceFn engineFactory = GetFactory( hEngine );
			CreateInterfaceFn materialFactory = GetFactory( hMaterialSystem );
			CreateInterfaceFn gameUiFactory = GetFactory( hGameUI );
			g_pClient = QueryInterface<IBaseClientDLL>( clientFactory, CLIENT_DLL_INTERFACE_VERSION );
			g_pEntityList = QueryInterface<IClientEntityList>( clientFactory, VCLIENTENTITYLIST_INTERFACE_VERSION );
			g_pEngine = QueryInterface<IVEngineClient>( engineFactory, VENGINE_CLIENT_INTERFACE_VERSION );
			g_pCvar = QueryInterface<ICvar>( engineFactory, VENGINE_CVAR_INTERFACE_VERSION );
			g_pRenderView = QueryInterface<IVRenderView>( engineFactory, VENGINE_RENDERVIEW_INTERFACE_VERSION );
			g_pModelRender = QueryInterface<IVModelRender>( engineFactory, VENGINE_HUDMODEL_INTERFACE_VERSION );
			g_pMaterials = QueryInterface<IMaterialSystem>( materialFactory, MATERIAL_SYSTEM_INTERFACE_VERSION );
			g_pGameConsole = QueryInterface<IGameConsole003>( gameUiFactory, "GameConsole003" );
			g_pFileSystem = QueryInterface<IFileSystem>( engineFactory, FILESYSTEM_INTERFACE_VERSION );

			if ( !g_pFileSystem )
			{
				static const char *s_pFileSystemModules[] =
				{
					"filesystem_stdio.dll",
					"FileSystem_Steam.dll"
				};
				LogMessage( "FILESYSTEM FALLBACK: engine factory did not return '%s'; scanning known Source filesystem modules",
					FILESYSTEM_INTERFACE_VERSION );
				for ( int i = 0; i < ARRAYSIZE( s_pFileSystemModules ) && !g_pFileSystem; ++i )
				{
					hFileSystem = GetModuleHandleA( s_pFileSystemModules[i] );
					LogModuleInfo( s_pFileSystemModules[i], hFileSystem );
					if ( !hFileSystem )
					{
						LogMessage( "FILESYSTEM MODULE SKIP: '%s' is not loaded", s_pFileSystemModules[i] );
						continue;
					}
					CreateInterfaceFn fileSystemFactory = GetFactory( hFileSystem );
					g_pFileSystem = QueryInterface<IFileSystem>( fileSystemFactory, FILESYSTEM_INTERFACE_VERSION );
					LogMessage( "FILESYSTEM MODULE RESULT: module='%s' interface='%s' result=%p",
						s_pFileSystemModules[i], FILESYSTEM_INTERFACE_VERSION, g_pFileSystem );
				}
			}

			if ( !g_pClient || !g_pEntityList || !g_pEngine || !g_pCvar || !g_pRenderView || !g_pModelRender ||
				!g_pMaterials || !g_pGameConsole || !g_pFileSystem )
			{
				LogMessage( "INITIALIZATION FAILED (code 1): interfaces client=%p entitylist=%p engine=%p cvar=%p renderview=%p modelrender=%p materials=%p gameconsole=%p filesystem=%p",
					g_pClient, g_pEntityList, g_pEngine, g_pCvar, g_pRenderView, g_pModelRender,
					g_pMaterials, g_pGameConsole, g_pFileSystem );
				FlushLog();
				return 1;
			}

			LogMessage( "INTERFACES READY: client=%p entitylist=%p engine=%p cvar=%p renderview=%p modelrender=%p materials=%p gameconsole=%p console_color='0 255 0' filesystem=%p game_directory='%s'",
				g_pClient, g_pEntityList, g_pEngine, g_pCvar, g_pRenderView, g_pModelRender, g_pMaterials,
				g_pGameConsole, g_pFileSystem, g_pEngine->GetGameDirectory() );
			if ( HasExistingArtCommand() || HasExistingArtHlaeCommand() )
			{
				LogMessage( "INITIALIZATION FAILED (code 2): ART or requested HLAE command conflict" );
				ArtConsoleMessage( "v34_art: an ART or requested mirv_* command already exists. "
					"Do not load ART together with another HLAE command provider.\n" );
				FlushLog();
				return 2;
			}

			if ( !InstallClientModeFovHook() )
			{
				LogMessage( "INITIALIZATION FAILED (code 3): global IClientMode GetViewModelFOV hook installation failed" );
				ArtConsoleMessage( "v34_art: global build-4044 viewmodel FOV hook validation failed; commands were not enabled.\n" );
				FlushLog();
				return 3;
			}
			if ( !InstallModelRenderHook() )
			{
				LogMessage( "INITIALIZATION FAILED (code 3): DrawModelEx hook installation failed" );
				RemoveClientModeFovHook();
				ArtConsoleMessage( "v34_art: VEngineModel012 DrawModelEx hook validation failed; commands were not enabled.\n" );
				FlushLog();
				return 3;
			}
			if ( !InstallViewRenderHook() )
			{
				LogMessage( "INITIALIZATION FAILED (code 3): View_Render hook installation failed" );
				RemoveModelRenderHook();
				RemoveClientModeFovHook();
				ArtConsoleMessage( "v34_art: VClient013 View_Render hook validation failed; commands were not enabled.\n" );
				FlushLog();
				return 3;
			}

			if ( !InitializeArtHlae( clientFactory, engineFactory ) )
			{
				LogMessage( "INITIALIZATION FAILED (code 3): HLAE compatibility module initialization failed" );
				RemoveViewRenderHook();
				RemoveModelRenderHook();
				RemoveClientModeFovHook();
				ArtConsoleMessage( "v34_art: HLAE compatibility module initialization failed.\n" );
				FlushLog();
				return 3;
			}

			RegisterArtCommands();

			InstallArtGui();
			LogMessage( "STARTUP HELP PRINT BEGIN" );
			PrintArtHelp();
			LogMessage( "STARTUP HELP PRINT COMPLETE" );
			LogMessage( "INITIALIZATION SUCCESS: four-pass recorder loaded; waiting for art_start [take_name]" );
			FlushLog();
			return 0;
		}
	}
}

BOOL WINAPI DllMain( HINSTANCE hInstance, DWORD reason, LPVOID reserved )
{
	using namespace art;
	if ( reason == DLL_PROCESS_ATTACH )
	{
		g_hThisModule = hInstance;
		InitializeLogging();
		if ( kEnableDebugLoggingOnInjection )
		{
			DWORD error = ERROR_SUCCESS;
			if ( !EnableDebugLogging( error ) )
			{
				char failure[512];
				_snprintf_s( failure, sizeof( failure ), _TRUNCATE,
					"v34_art: emergency startup logging failed for '%s' (Win32 error %lu).\n",
					GetLogPath(), error );
				OutputDebugStringA( failure );
			}
		}

		LogMessage( "DLL_PROCESS_ATTACH: instance=%p host_exe='%s' resolved_log='%s'",
			hInstance, GetHostExecutablePath(), GetLogPath() );
		const BOOL threadNotificationsDisabled = DisableThreadLibraryCalls( hInstance );
		LogMessage( "DisableThreadLibraryCalls: success=%d error=%lu", threadNotificationsDisabled ? 1 : 0,
			threadNotificationsDisabled ? 0 : GetLastError() );
		HANDLE hThread = CreateThread( NULL, 0, InitializeArt, NULL, 0, NULL );
		if ( hThread )
		{
			LogMessage( "CreateThread: initializer created handle=%p", hThread );
			CloseHandle( hThread );
			LogMessage( "CreateThread: initializer handle closed; thread continues asynchronously" );
		}
		else
		{
			LogMessage( "CreateThread FAILED: initializer was not started; error=%lu", GetLastError() );
			FlushLog();
		}
	}
	else if ( reason == DLL_PROCESS_DETACH )
	{
		BeginArtGuiTermination();

		// The DLL is pinned. Never tear down hooks, ImGui, MinHook, logging, or
		// Source interfaces from DllMain while the loader lock is held. WM_CLOSE starts
		// an out-of-loader-lock hook cleanup worker instead.
		InterlockedExchange( &g_bRecording, FALSE );
	}
	return TRUE;
}
