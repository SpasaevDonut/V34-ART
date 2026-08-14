// AdvancedFX -afxV34 features for CS:S V34 ADVANCED RECORDING TOOLS.
//
// The engine bridge follows AdvancedFX main.cpp's -afxV34 path:
// VCLIENTENGINETOOLS001::AdjustEngineViewport / SetupEngineView and
// VClient013::FrameStageNotify[32]. Camera command behavior and file formats
// are adapted from AdvancedFX / HLAE:
// https://github.com/advancedfx/advancedfx
// AdvancedFX is MIT licensed; see ../THIRD_PARTY_NOTICES.md.

#include "art_internal.h"
#include "art_hlae.h"
#include "art_gui.h"

#include <algorithm>
#include <ctype.h>
#include <float.h>
#include <math.h>
#include <map>
#include <set>
#include <string>
#include <vector>

#include "mathlib.h"
#include "toolframework/ienginetool.h"
#include "tier1/keyvalues.h"
#include "toolframework/iclientenginetools.h"
#include "toolframework/itoolentity.h"
#include "toolframework/itoolframework.h"
#include "tools/bonelist.h"

#include "../third_party/advancedfx/shared/CamPath.h"
#include "../third_party/advancedfx/shared/MirvCampath.h"
#include "../third_party/advancedfx/shared/MirvInput.h"

// memdbgon must be the last include file in a .cpp file.
#include "tier0/memdbgon.h"

namespace art
{
	namespace
	{
		static const double kPi = 3.14159265358979323846;

		struct CameraSample
		{
			double time;
			double x;
			double y;
			double z;
			double pitch;
			double yaw;
			double roll;
			double fov;

			CameraSample()
				: time( 0.0 ), x( 0.0 ), y( 0.0 ), z( 0.0 ),
				  pitch( 0.0 ), yaw( 0.0 ), roll( 0.0 ), fov( 90.0 )
			{
			}
		};

		struct HlaeState
		{
			bool enabled;
			bool campathEnabled;
			bool campathHold;
			bool campathDraw;
			bool campathDrawKeyAxis;
			bool campathDrawKeyCam;
			float campathDrawKeyIndex;
			double campathOffset;
			std::vector<CameraSample> campath;
			std::vector<ArtHlaeCampathDrawPoint> campathTrajectory;
			bool campathTrajectoryDirty;
			bool campathChangedCallbackRegistered;

			bool inputCamera;
			bool inputInitialized;
			double inputMouseSensitivity;
			double inputKeyboardSensitivity;
			bool inputSmooth;
			double inputSmoothHalfTime;
			bool inputMouseMoveSupport;
			int inputOffsetMode;
			double inputStepFactor;
			bool inputRotLocalSpace;
			bool inputSmoothRotShortestPath;
			double inputSmoothHalfTimeVec;
			double inputSmoothHalfTimeAng;
			double inputSmoothHalfTimeFov;
			double inputKeyboardForwardSpeed;
			double inputKeyboardBackwardSpeed;
			double inputKeyboardLeftSpeed;
			double inputKeyboardRightSpeed;
			double inputKeyboardUpSpeed;
			double inputKeyboardDownSpeed;
			double inputKeyboardPitchPositiveSpeed;
			double inputKeyboardPitchNegativeSpeed;
			double inputKeyboardYawPositiveSpeed;
			double inputKeyboardYawNegativeSpeed;
			double inputKeyboardRollPositiveSpeed;
			double inputKeyboardRollNegativeSpeed;
			double inputKeyboardFovPositiveSpeed;
			double inputKeyboardFovNegativeSpeed;
			double inputMouseYawSpeed;
			double inputMousePitchSpeed;
			double inputMouseFovPositiveSpeed;
			double inputMouseFovNegativeSpeed;
			double inputMouseForwardSpeed;
			double inputMouseBackwardSpeed;
			double inputMouseLeftSpeed;
			double inputMouseRightSpeed;
			double inputMouseUpSpeed;
			double inputMouseDownSpeed;
			CameraSample inputView;
			CameraSample inputSmoothed;
			DWORD lastInputTick;
			volatile LONG inputMouseDeltaX;
			volatile LONG inputMouseDeltaY;
			volatile LONG inputRawMouseTick;
			LONG inputCursorAnchorX;
			LONG inputCursorAnchorY;
			LONG inputCursorSampleX;
			LONG inputCursorSampleY;
			bool inputCursorAnchorValid;

			FILE *camExport;
			char camExportPath[MAX_PATH];
			std::vector<CameraSample> camImport;
			char camImportPath[MAX_PATH];
			double camImportStartTime;

			FILE *bvhExport;
			char bvhExportPath[MAX_PATH];
			long bvhFrameCountOffset;
			unsigned long bvhFrameCount;
			double bvhFrameTime;
			std::vector<CameraSample> bvhImport;
			char bvhImportPath[MAX_PATH];
			double bvhImportBaseTime;

			FILE *agrFile;
			char agrPath[MAX_PATH];
			bool agrEnabled;
			bool agrRecordCamera;
			bool agrRecordPlayers;
			int agrRecordPlayerCameras;
			bool agrRecordWeapons;
			bool agrRecordProjectiles;
			int agrRecordViewModels;
			bool agrRecordInvisible;
			bool agrDebug;
			std::map<std::string, int> agrDictionary;
			std::map<HTOOLHANDLE, bool> agrTrackedHandles;
			std::set<int> agrHidden;
			long agrHiddenOffset;
			bool agrFrameOpen;
			double agrLastTime;

			bool fovOverride;
			double fov;
			bool fovHandleZoom;
			double fovMinUnzoomed;

			bool autoExportAgr;
			bool autoExportCamio;
			bool autoExportBvh;
			double autoExportBvhFps;
			bool autoStartedAgr;
			bool autoStartedCamio;
			bool autoStartedBvh;
			bool autoAgrRecordingModeWasEnabled;

			CameraSample lastCamera;
			bool hasLastCamera;
			CameraSample gameCamera;
			bool hasGameCamera;
			double inputFrameTime;
			int lastWidth;
			int lastHeight;

			HlaeState()
				: enabled( true ), campathEnabled( false ), campathHold( false ),
				  campathDraw( false ), campathDrawKeyAxis( false ),
				  campathDrawKeyCam( true ), campathDrawKeyIndex( 18.0f ),
				  campathOffset( 0.0 ), campathTrajectoryDirty( true ),
				  campathChangedCallbackRegistered( false ),
				  inputCamera( false ), inputInitialized( false ),
				  inputMouseSensitivity( 0.1 ), inputKeyboardSensitivity( 1.0 ),
				  inputSmooth( false ), inputSmoothHalfTime( 0.5 ),
				  inputMouseMoveSupport( false ), inputOffsetMode( 0 ),
				  inputStepFactor( 2.0 ), inputRotLocalSpace( false ),
				  inputSmoothRotShortestPath( true ),
				  inputSmoothHalfTimeVec( 0.5 ), inputSmoothHalfTimeAng( 0.5 ),
				  inputSmoothHalfTimeFov( 0.5 ),
				  inputKeyboardForwardSpeed( 320.0 ), inputKeyboardBackwardSpeed( 320.0 ),
				  inputKeyboardLeftSpeed( 320.0 ), inputKeyboardRightSpeed( 320.0 ),
				  inputKeyboardUpSpeed( 320.0 ), inputKeyboardDownSpeed( 320.0 ),
				  inputKeyboardPitchPositiveSpeed( 180.0 ), inputKeyboardPitchNegativeSpeed( 180.0 ),
				  inputKeyboardYawPositiveSpeed( 180.0 ), inputKeyboardYawNegativeSpeed( 180.0 ),
				  inputKeyboardRollPositiveSpeed( 180.0 ), inputKeyboardRollNegativeSpeed( 180.0 ),
				  inputKeyboardFovPositiveSpeed( 10.0 ), inputKeyboardFovNegativeSpeed( 10.0 ),
				  inputMouseYawSpeed( 180.0 ), inputMousePitchSpeed( 180.0 ),
				  inputMouseFovPositiveSpeed( 45.0 ), inputMouseFovNegativeSpeed( 45.0 ),
				  inputMouseForwardSpeed( 320.0 ), inputMouseBackwardSpeed( 320.0 ),
				  inputMouseLeftSpeed( 320.0 ), inputMouseRightSpeed( 320.0 ),
				  inputMouseUpSpeed( 320.0 ), inputMouseDownSpeed( 320.0 ),
				  lastInputTick( 0 ), inputMouseDeltaX( 0 ), inputMouseDeltaY( 0 ),
				  inputRawMouseTick( 0 ), inputCursorAnchorX( 0 ), inputCursorAnchorY( 0 ),
				  inputCursorSampleX( 0 ), inputCursorSampleY( 0 ),
				  inputCursorAnchorValid( false ),
				  camExport( NULL ), camImportStartTime( 0.0 ),
				  bvhExport( NULL ), bvhFrameCountOffset( 0 ), bvhFrameCount( 0 ),
				  bvhFrameTime( 1.0 / 30.0 ), bvhImportBaseTime( 0.0 ),
				  agrFile( NULL ), agrEnabled( false ), agrRecordCamera( true ),
				  agrRecordPlayers( true ), agrRecordPlayerCameras( -1 ),
				  agrRecordWeapons( true ), agrRecordProjectiles( true ),
				  agrRecordViewModels( 0 ), agrRecordInvisible( false ),
				  agrDebug( false ), agrHiddenOffset( 0 ), agrFrameOpen( false ),
				  agrLastTime( -1.0 ),
				  fovOverride( false ), fov( 90.0 ), fovHandleZoom( false ),
				  fovMinUnzoomed( 90.0 ),
				  autoExportAgr( false ), autoExportCamio( false ),
				  autoExportBvh( false ), autoExportBvhFps( 30.0 ),
				  autoStartedAgr( false ), autoStartedCamio( false ),
				  autoStartedBvh( false ), autoAgrRecordingModeWasEnabled( false ),
				  hasLastCamera( false ), hasGameCamera( false ), inputFrameTime( 1.0 / 60.0 ),
				  lastWidth( 4 ), lastHeight( 3 )
			{
				camExportPath[0] = '\0';
				camImportPath[0] = '\0';
				bvhExportPath[0] = '\0';
				bvhImportPath[0] = '\0';
				agrPath[0] = '\0';
			}
		};

		HlaeState g_Hlae;
		CamPath g_HlaeCampath;
		MirvInput *g_pMirvInput = NULL;
		IClientEngineTools *g_pHlaeEngineTools = NULL;
		IEngineTool *g_pHlaeEngineTool = NULL;
		IClientTools *g_pHlaeClientTools = NULL;
		void **g_ppHlaePreRenderSlot = NULL;
		void **g_ppHlaePostRenderSlot = NULL;
		void **g_ppHlaePostMessageSlot = NULL;
		void **g_ppHlaeAdjustViewportSlot = NULL;
		void **g_ppHlaeSetupEngineViewSlot = NULL;
		void **g_ppHlaeFrameStageNotifySlot = NULL;
		typedef void ( __thiscall *HlaePreRenderFn )( IClientEngineTools * );
		typedef void ( __thiscall *HlaePostRenderFn )( IClientEngineTools * );
		typedef void ( __thiscall *HlaePostMessageFn )(
			IClientEngineTools *, HTOOLHANDLE, KeyValues * );
		typedef void ( __thiscall *HlaeAdjustViewportFn )(
			IClientEngineTools *, int &, int &, int &, int & );
		typedef bool ( __thiscall *HlaeSetupEngineViewFn )(
			IClientEngineTools *, Vector &, QAngle &, float & );
		typedef void ( __thiscall *HlaeFrameStageNotifyFn )(
			IBaseClientDLL *, ClientFrameStage_t );
		HlaePreRenderFn g_pOriginalHlaePreRender = NULL;
		HlaePostRenderFn g_pOriginalHlaePostRender = NULL;
		HlaePostMessageFn g_pOriginalHlaePostMessage = NULL;
		HlaeAdjustViewportFn g_pOriginalHlaeAdjustViewport = NULL;
		HlaeSetupEngineViewFn g_pOriginalHlaeSetupEngineView = NULL;
		HlaeFrameStageNotifyFn g_pOriginalHlaeFrameStageNotify = NULL;
		bool g_HlaeToolHooksInstalled = false;

		int Argc()
		{
			return g_pEngine ? g_pEngine->Cmd_Argc() : 0;
		}

		const char *Argv( int index )
		{
			return g_pEngine && index >= 0 && index < Argc() ?
				g_pEngine->Cmd_Argv( index ) : "";
		}

		double CurrentTime()
		{
			if ( g_pHlaeEngineTool )
				return static_cast<double>( g_pHlaeEngineTool->ClientTime() );
			return g_pEngine ? static_cast<double>( g_pEngine->Time() ) : 0.0;
		}

		bool ParseBool( const char *pText, bool &value )
		{
			if ( !pText )
				return false;
			if ( !_stricmp( pText, "1" ) || !_stricmp( pText, "on" ) ||
				!_stricmp( pText, "true" ) || !_stricmp( pText, "enabled" ) )
			{
				value = true;
				return true;
			}
			if ( !_stricmp( pText, "0" ) || !_stricmp( pText, "off" ) ||
				!_stricmp( pText, "false" ) || !_stricmp( pText, "disabled" ) )
			{
				value = false;
				return true;
			}
			return false;
		}

		double ClampFov( double value )
		{
			if ( value < 1.0 ) return 1.0;
			if ( value > 179.0 ) return 179.0;
			return value;
		}

		double AngleDelta( double from, double to )
		{
			double value = fmod( to - from, 360.0 );
			if ( value > 180.0 ) value -= 360.0;
			if ( value < -180.0 ) value += 360.0;
			return value;
		}

		double LerpAngle( double from, double to, double value )
		{
			return from + AngleDelta( from, to ) * value;
		}

		double InverseRealFov( double realFov, int width, int height )
		{
			if ( width <= 0 || height <= 0 )
				return ClampFov( realFov );
			const double aspect = static_cast<double>( width ) / height;
			const double radians = ClampFov( realFov ) * kPi / 180.0;
			return ClampFov( 2.0 * atan( tan( radians * 0.5 ) /
				( aspect / ( 4.0 / 3.0 ) ) ) * 180.0 / kPi );
		}

		double RealFov( double engineFov, int width, int height )
		{
			if ( width <= 0 || height <= 0 )
				return ClampFov( engineFov );
			return logic::CalculateWidescreenHorizontalFov(
				static_cast<float>( ClampFov( engineFov ) ),
				static_cast<float>( width ) / static_cast<float>( height ) );
		}

		const char *FileNamePart( const char *pPath )
		{
			const char *pName = pPath ? pPath : "";
			for ( const char *p = pName; *p; ++p )
			{
				if ( *p == '/' || *p == '\\' )
					pName = p + 1;
			}
			return pName;
		}

		bool HasExtension( const char *pName )
		{
			if ( !pName )
				return false;
			const char *pFileName = FileNamePart( pName );
			const char *pDot = strrchr( pFileName, '.' );
			return pDot && pDot != pFileName && pDot[1];
		}

		bool IsAbsoluteWindowsPath( const char *pPath )
		{
			return pPath && (
				( isalpha( static_cast<unsigned char>( pPath[0] ) ) &&
					pPath[1] == ':' && ( pPath[2] == '\\' || pPath[2] == '/' ) ) ||
				( pPath[0] == '\\' && pPath[1] == '\\' ) ||
				( pPath[0] == '/' && pPath[1] == '/' ) );
		}

		bool IsSafeOutputPath( const char *pPath, bool absolute )
		{
			if ( !pPath || !pPath[0] )
				return false;
			for ( size_t i = 0; pPath[i]; ++i )
			{
				const unsigned char c = static_cast<unsigned char>( pPath[i] );
				if ( c < 32 || c == '"' || c == '|' || c == '?' || c == '*' ||
					c == '<' || c == '>' || c == ';' )
					return false;
				if ( c == ':' && !( absolute && i == 1 ) )
					return false;
			}
			return true;
		}

		bool DirectoryExists( const char *pPath )
		{
			const DWORD attributes = pPath && pPath[0] ? GetFileAttributesA( pPath ) :
				INVALID_FILE_ATTRIBUTES;
			return attributes != INVALID_FILE_ATTRIBUTES &&
				( attributes & FILE_ATTRIBUTE_DIRECTORY ) != 0;
		}

