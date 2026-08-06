// Console command registration, parsing, status output, and in-game help.

#include "art_internal.h"
#include "art_gui.h"
#include "art_hlae.h"

// memdbgon must be the last include file in a .cpp file.
#include "tier0/memdbgon.h"

namespace art
{
	namespace
	{
		class ArtConVarAccessor : public IConCommandBaseAccessor
		{
		public:
			virtual bool RegisterConCommandBase( ConCommandBase *pCommand )
			{
				LogMessage( "COMMAND REGISTER: name='%s' ptr=%p",
					pCommand ? pCommand->GetName() : "<null>", pCommand );
				pCommand->AddFlags( FCVAR_PLUGIN );
				pCommand->SetNext( NULL );
				g_pCvar->RegisterConCommandBase( pCommand );
				LogMessage( "COMMAND REGISTERED: name='%s' FCVAR_PLUGIN added", pCommand->GetName() );
				return true;
			}
		};

		ArtConVarAccessor g_ConVarAccessor;

		const char *RecordStateName( LONG mask, LONG bit )
		{
			return ( mask & bit ) ? "on" : "off";
		}

		void PrintPassMask( const char *pCommandName, LONG mask )
		{
			ArtConsoleMessage(
				"%s: normal %s; clear %s; clear-noplayers %s; viewmodel %s; "
				"depth %s; players %s; objectid %s.\n",
				pCommandName,
				RecordStateName( mask, ART_RECORD_NORMAL ),
				RecordStateName( mask, ART_RECORD_CLEAR ),
				RecordStateName( mask, ART_RECORD_CLEAR_NOPLAYERS ),
				RecordStateName( mask, ART_RECORD_VIEWMODEL ),
				RecordStateName( mask, ART_RECORD_DEPTH ),
				RecordStateName( mask, ART_RECORD_PLAYERS ),
				RecordStateName( mask, ART_RECORD_OBJECTID ) );
		}

		void PrintRecordMask( LONG mask )
		{
			PrintPassMask( "art_record", mask );
		}

		void PrintRecordPath()
		{
			char configuredPath[MAX_PATH];
			FormatConfiguredRecordPath( configuredPath, sizeof( configuredPath ) );
			ArtConsoleMessage( "art_record path: %s%s.\n", configuredPath,
				( !g_bRecordBaseAbsolute && !Q_stricmp( g_szRecordBase, "art" ) ) ? " (default)" : "" );
		}

		void PrintCapturePrefix()
		{
			if ( g_szCapturePrefix[0] )
			{
				ArtConsoleMessage( "art_prefix: %s (files: %s_<pass>_0000.tga; automatic takes: %s_take0000).\n",
					g_szCapturePrefix, g_szCapturePrefix, g_szCapturePrefix );
			}
			else
			{
				ArtConsoleMessage( "art_prefix: default (files: <pass>_0000.tga; automatic takes: take0000).\n" );
			}
		}

		void PrintHudMask( LONG mask )
		{
			PrintPassMask( "art_hud", mask );
		}

		void ArtStart_f()
		{
			const int argc = g_pEngine ? g_pEngine->Cmd_Argc() : 0;
			const char *pLoggedName = argc > 1 ? g_pEngine->Cmd_Argv( 1 ) : NULL;
			const LONG recordMask = InterlockedCompareExchange( &g_nRecordMask, 0, 0 );
			const LONG hudMask = InterlockedCompareExchange( &g_nHudMask, 0, 0 );
			LogMessage( "COMMAND EXECUTE: art_start argc=%d requested_name='%s' recording=%ld record_mask=0x%lX hud_mask=0x%lX prefix='%s'",
				argc, pLoggedName && pLoggedName[0] ? pLoggedName : "<automatic>",
				InterlockedCompareExchange( &g_bRecording, FALSE, FALSE ), recordMask, hudMask,
				g_szCapturePrefix[0] ? g_szCapturePrefix : "<default>" );
			if ( InterlockedCompareExchange( &g_bRecording, FALSE, FALSE ) )
			{
				char displayPath[MAX_PATH];
				FormatTakeDisplayPath( displayPath, sizeof( displayPath ) );
				LogMessage( "COMMAND RESULT: art_start ignored; already recording root='%s' frame=%d", g_szTakeRoot, g_nFrame );
				ArtConsoleMessage( "art: already recording to %s (frame %d).\n", displayPath, g_nFrame );
				return;
			}
			PublishArtValidationCompletion();
			if ( IsArtValidationRunning() )
			{
				ArtConsoleMessage( "art_start: wait for validation to finish (%ld/%ld files, %.1f%%).\n",
					InterlockedCompareExchange( &g_ArtValidationProgress.completedFiles, 0, 0 ),
					InterlockedCompareExchange( &g_ArtValidationProgress.totalFiles, 0, 0 ),
					GetArtValidationProgressFraction() * 100.0f );
				return;
			}

			LogMessage( "DEPTH MODE: forward fog depth start=%g end=%g near='0 0 0' far='255 255 255' sky='255 255 255'",
				art_depth_start.GetFloat(), art_depth_end.GetFloat() );
			const char *pName = argc > 1 ? g_pEngine->Cmd_Argv( 1 ) : NULL;
			if ( !MakeTakeDirectories( pName ) )
			{
				LogMessage( "COMMAND RESULT: art_start failed while creating take directories" );
				FlushLog();
				return;
			}

			g_nFrame = 0;
			BeginArtRecordingStatistics( recordMask, hudMask );
			BeginArtHlaeTakeExports();
			InterlockedExchange( &g_bRecording, TRUE );
			char displayPath[MAX_PATH];
			FormatTakeDisplayPath( displayPath, sizeof( displayPath ) );
			LogMessage( "RECORDING STATE: started root='%s' display='%s' path_id='%s' prefix='%s' frame=0 record_mask=0x%lX hud_mask=0x%lX viewmodel_background='%d %d %d' players_background='%d %d %d' viewmodel_fov=%g depth_start=%g depth_end=%g",
				g_szTakeRoot, displayPath, RecordPathId() ? RecordPathId() : "<absolute>",
				g_szCapturePrefix[0] ? g_szCapturePrefix : "<default>", recordMask, hudMask,
				g_nViewmodelBackgroundRed, g_nViewmodelBackgroundGreen, g_nViewmodelBackgroundBlue,
				g_nPlayersBackgroundRed, g_nPlayersBackgroundGreen, g_nPlayersBackgroundBlue,
				g_flViewmodelFov,
				art_depth_start.GetFloat(), art_depth_end.GetFloat() );
			ArtConsoleMessage( "art: recording started: %s\n", displayPath );
			PrintRecordMask( recordMask );
			PrintRecordPath();
			PrintCapturePrefix();
			PrintHudMask( hudMask );
			if ( recordMask == 0 )
				ArtConsoleMessage( "art: warning: every recording pass is off; no images will be written until a pass is enabled.\n" );
			ArtConsoleMessage( "art: Viewmodel background color is %d %d %d; Players background color is %d %d %d.\n",
				g_nViewmodelBackgroundRed, g_nViewmodelBackgroundGreen, g_nViewmodelBackgroundBlue,
				g_nPlayersBackgroundRed, g_nPlayersBackgroundGreen, g_nPlayersBackgroundBlue );
			ArtConsoleMessage( "art: global viewmodel FOV is %g.\n", g_flViewmodelFov );
			ArtConsoleMessage( "art: depth fades from black nearby to white at distance (start=%g, end=%g; sky white).\n",
				art_depth_start.GetFloat(), art_depth_end.GetFloat() );
			FlushLog();
		}

