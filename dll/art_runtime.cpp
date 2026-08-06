// Shared recorder state, take paths, and hook lifetime management.

#include "art_internal.h"

// memdbgon must be the last include file in a .cpp file.
#include "tier0/memdbgon.h"

namespace art
{
	HMODULE g_hThisModule = NULL;
	HMODULE g_hClientModule = NULL;
	HMODULE g_hEngineModule = NULL;
	IBaseClientDLL *g_pClient = NULL;
	IVEngineClient *g_pEngine = NULL;
	IClientEntityList *g_pEntityList = NULL;
	ICvar *g_pCvar = NULL;
	IMaterialSystem *g_pMaterials = NULL;
	IFileSystem *g_pFileSystem = NULL;
	IVRenderView *g_pRenderView = NULL;
	IVModelRender *g_pModelRender = NULL;
	IGameConsole003 *g_pGameConsole = NULL;

	volatile LONG g_bRecording = FALSE;
	volatile LONG g_bRenderingArt = FALSE;
	volatile LONG g_nPreviewPass = PREVIEW_NONE;
	volatile LONG g_nRecordMask = RECORD_NORMAL;
	volatile LONG g_nHudMask = RECORD_NORMAL | RECORD_CLEAR | RECORD_CLEAR_NOPLAYERS;
	int g_nFrame = 0;
	unsigned long g_nRenderHookCalls = 0;
	unsigned long g_nPreviewRenderCalls = 0;
	char g_szRecordBase[MAX_PATH] = "art";
	bool g_bRecordBaseAbsolute = false;
	char g_szCapturePrefix[48] = "";
	char g_szTakeRoot[MAX_PATH] = "art/take0000";
	int g_nViewmodelBackgroundRed = 0;
	int g_nViewmodelBackgroundGreen = 255;
	int g_nViewmodelBackgroundBlue = 0;
	int g_nPlayersBackgroundRed = 0;
	int g_nPlayersBackgroundGreen = 255;
	int g_nPlayersBackgroundBlue = 0;
	const float kDefaultViewmodelFov = 74.0f;
	float g_flViewmodelFov = kDefaultViewmodelFov;

	ConVar art_depth_start( "art_depth_start", "150", FCVAR_ARCHIVE,
		"Distance where the depth pass begins fading from black to white." );
	ConVar art_depth_end( "art_depth_end", "800", FCVAR_ARCHIVE,
		"Distance where the depth pass reaches white." );

	const char *PreviewName( LONG preview )
{
	switch ( preview )
	{
	case PREVIEW_NORMAL: return "normal";
	case PREVIEW_CLEAR: return "clear";
	case PREVIEW_VIEWMODEL: return "viewmodel";
	case PREVIEW_DEPTH: return "depth";
	case PREVIEW_PLAYERS: return "players";
	case PREVIEW_OBJECTID: return "objectid";
	case PREVIEW_CLEAR_NOPLAYERS: return "clear-noplayers";
	default: return "off";
	}
}
LONG RecordBitFromName( const char *pName )
{
	if ( !pName ) return 0;
	if ( !Q_stricmp( pName, "normal" ) ) return RECORD_NORMAL;
	if ( !Q_stricmp( pName, "clear" ) ) return RECORD_CLEAR;
	if ( !Q_stricmp( pName, "viewmodel" ) || !Q_stricmp( pName, "green" ) ) return RECORD_VIEWMODEL;
	if ( !Q_stricmp( pName, "depth" ) ) return RECORD_DEPTH;
	if ( !Q_stricmp( pName, "players" ) || !Q_stricmp( pName, "player" ) ) return RECORD_PLAYERS;
	if ( !Q_stricmp( pName, "objectid" ) || !Q_stricmp( pName, "object-id" ) || !Q_stricmp( pName, "object_id" ) ) return RECORD_OBJECTID;
	if ( !Q_stricmp( pName, "clear-noplayers" ) || !Q_stricmp( pName, "clear_noplayers" ) ) return RECORD_CLEAR_NOPLAYERS;
	if ( !Q_stricmp( pName, "all" ) ) return RECORD_ALL;
	return 0;
}
LONG PassBitFromPreview( LONG preview )
{
	switch ( preview )
	{
	case PREVIEW_NORMAL: return RECORD_NORMAL;
	case PREVIEW_CLEAR: return RECORD_CLEAR;
	case PREVIEW_VIEWMODEL: return RECORD_VIEWMODEL;
	case PREVIEW_DEPTH: return RECORD_DEPTH;
	case PREVIEW_PLAYERS: return RECORD_PLAYERS;
	case PREVIEW_OBJECTID: return RECORD_OBJECTID;
	case PREVIEW_CLEAR_NOPLAYERS: return RECORD_CLEAR_NOPLAYERS;
	default: return 0;
	}
}
bool ShouldLogPassConVars()
	{
		if ( InterlockedCompareExchange( &g_bRecording, FALSE, FALSE ) )
			return g_nFrame == 0;
		return g_nPreviewRenderCalls <= 1;
	}