		bool EnsureDirectoryTree( const char *pDirectory )
		{
			if ( !pDirectory || !pDirectory[0] )
				return false;
			if ( DirectoryExists( pDirectory ) )
				return true;

			char path[MAX_PATH];
			Q_strncpy( path, pDirectory, sizeof( path ) );
			for ( char *p = path; *p; ++p )
				if ( *p == '/' ) *p = '\\';

			size_t start = 0;
			if ( isalpha( static_cast<unsigned char>( path[0] ) ) && path[1] == ':' )
				start = 3;
			else if ( path[0] == '\\' && path[1] == '\\' )
			{
				char *pServerEnd = strchr( path + 2, '\\' );
				char *pShareEnd = pServerEnd ? strchr( pServerEnd + 1, '\\' ) : NULL;
				start = pShareEnd ? static_cast<size_t>( pShareEnd - path + 1 ) :
					strlen( path );
			}

			for ( size_t i = start; path[i]; ++i )
			{
				if ( path[i] != '\\' )
					continue;
				path[i] = '\0';
				if ( path[0] && !DirectoryExists( path ) &&
					!CreateDirectoryA( path, NULL ) &&
					GetLastError() != ERROR_ALREADY_EXISTS )
				{
					path[i] = '\\';
					return false;
				}
				path[i] = '\\';
			}
			return DirectoryExists( path ) || CreateDirectoryA( path, NULL ) ||
				GetLastError() == ERROR_ALREADY_EXISTS;
		}

		bool EnsureOutputParentDirectory( const char *pFilePath )
		{
			char parent[MAX_PATH];
			Q_strncpy( parent, pFilePath, sizeof( parent ) );
			char *pSlash = strrchr( parent, '\\' );
			if ( !pSlash )
				pSlash = strrchr( parent, '/' );
			if ( !pSlash )
				return true;
			*pSlash = '\0';
			return EnsureDirectoryTree( parent );
		}

		bool NormalizeHlaeFilePath( const char *pRequested, const char *pDefaultName,
			const char *pExtension, char *pOutput, size_t outputBytes, bool &absolute )
		{
			if ( !pOutput || !outputBytes )
				return false;
			pOutput[0] = '\0';

			const char *pValue = pRequested && pRequested[0] ? pRequested : pDefaultName;
			absolute = IsAbsoluteWindowsPath( pValue );
			if ( !IsSafeOutputPath( pValue, absolute ) )
			{
				ArtConsoleMessage( "HLAE: path contains invalid characters.\n" );
				return false;
			}

			char normalized[MAX_PATH];
			Q_strncpy( normalized, pValue, sizeof( normalized ) );
			Q_FixSlashes( normalized, '/' );
			if ( !Q_RemoveDotSlashes( normalized ) )
			{
				ArtConsoleMessage( "HLAE: path contains invalid '..' traversal.\n" );
				return false;
			}

			const char *pName = FileNamePart( normalized );
			if ( !pName[0] || !strcmp( pName, "." ) || !strcmp( pName, ".." ) )
			{
				ArtConsoleMessage( "HLAE: invalid file name.\n" );
				return false;
			}

			const int written = HasExtension( normalized ) || !pExtension ?
				Q_snprintf( pOutput, static_cast<int>( outputBytes ), "%s", normalized ) :
				Q_snprintf( pOutput, static_cast<int>( outputBytes ), "%s%s",
					normalized, pExtension );
			if ( written < 0 || static_cast<size_t>( written ) >= outputBytes )
			{
				ArtConsoleMessage( "HLAE: requested path is too long.\n" );
				pOutput[0] = '\0';
				return false;
			}

			Q_FixSlashes( pOutput, '\\' );
			return true;
		}

		bool ResolveConfiguredOutputDirectory( char *pOutput, size_t outputBytes )
		{
			if ( !pOutput || !outputBytes )
				return false;

			int written = 0;
			if ( g_bRecordBaseAbsolute )
				written = Q_snprintf( pOutput, static_cast<int>( outputBytes ), "%s",
					g_szRecordBase );
			else
			{
				const char *pGameDirectory = g_pEngine ? g_pEngine->GetGameDirectory() : NULL;
				if ( !pGameDirectory || !pGameDirectory[0] )
				{
					ArtConsoleMessage( "HLAE: configured ART output directory is unavailable.\n" );
					return false;
				}
				written = Q_snprintf( pOutput, static_cast<int>( outputBytes ), "%s\\%s",
					pGameDirectory, g_szRecordBase );
			}

			if ( written < 0 || static_cast<size_t>( written ) >= outputBytes )
			{
				ArtConsoleMessage( "HLAE: configured ART output path is too long.\n" );
				pOutput[0] = '\0';
				return false;
			}
			Q_FixSlashes( pOutput, '\\' );
			return true;
		}

		bool ResolveRelativeHlaeBaseDirectory( char *pOutput, size_t outputBytes )
		{
			if ( g_ArtRecordingStats.takeActive &&
				g_ArtRecordingStats.takeAbsolutePath[0] )
			{
				Q_strncpy( pOutput, g_ArtRecordingStats.takeAbsolutePath,
					static_cast<int>( outputBytes ) );
				return true;
			}
			return ResolveConfiguredOutputDirectory( pOutput, outputBytes );
		}

		bool ResolveOutputPath( const char *pRequested, const char *pDefaultName,
			const char *pExtension, char *pOutput, size_t outputBytes )
		{
			if ( !pOutput || !outputBytes )
				return false;
			pOutput[0] = '\0';

			char requested[MAX_PATH];
			bool absolute = false;
			if ( !NormalizeHlaeFilePath( pRequested, pDefaultName, pExtension,
				requested, sizeof( requested ), absolute ) )
				return false;

			int written = 0;
			if ( absolute )
			{
				written = Q_snprintf( pOutput, static_cast<int>( outputBytes ), "%s",
					requested );
			}
			else
			{
				char baseDirectory[MAX_PATH];
				if ( !ResolveRelativeHlaeBaseDirectory( baseDirectory,
					sizeof( baseDirectory ) ) )
					return false;
				written = Q_snprintf( pOutput, static_cast<int>( outputBytes ),
					"%s\\%s", baseDirectory, requested );
			}

			if ( written < 0 || static_cast<size_t>( written ) >= outputBytes )
			{
				ArtConsoleMessage( "HLAE: resolved output path is too long.\n" );
				pOutput[0] = '\0';
				return false;
			}
			Q_FixSlashes( pOutput, '\\' );
			if ( !EnsureOutputParentDirectory( pOutput ) )
			{
				ArtConsoleMessage( "HLAE: could not create output directory for %s.\n",
					pOutput );
				pOutput[0] = '\0';
				return false;
			}
			return true;
		}

		bool FileExists( const char *pPath )
		{
			const DWORD attributes = pPath && pPath[0] ? GetFileAttributesA( pPath ) :
				INVALID_FILE_ATTRIBUTES;
			return attributes != INVALID_FILE_ATTRIBUTES &&
				!( attributes & FILE_ATTRIBUTE_DIRECTORY );
		}

		bool ResolveInputPath( const char *pRequested, char *pOutput, size_t outputBytes )
		{
			if ( !pOutput || !outputBytes )
				return false;
			pOutput[0] = '\0';

			char requested[MAX_PATH];
			bool absolute = false;
			if ( !NormalizeHlaeFilePath( pRequested, NULL, NULL,
				requested, sizeof( requested ), absolute ) )
				return false;

			if ( absolute )
			{
				if ( FileExists( requested ) )
				{
					Q_strncpy( pOutput, requested, static_cast<int>( outputBytes ) );
					return true;
				}
				pOutput[0] = '\0';
				return false;
			}

			char baseDirectory[MAX_PATH];
			if ( !ResolveRelativeHlaeBaseDirectory( baseDirectory,
				sizeof( baseDirectory ) ) )
				return false;
			const int written = Q_snprintf( pOutput, static_cast<int>( outputBytes ),
				"%s\\%s", baseDirectory, requested );
			if ( written < 0 || static_cast<size_t>( written ) >= outputBytes )
			{
				pOutput[0] = '\0';
				return false;
			}
			Q_FixSlashes( pOutput, '\\' );
			if ( FileExists( pOutput ) )
				return true;
			pOutput[0] = '\0';
			return false;
		}

		bool RequireEnabled( const char *pCommand )
		{
			if ( g_Hlae.enabled )
				return true;
			ArtConsoleMessage( "%s: HLAE features are globally disabled; use art_hlae enabled 1.\n",
				pCommand );
			return false;
		}

		class AdvancedFxCommandArgs : public advancedfx::ICommandArgs
		{
		public:
			AdvancedFxCommandArgs()
			{
				for ( int i = 0; i < Argc(); ++i )
					m_Args.push_back( Argv( i ) ? Argv( i ) : "" );
			}

			virtual int ArgC()
			{
				return static_cast<int>( m_Args.size() );
			}

			virtual const char *ArgV( int index )
			{
				return 0 <= index && index < static_cast<int>( m_Args.size() ) ?
					m_Args[index].c_str() : "";
			}

			bool ResolveFileArgument( int index, bool output,
				const char *pDefaultName, const char *pExtension )
			{
				if ( index < 0 || static_cast<int>( m_Args.size() ) <= index )
					return false;
				char path[MAX_PATH];
				const bool resolved = output ?
					ResolveOutputPath( m_Args[index].c_str(), pDefaultName,
						pExtension, path, sizeof( path ) ) :
					ResolveInputPath( m_Args[index].c_str(), path, sizeof( path ) );
				if ( resolved )
					m_Args[index] = path;
				return resolved;
			}

		private:
			std::vector<std::string> m_Args;
		};

		class ArtMirvCampathTime : public IMirvCampath_Time
		{
		public:
			virtual double GetTime()
			{
				return CurrentTime();
			}

			virtual double GetCurTime()
			{
				return CurrentTime();
			}

			virtual bool GetCurrentDemoTick( int &outTick )
			{
				if ( !g_pEngine || !g_pEngine->IsPlayingDemo() )
					return false;
				outTick = g_pHlaeEngineTool ? g_pHlaeEngineTool->ClientTick() : 0;
				return 0 <= outTick;
			}

			virtual bool GetCurrentDemoTime( double &outDemoTime )
			{
				if ( !g_pEngine || !g_pEngine->IsPlayingDemo() )
					return false;
				outDemoTime = CurrentTime();
				return true;
			}

			virtual bool GetDemoTickFromDemoTime( double curTime,
				double demoTime, int &outTick )
			{
				int currentTick = 0;
				if ( !GetCurrentDemoTick( currentTick ) )
					return false;
				double interval = 0.0;
				if ( g_pHlaeEngineTool )
				{
					const int clientTick = g_pHlaeEngineTool->ClientTick();
					const double clientTime = g_pHlaeEngineTool->ClientTime();
					if ( 0 < clientTick && 0.0 < clientTime )
						interval = clientTime / clientTick;
				}
				if ( interval <= 0.0 )
					interval = 0.01;
				outTick = currentTick +
					static_cast<int>( floor( ( demoTime - curTime ) / interval + 0.5 ) );
				return true;
			}

			virtual bool GetDemoTimeFromClientTime( double curTime,
				double clientTime, double &outDemoTime )
			{
				outDemoTime = CurrentTime() + ( clientTime - curTime );
				return g_pEngine && g_pEngine->IsPlayingDemo();
			}

			virtual bool GetDemoTickFromClientTime( double curTime,
				double targetTime, int &outTick )
			{
				return GetDemoTickFromDemoTime( curTime, targetTime, outTick );
			}
		};

		class ArtMirvCampathCamera : public IMirvCampath_Camera
		{
		public:
			virtual SMirvCameraValue GetCamera()
			{
				const CameraSample &camera = g_Hlae.lastCamera;
				return SMirvCameraValue( camera.x, camera.y, camera.z,
					camera.pitch, camera.yaw, camera.roll, camera.fov );
			}
		};

		class ArtMirvCampathDrawer : public IMirvCampath_Drawer
		{
		public:
			virtual bool GetEnabled() { return g_Hlae.campathDraw; }
			virtual void SetEnabled( bool value ) { g_Hlae.campathDraw = value; }
			virtual bool GetDrawKeyframeAxis() { return g_Hlae.campathDrawKeyAxis; }
			virtual void SetDrawKeyframeAxis( bool value )
			{
				g_Hlae.campathDrawKeyAxis = value;
			}
			virtual bool GetDrawKeyframeCam() { return g_Hlae.campathDrawKeyCam; }
			virtual void SetDrawKeyframeCam( bool value )
			{
				g_Hlae.campathDrawKeyCam = value;
			}
			virtual float GetDrawKeyframeIndex()
			{
				return g_Hlae.campathDrawKeyIndex;
			}
			virtual void SetDrawKeyframeIndex( float value )
			{
				g_Hlae.campathDrawKeyIndex = value;
			}
		};

		class ArtMirvInputDependencies : public IMirvInputDependencies
		{
		public:
			virtual bool GetSuspendMirvInput()
			{
				return ( IsArtGuiVisible() && !IsArtGuiMirvInputPassthroughActive() ) ||
					( g_pEngine && g_pEngine->Con_IsVisible() );
			}

			virtual void GetLastCameraData( double &x, double &y, double &z,
				double &rX, double &rY, double &rZ, double &fov )
			{
				ReadCamera( g_Hlae.hasLastCamera ? g_Hlae.lastCamera :
					g_Hlae.gameCamera, x, y, z, rX, rY, rZ, fov );
			}

			virtual void GetGameCameraData( double &x, double &y, double &z,
				double &rX, double &rY, double &rZ, double &fov )
			{
				ReadCamera( g_Hlae.gameCamera, x, y, z, rX, rY, rZ, fov );
			}

			virtual double GetInverseScaledFov( double fov )
			{
				return InverseRealFov( fov, g_Hlae.lastWidth, g_Hlae.lastHeight );
			}

		private:
			static void ReadCamera( const CameraSample &camera,
				double &x, double &y, double &z, double &rX, double &rY,
				double &rZ, double &fov )
			{
				x = camera.x;
				y = camera.y;
				z = camera.z;
				rX = camera.pitch;
				rY = camera.yaw;
				rZ = camera.roll;
				fov = camera.fov;
			}
		};

		ArtMirvCampathTime g_MirvCampathTime;
		ArtMirvCampathCamera g_MirvCampathCamera;
		ArtMirvCampathDrawer g_MirvCampathDrawer;
		ArtMirvInputDependencies g_MirvInputDependencies;

		CameraSample FromCamPathValue( double time, const CamPathValue &value )
		{
			CameraSample sample;
			const Afx::Math::QEulerAngles angles =
				value.R.ToQREulerAngles().ToQEulerAngles();
			sample.time = time;
			sample.x = value.X;
			sample.y = value.Y;
			sample.z = value.Z;
			sample.pitch = angles.Pitch;
			sample.yaw = angles.Yaw;
			sample.roll = angles.Roll;
			sample.fov = value.Fov;
			return sample;
		}

		ArtHlaeCampathDrawPoint MakeCampathDrawPoint( double time,
			const CamPathValue &value )
		{
			const CameraSample sample = FromCamPathValue( time, value );
			ArtHlaeCampathDrawPoint point;
			point.x = static_cast<float>( sample.x );
			point.y = static_cast<float>( sample.y );
			point.z = static_cast<float>( sample.z );
			point.pitch = static_cast<float>( sample.pitch );
			point.yaw = static_cast<float>( sample.yaw );
			point.roll = static_cast<float>( sample.roll );
			point.fov = static_cast<float>( ClampFov( sample.fov ) );
			point.time = time;
			point.selected = value.Selected;
			return point;
		}

		double CampathPointSegmentDistanceSquared(
			const ArtHlaeCampathDrawPoint &point,
			const ArtHlaeCampathDrawPoint &lineStart,
			const ArtHlaeCampathDrawPoint &lineEnd )
		{
			const double vx = lineEnd.x - lineStart.x;
			const double vy = lineEnd.y - lineStart.y;
			const double vz = lineEnd.z - lineStart.z;
			const double wx = point.x - lineStart.x;
			const double wy = point.y - lineStart.y;
			const double wz = point.z - lineStart.z;
			const double lengthSquared = vx * vx + vy * vy + vz * vz;
			double t = lengthSquared > 0.0 ?
				( wx * vx + wy * vy + wz * vz ) / lengthSquared : 0.0;
			if ( t < 0.0 ) t = 0.0;
			if ( 1.0 < t ) t = 1.0;
			const double dx = point.x - ( lineStart.x + t * vx );
			const double dy = point.y - ( lineStart.y + t * vy );
			const double dz = point.z - ( lineStart.z + t * vz );
			return dx * dx + dy * dy + dz * dz;
		}