		void ArtStop_f()
		{
			LogMessage( "COMMAND EXECUTE: art_stop recording=%ld frame=%d root='%s'",
				InterlockedCompareExchange( &g_bRecording, FALSE, FALSE ), g_nFrame, g_szTakeRoot );
			if ( !InterlockedExchange( &g_bRecording, FALSE ) )
			{
				LogMessage( "COMMAND RESULT: art_stop ignored; recorder was idle" );
				ArtConsoleMessage( "art: not recording.\n" );
				return;
			}

			const bool demoPauseRequested = PauseArtDemoAfterRecordingIfEnabled();
			EndArtHlaeTakeExports();
			FlushArtWriteQueue( "art_stop", true );
			FinishArtRecordingStatistics( false );
			char displayPath[MAX_PATH];
			FormatTakeDisplayPath( displayPath, sizeof( displayPath ) );
			ArtConsoleMessage( "art: stopped after %d frames. Output: %s\n", g_nFrame, displayPath );
			if ( demoPauseRequested )
				ArtConsoleMessage( "art: demo pause requested after recording stop.\n" );
			LogMessage( "COMMAND RESULT: art_stop complete frames=%d root='%s' display='%s'", g_nFrame, g_szTakeRoot, displayPath );
			FlushLog();
			RunAutomaticArtValidation();
		}

		void ArtToggle_f()
		{
			if ( InterlockedCompareExchange( &g_bRecording, FALSE, FALSE ) )
				ArtStop_f();
			else
				ArtStart_f();
		}

		void ArtStatus_f()
		{
			const LONG preview = InterlockedCompareExchange( &g_nPreviewPass, ART_PREVIEW_NONE, ART_PREVIEW_NONE );
			const LONG recordMask = InterlockedCompareExchange( &g_nRecordMask, 0, 0 );
			const LONG hudMask = InterlockedCompareExchange( &g_nHudMask, 0, 0 );
			char displayPath[MAX_PATH];
			FormatTakeDisplayPath( displayPath, sizeof( displayPath ) );
			LogMessage( "COMMAND EXECUTE: art_status recording=%ld frame=%d root='%s' preview='%s' record_mask=0x%lX hud_mask=0x%lX prefix='%s' viewmodel_background='%d %d %d' players_background='%d %d %d' viewmodel_fov=%g depth_start=%g depth_end=%g",
				InterlockedCompareExchange( &g_bRecording, FALSE, FALSE ), g_nFrame, g_szTakeRoot,
				PreviewName( preview ), recordMask, hudMask,
				g_szCapturePrefix[0] ? g_szCapturePrefix : "<default>",
				g_nViewmodelBackgroundRed, g_nViewmodelBackgroundGreen, g_nViewmodelBackgroundBlue,
				g_nPlayersBackgroundRed, g_nPlayersBackgroundGreen, g_nPlayersBackgroundBlue,
				g_flViewmodelFov,
				art_depth_start.GetFloat(), art_depth_end.GetFloat() );
			if ( InterlockedCompareExchange( &g_bRecording, FALSE, FALSE ) )
			{
				ArtConsoleMessage( "art: recording frame %d to %s; preview %s; Viewmodel RGB %d %d %d; Players RGB %d %d %d; viewmodel FOV %g; fog depth start %g end %g.\n",
					g_nFrame, displayPath, PreviewName( preview ),
					g_nViewmodelBackgroundRed, g_nViewmodelBackgroundGreen, g_nViewmodelBackgroundBlue,
					g_nPlayersBackgroundRed, g_nPlayersBackgroundGreen, g_nPlayersBackgroundBlue,
					g_flViewmodelFov, art_depth_start.GetFloat(), art_depth_end.GetFloat() );
			}
			else
			{
				ArtConsoleMessage( "art: idle; preview %s; standalone hook is loaded; Viewmodel RGB %d %d %d; Players RGB %d %d %d; viewmodel FOV %g; fog depth start %g end %g.\n",
					PreviewName( preview ),
					g_nViewmodelBackgroundRed, g_nViewmodelBackgroundGreen, g_nViewmodelBackgroundBlue,
					g_nPlayersBackgroundRed, g_nPlayersBackgroundGreen, g_nPlayersBackgroundBlue,
					g_flViewmodelFov, art_depth_start.GetFloat(), art_depth_end.GetFloat() );
			}
			PrintRecordMask( recordMask );
			PrintRecordPath();
			PrintCapturePrefix();
			PrintHudMask( hudMask );
		}

		void ArtBackgroundColor_f( const char *pCommandName, const char *pLabel,
			int &targetRed, int &targetGreen, int &targetBlue )
		{
			const int argc = g_pEngine ? g_pEngine->Cmd_Argc() : 0;
			LogMessage( "COMMAND EXECUTE: %s argc=%d current='%d %d %d'",
				pCommandName, argc, targetRed, targetGreen, targetBlue );
			if ( argc == 1 )
			{
				ArtConsoleMessage( "%s is %d %d %d. Usage: %s <red> <green> <blue>.\n",
					pCommandName, targetRed, targetGreen, targetBlue, pCommandName );
				return;
			}

			char requested[128];
			if ( argc == 4 )
				Q_snprintf( requested, sizeof( requested ), "%s %s %s", g_pEngine->Cmd_Argv( 1 ),
					g_pEngine->Cmd_Argv( 2 ), g_pEngine->Cmd_Argv( 3 ) );
			else if ( argc == 2 )
				Q_strncpy( requested, g_pEngine->Cmd_Argv( 1 ), sizeof( requested ) );
			else
				requested[0] = '\0';

			int red = 0;
			int green = 0;
			int blue = 0;
			if ( !ParseChromaColor( requested, red, green, blue ) )
			{
				LogMessage( "COMMAND RESULT: %s rejected argc=%d value='%s'", pCommandName, argc,
					requested[0] ? requested : "<invalid argument count>" );
				ArtConsoleMessage( "%s: expected three integers from 0 to 255. Example: %s 0 255 0.\n",
					pCommandName, pCommandName );
				return;
			}

			targetRed = red;
			targetGreen = green;
			targetBlue = blue;
			LogMessage( "COMMAND RESULT: %s set rgb='%d %d %d' recording=%ld frame=%d",
				pCommandName, red, green, blue,
				InterlockedCompareExchange( &g_bRecording, FALSE, FALSE ), g_nFrame );
			ArtConsoleMessage( "art: %s background changed to %d %d %d.\n", pLabel, red, green, blue );
		}