	bool ParseViewmodelFov( const char *pValue, float &fov )
	{
		return logic::ParseViewmodelFov( pValue, fov );
	}

	bool ParseChromaColor( const char *pValue, int &red, int &green, int &blue )
	{
		return logic::ParseRgbColor( pValue, red, green, blue );
	}

	void GetViewmodelBackgroundColorString( char *pBuffer, int bufferSize )
	{
		Q_snprintf( pBuffer, bufferSize, "%d %d %d", g_nViewmodelBackgroundRed,
			g_nViewmodelBackgroundGreen, g_nViewmodelBackgroundBlue );
	}

	void GetPlayersBackgroundColorString( char *pBuffer, int bufferSize )
	{
		Q_snprintf( pBuffer, bufferSize, "%d %d %d", g_nPlayersBackgroundRed,
			g_nPlayersBackgroundGreen, g_nPlayersBackgroundBlue );
	}

	ConVarRestore::ConVarRestore() : m_nCount( 0 )
	{
	}

	void ConVarRestore::Set( const char *pName, const char *pValue )
	{
		if ( !g_pCvar )
		{
			LogMessage( "CVAR SET FAILED: ICvar is null; name='%s' requested='%s'", pName, pValue );
			return;
		}
		if ( m_nCount >= ARRAYSIZE( m_Saved ) )
		{
			LogMessage( "CVAR SET FAILED: restore array full; name='%s' requested='%s'", pName, pValue );
			return;
		}

		ConVar *pVar = g_pCvar->FindVar( pName );
		if ( !pVar )
		{
			if ( ShouldLogPassConVars() )
				LogMessage( "CVAR NOT FOUND: '%s' (requested '%s'); continuing without it", pName, pValue );
			return;
		}

		for ( int i = 0; i < m_nCount; ++i )
		{
			if ( m_Saved[i].pVar != pVar )
				continue;

			if ( ShouldLogPassConVars() )
			{
				if ( pVar->IsBitSet( FCVAR_NEVER_AS_STRING ) )
					LogMessage( "CVAR UPDATE: %s=%d -> '%s' (numeric-only)", pName, pVar->GetInt(), pValue );
				else
					LogMessage( "CVAR UPDATE: %s='%s' -> '%s'", pName, pVar->GetString(), pValue );
			}
			pVar->SetValue( pValue );
			return;
		}

		SavedConVar &saved = m_Saved[m_nCount];
		saved.pVar = pVar;
		saved.restoreAsInt = pVar->IsBitSet( FCVAR_NEVER_AS_STRING );
		saved.intValue = pVar->GetInt();
		if ( saved.restoreAsInt )
			Q_snprintf( saved.value, sizeof( saved.value ), "%d", saved.intValue );
		else
			Q_strncpy( saved.value, pVar->GetString(), sizeof( saved.value ) );
		if ( ShouldLogPassConVars() )
		{
			LogMessage( "CVAR SAVE+SET: %s='%s' -> '%s' (ptr=%p restore_mode=%s)",
				pName, saved.value, pValue, pVar, saved.restoreAsInt ? "integer" : "string" );
		}
		++m_nCount;
		pVar->SetValue( pValue );
	}

	void ConVarRestore::Set( const char *pName, float value )
	{
		char buffer[64];
		Q_snprintf( buffer, sizeof( buffer ), "%g", value );
		Set( pName, buffer );
	}