		void ReduceCampathTrajectory(
			const std::vector<ArtHlaeCampathDrawPoint> &samples,
			size_t first, size_t last, double epsilonSquared,
			std::vector<unsigned char> &keep )
		{
			if ( last <= first + 1 )
				return;
			double maximumDistance = -1.0;
			size_t maximumIndex = first;
			for ( size_t i = first + 1; i < last; ++i )
			{
				const double distance = CampathPointSegmentDistanceSquared(
					samples[i], samples[first], samples[last] );
				if ( maximumDistance < distance )
				{
					maximumDistance = distance;
					maximumIndex = i;
				}
			}
			if ( maximumDistance <= epsilonSquared )
				return;
			keep[maximumIndex] = 1;
			ReduceCampathTrajectory( samples, first, maximumIndex,
				epsilonSquared, keep );
			ReduceCampathTrajectory( samples, maximumIndex, last,
				epsilonSquared, keep );
		}

		void RebuildCampathTrajectory()
		{
			if ( !g_Hlae.campathTrajectoryDirty )
				return;
			g_Hlae.campathTrajectoryDirty = false;
			g_Hlae.campathTrajectory.clear();
			if ( !g_HlaeCampath.CanEval() || g_HlaeCampath.GetSize() < 2 )
				return;

			static const size_t kSamplesPerInterval = 1024;
			static const double kReductionEpsilonSquared = 1.0;
			std::vector<ArtHlaeCampathDrawPoint> samples( kSamplesPerInterval );
			std::vector<unsigned char> keep( kSamplesPerInterval );
			CamPathIterator previous = g_HlaeCampath.GetBegin();
			CamPathIterator current = previous;
			++current;
			for ( ; current != g_HlaeCampath.GetEnd(); ++current )
			{
				const double startTime = previous.GetTime();
				const double interval = current.GetTime() - startTime;
				for ( size_t i = 0; i < kSamplesPerInterval; ++i )
				{
					const double fraction = static_cast<double>( i ) /
						static_cast<double>( kSamplesPerInterval - 1 );
					const double time = startTime + interval * fraction;
					samples[i] = MakeCampathDrawPoint( time,
						g_HlaeCampath.Eval( time ) );
				}
				std::fill( keep.begin(), keep.end(), 0 );
				keep.front() = 1;
				keep.back() = 1;
				ReduceCampathTrajectory( samples, 0, samples.size() - 1,
					kReductionEpsilonSquared, keep );
				for ( size_t i = 0; i < samples.size(); ++i )
				{
					if ( !keep[i] ||
						( !g_Hlae.campathTrajectory.empty() && i == 0 ) )
						continue;
					g_Hlae.campathTrajectory.push_back( samples[i] );
				}
				previous = current;
			}
		}

		void CampathChanged( void * )
		{
			g_Hlae.campathTrajectoryDirty = true;
		}

		bool InterpolateSamples( const std::vector<CameraSample> &samples, double time,
			bool hold, CameraSample &result )
		{
			if ( samples.empty() )
				return false;
			if ( time < samples.front().time || time > samples.back().time )
				return false;
			std::vector<CameraSample>::const_iterator next =
				std::lower_bound( samples.begin(), samples.end(), time,
					[]( const CameraSample &sample, double value )
					{
						return sample.time < value;
					} );
			if ( next == samples.begin() || next == samples.end() || next->time == time )
			{
				result = next == samples.end() ? samples.back() : *next;
				return true;
			}
			const CameraSample &before = *( next - 1 );
			if ( hold )
			{
				result = before;
				return true;
			}
			const double range = next->time - before.time;
			const double t = range > 0.0 ? ( time - before.time ) / range : 0.0;
			result.time = time;
			result.x = before.x + ( next->x - before.x ) * t;
			result.y = before.y + ( next->y - before.y ) * t;
			result.z = before.z + ( next->z - before.z ) * t;
			result.pitch = LerpAngle( before.pitch, next->pitch, t );
			result.yaw = LerpAngle( before.yaw, next->yaw, t );
			result.roll = LerpAngle( before.roll, next->roll, t );
			result.fov = before.fov + ( next->fov - before.fov ) * t;
			return true;
		}

		void SortSamples( std::vector<CameraSample> &samples )
		{
			std::sort( samples.begin(), samples.end(),
				[]( const CameraSample &left, const CameraSample &right )
				{
					return left.time < right.time;
				} );
		}

		void ApplySample( const CameraSample &sample, CViewSetup &view )
		{
			view.origin.x = static_cast<float>( sample.x );
			view.origin.y = static_cast<float>( sample.y );
			view.origin.z = static_cast<float>( sample.z );
			view.angles.x = static_cast<float>( sample.pitch );
			view.angles.y = static_cast<float>( sample.yaw );
			view.angles.z = static_cast<float>( sample.roll );
			view.fov = static_cast<float>( ClampFov( sample.fov ) );
		}

		CameraSample FromView( const CViewSetup &view, double time )
		{
			CameraSample result;
			result.time = time;
			result.x = view.origin.x;
			result.y = view.origin.y;
			result.z = view.origin.z;
			result.pitch = view.angles.x;
			result.yaw = view.angles.y;
			result.roll = view.angles.z;
			result.fov = view.fov;
			return result;
		}

		bool ParseCamFile( const char *pPath, std::vector<CameraSample> &samples )
		{
			FILE *pFile = NULL;
			if ( fopen_s( &pFile, pPath, "rb" ) || !pFile )
				return false;
			char line[1024];
			bool magic = false;
			bool data = false;
			int version = 0;
			samples.clear();
			while ( fgets( line, sizeof( line ), pFile ) )
			{
				if ( !magic )
				{
					magic = !strncmp( line, "advancedfx Cam", 14 );
					if ( !magic ) break;
					continue;
				}
				if ( !data )
				{
					if ( sscanf_s( line, "version %d", &version ) == 1 )
						continue;
					if ( !strncmp( line, "DATA", 4 ) )
						data = true;
					continue;
				}
				CameraSample sample;
				double xRotation = 0.0;
				if ( sscanf_s( line, "%lf %lf %lf %lf %lf %lf %lf %lf",
					&sample.time, &sample.x, &sample.y, &sample.z, &xRotation,
					&sample.pitch, &sample.yaw, &sample.fov ) == 8 )
				{
					sample.roll = xRotation;
					samples.push_back( sample );
				}
			}
			fclose( pFile );
			return magic && data && version >= 1 && version <= 2 && !samples.empty();
		}

		bool ParseBvhFile( const char *pPath, std::vector<CameraSample> &samples )
		{
			FILE *pFile = NULL;
			if ( fopen_s( &pFile, pPath, "rb" ) || !pFile )
				return false;
			char line[1024];
			bool motion = false;
			double frameTime = 0.0;
			unsigned long expectedFrames = 0;
			samples.clear();
			while ( fgets( line, sizeof( line ), pFile ) )
			{
				if ( !motion )
				{
					if ( !strncmp( line, "MOTION", 6 ) )
						motion = true;
					continue;
				}
				if ( !expectedFrames && sscanf_s( line, "Frames: %lu", &expectedFrames ) == 1 )
					continue;
				if ( frameTime <= 0.0 && sscanf_s( line, "Frame Time: %lf", &frameTime ) == 1 )
					continue;
				if ( frameTime <= 0.0 )
					continue;
				double values[6];
				if ( sscanf_s( line, "%lf %lf %lf %lf %lf %lf",
					&values[0], &values[1], &values[2],
					&values[3], &values[4], &values[5] ) == 6 )
				{
					CameraSample sample;
					sample.time = samples.size() * frameTime;
					sample.y = -values[0];
					sample.z = values[1];
					sample.x = -values[2];
					sample.roll = -values[3];
					sample.pitch = -values[4];
					sample.yaw = values[5];
					samples.push_back( sample );
				}
			}
			fclose( pFile );
			return motion && frameTime > 0.0 && !samples.empty() &&
				( !expectedFrames || samples.size() == expectedFrames );
		}

		void StopCamExport()
		{
			if ( g_Hlae.camExport )
			{
				fclose( g_Hlae.camExport );
				g_Hlae.camExport = NULL;
				ArtConsoleMessage( "mirv_camio: export ended (%s).\n", g_Hlae.camExportPath );
			}
			g_Hlae.autoStartedCamio = false;
		}

		bool StartCamExport( const char *pRequested )
		{
			StopCamExport();
			char path[MAX_PATH];
			if ( !ResolveOutputPath( pRequested, "camera.cam", ".cam",
				path, sizeof( path ) ) )
				return false;
			if ( fopen_s( &g_Hlae.camExport, path, "wb" ) || !g_Hlae.camExport )
			{
				ArtConsoleMessage( "mirv_camio: could not open output file.\n" );
				return false;
			}
			Q_strncpy( g_Hlae.camExportPath, path, sizeof( g_Hlae.camExportPath ) );
			fprintf( g_Hlae.camExport,
				"advancedfx Cam\nversion 2\n"
				"channels time xPosition yPosition zPosition xRotation "
				"yRotation zRotation fov\nDATA\n" );
			ArtConsoleMessage( "mirv_camio: export started: %s\n", path );
			return true;
		}

		void StopBvhExport()
		{
			if ( !g_Hlae.bvhExport )
			{
				g_Hlae.autoStartedBvh = false;
				return;
			}
			const long end = ftell( g_Hlae.bvhExport );
			if ( g_Hlae.bvhFrameCountOffset >= 0 &&
				!fseek( g_Hlae.bvhExport, g_Hlae.bvhFrameCountOffset, SEEK_SET ) )
			{
				fprintf( g_Hlae.bvhExport, "Frames: %11lu", g_Hlae.bvhFrameCount );
				fseek( g_Hlae.bvhExport, end, SEEK_SET );
			}
			fclose( g_Hlae.bvhExport );
			g_Hlae.bvhExport = NULL;
			ArtConsoleMessage( "mirv_camexport: stopped after %lu frames (%s).\n",
				g_Hlae.bvhFrameCount, g_Hlae.bvhExportPath );
			g_Hlae.autoStartedBvh = false;
		}

		bool StartBvhExport( const char *pRequested, double fps )
		{
			StopBvhExport();
			char path[MAX_PATH];
			if ( !ResolveOutputPath( pRequested, "camera.bvh", ".bvh",
				path, sizeof( path ) ) )
				return false;
			if ( fps < 0.1 ) fps = 0.1;
			if ( fps > 1000.0 ) fps = 1000.0;
			if ( fopen_s( &g_Hlae.bvhExport, path, "w+b" ) || !g_Hlae.bvhExport )
			{
				ArtConsoleMessage( "Error: exporting failed.\n" );
				return false;
			}
			Q_strncpy( g_Hlae.bvhExportPath, path, sizeof( g_Hlae.bvhExportPath ) );
			g_Hlae.bvhFrameCount = 0;
			g_Hlae.bvhFrameTime = 1.0 / fps;
			fprintf( g_Hlae.bvhExport,
				"HIERARCHY\nROOT MdtCam\n{\n\tOFFSET 0.00 0.00 0.00\n"
				"\tCHANNELS 6 Xposition Yposition Zposition Zrotation Xrotation Yrotation\n"
				"\tEnd Site\n\t{\n\t\tOFFSET 0.00 0.00 -1.00\n\t}\n}\nMOTION\n" );
			g_Hlae.bvhFrameCountOffset = ftell( g_Hlae.bvhExport );
			fprintf( g_Hlae.bvhExport, "Frames: %11d\nFrame Time: %f\n",
				0, g_Hlae.bvhFrameTime );
			ArtConsoleMessage( "mirv_camexport: started: %s\n", path );
			return true;
		}

		void AgrWriteRaw( const void *pData, size_t bytes )
		{
			if ( g_Hlae.agrFile && pData && bytes )
				fwrite( pData, 1, bytes, g_Hlae.agrFile );
		}

		void AgrWriteInt( int value )
		{
			AgrWriteRaw( &value, sizeof( value ) );
		}

		void AgrWriteFloat( float value )
		{
			AgrWriteRaw( &value, sizeof( value ) );
		}

		void AgrWriteBool( bool value )
		{
			const unsigned char byte = value ? 1 : 0;
			AgrWriteRaw( &byte, sizeof( byte ) );
		}

		void AgrWriteDictionary( const char *pValue )
		{
			const std::string key = pValue ? pValue : "";
			std::map<std::string, int>::iterator found = g_Hlae.agrDictionary.find( key );
			if ( found != g_Hlae.agrDictionary.end() )
			{
				AgrWriteInt( found->second );
				return;
			}
			const int index = static_cast<int>( g_Hlae.agrDictionary.size() );
			g_Hlae.agrDictionary[key] = index;
			AgrWriteInt( -1 );
			AgrWriteRaw( key.c_str(), key.size() + 1 );
		}

		void AgrWriteMatrix( const matrix3x4_t &matrix )
		{
			for ( int row = 0; row < 3; ++row )
				for ( int column = 0; column < 4; ++column )
					AgrWriteFloat( matrix[row][column] );
		}

		void AgrBeginFrame( float frameTime )
		{
			if ( !g_Hlae.agrFile )
				return;
			if ( g_Hlae.agrFrameOpen )
			{
				// The Source tool interface may produce nested skybox/main-view
				// callbacks. Keep the AGR stream structurally valid.
				AgrWriteDictionary( "afxFrameEnd" );
			}
			AgrWriteDictionary( "afxFrame" );
			AgrWriteFloat( frameTime );
			g_Hlae.agrHiddenOffset = ftell( g_Hlae.agrFile );
			AgrWriteInt( 0 );
			g_Hlae.agrHidden.clear();
			g_Hlae.agrFrameOpen = true;
		}

		void AgrEndFrame()
		{
			if ( !g_Hlae.agrFile || !g_Hlae.agrFrameOpen )
				return;
			if ( !g_Hlae.agrHidden.empty() && g_Hlae.agrHiddenOffset > 0 )
			{
				AgrWriteDictionary( "afxHidden" );
				const long current = ftell( g_Hlae.agrFile );
				const int offset = static_cast<int>( current - g_Hlae.agrHiddenOffset );
				fseek( g_Hlae.agrFile, g_Hlae.agrHiddenOffset, SEEK_SET );
				AgrWriteInt( offset );
				fseek( g_Hlae.agrFile, current, SEEK_SET );
				AgrWriteInt( static_cast<int>( g_Hlae.agrHidden.size() ) );
				for ( std::set<int>::const_iterator it = g_Hlae.agrHidden.begin();
					it != g_Hlae.agrHidden.end(); ++it )
					AgrWriteInt( *it );
			}
			AgrWriteDictionary( "afxFrameEnd" );
			g_Hlae.agrFrameOpen = false;
			g_Hlae.agrHiddenOffset = 0;
			g_Hlae.agrHidden.clear();
			fflush( g_Hlae.agrFile );
		}

		bool BeginsWithInsensitive( const char *pText, const char *pPrefix )
		{
			if ( !pText || !pPrefix )
				return false;
			return !_strnicmp( pText, pPrefix, strlen( pPrefix ) );
		}