		void ArtViewmodelColor_f()
		{
			ArtBackgroundColor_f( "art_viewmodel_color", "Viewmodel",
				g_nViewmodelBackgroundRed, g_nViewmodelBackgroundGreen, g_nViewmodelBackgroundBlue );
		}

		void ArtPlayersColor_f()
		{
			ArtBackgroundColor_f( "art_players_color", "Players",
				g_nPlayersBackgroundRed, g_nPlayersBackgroundGreen, g_nPlayersBackgroundBlue );
		}

		void ArtViewmodelFov_f()
		{
			const int argc = g_pEngine ? g_pEngine->Cmd_Argc() : 0;
			const char *pRequested = argc > 1 ? g_pEngine->Cmd_Argv( 1 ) : NULL;
			LogMessage( "COMMAND EXECUTE: art_viewmodel_fov argc=%d requested='%s' current=%g recording=%ld frame=%d",
				argc, pRequested && pRequested[0] ? pRequested : "<query>", g_flViewmodelFov,
				InterlockedCompareExchange( &g_bRecording, FALSE, FALSE ), g_nFrame );

			if ( argc == 1 )
			{
				ArtConsoleMessage( "art_viewmodel_fov is %g. Usage: art_viewmodel_fov <1-179|default>.\n",
					g_flViewmodelFov );
				return;
			}
			if ( argc != 2 )
			{
				ArtConsoleMessage( "art_viewmodel_fov: expected one value from 1 to 179, or default.\n" );
				return;
			}

			float requestedFov = kDefaultViewmodelFov;
			if ( Q_stricmp( pRequested, "default" ) && !ParseViewmodelFov( pRequested, requestedFov ) )
			{
				ArtConsoleMessage( "art_viewmodel_fov: '%s' is invalid; expected a value from 1 to 179, or default.\n",
					pRequested );
				return;
			}

			const float previousFov = g_flViewmodelFov;
			g_flViewmodelFov = requestedFov;
			LogMessage( "COMMAND RESULT: art_viewmodel_fov changed previous=%g next=%g recording=%ld frame=%d",
				previousFov, g_flViewmodelFov,
				InterlockedCompareExchange( &g_bRecording, FALSE, FALSE ), g_nFrame );
			ArtConsoleMessage( "art: global viewmodel FOV changed to %g; it applies from the next rendered frame.\n",
				g_flViewmodelFov );
		}

		void ArtPrefix_f()
		{
			const int argc = g_pEngine ? g_pEngine->Cmd_Argc() : 0;
			const char *pRequested = argc > 1 ? g_pEngine->Cmd_Argv( 1 ) : NULL;
			LogMessage( "COMMAND EXECUTE: art_prefix argc=%d requested='%s' current='%s' recording=%ld frame=%d",
				argc, pRequested && pRequested[0] ? pRequested : "<query>",
				g_szCapturePrefix[0] ? g_szCapturePrefix : "<default>",
				InterlockedCompareExchange( &g_bRecording, FALSE, FALSE ), g_nFrame );
			if ( argc == 1 )
			{
				PrintCapturePrefix();
				ArtConsoleMessage( "Usage: art_prefix <prefix|default>.\n" );
				return;
			}
			if ( argc != 2 )
			{
				ArtConsoleMessage( "art_prefix: expected one prefix containing letters, numbers, '_' or '-'.\n" );
				return;
			}
			if ( InterlockedCompareExchange( &g_bRecording, FALSE, FALSE ) )
			{
				ArtConsoleMessage( "art_prefix: stop recording before changing the prefix.\n" );
				return;
			}

			if ( !Q_stricmp( pRequested, "default" ) )
			{
				g_szCapturePrefix[0] = '\0';
				LogMessage( "COMMAND RESULT: art_prefix restored default naming" );
				ArtConsoleMessage( "art_prefix restored to default.\n" );
				PrintCapturePrefix();
				return;
			}
			if ( strlen( pRequested ) >= sizeof( g_szCapturePrefix ) || !IsSafeTakeName( pRequested ) )
			{
				ArtConsoleMessage( "art_prefix: use fewer than %d letters, numbers, '_' or '-' characters.\n",
					static_cast<int>( sizeof( g_szCapturePrefix ) ) );
				return;
			}

			Q_strncpy( g_szCapturePrefix, pRequested, sizeof( g_szCapturePrefix ) );
			LogMessage( "COMMAND RESULT: art_prefix changed prefix='%s' file_pattern='%s_<pass>_%%04d.tga' automatic_take_pattern='%s_take%%04d'",
				g_szCapturePrefix, g_szCapturePrefix, g_szCapturePrefix );
			ArtConsoleMessage( "art_prefix set to %s. The next automatic take will use this prefix.\n", g_szCapturePrefix );
			PrintCapturePrefix();
		}