	ConVarRestore::~ConVarRestore()
	{
		for ( int i = m_nCount - 1; i >= 0; --i )
		{
			if ( ShouldLogPassConVars() )
			{
				LogMessage( "CVAR RESTORE: %s='%s' mode=%s", m_Saved[i].pVar->GetName(),
					m_Saved[i].value, m_Saved[i].restoreAsInt ? "integer" : "string" );
			}
			if ( m_Saved[i].restoreAsInt )
				m_Saved[i].pVar->SetValue( m_Saved[i].intValue );
			else
				m_Saved[i].pVar->SetValue( m_Saved[i].value );
		}
	}

	bool IsSafeTakeName( const char *pName )
	{
		return logic::IsSafeName( pName );
	}

	const char *RecordPathId()
	{
		return g_bRecordBaseAbsolute ? NULL : "MOD";
	}

	void FormatConfiguredRecordPath( char *pOutput, size_t outputBytes )
	{
		if ( g_bRecordBaseAbsolute )
			Q_strncpy( pOutput, g_szRecordBase, static_cast<int>( outputBytes ) );
		else
			Q_snprintf( pOutput, static_cast<int>( outputBytes ), "cstrike/%s", g_szRecordBase );
	}

	void FormatTakeDisplayPath( char *pOutput, size_t outputBytes )
	{
		if ( g_bRecordBaseAbsolute )
			Q_strncpy( pOutput, g_szTakeRoot, static_cast<int>( outputBytes ) );
		else
			Q_snprintf( pOutput, static_cast<int>( outputBytes ), "cstrike/%s", g_szTakeRoot );
	}

	bool BuildCapturePath( char *pOutput, size_t outputBytes, const char *pPassName, int frame )
	{
		char fileName[128];
		if ( !logic::FormatCaptureFileName( fileName, sizeof( fileName ), g_szCapturePrefix, pPassName, frame ) )
			return false;

		const int written = g_bRecordBaseAbsolute ?
			Q_snprintf( pOutput, static_cast<int>( outputBytes ), "%s/%s/%s", g_szTakeRoot, pPassName, fileName ) :
			Q_snprintf( pOutput, static_cast<int>( outputBytes ), "//MOD/%s/%s/%s", g_szTakeRoot, pPassName, fileName );
		return written >= 0 && static_cast<size_t>( written ) < outputBytes;
	}

	bool SetRecordBasePath( const char *pRequestedPath, char *pError, size_t errorBytes )
	{
		if ( !pRequestedPath || !pRequestedPath[0] )
		{
			Q_strncpy( pError, "path is empty", static_cast<int>( errorBytes ) );
			return false;
		}

		if ( !Q_stricmp( pRequestedPath, "default" ) )
		{
			Q_strncpy( g_szRecordBase, "art", sizeof( g_szRecordBase ) );
			g_bRecordBaseAbsolute = false;
			return true;
		}

		if ( strlen( pRequestedPath ) >= sizeof( g_szRecordBase ) - 48 )
		{
			Q_strncpy( pError, "path is too long", static_cast<int>( errorBytes ) );
			return false;
		}

		char normalized[MAX_PATH];
		Q_strncpy( normalized, pRequestedPath, sizeof( normalized ) );
		Q_FixSlashes( normalized, '/' );
		if ( !Q_RemoveDotSlashes( normalized ) )
		{
			Q_strncpy( pError, "path contains invalid '..' traversal", static_cast<int>( errorBytes ) );
			return false;
		}

		size_t length = strlen( normalized );
		while ( length > 1 && normalized[length - 1] == '/' && !( length == 3 && normalized[1] == ':' ) )
			normalized[--length] = '\0';

		if ( !normalized[0] || !Q_stricmp( normalized, "." ) )
		{
			Q_strncpy( pError, "path does not name a folder", static_cast<int>( errorBytes ) );
			return false;
		}

		for ( size_t i = 0; normalized[i]; ++i )
		{
			const unsigned char ch = static_cast<unsigned char>( normalized[i] );
			if ( ch < 32 || ch == '*' || ch == '?' || ch == '"' || ch == '<' || ch == '>' || ch == '|' )
			{
				Q_strncpy( pError, "path contains invalid characters", static_cast<int>( errorBytes ) );
				return false;
			}
			if ( ch == ':' && i != 1 )
			{
				Q_strncpy( pError, "':' is allowed only after a drive letter", static_cast<int>( errorBytes ) );
				return false;
			}
		}

		const bool absolute = Q_IsAbsolutePath( normalized );
		if ( normalized[0] == '/' && normalized[1] != '/' )
		{
			Q_strncpy( pError, "absolute paths must use a drive letter or UNC path", static_cast<int>( errorBytes ) );
			return false;
		}
		if ( normalized[1] == ':' && normalized[2] != '/' )
		{
			Q_strncpy( pError, "drive paths must include '/'; example: D:/captures", static_cast<int>( errorBytes ) );
			return false;
		}
		if ( !Q_strnicmp( normalized, "//MOD", 5 ) || !Q_strnicmp( normalized, "//GAME", 6 ) )
		{
			Q_strncpy( pError, "reserved filesystem prefixes are not accepted", static_cast<int>( errorBytes ) );
			return false;
		}

		Q_strncpy( g_szRecordBase, normalized, sizeof( g_szRecordBase ) );
		g_bRecordBaseAbsolute = absolute;
		return true;
	}