		void AgrHandleToolMessage( HTOOLHANDLE handle, KeyValues *pMessage )
		{
			if ( !pMessage || handle == HTOOLHANDLE_INVALID )
				return;
			const char *pMessageName = pMessage->GetName();
			if ( !pMessageName )
				return;
			if ( !_stricmp( pMessageName, "created" ) )
			{
				g_Hlae.agrTrackedHandles[handle] = false;
				if ( g_Hlae.agrFile && g_pHlaeClientTools )
					g_pHlaeClientTools->SetRecording( handle, true );
				return;
			}
			if ( !_stricmp( pMessageName, "deleted" ) )
			{
				std::map<HTOOLHANDLE, bool>::iterator found =
					g_Hlae.agrTrackedHandles.find( handle );
				if ( found != g_Hlae.agrTrackedHandles.end() )
				{
					if ( g_Hlae.agrFile && g_Hlae.agrFrameOpen )
					{
						AgrWriteDictionary( "deleted" );
						AgrWriteInt( static_cast<int>( handle ) );
					}
					g_Hlae.agrTrackedHandles.erase( found );
				}
				return;
			}
			if ( _stricmp( pMessageName, "entity_state" ) || !g_Hlae.agrFile ||
				!g_Hlae.agrFrameOpen || !g_pHlaeClientTools )
				return;

			const char *pClassName = g_pHlaeClientTools->GetClassname( handle );
			if ( !pClassName ) pClassName = "[NULL]";
			const bool isPlayer = !_stricmp( pClassName, "class C_CSPlayer" ) ||
				!_stricmp( pClassName, "class C_CSRagdoll" );
			const bool isWeapon = BeginsWithInsensitive( pClassName, "weapon_" ) ||
				!_stricmp( pClassName, "class C_BreakableProp" );
			const bool isProjectile = !_stricmp( pClassName, "grenade" ) ||
				strstr( pClassName, "Grenade" ) != NULL ||
				strstr( pClassName, "Projectile" ) != NULL;
			const bool isViewModel = !_stricmp( pClassName, "viewmodel" ) ||
				strstr( pClassName, "ViewModel" ) != NULL;
			if ( !( g_Hlae.agrRecordPlayers && isPlayer ) &&
				!( g_Hlae.agrRecordWeapons && isWeapon ) &&
				!( g_Hlae.agrRecordProjectiles && isProjectile ) &&
				!( g_Hlae.agrRecordViewModels && isViewModel ) )
				return;

			BaseEntityRecordingState_t *pBase =
				static_cast<BaseEntityRecordingState_t *>( pMessage->GetPtr( "baseentity" ) );
			if ( !pBase )
				return;
			std::map<HTOOLHANDLE, bool>::iterator tracked =
				g_Hlae.agrTrackedHandles.find( handle );
			if ( !g_Hlae.agrRecordInvisible && !pBase->m_bVisible )
			{
				if ( tracked != g_Hlae.agrTrackedHandles.end() && tracked->second )
				{
					g_Hlae.agrHidden.insert( static_cast<int>( handle ) );
					tracked->second = false;
				}
				return;
			}

			AgrWriteDictionary( "entity_state" );
			AgrWriteInt( static_cast<int>( handle ) );
			AgrWriteDictionary( "baseentity" );
			AgrWriteDictionary( pBase->m_pModelName ? pBase->m_pModelName : "[NULL]" );
			AgrWriteBool( pBase->m_bVisible );
			matrix3x4_t parentTransform;
			AngleMatrix( pBase->m_vecRenderAngles, pBase->m_vecRenderOrigin, parentTransform );
			AgrWriteMatrix( parentTransform );
			BaseAnimatingRecordingState_t *pAnimating =
				static_cast<BaseAnimatingRecordingState_t *>(
					pMessage->GetPtr( "baseanimating" ) );
			if ( pAnimating )
			{
				AgrWriteDictionary( "baseanimating" );
				CBoneList *pBones = pAnimating->m_pBoneList;
				const bool hasBones = pBones && pBones->m_nBones > 0 &&
					pBones->m_nBones <= MAXSTUDIOBONES;
				AgrWriteBool( hasBones );
				if ( hasBones )
				{
					AgrWriteInt( pBones->m_nBones );
					for ( int bone = 0; bone < pBones->m_nBones; ++bone )
					{
						matrix3x4_t boneTransform;
						QuaternionMatrix( pBones->m_quatRot[bone],
							pBones->m_vecPos[bone], boneTransform );
						AgrWriteMatrix( boneTransform );
					}
				}
			}
			AgrWriteDictionary( "/" );
			AgrWriteBool( isViewModel || pMessage->GetInt( "viewmodel" ) != 0 );
			g_Hlae.agrTrackedHandles[handle] = pBase->m_bVisible;
			if ( g_Hlae.agrDebug )
				ArtConsoleMessage( "mirv_agr: %u %s %s\n",
					handle, pClassName, pBase->m_pModelName ? pBase->m_pModelName : "[NULL]" );
		}

		void AgrEnableClientRecording( bool enabled )
		{
			if ( g_pHlaeClientTools )
				g_pHlaeClientTools->EnableRecordingMode( enabled );
			g_Hlae.agrEnabled = enabled;
		}

		void AgrSetAllRecordables( bool recording )
		{
			if ( !g_pHlaeClientTools )
				return;
			const int count = g_pHlaeClientTools->GetNumRecordables();
			for ( int i = 0; i < count; ++i )
			{
				const HTOOLHANDLE handle = g_pHlaeClientTools->GetRecordable( i );
				if ( handle == HTOOLHANDLE_INVALID )
					continue;
				g_Hlae.agrTrackedHandles[handle] = false;
				g_pHlaeClientTools->SetRecording( handle, recording );
			}
		}

		void StopAgr()
		{
			if ( g_Hlae.agrFile )
			{
				AgrEndFrame();
				AgrSetAllRecordables( false );
				fclose( g_Hlae.agrFile );
				g_Hlae.agrFile = NULL;
				g_Hlae.agrDictionary.clear();
				ArtConsoleMessage( "mirv_agr: stopped (%s).\n", g_Hlae.agrPath );
			}
			g_Hlae.autoStartedAgr = false;
		}

		bool StartAgrExport( const char *pRequested )
		{
			StopAgr();
			char path[MAX_PATH];
			if ( !ResolveOutputPath( pRequested, "afxGameRecord.agr", ".agr",
				path, sizeof( path ) ) )
				return false;
			if ( fopen_s( &g_Hlae.agrFile, path, "wb" ) || !g_Hlae.agrFile )
			{
				ArtConsoleMessage( "Error opening AGR output file.\n" );
				return false;
			}
			Q_strncpy( g_Hlae.agrPath, path, sizeof( g_Hlae.agrPath ) );
			g_Hlae.agrDictionary.clear();
			g_Hlae.agrLastTime = -1.0;
			fwrite( "afxGameRecord", 1, 13, g_Hlae.agrFile );
			fputc( '\0', g_Hlae.agrFile );
			AgrWriteInt( 6 );
			if ( !g_Hlae.agrEnabled )
			{
				ArtConsoleMessage(
					"WARNING: recording mode was disabled; enabling it now. "
					"For complete entity state, enable before loading the demo.\n" );
				AgrEnableClientRecording( true );
			}
			AgrSetAllRecordables( true );
			ArtConsoleMessage( "Started AGR recording: %s\n", path );
			return true;
		}

		void StopAllWritersAndOverrides()
		{
			StopCamExport();
			StopBvhExport();
			StopAgr();
			AgrEnableClientRecording( false );
			g_Hlae.camImport.clear();
			g_Hlae.bvhImport.clear();
			g_HlaeCampath.Enabled_set( false );
			g_Hlae.campathDraw = false;
			if ( g_pMirvInput )
				g_pMirvInput->SetCameraControlMode( false );
			g_Hlae.inputInitialized = false;
			g_Hlae.fovOverride = false;
		}

		bool ReplaceHlaeVtableSlot( void **pSlot, void *pReplacement, void **pOriginal )
		{
			if ( !pSlot || !pReplacement || !pOriginal || !*pSlot )
				return false;
			DWORD oldProtect = 0;
			if ( !VirtualProtect( pSlot, sizeof( void * ), PAGE_EXECUTE_READWRITE,
				&oldProtect ) )
				return false;
			*pOriginal = *pSlot;
			*pSlot = pReplacement;
			FlushInstructionCache( GetCurrentProcess(), pSlot, sizeof( void * ) );
			DWORD ignored = 0;
			VirtualProtect( pSlot, sizeof( void * ), oldProtect, &ignored );
			return true;
		}

		void RestoreHlaeVtableSlot( void **pSlot, void *pReplacement, void *pOriginal )
		{
			if ( !pSlot || !pOriginal )
				return;
			DWORD oldProtect = 0;
			if ( !VirtualProtect( pSlot, sizeof( void * ), PAGE_EXECUTE_READWRITE,
				&oldProtect ) )
				return;
			if ( *pSlot == pReplacement )
				*pSlot = pOriginal;
			FlushInstructionCache( GetCurrentProcess(), pSlot, sizeof( void * ) );
			DWORD ignored = 0;
			VirtualProtect( pSlot, sizeof( void * ), oldProtect, &ignored );
		}

		void __fastcall HookedHlaePreRender( IClientEngineTools *pThis, void * )
		{
			if ( g_pOriginalHlaePreRender )
				g_pOriginalHlaePreRender( pThis );
		}

		void __fastcall HookedHlaePostRender( IClientEngineTools *pThis, void * )
		{
			if ( g_pOriginalHlaePostRender )
				g_pOriginalHlaePostRender( pThis );
		}

		void __fastcall HookedHlaePostMessage( IClientEngineTools *pThis, void *,
			HTOOLHANDLE handle, KeyValues *pMessage )
		{
			if ( g_Hlae.enabled )
				AgrHandleToolMessage( handle, pMessage );
			if ( g_pOriginalHlaePostMessage )
				g_pOriginalHlaePostMessage( pThis, handle, pMessage );
		}

		// This is the CS:S v34 path used by AdvancedFX when -afxV34 is selected:
		// IClientEngineTools::AdjustEngineViewport supplies the actual viewport,
		// and SetupEngineView is the authoritative camera override point.
		void __fastcall HookedHlaeAdjustViewport( IClientEngineTools *pThis, void *,
			int &x, int &y, int &width, int &height )
		{
			if ( g_pOriginalHlaeAdjustViewport )
				g_pOriginalHlaeAdjustViewport( pThis, x, y, width, height );
			if ( width > 0 ) g_Hlae.lastWidth = width;
			if ( height > 0 ) g_Hlae.lastHeight = height;
		}

		bool __fastcall HookedHlaeSetupEngineView( IClientEngineTools *pThis, void *,
			Vector &origin, QAngle &angles, float &fov )
		{
			bool result = false;
			if ( g_pOriginalHlaeSetupEngineView )
				result = g_pOriginalHlaeSetupEngineView(
					pThis, origin, angles, fov );

			CViewSetup view;
			view.x = 0;
			view.y = 0;
			view.width = g_Hlae.lastWidth;
			view.height = g_Hlae.lastHeight;
			view.origin = origin;
			view.angles = angles;
			view.fov = fov;
			ApplyArtHlaeView( view );
			origin = view.origin;
			angles = view.angles;
			fov = view.fov;
			return result;
		}

		void __fastcall HookedHlaeFrameStageNotify( IBaseClientDLL *pThis, void *,
			ClientFrameStage_t stage )
		{
			// AdvancedFX -afxV34 brackets AGR frames at these VClient013
			// stages, not at PreRenderAllTools / PostRenderAllTools (which can
			// run more than once for skybox and world views).
			if ( stage == FRAME_RENDER_START && g_Hlae.enabled && g_Hlae.agrFile )
			{
				const double time = CurrentTime();
				float frameTime = 0.0f;
				if ( g_Hlae.agrLastTime >= 0.0 && time >= g_Hlae.agrLastTime )
					frameTime = static_cast<float>( time - g_Hlae.agrLastTime );
				g_Hlae.agrLastTime = time;
				AgrBeginFrame( frameTime );
			}

			if ( g_pOriginalHlaeFrameStageNotify )
				g_pOriginalHlaeFrameStageNotify( pThis, stage );

			if ( stage == FRAME_RENDER_END && g_Hlae.enabled &&
				g_Hlae.agrFile && g_Hlae.agrFrameOpen )
			{
				if ( g_Hlae.agrRecordCamera && g_Hlae.hasLastCamera )
				{
					const CameraSample &camera = g_Hlae.lastCamera;
					AgrWriteDictionary( "afxCam" );
					AgrWriteFloat( static_cast<float>( camera.x ) );
					AgrWriteFloat( static_cast<float>( camera.y ) );
					AgrWriteFloat( static_cast<float>( camera.z ) );
					AgrWriteFloat( static_cast<float>( camera.pitch ) );
					AgrWriteFloat( static_cast<float>( camera.yaw ) );
					AgrWriteFloat( static_cast<float>( camera.roll ) );
					AgrWriteFloat( static_cast<float>( RealFov(
						camera.fov, g_Hlae.lastWidth, g_Hlae.lastHeight ) ) );
				}
				AgrEndFrame();
			}
		}

		bool InstallHlaeToolHooks()
		{
			if ( !g_pHlaeEngineTools || !g_pClient || g_HlaeToolHooksInstalled )
				return g_HlaeToolHooksInstalled;
			void **pVtable = *reinterpret_cast<void ***>( g_pHlaeEngineTools );
			void **pClientVtable = *reinterpret_cast<void ***>( g_pClient );
			if ( !pVtable || !pClientVtable )
				return false;
			// IClientEngineTools derives from IBaseInterface, whose virtual
			// destructor occupies slot 0 in this SDK / build. The five
			// AdvancedFX callbacks consequently begin at slot 5, not slot 4.
			g_ppHlaePreRenderSlot = &pVtable[5];
			g_ppHlaePostRenderSlot = &pVtable[6];
			g_ppHlaePostMessageSlot = &pVtable[7];
			g_ppHlaeAdjustViewportSlot = &pVtable[8];
			g_ppHlaeSetupEngineViewSlot = &pVtable[9];
			g_ppHlaeFrameStageNotifySlot = &pClientVtable[32];
			if ( !ReplaceHlaeVtableSlot( g_ppHlaePreRenderSlot,
				reinterpret_cast<void *>( &HookedHlaePreRender ),
				reinterpret_cast<void **>( &g_pOriginalHlaePreRender ) ) )
				return false;
			if ( !ReplaceHlaeVtableSlot( g_ppHlaePostRenderSlot,
				reinterpret_cast<void *>( &HookedHlaePostRender ),
				reinterpret_cast<void **>( &g_pOriginalHlaePostRender ) ) )
				goto install_failed;
			if ( !ReplaceHlaeVtableSlot( g_ppHlaePostMessageSlot,
				reinterpret_cast<void *>( &HookedHlaePostMessage ),
				reinterpret_cast<void **>( &g_pOriginalHlaePostMessage ) ) )
				goto install_failed;
			if ( !ReplaceHlaeVtableSlot( g_ppHlaeAdjustViewportSlot,
				reinterpret_cast<void *>( &HookedHlaeAdjustViewport ),
				reinterpret_cast<void **>( &g_pOriginalHlaeAdjustViewport ) ) )
				goto install_failed;
			if ( !ReplaceHlaeVtableSlot( g_ppHlaeSetupEngineViewSlot,
				reinterpret_cast<void *>( &HookedHlaeSetupEngineView ),
				reinterpret_cast<void **>( &g_pOriginalHlaeSetupEngineView ) ) )
				goto install_failed;
			if ( !ReplaceHlaeVtableSlot( g_ppHlaeFrameStageNotifySlot,
				reinterpret_cast<void *>( &HookedHlaeFrameStageNotify ),
				reinterpret_cast<void **>( &g_pOriginalHlaeFrameStageNotify ) ) )
				goto install_failed;
			g_HlaeToolHooksInstalled = true;
			LogMessage(
				"HLAE -afxV34 BRIDGE READY: VCLIENTENGINETOOLS001=%p "
				"PreRender[5]=%p PostRender[6]=%p PostToolMessage[7]=%p "
				"AdjustEngineViewport[8]=%p SetupEngineView[9]=%p "
				"VClient013::FrameStageNotify[32]=%p",
				g_pHlaeEngineTools, g_ppHlaePreRenderSlot,
				g_ppHlaePostRenderSlot, g_ppHlaePostMessageSlot,
				g_ppHlaeAdjustViewportSlot, g_ppHlaeSetupEngineViewSlot,
				g_ppHlaeFrameStageNotifySlot );
			return true;

		install_failed:
			RestoreHlaeVtableSlot( g_ppHlaeFrameStageNotifySlot,
				reinterpret_cast<void *>( &HookedHlaeFrameStageNotify ),
				reinterpret_cast<void *>( g_pOriginalHlaeFrameStageNotify ) );
			RestoreHlaeVtableSlot( g_ppHlaeSetupEngineViewSlot,
				reinterpret_cast<void *>( &HookedHlaeSetupEngineView ),
				reinterpret_cast<void *>( g_pOriginalHlaeSetupEngineView ) );
			RestoreHlaeVtableSlot( g_ppHlaeAdjustViewportSlot,
				reinterpret_cast<void *>( &HookedHlaeAdjustViewport ),
				reinterpret_cast<void *>( g_pOriginalHlaeAdjustViewport ) );
			RestoreHlaeVtableSlot( g_ppHlaePostMessageSlot,
				reinterpret_cast<void *>( &HookedHlaePostMessage ),
				reinterpret_cast<void *>( g_pOriginalHlaePostMessage ) );
			RestoreHlaeVtableSlot( g_ppHlaePostRenderSlot,
				reinterpret_cast<void *>( &HookedHlaePostRender ),
				reinterpret_cast<void *>( g_pOriginalHlaePostRender ) );
			RestoreHlaeVtableSlot( g_ppHlaePreRenderSlot,
				reinterpret_cast<void *>( &HookedHlaePreRender ),
				reinterpret_cast<void *>( g_pOriginalHlaePreRender ) );
			g_pOriginalHlaeSetupEngineView = NULL;
			g_pOriginalHlaeFrameStageNotify = NULL;
			g_pOriginalHlaeAdjustViewport = NULL;
			g_pOriginalHlaePostMessage = NULL;
			g_pOriginalHlaePostRender = NULL;
			g_pOriginalHlaePreRender = NULL;
			LogMessage( "HLAE -afxV34 BRIDGE INSTALL FAILED" );
			return false;
		}