		void ArtRecord_f()
		{
			const int argc = g_pEngine ? g_pEngine->Cmd_Argc() : 0;
			const LONG current = InterlockedCompareExchange( &g_nRecordMask, 0, 0 );
			const char *pPass = argc > 1 ? g_pEngine->Cmd_Argv( 1 ) : NULL;
			const char *pMode = argc > 2 ? g_pEngine->Cmd_Argv( 2 ) : NULL;
			LogMessage( "COMMAND EXECUTE: art_record argc=%d pass='%s' mode='%s' current_mask=0x%lX recording=%ld frame=%d",
				argc, pPass && pPass[0] ? pPass : "<all query>",
				pMode && pMode[0] ? pMode : "<query>", current,
				InterlockedCompareExchange( &g_bRecording, FALSE, FALSE ), g_nFrame );
			if ( argc == 1 )
			{
				PrintRecordMask( current );
				PrintRecordPath();
				ArtConsoleMessage( "Usage: art_record <normal|clear|clear-noplayers|viewmodel|depth|players|objectid|all> <on|off>, or art_record path <folder|default>.\n" );
				return;
			}

			if ( pPass && !Q_stricmp( pPass, "path" ) )
			{
				if ( argc == 2 )
				{
					PrintRecordPath();
					ArtConsoleMessage( "Usage: art_record path \"path_to_folder\". Use 'default' to restore cstrike/art.\n" );
					return;
				}
				if ( argc != 3 )
				{
					ArtConsoleMessage( "art_record path: expected one quoted folder path.\n" );
					return;
				}
				if ( InterlockedCompareExchange( &g_bRecording, FALSE, FALSE ) )
				{
					ArtConsoleMessage( "art_record path: stop recording before changing the output folder.\n" );
					return;
				}

				char error[160] = "";
				if ( !SetRecordBasePath( pMode, error, sizeof( error ) ) )
				{
					LogMessage( "COMMAND RESULT: art_record path rejected requested='%s' reason='%s'",
						pMode ? pMode : "<null>", error );
					ArtConsoleMessage( "art_record path: %s.\n", error );
					return;
				}

				char configuredPath[MAX_PATH];
				FormatConfiguredRecordPath( configuredPath, sizeof( configuredPath ) );
				LogMessage( "COMMAND RESULT: art_record path changed requested='%s' configured='%s' absolute=%d",
					pMode, configuredPath, g_bRecordBaseAbsolute ? 1 : 0 );
				ArtConsoleMessage( "art_record path set to %s. The next take will be written there.\n", configuredPath );
				return;
			}

			const LONG bit = RecordBitFromName( pPass );
			if ( !bit )
			{
				ArtConsoleMessage( "art_record: unknown pass '%s'; expected normal, clear, clear-noplayers, viewmodel, depth, players, objectid, or all.\n",
					pPass ? pPass : "" );
				return;
			}
			if ( argc == 2 )
			{
				if ( bit == ART_RECORD_ALL )
					PrintRecordMask( current );
				else
					ArtConsoleMessage( "art_record %s is %s.\n", pPass, RecordStateName( current, bit ) );
				return;
			}
			if ( argc != 3 )
			{
				ArtConsoleMessage( "art_record: usage: art_record <normal|clear|clear-noplayers|viewmodel|depth|players|objectid|all> <on|off>, or art_record path <folder|default>.\n" );
				return;
			}

			bool enable = false;
			if ( !Q_stricmp( pMode, "on" ) || !Q_stricmp( pMode, "1" ) || !Q_stricmp( pMode, "yes" ) )
				enable = true;
			else if ( Q_stricmp( pMode, "off" ) && Q_stricmp( pMode, "0" ) && Q_stricmp( pMode, "no" ) )
			{
				ArtConsoleMessage( "art_record: mode must be on or off.\n" );
				return;
			}

			LONG previous = 0;
			LONG next = 0;
			do
			{
				previous = InterlockedCompareExchange( &g_nRecordMask, 0, 0 );
				next = enable ? ( previous | bit ) : ( previous & ~bit );
			}
			while ( InterlockedCompareExchange( &g_nRecordMask, next, previous ) != previous );
			LogMessage( "COMMAND RESULT: art_record changed pass='%s' enabled=%d previous_mask=0x%lX next_mask=0x%lX recording=%ld frame=%d",
				pPass, enable ? 1 : 0, previous, next,
				InterlockedCompareExchange( &g_bRecording, FALSE, FALSE ), g_nFrame );
			ArtConsoleMessage( "art_record: %s %s.\n", pPass, enable ? "on" : "off" );
			PrintRecordMask( next );
			if ( next == 0 )
				ArtConsoleMessage( "art: warning: every recording pass is off; recording will advance without writing images.\n" );
			else if ( InterlockedCompareExchange( &g_bRecording, FALSE, FALSE ) )
				ArtConsoleMessage( "art: recording change applies from the next frame.\n" );
		}

		void ArtHud_f()
		{
			const int argc = g_pEngine ? g_pEngine->Cmd_Argc() : 0;
			const LONG current = InterlockedCompareExchange( &g_nHudMask, 0, 0 );
			const char *pPass = argc > 1 ? g_pEngine->Cmd_Argv( 1 ) : NULL;
			const char *pMode = argc > 2 ? g_pEngine->Cmd_Argv( 2 ) : NULL;
			if ( argc == 1 )
			{
				PrintHudMask( current );
				ArtConsoleMessage( "Usage: art_hud <normal|clear|clear-noplayers|viewmodel|depth|players|objectid|all> <on|off>.\n" );
				return;
			}

			const LONG bit = RecordBitFromName( pPass );
			if ( !bit )
			{
				ArtConsoleMessage( "art_hud: unknown pass '%s'; expected normal, clear, clear-noplayers, viewmodel, depth, players, objectid, or all.\n",
					pPass ? pPass : "" );
				return;
			}
			if ( argc == 2 )
			{
				if ( bit == ART_RECORD_ALL )
					PrintHudMask( current );
				else
					ArtConsoleMessage( "art_hud %s is %s.\n", pPass, RecordStateName( current, bit ) );
				return;
			}
			if ( argc != 3 )
			{
				ArtConsoleMessage( "art_hud: usage: art_hud <normal|clear|clear-noplayers|viewmodel|depth|players|objectid|all> <on|off>.\n" );
				return;
			}

			bool enable = false;
			if ( !Q_stricmp( pMode, "on" ) || !Q_stricmp( pMode, "1" ) || !Q_stricmp( pMode, "yes" ) )
				enable = true;
			else if ( Q_stricmp( pMode, "off" ) && Q_stricmp( pMode, "0" ) && Q_stricmp( pMode, "no" ) )
			{
				ArtConsoleMessage( "art_hud: mode must be on or off.\n" );
				return;
			}

			LONG previous = 0;
			LONG next = 0;
			do
			{
				previous = InterlockedCompareExchange( &g_nHudMask, 0, 0 );
				next = enable ? ( previous | bit ) : ( previous & ~bit );
			}
			while ( InterlockedCompareExchange( &g_nHudMask, next, previous ) != previous );
			LogMessage( "COMMAND RESULT: art_hud changed pass='%s' enabled=%d previous_mask=0x%lX next_mask=0x%lX recording=%ld frame=%d",
				pPass, enable ? 1 : 0, previous, next,
				InterlockedCompareExchange( &g_bRecording, FALSE, FALSE ), g_nFrame );
			ArtConsoleMessage( "art_hud: %s %s.\n", pPass, enable ? "on" : "off" );
			PrintHudMask( next );
			if ( InterlockedCompareExchange( &g_bRecording, FALSE, FALSE ) )
				ArtConsoleMessage( "art: HUD change applies from the next frame.\n" );
		}