	namespace
	{
		bool CreatePassDirectory( const char *pPassName )
		{
			char path[MAX_PATH];
			const int written = Q_snprintf( path, sizeof( path ), "%s/%s", g_szTakeRoot, pPassName );
			if ( written < 0 || written >= static_cast<int>( sizeof( path ) ) )
			{
				LogMessage( "DIRECTORY FAILED: pass='%s' combined path is too long", pPassName );
				return false;
			}
			const char *pPathId = RecordPathId();
			LogMessage( "DIRECTORY CREATE: path='%s' path_id='%s'", path, pPathId ? pPathId : "<absolute>" );
			g_pFileSystem->CreateDirHierarchy( path, pPathId );
			const bool exists = g_pFileSystem->IsDirectory( path, pPathId );
			LogMessage( "DIRECTORY RESULT: path='%s' path_id='%s' exists=%d",
				path, pPathId ? pPathId : "<absolute>", exists ? 1 : 0 );
			return exists;
		}
	}

	bool MakeTakeDirectories( const char *pRequestedName )
	{
		char configuredPath[MAX_PATH];
		FormatConfiguredRecordPath( configuredPath, sizeof( configuredPath ) );
		LogMessage( "TAKE SETUP BEGIN: requested_name='%s' configured_path='%s' absolute=%d prefix='%s' game_directory='%s'",
			pRequestedName && pRequestedName[0] ? pRequestedName : "<automatic>",
			configuredPath, g_bRecordBaseAbsolute ? 1 : 0,
			g_szCapturePrefix[0] ? g_szCapturePrefix : "<default capture prefix>",
			g_pEngine ? g_pEngine->GetGameDirectory() : "<engine unavailable>" );
		char takeName[64];
		if ( pRequestedName && pRequestedName[0] )
		{
			if ( !IsSafeTakeName( pRequestedName ) )
			{
				LogMessage( "TAKE SETUP FAILED: invalid requested name '%s'", pRequestedName );
				ArtConsoleMessage( "art_start: take name may contain only letters, numbers, '_' and '-'.\n" );
				return false;
			}
			Q_strncpy( takeName, pRequestedName, sizeof( takeName ) );
		}
		else
		{
			int take = 0;
			for ( ; take < 10000; ++take )
			{
				if ( g_szCapturePrefix[0] )
					Q_snprintf( takeName, sizeof( takeName ), "%s_take%04d", g_szCapturePrefix, take );
				else
					Q_snprintf( takeName, sizeof( takeName ), "take%04d", take );
				const char *pSeparator = g_szRecordBase[strlen( g_szRecordBase ) - 1] == '/' ? "" : "/";
				Q_snprintf( g_szTakeRoot, sizeof( g_szTakeRoot ), "%s%s%s", g_szRecordBase, pSeparator, takeName );
				if ( !g_pFileSystem->IsDirectory( g_szTakeRoot, RecordPathId() ) )
					break;
			}

			if ( take == 10000 )
			{
				LogMessage( "TAKE SETUP FAILED: all 10000 automatic names exist prefix='%s'",
					g_szCapturePrefix[0] ? g_szCapturePrefix : "<default>" );
				ArtConsoleMessage( "art_start: no free automatic take directory.\n" );
				return false;
			}
		}

		const char *pSeparator = g_szRecordBase[strlen( g_szRecordBase ) - 1] == '/' ? "" : "/";
		const int rootWritten = Q_snprintf( g_szTakeRoot, sizeof( g_szTakeRoot ), "%s%s%s",
			g_szRecordBase, pSeparator, takeName );
		if ( rootWritten < 0 || rootWritten >= static_cast<int>( sizeof( g_szTakeRoot ) ) )
		{
			LogMessage( "TAKE SETUP FAILED: combined base path and take name are too long" );
			ArtConsoleMessage( "art_start: configured recording path is too long.\n" );
			return false;
		}
		char displayPath[MAX_PATH];
		FormatTakeDisplayPath( displayPath, sizeof( displayPath ) );
		LogMessage( "TAKE SELECTED: root='%s' path_id='%s'", g_szTakeRoot,
			RecordPathId() ? RecordPathId() : "<absolute>" );

		if ( !CreatePassDirectory( "normal" ) || !CreatePassDirectory( "clear" ) || !CreatePassDirectory( "clear-noplayers" ) || !CreatePassDirectory( "viewmodel" ) || !CreatePassDirectory( "players" ) || !CreatePassDirectory( "objectid" ) || !CreatePassDirectory( "depth" ) )
		{
			LogMessage( "TAKE SETUP FAILED: one or more pass directories could not be created root='%s'", g_szTakeRoot );
			ArtConsoleMessage( "art_start: unable to create output directories under %s.\n", configuredPath );
			return false;
		}
		LogMessage( "TAKE SETUP COMPLETE: root='%s' display='%s'", g_szTakeRoot, displayPath );
		return true;
	}