		void RemoveHlaeToolHooks()
		{
			if ( !g_HlaeToolHooksInstalled )
				return;
			RestoreHlaeVtableSlot( g_ppHlaeFrameStageNotifySlot,
				reinterpret_cast<void *>( &HookedHlaeFrameStageNotify ),
				reinterpret_cast<void *>( g_pOriginalHlaeFrameStageNotify ) );
			RestoreHlaeVtableSlot( g_ppHlaeSetupEngineViewSlot,
				reinterpret_cast<void *>( &HookedHlaeSetupEngineView ),
				reinterpret_cast<void *>( g_pOriginalHlaeSetupEngineView ) );
			RestoreHlaeVtableSlot( g_ppHlaeAdjustViewportSlot,
				reinterpret_cast<void *>( &HookedHlaeAdjustViewport ),
				reinterpret_cast<void *>( g_pOriginalHlaeAdjustViewport ) );
			RestoreHlaeVtableSlot( g_ppHlaePostMessageSlot,
				reinterpret_cast<void *>( &HookedHlaePostMessage ),
				reinterpret_cast<void *>( g_pOriginalHlaePostMessage ) );
			RestoreHlaeVtableSlot( g_ppHlaePostRenderSlot,
				reinterpret_cast<void *>( &HookedHlaePostRender ),
				reinterpret_cast<void *>( g_pOriginalHlaePostRender ) );
			RestoreHlaeVtableSlot( g_ppHlaePreRenderSlot,
				reinterpret_cast<void *>( &HookedHlaePreRender ),
				reinterpret_cast<void *>( g_pOriginalHlaePreRender ) );
			g_HlaeToolHooksInstalled = false;
			g_pOriginalHlaeFrameStageNotify = NULL;
			g_pOriginalHlaeSetupEngineView = NULL;
			g_pOriginalHlaeAdjustViewport = NULL;
			g_pOriginalHlaePostMessage = NULL;
			g_pOriginalHlaePostRender = NULL;
			g_pOriginalHlaePreRender = NULL;
		}

		void WriteCamFrame( const CViewSetup &view, double time )
		{
			if ( !g_Hlae.camExport )
				return;
			fprintf( g_Hlae.camExport, "%.6f %.6f %.6f %.6f %.6f %.6f %.6f %.6f\n",
				time, view.origin.x, view.origin.y, view.origin.z,
				view.angles.z, view.angles.x, view.angles.y,
				RealFov( view.fov, view.width, view.height ) );
			fflush( g_Hlae.camExport );
		}

		void WriteBvhFrame( const CViewSetup &view )
		{
			if ( !g_Hlae.bvhExport )
				return;
			fprintf( g_Hlae.bvhExport, "%f %f %f %f %f %f\n",
				-view.origin.y, view.origin.z, -view.origin.x,
				-view.angles.z, -view.angles.x, view.angles.y );
			++g_Hlae.bvhFrameCount;
		}

		void WriteAgrFrame( const CViewSetup &view, double time )
		{
			if ( !g_Hlae.agrFile )
				return;
			float frameTime = 0.0f;
			if ( g_Hlae.agrLastTime >= 0.0 && time >= g_Hlae.agrLastTime )
				frameTime = static_cast<float>( time - g_Hlae.agrLastTime );
			g_Hlae.agrLastTime = time;
			AgrBeginFrame( frameTime );
			if ( g_Hlae.agrRecordCamera )
			{
				AgrWriteDictionary( "afxCam" );
				AgrWriteFloat( view.origin.x );
				AgrWriteFloat( view.origin.y );
				AgrWriteFloat( view.origin.z );
				AgrWriteFloat( view.angles.x );
				AgrWriteFloat( view.angles.y );
				AgrWriteFloat( view.angles.z );
				AgrWriteFloat( static_cast<float>(
					RealFov( view.fov, view.width, view.height ) ) );
			}
			AgrEndFrame();
		}

		bool IsInputSuspended()
		{
			return IsArtGuiVisible() || ( g_pEngine && g_pEngine->Con_IsVisible() );
		}

		void ResetInputMouseTracking()
		{
			InterlockedExchange( &g_Hlae.inputMouseDeltaX, 0 );
			InterlockedExchange( &g_Hlae.inputMouseDeltaY, 0 );
			InterlockedExchange( &g_Hlae.inputRawMouseTick, 0 );
			g_Hlae.inputCursorAnchorValid = false;
		}

		void UpdateInput( CViewSetup &view )
		{
			if ( !g_pMirvInput )
				return;
			float x = view.origin.x;
			float y = view.origin.y;
			float z = view.origin.z;
			float pitch = view.angles.x;
			float yaw = view.angles.y;
			float roll = view.angles.z;
			float fov = view.fov;
			double dt = g_pHlaeEngineTool ?
				static_cast<double>( g_pHlaeEngineTool->GetRealFrameTime() ) :
				g_Hlae.inputFrameTime;
			if ( dt < 0.0 ) dt = 0.0;
			if ( 0.25 < dt ) dt = 0.25;
			g_Hlae.inputFrameTime = dt;
			g_pMirvInput->Override( static_cast<float>( dt ),
				x, y, z, pitch, yaw, roll, fov );
			view.origin.x = x;
			view.origin.y = y;
			view.origin.z = z;
			view.angles.x = pitch;
			view.angles.y = yaw;
			view.angles.z = roll;
			view.fov = static_cast<float>( ClampFov( fov ) );
			g_pMirvInput->Supply_MouseFrameEnd();
			return;
		}

		double XmlAttribute( const char *pStart, const char *pName, double fallback )
		{
			char needle[64];
			Q_snprintf( needle, sizeof( needle ), "%s=\"", pName );
			const char *pValue = strstr( pStart, needle );
			return pValue ? atof( pValue + strlen( needle ) ) : fallback;
		}

		bool SaveCampath( const char *pPath )
		{
			FILE *pFile = NULL;
			if ( fopen_s( &pFile, pPath, "wb" ) || !pFile )
				return false;
			fprintf( pFile, "<?xml version=\"1.0\" encoding=\"utf-8\"?>\n" );
			fprintf( pFile, "<campath positionInterp=\"linear\" rotationInterp=\"sLinear\" "
				"fovInterp=\"linear\" offset=\"%.6f\"%s>\n<points>\n",
				g_Hlae.campathOffset, g_Hlae.campathHold ? " hold=\"\"" : "" );
			for ( size_t i = 0; i < g_Hlae.campath.size(); ++i )
			{
				const CameraSample &sample = g_Hlae.campath[i];
				fprintf( pFile,
					"<p t=\"%.6f\" x=\"%.6f\" y=\"%.6f\" z=\"%.6f\" "
					"fov=\"%.6f\" rx=\"%.6f\" ry=\"%.6f\" rz=\"%.6f\" />\n",
					sample.time, sample.x, sample.y, sample.z, sample.fov,
					sample.roll, sample.pitch, sample.yaw );
			}
			fprintf( pFile, "</points>\n</campath>\n" );
			fclose( pFile );
			return true;
		}

		bool LoadCampath( const char *pPath )
		{
			FILE *pFile = NULL;
			if ( fopen_s( &pFile, pPath, "rb" ) || !pFile )
				return false;
			fseek( pFile, 0, SEEK_END );
			const long size = ftell( pFile );
			rewind( pFile );
			if ( size <= 0 || size > 16 * 1024 * 1024 )
			{
				fclose( pFile );
				return false;
			}
			std::vector<char> data( static_cast<size_t>( size ) + 1 );
			const bool readOk = fread( &data[0], 1, size, pFile ) ==
				static_cast<size_t>( size );
			fclose( pFile );
			data[size] = '\0';
			if ( !readOk || !strstr( &data[0], "<campath" ) )
				return false;

			std::vector<CameraSample> loaded;
			const char *p = &data[0];
			while ( ( p = strstr( p, "<p " ) ) != NULL )
			{
				const char *pEnd = strchr( p, '>' );
				if ( !pEnd ) break;
				CameraSample sample;
				sample.time = XmlAttribute( p, "t", DBL_MAX );
				sample.x = XmlAttribute( p, "x", 0.0 );
				sample.y = XmlAttribute( p, "y", 0.0 );
				sample.z = XmlAttribute( p, "z", 0.0 );
				sample.fov = XmlAttribute( p, "fov", 90.0 );
				sample.roll = XmlAttribute( p, "rx", 0.0 );
				sample.pitch = XmlAttribute( p, "ry", 0.0 );
				sample.yaw = XmlAttribute( p, "rz", 0.0 );
				if ( sample.time != DBL_MAX )
					loaded.push_back( sample );
				p = pEnd + 1;
			}
			if ( loaded.empty() )
				return false;
			g_Hlae.campath.swap( loaded );
			SortSamples( g_Hlae.campath );
			const char *pRoot = strstr( &data[0], "<campath" );
			g_Hlae.campathOffset = XmlAttribute( pRoot, "offset", 0.0 );
			g_Hlae.campathHold = strstr( pRoot, " hold" ) != NULL &&
				strstr( pRoot, " hold" ) < strchr( pRoot, '>' );
			return true;
		}

		void ArtHlae_f()
		{
			if ( Argc() >= 3 && !_stricmp( Argv( 1 ), "enabled" ) )
			{
				bool enabled = false;
				if ( !ParseBool( Argv( 2 ), enabled ) )
				{
					ArtConsoleMessage( "art_hlae enabled 0|1\n" );
					return;
				}
				if ( g_Hlae.enabled && !enabled )
					StopAllWritersAndOverrides();
				g_Hlae.enabled = enabled;
				ArtConsoleMessage( "art_hlae: AdvancedFX -afxV34 features %s.\n",
					enabled ? "enabled" : "disabled" );
				return;
			}
			if ( Argc() >= 3 && !_stricmp( Argv( 1 ), "autoExport" ) )
			{
				const char *pTarget = Argv( 2 );
				if ( !_stricmp( pTarget, "bvhFps" ) )
				{
					if ( Argc() >= 4 )
					{
						g_Hlae.autoExportBvhFps = atof( Argv( 3 ) );
						if ( g_Hlae.autoExportBvhFps < 0.1 )
							g_Hlae.autoExportBvhFps = 0.1;
						if ( g_Hlae.autoExportBvhFps > 1000.0 )
							g_Hlae.autoExportBvhFps = 1000.0;
					}
					else
						ArtConsoleMessage( "Current value: %.6f\n",
							g_Hlae.autoExportBvhFps );
					return;
				}

				bool *pValue = NULL;
				if ( !_stricmp( pTarget, "agr" ) ) pValue = &g_Hlae.autoExportAgr;
				else if ( !_stricmp( pTarget, "camio" ) ) pValue = &g_Hlae.autoExportCamio;
				else if ( !_stricmp( pTarget, "bvh" ) ) pValue = &g_Hlae.autoExportBvh;
				if ( pValue )
				{
					if ( Argc() >= 4 )
					{
						bool enabled = false;
						if ( ParseBool( Argv( 3 ), enabled ) )
							*pValue = enabled;
						else
							ArtConsoleMessage(
								"art_hlae autoExport %s 0|1\n", pTarget );
					}
					else
						ArtConsoleMessage( "Current value: %d\n", *pValue ? 1 : 0 );
					return;
				}
			}
			if ( Argc() >= 2 && !_stricmp( Argv( 1 ), "help" ) )
			{
				PrintArtHlaeHelp();
				return;
			}
			ArtConsoleMessage(
				"art_hlae enabled 0|1 - Globally enable or disable HLAE features.\n"
				"art_hlae autoExport agr|camio|bvh 0|1\n"
				"art_hlae autoExport bvhFps <fps>\n"
				"art_hlae help - Print AdvancedFX -afxV34 command help.\n"
				"Current value: %d\n", g_Hlae.enabled ? 1 : 0 );
		}

		void MirvCampath_f()
		{
			if ( !RequireEnabled( "mirv_campath" ) )
				return;
			if ( Argc() >= 2 && !_stricmp( Argv( 1 ), "add" ) &&
				!g_Hlae.hasLastCamera )
			{
				ArtConsoleMessage( "mirv_campath: no camera frame is available yet.\n" );
				return;
			}

			AdvancedFxCommandArgs advancedFxArgs;
			if ( Argc() == 3 && !_stricmp( Argv( 1 ), "save" ) &&
				!advancedFxArgs.ResolveFileArgument(
					2, true, "campath.xml", ".xml" ) )
				return;
			if ( Argc() == 3 && !_stricmp( Argv( 1 ), "load" ) &&
				!advancedFxArgs.ResolveFileArgument(
					2, false, "campath.xml", ".xml" ) )
			{
				ArtConsoleMessage( "mirv_campath: input file not found.\n" );
				return;
			}
			MirvCampath_ConCommand( &advancedFxArgs,
				advancedfx::Message, advancedfx::Warning,
				&g_HlaeCampath, &g_MirvCampathTime,
				&g_MirvCampathCamera, &g_MirvCampathDrawer );
			return;

			const char *pCommand = Argc() >= 2 ? Argv( 1 ) : "";
			if ( !_stricmp( pCommand, "add" ) )
			{
				if ( !g_Hlae.hasLastCamera )
				{
					ArtConsoleMessage( "mirv_campath: no camera frame is available yet.\n" );
					return;
				}
				CameraSample sample = g_Hlae.lastCamera;
				sample.time = CurrentTime() - g_Hlae.campathOffset;
				g_Hlae.campath.push_back( sample );
				SortSamples( g_Hlae.campath );
				ArtConsoleMessage( "mirv_campath: added keyframe at %.6f (%u total).\n",
					sample.time, static_cast<unsigned int>( g_Hlae.campath.size() ) );
				return;
			}
			if ( !_stricmp( pCommand, "clear" ) )
			{
				g_Hlae.campath.clear();
				return;
			}
			if ( !_stricmp( pCommand, "remove" ) && Argc() >= 3 )
			{
				const int index = atoi( Argv( 2 ) );
				if ( index < 0 || static_cast<size_t>( index ) >= g_Hlae.campath.size() )
				{
					ArtConsoleMessage( "mirv_campath: keyframe index %d is out of range.\n",
						index );
					return;
				}
				g_Hlae.campath.erase( g_Hlae.campath.begin() + index );
				ArtConsoleMessage( "mirv_campath: removed keyframe %d (%u remaining).\n",
					index, static_cast<unsigned int>( g_Hlae.campath.size() ) );
				return;
			}
			if ( !_stricmp( pCommand, "draw" ) )
			{
				const char *pOption = Argc() >= 3 ? Argv( 2 ) : "";
				if ( !_stricmp( pOption, "enabled" ) )
				{
					if ( Argc() >= 4 )
					{
						bool enabled = false;
						if ( ParseBool( Argv( 3 ), enabled ) )
							g_Hlae.campathDraw = enabled;
					}
					else ArtConsoleMessage( "Current value: %d\n",
						g_Hlae.campathDraw ? 1 : 0 );
					return;
				}
				if ( !_stricmp( pOption, "keyAxis" ) )
				{
					if ( Argc() >= 4 )
					{
						bool enabled = false;
						if ( ParseBool( Argv( 3 ), enabled ) )
							g_Hlae.campathDrawKeyAxis = enabled;
					}
					else ArtConsoleMessage( "Current value: %d\n",
						g_Hlae.campathDrawKeyAxis ? 1 : 0 );
					return;
				}
				if ( !_stricmp( pOption, "keyCam" ) )
				{
					if ( Argc() >= 4 )
					{
						bool enabled = false;
						if ( ParseBool( Argv( 3 ), enabled ) )
							g_Hlae.campathDrawKeyCam = enabled;
					}
					else ArtConsoleMessage( "Current value: %d\n",
						g_Hlae.campathDrawKeyCam ? 1 : 0 );
					return;
				}
				if ( !_stricmp( pOption, "keyIndex" ) )
				{
					if ( Argc() >= 4 )
						g_Hlae.campathDrawKeyIndex =
							static_cast<float>( atof( Argv( 3 ) ) );
					else ArtConsoleMessage( "Current value: %.3f\n",
						g_Hlae.campathDrawKeyIndex );
					return;
				}
				ArtConsoleMessage(
					"mirv_campath draw enabled 0|1\n"
					"mirv_campath draw keyAxis 0|1\n"
					"mirv_campath draw keyCam 0|1\n"
					"mirv_campath draw keyIndex <height>\n" );
				return;
			}
			if ( !_stricmp( pCommand, "enable" ) || !_stricmp( pCommand, "enabled" ) )
			{
				if ( Argc() >= 3 )
				{
					bool enabled = false;
					if ( ParseBool( Argv( 2 ), enabled ) )
						g_Hlae.campathEnabled = enabled;
					return;
				}
				ArtConsoleMessage( "Current value: %d\n", g_Hlae.campathEnabled ? 1 : 0 );
				return;
			}
			if ( !_stricmp( pCommand, "hold" ) )
			{
				if ( Argc() >= 3 )
				{
					bool hold = false;
					if ( ParseBool( Argv( 2 ), hold ) )
						g_Hlae.campathHold = hold;
					return;
				}
				ArtConsoleMessage( "Current value: %d\n", g_Hlae.campathHold ? 1 : 0 );
				return;
			}
			if ( !_stricmp( pCommand, "offset" ) )
			{
				if ( Argc() >= 3 )
					g_Hlae.campathOffset = atof( Argv( 2 ) );
				else
					ArtConsoleMessage( "Current value: %.6f\n", g_Hlae.campathOffset );
				return;
			}
			if ( !_stricmp( pCommand, "print" ) )
			{
				for ( size_t i = 0; i < g_Hlae.campath.size(); ++i )
				{
					const CameraSample &s = g_Hlae.campath[i];
					ArtConsoleMessage(
						"%u: %.6f -> %.3f %.3f %.3f | %.3f %.3f %.3f | %.3f\n",
						static_cast<unsigned int>( i ), s.time, s.x, s.y, s.z,
						s.pitch, s.yaw, s.roll, s.fov );
				}
				return;
			}
			if ( !_stricmp( pCommand, "save" ) && Argc() >= 3 )
			{
				char path[MAX_PATH];
				if ( ResolveOutputPath( Argv( 2 ), "campath.xml", ".xml",
					path, sizeof( path ) ) )
					ArtConsoleMessage( "Saving: %s (%s).\n",
						SaveCampath( path ) ? "OK" : "ERROR", path );
				return;
			}
			if ( !_stricmp( pCommand, "load" ) && Argc() >= 3 )
			{
				char path[MAX_PATH];
				if ( ResolveInputPath( Argv( 2 ), path, sizeof( path ) ) )
					ArtConsoleMessage( "Loading: %s (%s).\n",
						LoadCampath( path ) ? "OK" : "ERROR", path );
				else
					ArtConsoleMessage( "mirv_campath: input file not found.\n" );
				return;
			}
			ArtConsoleMessage(
				"mirv_campath add | remove <index> | clear | print\n"
				"mirv_campath enabled 0|1\n"
				"mirv_campath draw enabled|keyAxis|keyCam|keyIndex <value>\n"
				"mirv_campath hold 0|1\n"
				"mirv_campath offset <seconds>\n"
				"mirv_campath save <fileName> | load <fileName>\n" );
		}