		void ArtPreview_f()
		{
			const int argc = g_pEngine ? g_pEngine->Cmd_Argc() : 0;
			const LONG current = InterlockedCompareExchange( &g_nPreviewPass, ART_PREVIEW_NONE, ART_PREVIEW_NONE );
			const char *pRequested = argc > 1 ? g_pEngine->Cmd_Argv( 1 ) : NULL;
			if ( argc == 1 )
			{
				ArtConsoleMessage( "art_preview is %s. Usage: art_preview <normal|clear|clear-noplayers|viewmodel|depth|players|objectid|off>. Repeating the active pass toggles it off.\n",
					PreviewName( current ) );
				return;
			}
			if ( argc != 2 )
			{
				ArtConsoleMessage( "art_preview: expected normal, clear, clear-noplayers, viewmodel, depth, players, objectid, or off.\n" );
				return;
			}

			LONG requested = ART_PREVIEW_NONE;
			if ( !Q_stricmp( pRequested, "normal" ) ) requested = ART_PREVIEW_NORMAL; else if ( !Q_stricmp( pRequested, "clear" ) )
				requested = ART_PREVIEW_CLEAR;
			else if ( !Q_stricmp( pRequested, "clear-noplayers" ) || !Q_stricmp( pRequested, "clear_noplayers" ) )
				requested = ART_PREVIEW_CLEAR_NOPLAYERS;
			else if ( !Q_stricmp( pRequested, "viewmodel" ) )
				requested = ART_PREVIEW_VIEWMODEL;
			else if ( !Q_stricmp( pRequested, "depth" ) )
				requested = ART_PREVIEW_DEPTH;
			else if ( !Q_stricmp( pRequested, "players" ) || !Q_stricmp( pRequested, "player" ) )
				requested = ART_PREVIEW_PLAYERS;
			else if ( !Q_stricmp( pRequested, "objectid" ) || !Q_stricmp( pRequested, "object-id" ) || !Q_stricmp( pRequested, "object_id" ) )
				requested = ART_PREVIEW_OBJECTID;
			else if ( Q_stricmp( pRequested, "off" ) && Q_stricmp( pRequested, "none" ) && Q_stricmp( pRequested, "0" ) )
			{
				ArtConsoleMessage( "art_preview: unknown pass '%s'; expected normal, clear, clear-noplayers, viewmodel, depth, players, objectid, or off.\n", pRequested );
				return;
			}

			const bool toggledOff = requested != ART_PREVIEW_NONE && requested == current;
			const LONG next = toggledOff ? ART_PREVIEW_NONE : requested;
			InterlockedExchange( &g_nPreviewPass, next );
			g_nPreviewRenderCalls = 0;
			LogMessage( "COMMAND RESULT: art_preview changed previous='%s' requested='%s' next='%s' toggle_off=%d",
				PreviewName( current ), PreviewName( requested ), PreviewName( next ), toggledOff ? 1 : 0 );
			if ( next == ART_PREVIEW_NONE )
				ArtConsoleMessage( "art: preview off.\n" );
			else
				ArtConsoleMessage( "art: previewing %s pass. Repeat 'art_preview %s' to turn it off.\n",
					PreviewName( next ), PreviewName( next ) );
		}

		void ArtPreviewNext_f()
		{
			const LONG current = InterlockedCompareExchange(
				&g_nPreviewPass, ART_PREVIEW_NONE, ART_PREVIEW_NONE );
			LONG next = current + 1;
			if ( next < ART_PREVIEW_NORMAL || next > ART_PREVIEW_OBJECTID )
				next = ART_PREVIEW_NORMAL;
			InterlockedExchange( &g_nPreviewPass, next );
			g_nPreviewRenderCalls = 0;
			LogMessage( "COMMAND RESULT: art_preview_next changed previous='%s' next='%s'",
				PreviewName( current ), PreviewName( next ) );
			ArtConsoleMessage( "art: previewing %s pass.\n", PreviewName( next ) );
		}

		bool ParseOnOff( const char *pValue, bool &enabled )
		{
			if ( pValue && ( !Q_stricmp( pValue, "on" ) || !Q_stricmp( pValue, "1" ) ||
				!Q_stricmp( pValue, "true" ) || !Q_stricmp( pValue, "enabled" ) ) )
			{
				enabled = true;
				return true;
			}
			if ( pValue && ( !Q_stricmp( pValue, "off" ) || !Q_stricmp( pValue, "0" ) ||
				!Q_stricmp( pValue, "false" ) || !Q_stricmp( pValue, "disabled" ) ) )
			{
				enabled = false;
				return true;
			}
			return false;
		}

		void ArtTakeJson_f()
		{
			const int argc = g_pEngine ? g_pEngine->Cmd_Argc() : 0;
			const char *pAction = argc > 1 ? g_pEngine->Cmd_Argv( 1 ) : "status";
			if ( argc == 1 || ( argc == 2 && !Q_stricmp( pAction, "status" ) ) )
			{
				char manifestPath[MAX_PATH];
				FormatArtTakeManifestPath( manifestPath, sizeof( manifestPath ) );
				ArtConsoleMessage( "art_take_json is %s. Latest manifest: %s.\n",
					InterlockedCompareExchange( &g_bArtTakeManifestEnabled, 0, 0 ) ? "on" : "off",
					manifestPath[0] ? manifestPath : "<no take recorded this session>" );
				return;
			}
			if ( argc == 2 && !Q_stricmp( pAction, "write" ) )
			{
				if ( InterlockedCompareExchange( &g_bRecording, FALSE, FALSE ) )
				{
					ArtConsoleMessage( "art_take_json: stop recording before manually finalizing the manifest.\n" );
					return;
				}
				if ( WriteArtTakeManifest( true ) )
				{
					char manifestPath[MAX_PATH];
					FormatArtTakeManifestPath( manifestPath, sizeof( manifestPath ) );
					ArtConsoleMessage( "art_take_json: wrote %s.\n", manifestPath );
				}
				else
					ArtConsoleMessage( "art_take_json: no completed take is available, or the manifest could not be written.\n" );
				return;
			}
			bool enabled = false;
			if ( argc != 2 || !ParseOnOff( pAction, enabled ) )
			{
				ArtConsoleMessage( "Usage: art_take_json [on|off|status|write].\n" );
				return;
			}
			InterlockedExchange( &g_bArtTakeManifestEnabled, enabled ? TRUE : FALSE );
			ArtConsoleMessage( "art_take_json: %s. This setting is captured when the next take starts.\n",
				enabled ? "on" : "off" );
		}

		void PrintValidationOptions()
		{
			ArtConsoleMessage( "art_validation options: auto %s; file_size %s; dropped_frames %s; min_size %ld bytes.\n",
				InterlockedCompareExchange( &g_ArtValidationOptions.autoValidate, 0, 0 ) ? "on" : "off",
				InterlockedCompareExchange( &g_ArtValidationOptions.checkFileSize, 0, 0 ) ? "on" : "off",
				InterlockedCompareExchange( &g_ArtValidationOptions.checkDroppedFrames, 0, 0 ) ? "on" : "off",
				InterlockedCompareExchange( &g_ArtValidationOptions.minimumFileBytes, 0, 0 ) );
		}

		void ArtStats_f()
		{
			PrintArtRecordingStatistics();
		}