	bool CaptureTga( const CViewSetup &view, const char *pPassName )
	{
		LogMessage( "CAPTURE BEGIN: frame=%d pass='%s' viewport=(%d,%d %dx%d)",
			g_nFrame, pPassName, view.x, view.y, view.width, view.height );
		if ( view.width <= 0 || view.height <= 0 )
		{
			LogMessage( "CAPTURE FAILED: frame=%d pass='%s' invalid viewport dimensions", g_nFrame, pPassName );
			return false;
		}

		const unsigned __int64 pixelCount64 =
			static_cast<unsigned __int64>( view.width ) * static_cast<unsigned __int64>( view.height );
		if ( pixelCount64 > ( static_cast<size_t>( -1 ) - 2048 ) / 4 )
		{
			LogMessage( "CAPTURE FAILED: frame=%d pass='%s' viewport byte size overflow", g_nFrame, pPassName );
			ArtConsoleMessage( "art: capture dimensions are too large. Recording stopped.\n" );
			return false;
		}
		const size_t pixelCount = static_cast<size_t>( pixelCount64 );
		const size_t pixelBytes = pixelCount * 3;
		const LONG compressionMode = InterlockedCompareExchange(
			&g_nArtTgaCompressionMode, ART_TGA_COMPRESSION_AUTO, ART_TGA_COMPRESSION_AUTO );
		const size_t rlePacketOverhead =
			static_cast<size_t>( view.height ) *
			( ( static_cast<size_t>( view.width ) + 127 ) / 128 );
		const size_t pixelBufferBytes = compressionMode == ART_TGA_COMPRESSION_OFF ?
			pixelBytes : pixelBytes + 1024 + rlePacketOverhead;
		const size_t maxTgaBytes = 1024 + pixelCount * 4;
		EnsureArtQueueCapacity( pixelBytes + 1024, pixelBufferBytes + maxTgaBytes );

		LogMessage( "CAPTURE ALLOCATE PIXELS: frame=%d pass='%s' readback_bytes=%Iu allocation_bytes=%Iu compression='%s'",
			g_nFrame, pPassName, pixelBytes, pixelBufferBytes,
			ArtTgaCompressionModeName( compressionMode ) );
		unsigned char *pPixels = static_cast<unsigned char *>(
			AllocateArtCaptureMemory( pixelBufferBytes, "frame readback/RLE" ) );
		if ( !pPixels )
		{
			LogMessage( "CAPTURE FAILED: frame=%d pass='%s' pixel allocation returned null", g_nFrame, pPassName );
			ArtConsoleMessage( "art: unable to allocate frame readback buffer after queue recovery. Recording stopped.\n" );
			return false;
		}

		LogMessage( "READPIXELS BEGIN: frame=%d pass='%s' buffer=%p format=IMAGE_FORMAT_RGB888", g_nFrame, pPassName, pPixels );
		const unsigned __int64 readTiming = BeginArtStageTiming();
		g_pMaterials->ReadPixels( view.x, view.y, view.width, view.height, pPixels, IMAGE_FORMAT_RGB888 );
		EndArtStageTiming( ART_TIMING_READ, readTiming );
		LogMessage( "READPIXELS COMPLETE: frame=%d pass='%s'", g_nFrame, pPassName );

		if ( !Q_stricmp( pPassName, "depth" ) )
		{
			// Reflective materials can tint the fog ramp. The darkest channel is a
			// fast approximation of the underlying black-to-white depth value.
			const DWORD cleanupBegin = GetTickCount();
			for ( size_t i = 0; i < pixelBytes; i += 3 )
			{
				unsigned char depth = pPixels[i];
				if ( pPixels[i + 1] < depth )
					depth = pPixels[i + 1];
				if ( pPixels[i + 2] < depth )
					depth = pPixels[i + 2];
				pPixels[i] = depth;
				pPixels[i + 1] = depth;
				pPixels[i + 2] = depth;
			}
			LogMessage( "DEPTH PIXEL CLEANUP COMPLETE: frame=%d pixels=%Iu elapsed_ms=%lu method=min_rgb_grayscale",
				g_nFrame, pixelBytes / 3, GetTickCount() - cleanupBegin );
		}

		LogMessage( "TGA ALLOCATE: frame=%d pass='%s' maximum_bytes=%Iu", g_nFrame, pPassName, maxTgaBytes );
		void *pTga = AllocateArtCaptureMemory( maxTgaBytes, "TGA encode" );
		if ( !pTga )
		{
			free( pPixels );
			LogMessage( "CAPTURE FAILED: frame=%d pass='%s' TGA allocation returned null", g_nFrame, pPassName );
			ArtConsoleMessage( "art: unable to allocate TGA buffer after queue recovery. Recording stopped.\n" );
			return false;
		}

		const unsigned __int64 encodeTiming = BeginArtStageTiming();
		CUtlBuffer buffer( pTga, static_cast<int>( maxTgaBytes ) );
		const bool encoded = TGAWriter::WriteToBuffer( pPixels, buffer, view.width, view.height,
			IMAGE_FORMAT_RGB888, IMAGE_FORMAT_RGB888 );
		if ( !encoded )
		{
			EndArtStageTiming( ART_TIMING_ENCODE, encodeTiming );
			free( pPixels );
			free( pTga );
			LogMessage( "CAPTURE FAILED: frame=%d pass='%s' TGAWriter::WriteToBuffer returned false", g_nFrame, pPassName );
			ArtConsoleMessage( "art: TGA encoding failed. Recording stopped.\n" );
			return false;
		}

		const unsigned long uncompressedBytes = static_cast<unsigned long>( buffer.TellPut() );
		unsigned char *pOutput = static_cast<unsigned char *>( buffer.Base() );
		unsigned long outputBytes = uncompressedBytes;
		bool usingRle = false;
		if ( compressionMode != ART_TGA_COMPRESSION_OFF )
		{
			size_t rleBytes = 0;
			const bool rleEncoded = EncodeArtTgaRle(
				static_cast<const unsigned char *>( buffer.Base() ),
				static_cast<size_t>( buffer.TellPut() ), pPixels, pixelBufferBytes, rleBytes );
			if ( rleEncoded &&
				( compressionMode == ART_TGA_COMPRESSION_RLE ||
					rleBytes < static_cast<size_t>( uncompressedBytes ) ) )
			{
				pOutput = pPixels;
				outputBytes = static_cast<unsigned long>( rleBytes );
				usingRle = true;
				free( buffer.Base() );
			}
			else if ( compressionMode == ART_TGA_COMPRESSION_RLE && !rleEncoded )
			{
				EndArtStageTiming( ART_TIMING_ENCODE, encodeTiming );
				free( pPixels );
				free( buffer.Base() );
				LogMessage( "CAPTURE FAILED: frame=%d pass='%s' forced TGA RLE encoding failed", g_nFrame, pPassName );
				ArtConsoleMessage( "art: TGA RLE encoding failed. Recording stopped.\n" );
				return false;
			}
		}
		if ( !usingRle )
			free( pPixels );
		EndArtStageTiming( ART_TIMING_ENCODE, encodeTiming );

		char path[MAX_PATH];
		if ( !BuildCapturePath( path, sizeof( path ), pPassName, g_nFrame ) )
		{
			free( pOutput );
			LogMessage( "CAPTURE FAILED: frame=%d pass='%s' output path is too long root='%s'",
				g_nFrame, pPassName, g_szTakeRoot );
			ArtConsoleMessage( "art: output filename is too long. Recording stopped.\n" );
			return false;
		}

		LogMessage( "TGA ENCODE COMPLETE: frame=%d pass='%s' uncompressed_bytes=%lu encoded_bytes=%lu compression='%s' rle_used=%d output='%s' buffer=%p",
			g_nFrame, pPassName, uncompressedBytes, outputBytes,
			ArtTgaCompressionModeName( compressionMode ), usingRle ? 1 : 0, path, pOutput );
		const unsigned __int64 writeTiming = BeginArtStageTiming();
		const FSAsyncStatus_t status = g_pFileSystem->AsyncWrite(
			path, pOutput, static_cast<int>( outputBytes ), true );
		EndArtStageTiming( ART_TIMING_WRITE, writeTiming );
		LogMessage( "ASYNC WRITE RESULT: frame=%d pass='%s' status=%d ownership_transferred=%d path='%s'",
			g_nFrame, pPassName, static_cast<int>( status ), status >= FSASYNC_OK ? 1 : 0, path );
		if ( status < FSASYNC_OK )
		{
			free( pOutput );
			ArtConsoleMessage( "art: async write failed for %s. Recording stopped.\n", path );
			return false;
		}

		NoteArtQueuedWrite( outputBytes );
		RecordArtCompressionResult( uncompressedBytes, outputBytes );
	RecordArtCapturedFile( pPassName, g_nFrame, outputBytes, view.width, view.height,
		view.fov, view.fovViewmodel );
		LogMessage( "CAPTURE COMPLETE: frame=%d pass='%s'", g_nFrame, pPassName );
		return true;
	}