		void MirrorMirvInputCommandState()
		{
			if ( Argc() < 2 )
				return;

			const char *pCommand = Argv( 1 );
			if ( !_stricmp( pCommand, "camera" ) )
			{
				g_Hlae.inputCamera = true;
				return;
			}
			if ( !_stricmp( pCommand, "end" ) )
			{
				g_Hlae.inputCamera = false;
				return;
			}
			if ( _stricmp( pCommand, "cfg" ) || Argc() < 4 )
				return;

			const char *pOption = Argv( 2 );
			const double value = atof( Argv( 3 ) );

			if ( !_stricmp( pOption, "msens" ) ) g_Hlae.inputMouseSensitivity = value;
			else if ( !_stricmp( pOption, "ksens" ) ) g_Hlae.inputKeyboardSensitivity = value;
			else if ( !_stricmp( pOption, "stepFactor" ) ) g_Hlae.inputStepFactor = value;
			else if ( !_stricmp( pOption, "kForwardSpeed" ) ) g_Hlae.inputKeyboardForwardSpeed = value;
			else if ( !_stricmp( pOption, "kBackwardSpeed" ) ) g_Hlae.inputKeyboardBackwardSpeed = value;
			else if ( !_stricmp( pOption, "kLeftSpeed" ) ) g_Hlae.inputKeyboardLeftSpeed = value;
			else if ( !_stricmp( pOption, "kRightSpeed" ) ) g_Hlae.inputKeyboardRightSpeed = value;
			else if ( !_stricmp( pOption, "kUpSpeed" ) ) g_Hlae.inputKeyboardUpSpeed = value;
			else if ( !_stricmp( pOption, "kDownSpeed" ) ) g_Hlae.inputKeyboardDownSpeed = value;
			else if ( !_stricmp( pOption, "kPitchPositiveSpeed" ) ) g_Hlae.inputKeyboardPitchPositiveSpeed = value;
			else if ( !_stricmp( pOption, "kPitchNegativeSpeed" ) ) g_Hlae.inputKeyboardPitchNegativeSpeed = value;
			else if ( !_stricmp( pOption, "kYawPositiveSpeed" ) ) g_Hlae.inputKeyboardYawPositiveSpeed = value;
			else if ( !_stricmp( pOption, "kYawNegativeSpeed" ) ) g_Hlae.inputKeyboardYawNegativeSpeed = value;
			else if ( !_stricmp( pOption, "kRollPositiveSpeed" ) ) g_Hlae.inputKeyboardRollPositiveSpeed = value;
			else if ( !_stricmp( pOption, "kRollNegativeSpeed" ) ) g_Hlae.inputKeyboardRollNegativeSpeed = value;
			else if ( !_stricmp( pOption, "kFovPositiveSpeed" ) ) g_Hlae.inputKeyboardFovPositiveSpeed = value;
			else if ( !_stricmp( pOption, "kFovNegativeSpeed" ) ) g_Hlae.inputKeyboardFovNegativeSpeed = value;
			else if ( !_stricmp( pOption, "mYawSpeed" ) ) g_Hlae.inputMouseYawSpeed = value;
			else if ( !_stricmp( pOption, "mPitchSpeed" ) ) g_Hlae.inputMousePitchSpeed = value;
			else if ( !_stricmp( pOption, "mFovPositiveSpeed" ) ) g_Hlae.inputMouseFovPositiveSpeed = value;
			else if ( !_stricmp( pOption, "mFovNegativeSpeed" ) ) g_Hlae.inputMouseFovNegativeSpeed = value;
			else if ( !_stricmp( pOption, "mForwardSpeed" ) ) g_Hlae.inputMouseForwardSpeed = value;
			else if ( !_stricmp( pOption, "mBackSpeed" ) ) g_Hlae.inputMouseBackwardSpeed = value;
			else if ( !_stricmp( pOption, "mLeftSpeed" ) ) g_Hlae.inputMouseLeftSpeed = value;
			else if ( !_stricmp( pOption, "mRightSpeed" ) ) g_Hlae.inputMouseRightSpeed = value;
			else if ( !_stricmp( pOption, "mUpSpeed" ) ) g_Hlae.inputMouseUpSpeed = value;
			else if ( !_stricmp( pOption, "mDownSpeed" ) ) g_Hlae.inputMouseDownSpeed = value;
			else if ( !_stricmp( pOption, "mouseMoveSupport" ) )
			{
				bool enabled = false;
				if ( ParseBool( Argv( 3 ), enabled ) )
					g_Hlae.inputMouseMoveSupport = enabled;
			}
			else if ( !_stricmp( pOption, "rotLocalSpace" ) )
			{
				bool enabled = false;
				if ( ParseBool( Argv( 3 ), enabled ) )
					g_Hlae.inputRotLocalSpace = enabled;
			}
			else if ( !_stricmp( pOption, "offsetMode" ) )
			{
				if ( !_stricmp( Argv( 3 ), "last" ) ) g_Hlae.inputOffsetMode = 0;
				else if ( !_stricmp( Argv( 3 ), "ownLast" ) ) g_Hlae.inputOffsetMode = 1;
				else if ( !_stricmp( Argv( 3 ), "game" ) ) g_Hlae.inputOffsetMode = 2;
				else if ( !_stricmp( Argv( 3 ), "current" ) ) g_Hlae.inputOffsetMode = 3;
			}
			else if ( !_stricmp( pOption, "smooth" ) && Argc() >= 5 )
			{
				const char *pSmoothOption = Argv( 3 );
				const double smoothValue = atof( Argv( 4 ) );
				if ( !_stricmp( pSmoothOption, "enabled" ) )
				{
					bool enabled = false;
					if ( ParseBool( Argv( 4 ), enabled ) )
						g_Hlae.inputSmooth = enabled;
				}
				else if ( !_stricmp( pSmoothOption, "halfTimeVec" ) )
				{
					g_Hlae.inputSmoothHalfTime = smoothValue;
					g_Hlae.inputSmoothHalfTimeVec = smoothValue;
				}
				else if ( !_stricmp( pSmoothOption, "halfTimeAng" ) )
					g_Hlae.inputSmoothHalfTimeAng = smoothValue;
				else if ( !_stricmp( pSmoothOption, "halfTimeFov" ) )
					g_Hlae.inputSmoothHalfTimeFov = smoothValue;
				else if ( !_stricmp( pSmoothOption, "rotShortestPath" ) )
				{
					bool enabled = false;
					if ( ParseBool( Argv( 4 ), enabled ) )
						g_Hlae.inputSmoothRotShortestPath = enabled;
				}
			}
		}

		void MirvInput_f()
		{
			if ( !RequireEnabled( "mirv_input" ) )
				return;
			if ( !g_pMirvInput )
				g_pMirvInput = new MirvInput( &g_MirvInputDependencies );
			AdvancedFxCommandArgs advancedFxArgs;
			if ( Argc() == 4 && !_stricmp( Argv( 1 ), "mem" ) )
			{
				if ( !_stricmp( Argv( 2 ), "save" ) &&
					!advancedFxArgs.ResolveFileArgument(
						3, true, "mirv_input.xml", ".xml" ) )
					return;
				if ( !_stricmp( Argv( 2 ), "load" ) &&
					!advancedFxArgs.ResolveFileArgument(
						3, false, "mirv_input.xml", ".xml" ) )
				{
					ArtConsoleMessage( "mirv_input: input file not found.\n" );
					return;
				}
			}
			g_pMirvInput->ConCommand( &advancedFxArgs );
			MirrorMirvInputCommandState();
			return;

			const char *pCommand = Argc() >= 2 ? Argv( 1 ) : "";
			if ( !_stricmp( pCommand, "camera" ) )
			{
				g_Hlae.inputCamera = true;
				g_Hlae.inputInitialized = false;
				ResetInputMouseTracking();
				return;
			}
			if ( !_stricmp( pCommand, "end" ) )
			{
				g_Hlae.inputCamera = false;
				g_Hlae.inputInitialized = false;
				ResetInputMouseTracking();
				return;
			}
			if ( !_stricmp( pCommand, "position" ) )
			{
				if ( Argc() == 5 )
				{
					if ( strcmp( Argv( 2 ), "*" ) ) g_Hlae.inputView.x = atof( Argv( 2 ) );
					if ( strcmp( Argv( 3 ), "*" ) ) g_Hlae.inputView.y = atof( Argv( 3 ) );
					if ( strcmp( Argv( 4 ), "*" ) ) g_Hlae.inputView.z = atof( Argv( 4 ) );
					return;
				}
			}
			if ( !_stricmp( pCommand, "angles" ) )
			{
				if ( Argc() == 5 )
				{
					if ( strcmp( Argv( 2 ), "*" ) ) g_Hlae.inputView.pitch = atof( Argv( 2 ) );
					if ( strcmp( Argv( 3 ), "*" ) ) g_Hlae.inputView.roll = atof( Argv( 3 ) );
					if ( strcmp( Argv( 4 ), "*" ) ) g_Hlae.inputView.yaw = atof( Argv( 4 ) );
					return;
				}
			}
			if ( !_stricmp( pCommand, "fov" ) && Argc() >= 3 )
			{
				if ( !_stricmp( Argv( 2 ), "real" ) && Argc() >= 4 )
					g_Hlae.inputView.fov = InverseRealFov( atof( Argv( 3 ) ),
						g_Hlae.lastWidth, g_Hlae.lastHeight );
				else
					g_Hlae.inputView.fov = ClampFov( atof( Argv( 2 ) ) );
				return;
			}
			if ( !_stricmp( pCommand, "cfg" ) && Argc() >= 3 )
			{
				const char *pOption = Argv( 2 );
				if ( !_stricmp( pOption, "msens" ) )
				{
					if ( Argc() >= 4 ) g_Hlae.inputMouseSensitivity = atof( Argv( 3 ) );
					else ArtConsoleMessage( "Value: %f\n", g_Hlae.inputMouseSensitivity );
					return;
				}
				if ( !_stricmp( pOption, "ksens" ) )
				{
					if ( Argc() >= 4 ) g_Hlae.inputKeyboardSensitivity = atof( Argv( 3 ) );
					else ArtConsoleMessage( "Value: %f\n", g_Hlae.inputKeyboardSensitivity );
					return;
				}
				if ( !_stricmp( pOption, "smooth" ) && Argc() >= 4 )
				{
					if ( !_stricmp( Argv( 3 ), "enabled" ) )
					{
						if ( Argc() >= 5 )
						{
							bool enabled = false;
							if ( ParseBool( Argv( 4 ), enabled ) )
								g_Hlae.inputSmooth = enabled;
						}
						else ArtConsoleMessage( "Value: %d\n", g_Hlae.inputSmooth ? 1 : 0 );
						return;
					}
					if ( !_stricmp( Argv( 3 ), "halfTime" ) )
					{
						if ( Argc() >= 5 )
							g_Hlae.inputSmoothHalfTime = atof( Argv( 4 ) );
						else ArtConsoleMessage( "Value: %f\n", g_Hlae.inputSmoothHalfTime );
						return;
					}
				}
			}
			ArtConsoleMessage(
				"mirv_input camera | end\n"
				"mirv_input position <x> <y> <z>\n"
				"mirv_input angles <yPitch> <xRoll> <zYaw>\n"
				"mirv_input fov [real] <fov>\n"
				"mirv_input cfg msens <value> | ksens <value>\n"
				"mirv_input cfg smooth enabled 0|1 | halfTime <seconds>\n" );
		}

		void MirvCamio_f()
		{
			if ( !RequireEnabled( "mirv_camio" ) )
				return;
			const char *pMode = Argc() >= 2 ? Argv( 1 ) : "";
			const char *pAction = Argc() >= 3 ? Argv( 2 ) : "";
			if ( !_stricmp( pMode, "export" ) )
			{
				if ( !_stricmp( pAction, "start" ) && Argc() >= 4 )
				{
					StartCamExport( Argv( 3 ) );
					return;
				}
				if ( !_stricmp( pAction, "end" ) )
				{
					StopCamExport();
					return;
				}
			}
			if ( !_stricmp( pMode, "import" ) )
			{
				if ( !_stricmp( pAction, "start" ) && Argc() >= 4 )
				{
					char path[MAX_PATH];
					if ( !ResolveInputPath( Argv( 3 ), path, sizeof( path ) ) ||
						!ParseCamFile( path, g_Hlae.camImport ) )
					{
						ArtConsoleMessage( "Error importing CAM file \"%s\"\n", Argv( 3 ) );
						return;
					}
					g_Hlae.camImportStartTime = CurrentTime();
					Q_strncpy( g_Hlae.camImportPath, path, sizeof( g_Hlae.camImportPath ) );
					return;
				}
				if ( !_stricmp( pAction, "end" ) )
				{
					g_Hlae.camImport.clear();
					g_Hlae.camImportPath[0] = '\0';
					return;
				}
			}
			ArtConsoleMessage(
				"mirv_camio export start <fileName> | export end\n"
				"mirv_camio import start <fileName> | import end\n" );
		}