		void ArtQueue_f()
		{
			const int argc = g_pEngine ? g_pEngine->Cmd_Argc() : 0;
			const char *pAction = argc > 1 ? g_pEngine->Cmd_Argv( 1 ) : "status";
			if ( argc == 1 || ( argc == 2 && !Q_stricmp( pAction, "status" ) ) )
			{
				PrintArtQueueStatus();
				return;
			}
			if ( argc == 2 && !Q_stricmp( pAction, "flush" ) )
			{
				FlushArtWriteQueue( "art_queue flush", true );
				ArtConsoleMessage( "art_queue: pending writes finished.\n" );
				return;
			}
			if ( argc == 2 && !Q_stricmp( pAction, "default" ) )
			{
				InterlockedExchange( &g_ArtQueueOptions.maxFiles, ART_QUEUE_DEFAULT_MAX_FILES );
				InterlockedExchange( &g_ArtQueueOptions.maxMegabytes, ART_QUEUE_DEFAULT_MAX_MEGABYTES );
				InterlockedExchange( &g_ArtQueueOptions.reserveMegabytes, ART_QUEUE_DEFAULT_RESERVE_MEGABYTES );
				ArtConsoleMessage( "art_queue: safety limits restored to defaults.\n" );
				PrintArtQueueStatus();
				return;
			}

			volatile LONG *pTarget = NULL;
			unsigned long minimum = 0;
			unsigned long maximum = 0;
			if ( !Q_stricmp( pAction, "max_files" ) )
			{
				pTarget = &g_ArtQueueOptions.maxFiles;
				minimum = 1;
				maximum = 512;
			}
			else if ( !Q_stricmp( pAction, "max_mb" ) )
			{
				pTarget = &g_ArtQueueOptions.maxMegabytes;
				minimum = 16;
				maximum = 1024;
			}
			else if ( !Q_stricmp( pAction, "reserve_mb" ) )
			{
				pTarget = &g_ArtQueueOptions.reserveMegabytes;
				minimum = 64;
				maximum = 1024;
			}
			if ( pTarget )
			{
				if ( argc == 2 )
				{
					ArtConsoleMessage( "art_queue %s: %ld.\n", pAction,
						InterlockedCompareExchange( pTarget, 0, 0 ) );
					return;
				}
				char *pEnd = NULL;
				const unsigned long value = argc == 3 ?
					strtoul( g_pEngine->Cmd_Argv( 2 ), &pEnd, 10 ) : 0;
				if ( argc != 3 || !pEnd || *pEnd != '\0' || value < minimum || value > maximum )
				{
					ArtConsoleMessage( "Usage: art_queue %s <%lu-%lu>.\n", pAction, minimum, maximum );
					return;
				}
				InterlockedExchange( pTarget, static_cast<LONG>( value ) );
				ArtConsoleMessage( "art_queue %s: %lu.\n", pAction, value );
				return;
			}
			ArtConsoleMessage( "Usage: art_queue [status|flush|default|max_files <1-512>|max_mb <16-1024>|reserve_mb <64-1024>].\n" );
		}

		void ArtTgaCompression_f()
		{
			const int argc = g_pEngine ? g_pEngine->Cmd_Argc() : 0;
			if ( argc == 1 )
			{
				ArtConsoleMessage( "art_tga_compression: %s.\n",
					ArtTgaCompressionModeName( InterlockedCompareExchange(
						&g_nArtTgaCompressionMode, 0, 0 ) ) );
				return;
			}
			if ( argc != 2 )
			{
				ArtConsoleMessage( "Usage: art_tga_compression <off|auto|rle>.\n" );
				return;
			}
			const char *pMode = g_pEngine->Cmd_Argv( 1 );
			LONG mode = ART_TGA_COMPRESSION_AUTO;
			if ( !Q_stricmp( pMode, "off" ) || !Q_stricmp( pMode, "none" ) || !Q_stricmp( pMode, "0" ) )
				mode = ART_TGA_COMPRESSION_OFF;
			else if ( !Q_stricmp( pMode, "rle" ) || !Q_stricmp( pMode, "on" ) || !Q_stricmp( pMode, "1" ) )
				mode = ART_TGA_COMPRESSION_RLE;
			else if ( Q_stricmp( pMode, "auto" ) )
			{
				ArtConsoleMessage( "Usage: art_tga_compression <off|auto|rle>.\n" );
				return;
			}
			InterlockedExchange( &g_nArtTgaCompressionMode, mode );
			ArtConsoleMessage( "art_tga_compression: %s.\n", ArtTgaCompressionModeName( mode ) );
		}

		void ArtValidation_f()
		{
			const int argc = g_pEngine ? g_pEngine->Cmd_Argc() : 0;
			const char *pAction = argc > 1 ? g_pEngine->Cmd_Argv( 1 ) : "run";
			if ( argc == 1 || ( argc == 2 && !Q_stricmp( pAction, "run" ) ) )
			{
				RunArtValidation();
				return;
			}
			if ( argc == 2 && !Q_stricmp( pAction, "status" ) )
			{
				PrintArtValidationResult();
				PrintValidationOptions();
				return;
			}
			if ( !Q_stricmp( pAction, "auto" ) ||
				!Q_stricmp( pAction, "file_size" ) ||
				!Q_stricmp( pAction, "dropped_frames" ) )
			{
				volatile LONG *pTarget = !Q_stricmp( pAction, "auto" ) ?
					&g_ArtValidationOptions.autoValidate :
					!Q_stricmp( pAction, "file_size" ) ?
						&g_ArtValidationOptions.checkFileSize :
						&g_ArtValidationOptions.checkDroppedFrames;
				if ( argc == 2 )
				{
					ArtConsoleMessage( "art_validation %s is %s.\n", pAction,
						InterlockedCompareExchange( pTarget, 0, 0 ) ? "on" : "off" );
					return;
				}
				bool enabled = false;
				if ( argc != 3 || !ParseOnOff( g_pEngine->Cmd_Argv( 2 ), enabled ) )
				{
					ArtConsoleMessage( "Usage: art_validation %s <on|off>.\n", pAction );
					return;
				}
				InterlockedExchange( pTarget, enabled ? TRUE : FALSE );
				ArtConsoleMessage( "art_validation %s: %s.\n", pAction, enabled ? "on" : "off" );
				return;
			}
			if ( !Q_stricmp( pAction, "min_size" ) )
			{
				if ( argc == 2 )
				{
					ArtConsoleMessage( "art_validation min_size is %ld bytes.\n",
						InterlockedCompareExchange( &g_ArtValidationOptions.minimumFileBytes, 0, 0 ) );
					return;
				}
				char *pEnd = NULL;
				const unsigned long bytes = argc == 3 ? strtoul( g_pEngine->Cmd_Argv( 2 ), &pEnd, 10 ) : 0;
				if ( argc != 3 || !pEnd || *pEnd != '\0' || bytes < 18 || bytes > 1073741824UL )
				{
					ArtConsoleMessage( "Usage: art_validation min_size <18-1073741824 bytes>.\n" );
					return;
				}
				InterlockedExchange( &g_ArtValidationOptions.minimumFileBytes, static_cast<LONG>( bytes ) );
				ArtConsoleMessage( "art_validation min_size: %lu bytes.\n", bytes );
				return;
			}
			ArtConsoleMessage( "Usage: art_validation [run|status|auto <on|off>|file_size <on|off>|dropped_frames <on|off>|min_size <bytes>].\n" );
		}