	void ApplyUtilityBase( ConVarRestore &vars )
	{
		vars.Set( "cl_drawmonitors", "0" );
		vars.Set( "mat_postprocess_enable", "0" );
		vars.Set( "mat_colorcorrection", "0" );
		vars.Set( "mat_disable_bloom", "1" );
	}

	void ApplyViewmodel( ConVarRestore &vars )
	{
		char backgroundColor[32];
		GetViewmodelBackgroundColorString( backgroundColor, sizeof( backgroundColor ) );
		ApplyUtilityBase( vars );
		vars.Set( "fog_enable", "1" );
		vars.Set( "fog_color", backgroundColor );
		vars.Set( "fog_colorskybox", backgroundColor );
		vars.Set( "fog_enable_water_fog", "1" );
		vars.Set( "fog_override", "1" );
		vars.Set( "fog_start", "999999999" );
		vars.Set( "fog_end", "-1" );
		vars.Set( "r_3dsky", "0" );
		vars.Set( "r_skybox", "0" );
		vars.Set( "gl_clear", "1" );
		vars.Set( "r_drawparticles", "0" );
		vars.Set( "r_drawsprites", "0" );
	}

	void ApplyPlayers( ConVarRestore &vars )
	{
		char backgroundColor[32];
		GetPlayersBackgroundColorString( backgroundColor, sizeof( backgroundColor ) );
		ApplyUtilityBase( vars );
		vars.Set( "fog_enable", "1" );
		vars.Set( "fog_color", backgroundColor );
		vars.Set( "fog_colorskybox", backgroundColor );
		vars.Set( "fog_enable_water_fog", "1" );
		vars.Set( "fog_override", "1" );
		vars.Set( "fog_start", "-10000" );
		vars.Set( "fog_end", "-9999" );
		vars.Set( "r_drawviewmodel", "0" );
		vars.Set( "r_drawworld", "1" );
		vars.Set( "r_drawopaqueworld", "1" );
		vars.Set( "r_drawtranslucentworld", "0" );
		vars.Set( "r_drawbrushmodels", "1" );
		vars.Set( "r_drawstaticprops", "1" );
		vars.Set( "r_DrawDetailProps", "1" );
		vars.Set( "r_drawentities", "1" );
		vars.Set( "r_drawothermodels", "1" );
		vars.Set( "r_shadows", "0" );
		vars.Set( "r_3dsky", "0" );
		vars.Set( "r_skybox", "0" );
		vars.Set( "gl_clear", "1" );
		vars.Set( "r_drawparticles", "0" );
		vars.Set( "r_drawsprites", "0" );
		vars.Set( "r_renderoverlayfragment", "0" );
		vars.Set( "r_drawdecals", "0" );
		vars.Set( "r_drawmodeldecals", "0" );
		vars.Set( "r_drawropes", "0" );
		vars.Set( "r_DrawBeams", "0" );
	}