		void MirvCamexport_f()
		{
			if ( !RequireEnabled( "mirv_camexport" ) )
				return;
			if ( Argc() >= 2 && !_stricmp( Argv( 1 ), "stop" ) )
			{
				StopBvhExport();
				return;
			}
			if ( Argc() >= 2 && !_stricmp( Argv( 1 ), "timeinfo" ) )
			{
				ArtConsoleMessage( "Current (interpolated client) time: %f\n", CurrentTime() );
				return;
			}
			if ( Argc() >= 4 && !_stricmp( Argv( 1 ), "start" ) )
			{
				StartBvhExport( Argv( 2 ), atof( Argv( 3 ) ) );
				return;
			}
			ArtConsoleMessage(
				"mirv_camexport start <filename> <fps>\n"
				"mirv_camexport stop\nmirv_camexport timeinfo\n" );
		}

		void MirvCamimport_f()
		{
			if ( !RequireEnabled( "mirv_camimport" ) )
				return;
			if ( Argc() >= 2 && !_stricmp( Argv( 1 ), "stop" ) )
			{
				g_Hlae.bvhImport.clear();
				g_Hlae.bvhImportPath[0] = '\0';
				return;
			}
			if ( Argc() >= 3 && !_stricmp( Argv( 1 ), "start" ) )
			{
				char path[MAX_PATH];
				if ( !ResolveInputPath( Argv( 2 ), path, sizeof( path ) ) ||
					!ParseBvhFile( path, g_Hlae.bvhImport ) )
				{
					ArtConsoleMessage( "Loading failed.\n" );
					return;
				}
				g_Hlae.bvhImportBaseTime = CurrentTime();
				Q_strncpy( g_Hlae.bvhImportPath, path, sizeof( g_Hlae.bvhImportPath ) );
				return;
			}
			if ( Argc() >= 2 && !_stricmp( Argv( 1 ), "basetime" ) )
			{
				if ( Argc() >= 3 )
					g_Hlae.bvhImportBaseTime = !_stricmp( Argv( 2 ), "current" ) ?
						CurrentTime() : atof( Argv( 2 ) );
				else
					ArtConsoleMessage( "Current setting: %f\n", g_Hlae.bvhImportBaseTime );
				return;
			}
			if ( Argc() >= 4 && !_stricmp( Argv( 1 ), "toCamPath" ) )
			{
				if ( g_Hlae.bvhImport.empty() )
				{
					ArtConsoleMessage( "ERROR: Something went wrong when converting to mirv_campath!\n" );
					return;
				}
				const double fov = ClampFov( atof( Argv( 3 ) ) );
				g_HlaeCampath.SelectNone();
				g_HlaeCampath.Clear();
				for ( size_t i = 0; i < g_Hlae.bvhImport.size(); ++i )
				{
					const CameraSample &sample = g_Hlae.bvhImport[i];
					g_HlaeCampath.Add(
						sample.time + g_Hlae.bvhImportBaseTime,
						CamPathValue( sample.x, sample.y, sample.z,
							sample.pitch, sample.yaw, sample.roll, fov ) );
				}
				if ( atoi( Argv( 2 ) ) )
				{
					g_HlaeCampath.PositionInterpMethod_set( CamPath::DI_LINEAR );
					g_HlaeCampath.RotationInterpMethod_set( CamPath::QI_SLINEAR );
					g_HlaeCampath.FovInterpMethod_set( CamPath::DI_LINEAR );
				}
				g_HlaeCampath.Enabled_set( true );
				return;
			}
			ArtConsoleMessage(
				"mirv_camimport start <filename>\nmirv_camimport stop\n"
				"mirv_camimport basetime <newTime> | current\n"
				"mirv_camimport toCamPath 0|1 <fov>\n" );
		}

		bool HandleAgrBooleanOption( const char *pName, bool &value )
		{
			if ( _stricmp( Argv( 1 ), pName ) )
				return false;
			if ( Argc() >= 3 )
			{
				bool parsed = false;
				if ( ParseBool( Argv( 2 ), parsed ) )
					value = parsed;
			}
			else
				ArtConsoleMessage( "Current value: %d.\n", value ? 1 : 0 );
			return true;
		}

		void MirvAgr_f()
		{
			if ( !RequireEnabled( "mirv_agr" ) )
				return;
			if ( Argc() >= 2 && !_stricmp( Argv( 1 ), "enabled" ) )
			{
				if ( Argc() >= 3 )
				{
					bool enabled = false;
					if ( ParseBool( Argv( 2 ), enabled ) )
						AgrEnableClientRecording( enabled );
				}
				else
					ArtConsoleMessage( "Current value: %d\n",
						g_Hlae.agrEnabled ? 1 : 0 );
				return;
			}
			if ( Argc() >= 2 && !_stricmp( Argv( 1 ), "start" ) && Argc() >= 3 )
			{
				StartAgrExport( Argv( 2 ) );
				return;
			}
			if ( Argc() >= 2 && !_stricmp( Argv( 1 ), "stop" ) )
			{
				StopAgr();
				return;
			}
			if ( HandleAgrBooleanOption( "recordCamera", g_Hlae.agrRecordCamera ) ||
				HandleAgrBooleanOption( "recordPlayers", g_Hlae.agrRecordPlayers ) ||
				HandleAgrBooleanOption( "recordWeapons", g_Hlae.agrRecordWeapons ) ||
				HandleAgrBooleanOption( "recordProjectiles", g_Hlae.agrRecordProjectiles ) ||
				HandleAgrBooleanOption( "recordInvisible", g_Hlae.agrRecordInvisible ) ||
				HandleAgrBooleanOption( "debug", g_Hlae.agrDebug ) )
				return;
			if ( Argc() >= 2 && ( !_stricmp( Argv( 1 ), "recordViewModel" ) ||
				!_stricmp( Argv( 1 ), "recordViewModels" ) ) )
			{
				if ( Argc() >= 3 )
				{
					g_Hlae.agrRecordViewModels = !_stricmp( Argv( 1 ), "recordViewModel" ) ?
						( atoi( Argv( 2 ) ) ? -1 : 0 ) : atoi( Argv( 2 ) );
				}
				else ArtConsoleMessage( "Current value: %d.\n", g_Hlae.agrRecordViewModels );
				return;
			}
			if ( Argc() >= 2 && !_stricmp( Argv( 1 ), "recordPlayerCameras" ) )
			{
				if ( Argc() >= 3 ) g_Hlae.agrRecordPlayerCameras = atoi( Argv( 2 ) );
				else ArtConsoleMessage( "Current value: %d.\n", g_Hlae.agrRecordPlayerCameras );
				return;
			}
			ArtConsoleMessage(
				"mirv_agr enabled 0|1\nmirv_agr start <sFilePath>\nmirv_agr stop\n"
				"mirv_agr recordCamera|recordPlayers|recordWeapons|recordProjectiles 0|1\n"
				"mirv_agr recordPlayerCameras 0|<iEntIndex>|-1\n"
				"mirv_agr recordViewModel|recordViewModels 0|<iEntIndex>|-1\n"
				"mirv_agr recordInvisible 0|1\nmirv_agr debug 0|1\n" );
		}

		void MirvFov_f()
		{
			if ( !RequireEnabled( "mirv_fov" ) )
				return;
			if ( Argc() >= 2 && !_stricmp( Argv( 1 ), "default" ) )
			{
				g_Hlae.fovOverride = false;
				return;
			}
			if ( Argc() >= 2 && !_stricmp( Argv( 1 ), "handleZoom" ) )
			{
				if ( Argc() >= 4 && !_stricmp( Argv( 2 ), "enabled" ) )
				{
					bool enabled = false;
					if ( ParseBool( Argv( 3 ), enabled ) )
						g_Hlae.fovHandleZoom = enabled;
					return;
				}
				if ( Argc() >= 4 && !_stricmp( Argv( 2 ), "minUnzoomedFov" ) )
				{
					if ( !_stricmp( Argv( 3 ), "current" ) )
						g_Hlae.fovMinUnzoomed = g_Hlae.lastCamera.fov;
					else if ( !_stricmp( Argv( 3 ), "real" ) && Argc() >= 5 )
						g_Hlae.fovMinUnzoomed = InverseRealFov(
							atof( Argv( 4 ) ), g_Hlae.lastWidth, g_Hlae.lastHeight );
					else
						g_Hlae.fovMinUnzoomed = atof( Argv( 3 ) );
					return;
				}
			}
			if ( Argc() >= 3 && !_stricmp( Argv( 1 ), "real" ) )
			{
				int width = 4;
				int height = 3;
				if ( g_pEngine ) g_pEngine->GetScreenSize( width, height );
				g_Hlae.fov = InverseRealFov( atof( Argv( 2 ) ), width, height );
				g_Hlae.fovOverride = true;
				return;
			}
			if ( Argc() >= 2 && ( !isalpha( static_cast<unsigned char>( Argv( 1 )[0] ) ) ||
				Argv( 1 )[0] == '-' || Argv( 1 )[0] == '+' ) )
			{
				g_Hlae.fov = ClampFov( atof( Argv( 1 ) ) );
				g_Hlae.fovOverride = true;
				return;
			}
			ArtConsoleMessage(
				"mirv_fov [real] <f> | default\n"
				"mirv_fov handleZoom enabled 0|1\n"
				"mirv_fov handleZoom minUnzoomedFov current | [real] <f>\n"
				"Current value: %s", g_Hlae.fovOverride ? "" : "default\n" );
			if ( g_Hlae.fovOverride )
				ArtConsoleMessage( "%f\n", g_Hlae.fov );
		}

		ConCommand art_hlae( "art_hlae", ArtHlae_f,
			"Globally enable or disable the HLAE v34 compatibility features." );
		ConCommand mirv_campath( "mirv_campath", MirvCampath_f, "camera paths" );
		ConCommand mirv_input( "mirv_input", MirvInput_f, "Input mode configuration." );
		ConCommand mirv_camio( "mirv_camio", MirvCamio_f,
			"New camera motion data import / export." );
		ConCommand mirv_agr( "mirv_agr", MirvAgr_f, "AFX GameRecord" );
		ConCommand mirv_camexport( "mirv_camexport", MirvCamexport_f,
			"controls camera motion data export" );
		ConCommand mirv_camimport( "mirv_camimport", MirvCamimport_f,
			"controls camera motion data import" );
		ConCommand mirv_fov( "mirv_fov", MirvFov_f,
			"allows overriding FOV (Field Of View) of the camera" );
	}

	bool InitializeArtHlae( CreateInterfaceFn clientFactory, CreateInterfaceFn engineFactory )
	{
		int result = -1;
		// AdvancedFX -afxV34 receives this through the app-system (engine)
		// factory. Querying client.dll here silently returns NULL and leaves all
		// camera commands registered but disconnected from the engine.
		g_pHlaeEngineTools = engineFactory ?
			static_cast<IClientEngineTools *>(
				engineFactory( VCLIENTENGINETOOLS_INTERFACE_VERSION, &result ) ) : NULL;
		result = -1;
		g_pHlaeEngineTool = engineFactory ?
			static_cast<IEngineTool *>(
				engineFactory( VENGINETOOL_INTERFACE_VERSION, &result ) ) : NULL;
		result = -1;
		g_pHlaeClientTools = clientFactory ?
			static_cast<IClientTools *>(
				clientFactory( VCLIENTTOOLS_INTERFACE_VERSION, &result ) ) : NULL;
		if ( !g_pMirvInput )
			g_pMirvInput = new MirvInput( &g_MirvInputDependencies );
		if ( !g_Hlae.campathChangedCallbackRegistered )
		{
			g_HlaeCampath.OnChangedAdd( CampathChanged, NULL );
			g_Hlae.campathChangedCallbackRegistered = true;
			g_Hlae.campathTrajectoryDirty = true;
		}
		const bool toolHooks = InstallHlaeToolHooks();
		if ( g_pHlaeClientTools )
			g_Hlae.agrEnabled = g_pHlaeClientTools->IsInRecordingMode();
		LogMessage(
			"HLAE -afxV34: initialized enabled=1 "
			"output_policy='absolute path, active ART take, or configured ART output folder' "
			"engine_factory=%p client_factory=%p client_engine_tools=%p "
			"engine_tool=%p client_tools=%p bridge_ready=%d",
			engineFactory, clientFactory,
			g_pHlaeEngineTools, g_pHlaeEngineTool,
			g_pHlaeClientTools, toolHooks ? 1 : 0 );
		if ( !toolHooks )
			ArtConsoleMessage(
				"v34_art: AdvancedFX -afxV34 VCLIENTENGINETOOLS001 bridge failed; "
				"mirv_* features were not enabled.\n" );
		return toolHooks;
	}

	bool HasExistingArtHlaeCommand()
	{
		static const char *names[] =
		{
			"art_hlae", "mirv_campath", "mirv_input", "mirv_camio",
			"mirv_agr", "mirv_camexport", "mirv_camimport", "mirv_fov"
		};
		for ( const ConCommandBase *pCommand = g_pCvar ? g_pCvar->GetCommands() : NULL;
			pCommand; pCommand = pCommand->GetNext() )
		{
			for ( int i = 0; i < ARRAYSIZE( names ); ++i )
			{
				if ( !_stricmp( pCommand->GetName(), names[i] ) )
				{
					LogMessage( "HLAE COMMAND CONFLICT: '%s' already exists", names[i] );
					return true;
				}
			}
		}
		return false;
	}

	void ApplyArtHlaeView( CViewSetup &view )
	{
		if ( !g_Hlae.enabled )
			return;
		const double time = CurrentTime();
		CameraSample sample;
		g_Hlae.lastWidth = view.width > 0 ? view.width : g_Hlae.lastWidth;
		g_Hlae.lastHeight = view.height > 0 ? view.height : g_Hlae.lastHeight;
		g_Hlae.gameCamera = FromView( view, time );
		g_Hlae.hasGameCamera = true;

		if ( g_HlaeCampath.Enabled_get() && g_HlaeCampath.CanEval() )
		{
			double campathTime = time - g_HlaeCampath.GetOffset();
			if ( g_HlaeCampath.GetHold() )
			{
				if ( campathTime < g_HlaeCampath.GetLowerBound() )
					campathTime = g_HlaeCampath.GetLowerBound();
				else if ( g_HlaeCampath.GetUpperBound() < campathTime )
					campathTime = g_HlaeCampath.GetUpperBound();
			}
			if ( g_HlaeCampath.GetLowerBound() <= campathTime &&
				campathTime <= g_HlaeCampath.GetUpperBound() )
			{
				sample = FromCamPathValue(
					campathTime, g_HlaeCampath.Eval( campathTime ) );
				ApplySample( sample, view );
			}
		}

		if ( !g_Hlae.bvhImport.empty() &&
			InterpolateSamples( g_Hlae.bvhImport,
				time - g_Hlae.bvhImportBaseTime, false, sample ) )
			ApplySample( sample, view );

		if ( !g_Hlae.camImport.empty() &&
			InterpolateSamples( g_Hlae.camImport,
				time - g_Hlae.camImportStartTime + g_Hlae.camImport.front().time,
				false, sample ) )
		{
			sample.fov = InverseRealFov( sample.fov, view.width, view.height );
			ApplySample( sample, view );
		}

		if ( g_Hlae.fovOverride &&
			( !g_Hlae.fovHandleZoom || view.fov >= g_Hlae.fovMinUnzoomed ) )
			view.fov = static_cast<float>( g_Hlae.fov );

		UpdateInput( view );

		WriteBvhFrame( view );
		WriteCamFrame( view, time );
		if ( !g_HlaeToolHooksInstalled )
			WriteAgrFrame( view, time );
		g_Hlae.lastCamera = FromView( view, time );
		g_Hlae.hasLastCamera = true;
	}

	void BeginArtHlaeTakeExports()
	{
		g_Hlae.autoStartedAgr = false;
		g_Hlae.autoStartedCamio = false;
		g_Hlae.autoStartedBvh = false;
		if ( !g_Hlae.enabled )
			return;

		if ( g_Hlae.autoExportCamio )
		{
			if ( g_Hlae.camExport )
				ArtConsoleMessage(
					"art_hlae: replacing the active CAM export with this take's automatic export.\n" );
			g_Hlae.autoStartedCamio = StartCamExport( "camera.cam" );
		}
		if ( g_Hlae.autoExportBvh )
		{
			if ( g_Hlae.bvhExport )
				ArtConsoleMessage(
					"art_hlae: replacing the active BVH export with this take's automatic export.\n" );
			g_Hlae.autoStartedBvh =
				StartBvhExport( "camera.bvh", g_Hlae.autoExportBvhFps );
		}
		if ( g_Hlae.autoExportAgr )
		{
			if ( g_Hlae.agrFile )
				ArtConsoleMessage(
					"art_hlae: replacing the active AGR export with this take's automatic export.\n" );
			g_Hlae.autoAgrRecordingModeWasEnabled = g_Hlae.agrEnabled;
			g_Hlae.autoStartedAgr = StartAgrExport( "afxGameRecord.agr" );
		}
	}