		void ArtHelp_f()
		{
			LogMessage( "COMMAND EXECUTE: art_help" );
			PrintArtHelp();
			LogMessage( "COMMAND RESULT: art_help printed command reference" );
		}

		void PrintDebugSnapshot()
		{
			const LONG recording = InterlockedCompareExchange( &g_bRecording, FALSE, FALSE );
			const LONG rendering = InterlockedCompareExchange( &g_bRenderingArt, FALSE, FALSE );
			const LONG preview = InterlockedCompareExchange( &g_nPreviewPass, ART_PREVIEW_NONE, ART_PREVIEW_NONE );
			const LONG recordMask = InterlockedCompareExchange( &g_nRecordMask, 0, 0 );
			const LONG hudMask = InterlockedCompareExchange( &g_nHudMask, 0, 0 );
			char configuredPath[MAX_PATH];
			FormatConfiguredRecordPath( configuredPath, sizeof( configuredPath ) );
			char takeDisplayPath[MAX_PATH];
			FormatTakeDisplayPath( takeDisplayPath, sizeof( takeDisplayPath ) );
			const bool viewHookReady = IsViewRenderHookReady();
			const bool modelHookReady = IsModelRenderHookReady();
			const bool fovHookReady = IsClientModeFovHookReady();

			LogMessage( "DEBUG STATE: recording=%ld rendering_art=%ld frame=%d root='%s' display='%s' configured_path='%s' absolute=%d preview='%s' record_mask=0x%lX hud_mask=0x%lX",
				recording, rendering, g_nFrame, g_szTakeRoot, takeDisplayPath, configuredPath,
				g_bRecordBaseAbsolute ? 1 : 0, PreviewName( preview ), recordMask, hudMask );
			LogMessage( "DEBUG CONFIG: prefix='%s' viewmodel_background='%d %d %d' players_background='%d %d %d' take_json=%ld viewmodel_fov=%g depth_start=%g depth_end=%g render_hook_calls=%lu preview_render_calls=%lu",
				g_szCapturePrefix[0] ? g_szCapturePrefix : "<default>",
				g_nViewmodelBackgroundRed, g_nViewmodelBackgroundGreen, g_nViewmodelBackgroundBlue,
				g_nPlayersBackgroundRed, g_nPlayersBackgroundGreen, g_nPlayersBackgroundBlue,
				InterlockedCompareExchange( &g_bArtTakeManifestEnabled, 0, 0 ),
				g_flViewmodelFov, art_depth_start.GetFloat(),
				art_depth_end.GetFloat(), g_nRenderHookCalls, g_nPreviewRenderCalls );
			LogRenderHookState();
			LogMessage( "DEBUG MODULES: self=%p client=%p engine=%p interfaces client=%p entitylist=%p engineclient=%p cvar=%p renderview=%p modelrender=%p materials=%p filesystem=%p gameconsole=%p",
				g_hThisModule, g_hClientModule, g_hEngineModule, g_pClient, g_pEntityList, g_pEngine,
				g_pCvar, g_pRenderView, g_pModelRender, g_pMaterials, g_pFileSystem, g_pGameConsole );
			LogMessage( "DEBUG PATHS: host='%s' log='%s'", GetHostExecutablePath(), GetLogPath() );

			ArtConsoleMessage( "art_debug: logging on; recording %s; frame %d; take %s; preview %s.\n",
				recording ? "on" : "off", g_nFrame, takeDisplayPath, PreviewName( preview ) );
			PrintRecordMask( recordMask );
			PrintRecordPath();
			PrintCapturePrefix();
			PrintHudMask( hudMask );
			ArtConsoleMessage( "art_debug: view hook %s; model hook %s; global FOV hook %s; render calls %lu; log: %s\n",
				viewHookReady ? "ready" : "not ready", modelHookReady ? "ready" : "not ready",
				fovHookReady ? "ready" : "not ready", g_nRenderHookCalls, GetLogPath() );
			LogMessage( "DEBUG SNAPSHOT COMPLETE" );
			FlushLog();
		}

		void ArtDebug_f()
		{
			const int argc = g_pEngine ? g_pEngine->Cmd_Argc() : 0;
			const bool enabled = IsDebugLoggingEnabled();
			if ( argc == 1 )
			{
				ArtConsoleMessage( "art_debug is %s. Usage: art_debug <on|off>.\n",
					enabled ? "on" : "off" );
				if ( enabled )
					ArtConsoleMessage( "art_debug: log file: %s\n", GetLogPath() );
				return;
			}
			if ( argc != 2 )
			{
				ArtConsoleMessage( "art_debug: expected 'on' or 'off'.\n" );
				return;
			}

			const char *pMode = g_pEngine->Cmd_Argv( 1 );
			if ( !Q_stricmp( pMode, "on" ) )
			{
				DWORD error = ERROR_SUCCESS;
				if ( !EnableDebugLogging( error ) )
				{
					ArtConsoleMessage( "art_debug: failed to open %s (Win32 error %lu); logging remains off.\n",
						GetLogPath(), error );
					return;
				}
				LogMessage( "DEBUG LOGGING ENABLED: path='%s'", GetLogPath() );
				PrintDebugSnapshot();
				return;
			}

			if ( !Q_stricmp( pMode, "off" ) )
			{
				if ( !enabled )
				{
					ArtConsoleMessage( "art_debug: file logging is already off.\n" );
					return;
				}
				LogMessage( "DEBUG LOGGING DISABLED: flushing and closing path='%s'", GetLogPath() );
				DisableDebugLogging();
				ArtConsoleMessage( "art_debug: file logging off.\n" );
				return;
			}

			ArtConsoleMessage( "art_debug: '%s' is invalid; expected 'on' or 'off'.\n", pMode );
		}