	void ApplyObjectId( ConVarRestore &vars, int worldRed, int worldGreen, int worldBlue,
		int skyboxRed, int skyboxGreen, int skyboxBlue )
	{
		char worldColor[32];
		Q_snprintf( worldColor, sizeof( worldColor ), "%d %d %d", worldRed, worldGreen, worldBlue );
		( void )skyboxRed; ( void )skyboxGreen; ( void )skyboxBlue;
		ApplyUtilityBase( vars );
		// Never toggle material-system quality cvars here. Changing mat_fullbright,
		// mat_specular, mat_bumpmap or mat_phong every frame rebuilds material state.
		vars.Set( "fog_enable", "1" );
		vars.Set( "fog_color", worldColor );
		vars.Set( "fog_colorskybox", worldColor );
		vars.Set( "fog_enable_water_fog", "1" );
		vars.Set( "fog_override", "1" );
		vars.Set( "fog_start", "-10000" );
		vars.Set( "fog_end", "-9999" );
		vars.Set( "fog_maxdensity", "1" );
		vars.Set( "r_drawviewmodel", "1" );
		vars.Set( "r_drawworld", "1" );
		vars.Set( "r_drawopaqueworld", "1" );
		vars.Set( "r_drawtranslucentworld", "0" );
		vars.Set( "r_drawbrushmodels", "1" );
		vars.Set( "r_drawstaticprops", "1" );
		vars.Set( "r_DrawDetailProps", "1" );
		vars.Set( "r_drawentities", "1" );
		vars.Set( "r_drawothermodels", "1" );
		vars.Set( "r_3dsky", "0" );
		vars.Set( "r_skybox", "0" );
		vars.Set( "gl_clear", "1" );
		vars.Set( "r_shadows", "0" );
		vars.Set( "r_drawparticles", "0" );
		vars.Set( "r_drawsprites", "0" );
		vars.Set( "r_drawropes", "0" );
		vars.Set( "r_DrawBeams", "0" );
		vars.Set( "r_renderoverlayfragment", "0" );
		vars.Set( "r_drawdecals", "0" );
		vars.Set( "r_drawmodeldecals", "0" );
	}