	void EndArtHlaeTakeExports()
	{
		const bool stopCamio = g_Hlae.autoStartedCamio;
		const bool stopBvh = g_Hlae.autoStartedBvh;
		const bool stopAgr = g_Hlae.autoStartedAgr;
		const bool restoreAgrRecordingMode =
			stopAgr && !g_Hlae.autoAgrRecordingModeWasEnabled;
		if ( stopCamio ) StopCamExport();
		if ( stopBvh ) StopBvhExport();
		if ( stopAgr ) StopAgr();
		if ( restoreAgrRecordingMode )
			AgrEnableClientRecording( false );
		g_Hlae.autoStartedAgr = false;
		g_Hlae.autoStartedCamio = false;
		g_Hlae.autoStartedBvh = false;
	}

	void ShutdownArtHlae()
	{
		StopAllWritersAndOverrides();
		RemoveHlaeToolHooks();
		if ( g_Hlae.campathChangedCallbackRegistered )
		{
			g_HlaeCampath.OnChangedRemove( CampathChanged, NULL );
			g_Hlae.campathChangedCallbackRegistered = false;
		}
		g_Hlae.campathTrajectory.clear();
		g_Hlae.campathTrajectoryDirty = true;
		delete g_pMirvInput;
		g_pMirvInput = NULL;
		g_pHlaeEngineTool = NULL;
	}

	void GetArtHlaeStatus( ArtHlaeStatus &status )
	{
		memset( &status, 0, sizeof( status ) );
		status.enabled = g_Hlae.enabled;
		status.v34HookReady = g_HlaeToolHooksInstalled;
		status.campathEnabled = g_HlaeCampath.Enabled_get();
		status.campathHold = g_HlaeCampath.GetHold();
		status.campathDraw = g_Hlae.campathDraw;
		status.campathDrawKeyAxis = g_Hlae.campathDrawKeyAxis;
		status.campathDrawKeyCam = g_Hlae.campathDrawKeyCam;
		status.campathDrawKeyIndex = g_Hlae.campathDrawKeyIndex;
		status.campathKeyframes = g_HlaeCampath.GetSize();
		status.campathCanEval = g_HlaeCampath.CanEval();
		status.campathPositionInterp = static_cast<int>(
			g_HlaeCampath.PositionInterpMethod_get() );
		status.campathRotationInterp = static_cast<int>(
			g_HlaeCampath.RotationInterpMethod_get() );
		status.campathFovInterp = static_cast<int>(
			g_HlaeCampath.FovInterpMethod_get() );
		status.campathOffset = g_HlaeCampath.GetOffset();
		status.campathCurrentTime = CurrentTime() - status.campathOffset;
		if ( status.campathKeyframes )
		{
			status.campathLowerBound = g_HlaeCampath.GetLowerBound();
			status.campathUpperBound = g_HlaeCampath.GetUpperBound();
		}
		if ( g_pMirvInput )
			g_Hlae.inputCamera = g_pMirvInput->GetCameraControlMode();
		status.inputCamera = g_Hlae.inputCamera;
		status.inputHasCameraData = g_Hlae.hasLastCamera || g_Hlae.hasGameCamera;
		const CameraSample &inputCamera = g_Hlae.hasLastCamera ?
			g_Hlae.lastCamera : g_Hlae.gameCamera;
		status.inputCameraX = inputCamera.x;
		status.inputCameraY = inputCamera.y;
		status.inputCameraZ = inputCamera.z;
		status.inputCameraPitch = inputCamera.pitch;
		status.inputCameraYaw = inputCamera.yaw;
		status.inputCameraRoll = inputCamera.roll;
		status.inputCameraFov = inputCamera.fov;
		status.inputMouseSensitivity = g_Hlae.inputMouseSensitivity;
		status.inputKeyboardSensitivity = g_Hlae.inputKeyboardSensitivity;
		status.inputMouseMoveSupport = g_Hlae.inputMouseMoveSupport;
		status.inputOffsetMode = g_Hlae.inputOffsetMode;
		status.inputStepFactor = g_Hlae.inputStepFactor;
		status.inputRotLocalSpace = g_Hlae.inputRotLocalSpace;
		status.inputSmooth = g_Hlae.inputSmooth;
		status.inputSmoothRotShortestPath = g_Hlae.inputSmoothRotShortestPath;
		status.inputSmoothHalfTime = g_Hlae.inputSmoothHalfTime;
		status.inputSmoothHalfTimeVec = g_Hlae.inputSmoothHalfTimeVec;
		status.inputSmoothHalfTimeAng = g_Hlae.inputSmoothHalfTimeAng;
		status.inputSmoothHalfTimeFov = g_Hlae.inputSmoothHalfTimeFov;
		status.inputKeyboardForwardSpeed = g_Hlae.inputKeyboardForwardSpeed;
		status.inputKeyboardBackwardSpeed = g_Hlae.inputKeyboardBackwardSpeed;
		status.inputKeyboardLeftSpeed = g_Hlae.inputKeyboardLeftSpeed;
		status.inputKeyboardRightSpeed = g_Hlae.inputKeyboardRightSpeed;
		status.inputKeyboardUpSpeed = g_Hlae.inputKeyboardUpSpeed;
		status.inputKeyboardDownSpeed = g_Hlae.inputKeyboardDownSpeed;
		status.inputKeyboardPitchPositiveSpeed = g_Hlae.inputKeyboardPitchPositiveSpeed;
		status.inputKeyboardPitchNegativeSpeed = g_Hlae.inputKeyboardPitchNegativeSpeed;
		status.inputKeyboardYawPositiveSpeed = g_Hlae.inputKeyboardYawPositiveSpeed;
		status.inputKeyboardYawNegativeSpeed = g_Hlae.inputKeyboardYawNegativeSpeed;
		status.inputKeyboardRollPositiveSpeed = g_Hlae.inputKeyboardRollPositiveSpeed;
		status.inputKeyboardRollNegativeSpeed = g_Hlae.inputKeyboardRollNegativeSpeed;
		status.inputKeyboardFovPositiveSpeed = g_Hlae.inputKeyboardFovPositiveSpeed;
		status.inputKeyboardFovNegativeSpeed = g_Hlae.inputKeyboardFovNegativeSpeed;
		status.inputMouseYawSpeed = g_Hlae.inputMouseYawSpeed;
		status.inputMousePitchSpeed = g_Hlae.inputMousePitchSpeed;
		status.inputMouseFovPositiveSpeed = g_Hlae.inputMouseFovPositiveSpeed;
		status.inputMouseFovNegativeSpeed = g_Hlae.inputMouseFovNegativeSpeed;
		status.inputMouseForwardSpeed = g_Hlae.inputMouseForwardSpeed;
		status.inputMouseBackwardSpeed = g_Hlae.inputMouseBackwardSpeed;
		status.inputMouseLeftSpeed = g_Hlae.inputMouseLeftSpeed;
		status.inputMouseRightSpeed = g_Hlae.inputMouseRightSpeed;
		status.inputMouseUpSpeed = g_Hlae.inputMouseUpSpeed;
		status.inputMouseDownSpeed = g_Hlae.inputMouseDownSpeed;
		status.camioExporting = g_Hlae.camExport != NULL;
		status.camioImporting = !g_Hlae.camImport.empty();
		status.agrRecording = g_Hlae.agrFile != NULL;
		status.agrEnabled = g_Hlae.agrEnabled;
		status.agrRecordCamera = g_Hlae.agrRecordCamera;
		status.agrRecordPlayers = g_Hlae.agrRecordPlayers;
		status.agrRecordPlayerCameras = g_Hlae.agrRecordPlayerCameras;
		status.agrRecordWeapons = g_Hlae.agrRecordWeapons;
		status.agrRecordProjectiles = g_Hlae.agrRecordProjectiles;
		status.agrRecordViewModels = g_Hlae.agrRecordViewModels;
		status.agrRecordInvisible = g_Hlae.agrRecordInvisible;
		status.agrDebug = g_Hlae.agrDebug;
		status.camexportRecording = g_Hlae.bvhExport != NULL;
		status.camimportActive = !g_Hlae.bvhImport.empty();
		status.fovOverride = g_Hlae.fovOverride;
		status.fov = g_Hlae.fov;
		status.fovHandleZoom = g_Hlae.fovHandleZoom;
		status.fovMinUnzoomed = g_Hlae.fovMinUnzoomed;
		status.autoExportAgr = g_Hlae.autoExportAgr;
		status.autoExportCamio = g_Hlae.autoExportCamio;
		status.autoExportBvh = g_Hlae.autoExportBvh;
		status.autoExportBvhFps = g_Hlae.autoExportBvhFps;
		Q_strncpy( status.camioExportPath, g_Hlae.camExportPath,
			sizeof( status.camioExportPath ) );
		Q_strncpy( status.camioImportPath, g_Hlae.camImportPath,
			sizeof( status.camioImportPath ) );
		Q_strncpy( status.agrPath, g_Hlae.agrPath, sizeof( status.agrPath ) );
		Q_strncpy( status.camexportPath, g_Hlae.bvhExportPath,
			sizeof( status.camexportPath ) );
		Q_strncpy( status.camimportPath, g_Hlae.bvhImportPath,
			sizeof( status.camimportPath ) );
	}

	bool IsArtHlaeCampathDrawingEnabled()
	{
		return g_Hlae.enabled && g_Hlae.campathDraw &&
			0 < g_HlaeCampath.GetSize();
	}

	size_t GetArtHlaeCampathDrawPoints( ArtHlaeCampathDrawPoint *pPoints,
		size_t pointCapacity, double &currentPathTime )
	{
		currentPathTime = CurrentTime() - g_HlaeCampath.GetOffset();
		const size_t count = g_HlaeCampath.GetSize();
		if ( !pPoints || !pointCapacity )
			return count;
		const size_t copyCount = count < pointCapacity ? count : pointCapacity;
		size_t i = 0;
		for ( CamPathIterator it = g_HlaeCampath.GetBegin();
			it != g_HlaeCampath.GetEnd() && i < copyCount; ++it, ++i )
		{
			pPoints[i] = MakeCampathDrawPoint( it.GetTime(), it.GetValue() );
		}
		return count;
	}

	size_t GetArtHlaeCampathTrajectoryPoints( ArtHlaeCampathDrawPoint *pPoints,
		size_t pointCapacity, double &currentPathTime )
	{
		currentPathTime = CurrentTime() - g_HlaeCampath.GetOffset();
		RebuildCampathTrajectory();
		const size_t count = g_Hlae.campathTrajectory.size();
		if ( !pPoints || !pointCapacity )
			return count;
		const size_t copyCount = count < pointCapacity ? count : pointCapacity;
		for ( size_t i = 0; i < copyCount; ++i )
			pPoints[i] = g_Hlae.campathTrajectory[i];
		return count;
	}

	bool GetArtHlaeCampathCurrentCamera( ArtHlaeCampathDrawPoint &point,
		bool &campathEnabled )
	{
		campathEnabled = g_HlaeCampath.Enabled_get();
		if ( !g_HlaeCampath.CanEval() )
			return false;
		const double currentPathTime =
			CurrentTime() - g_HlaeCampath.GetOffset();
		if ( currentPathTime < g_HlaeCampath.GetLowerBound() ||
			g_HlaeCampath.GetUpperBound() < currentPathTime )
			return false;
		point = MakeCampathDrawPoint( currentPathTime,
			g_HlaeCampath.Eval( currentPathTime ) );
		return true;
	}

	bool IsArtHlaeEnabled()
	{
		return g_Hlae.enabled;
	}

	bool CaptureArtHlaeCursorPosition( LONG &x, LONG &y )
	{
		if ( !g_Hlae.enabled || !g_pMirvInput )
			return false;

		// While the ART GUI is visible, passthrough uses WM_INPUT relative deltas
		// exclusively. Feeding absolute GetCursorPos samples at the same time uses
		// MirvInput's stale pre-GUI cursor anchor and can produce extreme rotation.
		if ( IsArtGuiVisible() )
			return false;

		POINT point = { x, y };
		g_pMirvInput->Supply_GetCursorPos( &point );
		const bool captured = point.x != x || point.y != y;
		x = point.x;
		y = point.y;
		return captured;
	}

	void NotifyArtHlaeCursorWarp( LONG x, LONG y )
	{
		if ( g_Hlae.enabled && g_pMirvInput )
		{
			g_pMirvInput->Supply_SetCursorPos( x, y );
			return;
		}
		g_Hlae.inputCursorAnchorX = x;
		g_Hlae.inputCursorAnchorY = y;
		g_Hlae.inputCursorSampleX = x;
		g_Hlae.inputCursorSampleY = y;
		g_Hlae.inputCursorAnchorValid = true;
	}

	void SupplyArtHlaeRawMouseDelta( LONG x, LONG y )
	{
		if ( g_Hlae.enabled && g_pMirvInput )
		{
			RAWINPUT rawInput;
			memset( &rawInput, 0, sizeof( rawInput ) );
			rawInput.header.dwType = RIM_TYPEMOUSE;
			rawInput.data.mouse.usFlags = MOUSE_MOVE_RELATIVE;
			rawInput.data.mouse.lLastX = x;
			rawInput.data.mouse.lLastY = y;
			UINT bytes = sizeof( rawInput );
			g_pMirvInput->Supply_RawInputData( sizeof( rawInput ),
				reinterpret_cast<HRAWINPUT>( 1 ),
				RID_INPUT, &rawInput, &bytes, sizeof( RAWINPUTHEADER ) );
			return;
		}
		if ( x )
			InterlockedExchangeAdd( &g_Hlae.inputMouseDeltaX, x );
		if ( y )
			InterlockedExchangeAdd( &g_Hlae.inputMouseDeltaY, y );
		InterlockedExchange( &g_Hlae.inputRawMouseTick, static_cast<LONG>( GetTickCount() ) );
	}

	bool SupplyArtHlaeRawInput( RAWINPUT &rawInput )
	{
		if ( !g_Hlae.enabled || !g_pMirvInput )
			return false;
		UINT bytes = sizeof( rawInput );
		g_pMirvInput->Supply_RawInputData( sizeof( rawInput ),
			reinterpret_cast<HRAWINPUT>( 1 ), RID_INPUT, &rawInput,
			&bytes, sizeof( RAWINPUTHEADER ) );
		return g_pMirvInput->GetCameraControlMode();
	}

	bool SupplyArtHlaeKeyEvent( bool down, WPARAM wParam, LPARAM lParam )
	{
		return g_Hlae.enabled && g_pMirvInput &&
			g_pMirvInput->Supply_KeyEvent(
				down ? MirvInput::KS_DOWN : MirvInput::KS_UP, wParam, lParam );
	}

	bool SupplyArtHlaeCharEvent( WPARAM wParam, LPARAM lParam )
	{
		return g_Hlae.enabled && g_pMirvInput &&
			g_pMirvInput->Supply_CharEvent( wParam, lParam );
	}

	bool SupplyArtHlaeMouseEvent( UINT message, WPARAM &wParam, LPARAM &lParam )
	{
		return g_Hlae.enabled && g_pMirvInput &&
			g_pMirvInput->Supply_MouseEvent( message, wParam, lParam );
	}

	void SupplyArtHlaeFocus( bool focused )
	{
		if ( g_pMirvInput )
			g_pMirvInput->Supply_Focus( focused );
	}

	bool IsArtHlaeInputActive()
	{
		return g_Hlae.enabled && g_pMirvInput &&
			g_pMirvInput->GetCameraControlMode();
	}

	void PrintArtHlaeHelp()
	{
		ArtConsoleMessage(
			"AdvancedFX -afxV34 commands (original command names preserved):\n"
			"  mirv_campath   camera path keyframes, save/load and playback\n"
			"  mirv_input     free-camera input and smoothing\n"
			"  mirv_camio     AdvancedFX CAM import/export\n"
			"  mirv_agr       AdvancedFX GameRecord output\n"
			"  mirv_camexport BVH camera export\n"
			"  mirv_camimport BVH camera import / conversion to campath\n"
			"  mirv_fov       HLAE camera FOV override\n"
			"Use each command without arguments for its original-style usage.\n"
			"Use art_hlae autoExport agr|camio|bvh 0|1 for per-take exports.\n"
			"Absolute paths are used directly. Relative files use the active ART take, "
			"or the configured ART output folder while idle.\n" );
	}
}