		ConCommand art_start( "art_start", ArtStart_f,
			"Start synchronized multi-pass TGA recording. Usage: art_start [take_name]" );
		ConCommand art_stop( "art_stop", ArtStop_f,
			"Stop recording, finish pending image writes, and finalize take metadata." );
		ConCommand art_toggle( "art_toggle", ArtToggle_f,
			"Toggle synchronized recording. Usage: art_toggle [take_name]." );
		ConCommand art_status( "art_status", ArtStatus_f,
			"Print recorder status." );
		ConCommand art_stats( "art_stats", ArtStats_f,
			"Print current/latest take and ART session recording statistics." );
		ConCommand art_queue( "art_queue", ArtQueue_f,
			"Configure bounded asynchronous capture writes. Usage: art_queue [status|flush|default|max_files <n>|max_mb <n>|reserve_mb <n>]." );
		ConCommand art_tga_compression( "art_tga_compression", ArtTgaCompression_f,
			"Configure TGA compression. Usage: art_tga_compression <off|auto|rle>." );
		ConCommand art_validation( "art_validation", ArtValidation_f,
			"Validate the latest take for missing frames, file size, and TGA consistency." );
		ConCommand art_take_json( "art_take_json", ArtTakeJson_f,
			"Configure per-take JSON metadata. Usage: art_take_json [on|off|status|write]." );
		ConCommand art_viewmodel_color( "art_viewmodel_color", ArtViewmodelColor_f,
			"Set the Viewmodel pass RGB background color. Usage: art_viewmodel_color <red> <green> <blue>." );
		ConCommand art_players_color( "art_players_color", ArtPlayersColor_f,
			"Set the Players pass RGB background color. Usage: art_players_color <red> <green> <blue>." );
		ConCommand art_viewmodel_fov( "art_viewmodel_fov", ArtViewmodelFov_f,
			"Set the global viewmodel FOV. Usage: art_viewmodel_fov <1-179|default>." );
		ConCommand art_prefix( "art_prefix", ArtPrefix_f,
			"Set screenshot and automatic-take naming prefix. Usage: art_prefix <prefix|default>." );
		ConCommand art_record( "art_record", ArtRecord_f,
			"Configure recorded passes or output path. Usage: art_record <normal|clear|clear-noplayers|viewmodel|depth|players|objectid|all> <on|off>; art_record path <folder|default>." );
		ConCommand art_hud( "art_hud", ArtHud_f,
			"Enable or disable HUD capture per pass. Usage: art_hud <normal|clear|clear-noplayers|viewmodel|depth|players|objectid|all> <on|off>." );
		ConCommand art_preview( "art_preview", ArtPreview_f,
			"Toggle a live pass preview. Usage: art_preview <normal|clear|clear-noplayers|viewmodel|depth|players|objectid|off>." );
		ConCommand art_preview_next( "art_preview_next", ArtPreviewNext_f,
			"Advance live preview to the next recording pass." );
		ConCommand art_help( "art_help", ArtHelp_f,
			"Print the v34_art command reference." );
		ConCommand art_debug( "art_debug", ArtDebug_f,
			"Enable or disable detailed file logging. Usage: art_debug <on|off>." );
	}

	bool HasExistingArtCommand()
	{
		LogMessage( "COMMAND CONFLICT CHECK: scanning ICvar command list" );
		int count = 0;
		for ( const ConCommandBase *pCommand = g_pCvar->GetCommands(); pCommand; pCommand = pCommand->GetNext() )
		{
			++count;
			const char *pName = pCommand->GetName();
			if ( pName && ( !Q_stricmp( pName, "art_start" ) ||
				!Q_stricmp( pName, "art_stop" ) || !Q_stricmp( pName, "art_toggle" ) ||
				!Q_stricmp( pName, "art_status" ) || !Q_stricmp( pName, "art_stats" ) ||
				!Q_stricmp( pName, "art_queue" ) || !Q_stricmp( pName, "art_tga_compression" ) ||
				!Q_stricmp( pName, "art_validation" ) || !Q_stricmp( pName, "art_take_json" ) ||
				!Q_stricmp( pName, "art_viewmodel_color" ) ||
				!Q_stricmp( pName, "art_players_color" ) || !Q_stricmp( pName, "art_preview" ) ||
				!Q_stricmp( pName, "art_preview_next" ) ||
				!Q_stricmp( pName, "art_record" ) || !Q_stricmp( pName, "art_hud" ) ||
				!Q_stricmp( pName, "art_prefix" ) || !Q_stricmp( pName, "art_viewmodel_fov" ) ||
				!Q_stricmp( pName, "art_help" ) || !Q_stricmp( pName, "art_debug" ) ) )
			{
				LogMessage( "COMMAND CONFLICT: found existing '%s' at %p after scanning %d entries", pName, pCommand, count );
				return true;
			}
		}
		LogMessage( "COMMAND CONFLICT CHECK: no art_* command found after scanning %d entries", count );
		return false;
	}

	void RegisterArtCommands()
	{
		LogMessage( "COMMAND REGISTRATION BEGIN: ConCommandBaseMgr::OneTimeInit" );
		ConCommandBaseMgr::OneTimeInit( &g_ConVarAccessor );
		LogMessage( "COMMAND REGISTRATION COMPLETE" );
	}

	void PrintArtHelp()
	{
		static const char *s_pHelpLines[] =
		{
			"Recording: art_start [name] | art_stop | art_toggle [name] | art_status | art_stats",
			"Passes: art_record <pass|all> <on|off> | art_hud <pass|all> <on|off>",
			"Preview: art_preview <pass|off> | art_preview_next",
			"Output: art_record path <folder|default> | art_prefix <prefix|default> | art_open_folder",
			"Colors: art_viewmodel_color <r> <g> <b> | art_players_color <r> <g> <b>",
			"FOV/depth: art_fov ... | art_viewmodel_fov <1-179|default> | art_depth_start/end <distance>",
			"Safety: art_queue ... | art_tga_compression <off|auto|rle> | art_validation ...",
			"Metadata: art_take_json <on|off|status|write> | art_debug <on|off>",
			"GUI: art_gui [on|off|toggle|status] | art_overlay <on|off> | art_config <save|load|delete|list>",
			"Visuals: art_visible ... | art_chams ... | art_noflash/nosmoke <on|off> | art_force_r_lod ...",
			"Players/ObjectID: art_players_through_walls ... | art_players_world_weapons ... | art_objectid_color ...",
			"AdvancedFX: art_hlae help | mirv_campath | mirv_input | mirv_camio | mirv_agr",
			"AdvancedFX: mirv_camexport | mirv_camimport | mirv_fov",
			"Pass names: normal, clear, clear-noplayers, viewmodel, depth, players, objectid.",
			"Run a command without values to query it. Full reference: docs/COMMANDS.md"
		};

		ArtConsoleMessage( V34_ART_PRODUCT_NAME " v" V34_ART_VERSION_STRING
			". Built by " V34_ART_COMPANY_NAME ". Build date: %s %s\n", __DATE__, __TIME__ );
		for ( int i = 0; i < ARRAYSIZE( s_pHelpLines ); ++i )
			ArtConsoleMessage( "  %s\n", s_pHelpLines[i] );
	}


}