	void ApplyDepth( ConVarRestore &vars )
	{
		ApplyUtilityBase( vars );
		vars.Set( "fog_enable", "1" );
		vars.Set( "fog_color", "255 255 255" );
		vars.Set( "fog_colorskybox", "255 255 255" );
		vars.Set( "fog_enable_water_fog", "1" );
		vars.Set( "fog_override", "1" );
		vars.Set( "fog_start", art_depth_start.GetFloat() );
		vars.Set( "fog_end", art_depth_end.GetFloat() );
		vars.Set( "r_drawviewmodel", "0" );
		vars.Set( "r_skybox", "0" );
		vars.Set( "gl_clear", "1" );
		vars.Set( "r_shadows", "0" );
		vars.Set( "mat_diffuse", "0" );
		vars.Set( "r_3dsky", "0" );
		vars.Set( "r_drawparticles", "0" );
		vars.Set( "r_renderoverlayfragment", "0" );
		vars.Set( "r_drawdecals", "0" );
	}

	int AddHudDrawFlag( int baseFlags, LONG hudMask, LONG passBit )
	{
		return ( hudMask & passBit ) ? ( baseFlags | RENDERVIEW_DRAWHUD ) : baseFlags;
	}

	void ApplyHudSetting( ConVarRestore &vars, LONG hudMask, LONG passBit )
	{
		vars.Set( "cl_drawhud", ( hudMask & passBit ) ? "1" : "0" );
	}
}
