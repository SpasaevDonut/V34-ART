// Recording statistics, JSON manifests, and background take validation.

#include "art_internal.h"
#include "art_gui.h"
#include "art_logic.h"

#include <algorithm>
#include <new>
#include <string>
#include <vector>

// memdbgon must be the last include file in a .cpp file.
#include "tier0/memdbgon.h"

namespace art
{
	ArtRecordingStatistics g_ArtRecordingStats = {};
	volatile LONG g_bArtTakeManifestEnabled = TRUE;
	ArtValidationOptions g_ArtValidationOptions = { TRUE, TRUE, 18, FALSE };
	ArtValidationResult g_ArtValidationResult = {};
	ArtValidationProgress g_ArtValidationProgress = {};

	namespace
	{
		struct PassDefinition
		{
			const char *name;
			LONG bit;
		};

		struct ActualFrame
		{
			int frame;
			unsigned __int64 bytes;
		};

		struct ValidationSnapshot
		{
			bool checkFileSize;
			bool checkDroppedFrames;
			bool expectedFrameHistoryAvailable;
			unsigned long minimumFileBytes;
			char takeDisplayPath[MAX_PATH];
			char takeAbsolutePath[MAX_PATH];
			char takePrefix[48];
			std::vector<int> expectedFrames[ART_CAPTURE_PASS_COUNT];
		};

		class BufferedFileReader
		{
		public:
			explicit BufferedFileReader( HANDLE hFile )
				: m_hFile( hFile ), m_Position( 0 ), m_Available( 0 ) {}

			bool ReadByte( unsigned char &value )
			{
				if ( m_Position == m_Available && !Fill() )
					return false;
				value = m_Buffer[m_Position++];
				return true;
			}

			bool Skip( size_t bytes )
			{
				while ( bytes )
				{
					if ( m_Position == m_Available && !Fill() )
						return false;
					const size_t available = m_Available - m_Position;
					const size_t consume = bytes < available ? bytes : available;
					m_Position += consume;
					bytes -= consume;
				}
				return true;
			}

		private:
			bool Fill()
			{
				DWORD read = 0;
				if ( !ReadFile( m_hFile, m_Buffer, sizeof( m_Buffer ), &read, NULL ) || read == 0 )
					return false;
				m_Position = 0;
				m_Available = read;
				return true;
			}

			HANDLE m_hFile;
			unsigned char m_Buffer[64 * 1024];
			size_t m_Position;
			size_t m_Available;
		};

		static const PassDefinition kPasses[ART_CAPTURE_PASS_COUNT] =
		{
			{ "normal", ART_RECORD_NORMAL },
			{ "clear", ART_RECORD_CLEAR },
			{ "clear-noplayers", ART_RECORD_CLEAR_NOPLAYERS },
			{ "viewmodel", ART_RECORD_VIEWMODEL },
			{ "depth", ART_RECORD_DEPTH },
			{ "players", ART_RECORD_PLAYERS },
			{ "objectid", ART_RECORD_OBJECTID }
		};

		std::vector<int> g_ExpectedFrames[ART_CAPTURE_PASS_COUNT];
		bool g_bExpectedFrameHistoryAvailable = false;
		int g_LastCaptureWidth = 0;
		int g_LastCaptureHeight = 0;
		int g_LastFovFrame = -1;
		float g_LastCameraFov = 0.0f;
		float g_LastViewmodelFov = 0.0f;
		float g_LastFovAspectRatio = 0.0f;

		int FindPassIndex( const char *pPassName )
		{
			if ( !pPassName )
				return -1;
			for ( int i = 0; i < ART_CAPTURE_PASS_COUNT; ++i )
			{
				if ( !Q_stricmp( pPassName, kPasses[i].name ) )
					return i;
			}
			return -1;
		}

		bool BuildTakeAbsolutePath( char *pOutput, size_t outputBytes )
		{
			if ( !pOutput || !outputBytes )
				return false;
			pOutput[0] = '\0';
			if ( g_bRecordBaseAbsolute )
			{
				Q_strncpy( pOutput, g_szTakeRoot, static_cast<int>( outputBytes ) );
			}
			else
			{
				const char *pGameDirectory = g_pEngine ? g_pEngine->GetGameDirectory() : NULL;
				if ( !pGameDirectory || !pGameDirectory[0] )
					return false;
				const int written = Q_snprintf( pOutput, static_cast<int>( outputBytes ), "%s\\%s",
					pGameDirectory, g_szTakeRoot );
				if ( written < 0 || written >= static_cast<int>( outputBytes ) )
					return false;
			}
			Q_FixSlashes( pOutput, '\\' );
			return pOutput[0] != '\0';
		}

		void CopyTakeName( const char *pTakePath, char *pOutput, size_t outputBytes )
		{
			if ( !pOutput || !outputBytes )
				return;
			pOutput[0] = '\0';
			if ( !pTakePath || !pTakePath[0] )
				return;
			const char *pSlash = strrchr( pTakePath, '\\' );
			const char *pForwardSlash = strrchr( pTakePath, '/' );
			if ( !pSlash || ( pForwardSlash && pForwardSlash > pSlash ) )
				pSlash = pForwardSlash;
			Q_strncpy( pOutput, pSlash ? pSlash + 1 : pTakePath,
				static_cast<int>( outputBytes ) );
		}

		void CopyMapName( const char *pLevelName, char *pOutput, size_t outputBytes )
		{
			if ( !pOutput || !outputBytes )
				return;
			pOutput[0] = '\0';
			if ( !pLevelName || !pLevelName[0] )
				return;
			const char *pSlash = strrchr( pLevelName, '\\' );
			const char *pForwardSlash = strrchr( pLevelName, '/' );
			if ( !pSlash || ( pForwardSlash && pForwardSlash > pSlash ) )
				pSlash = pForwardSlash;
			Q_strncpy( pOutput, pSlash ? pSlash + 1 : pLevelName,
				static_cast<int>( outputBytes ) );
			char *pExtension = strrchr( pOutput, '.' );
			if ( pExtension && !Q_stricmp( pExtension, ".bsp" ) )
				*pExtension = '\0';
		}

		void AppendJsonEscaped( std::string &json, const char *pValue )
		{
			json += '"';
			if ( pValue )
			{
				for ( const unsigned char *p =
					reinterpret_cast<const unsigned char *>( pValue ); *p; ++p )
				{
					switch ( *p )
					{
					case '"': json += "\\\""; break;
					case '\\': json += "\\\\"; break;
					case '\b': json += "\\b"; break;
					case '\f': json += "\\f"; break;
					case '\n': json += "\\n"; break;
					case '\r': json += "\\r"; break;
					case '\t': json += "\\t"; break;
					default:
						if ( *p < 0x20 )
						{
							char escaped[8];
							Q_snprintf( escaped, sizeof( escaped ), "\\u%04x", *p );
							json += escaped;
						}
						else
							json += static_cast<char>( *p );
						break;
					}
				}
			}
			json += '"';
		}

		void AppendJsonFormat( std::string &json, const char *pFormat, ... )
		{
			char buffer[2048];
			va_list args;
			va_start( args, pFormat );
			const int written = _vsnprintf_s( buffer, sizeof( buffer ), _TRUNCATE,
				pFormat, args );
			va_end( args );
			if ( written >= 0 )
				json.append( buffer, written );
			else
				json += buffer;
		}

		void AppendUtcTimestamp( std::string &json, const SYSTEMTIME &time )
		{
			char timestamp[40];
			Q_snprintf( timestamp, sizeof( timestamp ),
				"%04u-%02u-%02uT%02u:%02u:%02u.%03uZ",
				time.wYear, time.wMonth, time.wDay, time.wHour,
				time.wMinute, time.wSecond, time.wMilliseconds );
			AppendJsonEscaped( json, time.wYear ? timestamp : "" );
		}

		void AppendFrameRanges( std::string &json, const std::vector<int> &frames )
		{
			json += '[';
			if ( !frames.empty() )
			{
				std::vector<int> ordered = frames;
				std::sort( ordered.begin(), ordered.end() );
				int start = ordered[0];
				int end = start;
				bool firstRange = true;
				for ( size_t i = 1; i <= ordered.size(); ++i )
				{
					if ( i < ordered.size() && ordered[i] <= end + 1 )
					{
						if ( ordered[i] > end )
							end = ordered[i];
						continue;
					}
					if ( !firstRange )
						json += ',';
					AppendJsonFormat( json, "{\"start\": %d, \"end\": %d}", start, end );
					firstRange = false;
					if ( i < ordered.size() )
						start = end = ordered[i];
				}
			}
			json += ']';
		}

		void AppendPassManifest( std::string &json, int passIndex )
		{
			const PassDefinition &pass = kPasses[passIndex];
			const ArtPassRecordingStatistics &statistics =
				g_ArtRecordingStats.passes[passIndex];
			if ( passIndex )
				json += ",\n";
			json += "    {\n      \"name\": ";
			AppendJsonEscaped( json, pass.name );
			AppendJsonFormat( json,
				",\n      \"enabled_at_start\": %s,\n      \"hud_at_start\": %s,"
				"\n      \"files\": %lu,\n      \"bytes\": %I64u,\n      \"directory\": ",
				( g_ArtRecordingStats.takeRecordMask & pass.bit ) ? "true" : "false",
				( g_ArtRecordingStats.takeHudMask & pass.bit ) ? "true" : "false",
				statistics.files, statistics.bytes );
			AppendJsonEscaped( json, pass.name );
			json += ",\n      \"filename_pattern\": ";
			char filenamePattern[128];
			Q_snprintf( filenamePattern, sizeof( filenamePattern ), "%s%s%s_%%04d.tga",
				g_ArtRecordingStats.takePrefix[0] ? g_ArtRecordingStats.takePrefix : "",
				g_ArtRecordingStats.takePrefix[0] ? "_" : "", pass.name );
			AppendJsonEscaped( json, filenamePattern );
			json += ",\n      \"frame_ranges\": ";
			AppendFrameRanges( json, g_ExpectedFrames[passIndex] );
			if ( !Q_stricmp( pass.name, "viewmodel" ) )
				AppendJsonFormat( json, ",\n      \"background_rgb\": [%d, %d, %d]",
					g_ArtRecordingStats.takeViewmodelColor[0],
					g_ArtRecordingStats.takeViewmodelColor[1],
					g_ArtRecordingStats.takeViewmodelColor[2] );
			else if ( !Q_stricmp( pass.name, "players" ) )
				AppendJsonFormat( json, ",\n      \"background_rgb\": [%d, %d, %d]",
					g_ArtRecordingStats.takePlayersColor[0],
					g_ArtRecordingStats.takePlayersColor[1],
					g_ArtRecordingStats.takePlayersColor[2] );
			else if ( !Q_stricmp( pass.name, "objectid" ) )
				AppendJsonFormat( json,
					",\n      \"category_colors\": {\n"
					"        \"viewmodel\": [%d, %d, %d],\n"
					"        \"players\": [%d, %d, %d],\n"
					"        \"world\": [%d, %d, %d],\n"
					"        \"skybox\": [%d, %d, %d]\n"
					"      }",
					g_ArtRecordingStats.takeObjectIdColors[0][0],
					g_ArtRecordingStats.takeObjectIdColors[0][1],
					g_ArtRecordingStats.takeObjectIdColors[0][2],
					g_ArtRecordingStats.takeObjectIdColors[1][0],
					g_ArtRecordingStats.takeObjectIdColors[1][1],
					g_ArtRecordingStats.takeObjectIdColors[1][2],
					g_ArtRecordingStats.takeObjectIdColors[2][0],
					g_ArtRecordingStats.takeObjectIdColors[2][1],
					g_ArtRecordingStats.takeObjectIdColors[2][2],
					g_ArtRecordingStats.takeObjectIdColors[3][0],
					g_ArtRecordingStats.takeObjectIdColors[3][1],
					g_ArtRecordingStats.takeObjectIdColors[3][2] );
			json += "\n    }";
		}

		bool ParseFrameNumber( const char *pName, int &frame )
		{
			frame = -1;
			if ( !pName )
				return false;
			const char *pExtension = strrchr( pName, '.' );
			const char *pSeparator = strrchr( pName, '_' );
			if ( !pExtension || !pSeparator || pSeparator >= pExtension || Q_stricmp( pExtension, ".tga" ) )
				return false;

			++pSeparator;
			if ( pSeparator == pExtension )
				return false;
			unsigned long value = 0;
			for ( const char *p = pSeparator; p < pExtension; ++p )
			{
				if ( *p < '0' || *p > '9' )
					return false;
				value = value * 10 + static_cast<unsigned long>( *p - '0' );
				if ( value > 2147483647UL )
					return false;
			}
			frame = static_cast<int>( value );
			return true;
		}

		enum TgaValidationStatus
		{
			TGA_VALID = 0,
			TGA_INVALID_HEADER,
			TGA_INVALID_PIXEL_STREAM
		};

		TgaValidationStatus ValidateTgaFile( const char *pPath,
			unsigned short &width, unsigned short &height, unsigned char &bitsPerPixel )
		{
			width = 0;
			height = 0;
			bitsPerPixel = 0;
			HANDLE hFile = CreateFileA( pPath, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE,
				NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL );
			if ( hFile == INVALID_HANDLE_VALUE )
				return TGA_INVALID_HEADER;

			unsigned char header[18];
			DWORD read = 0;
			const BOOL ok = ReadFile( hFile, header, sizeof( header ), &read, NULL );
			if ( !ok || read != sizeof( header ) )
			{
				CloseHandle( hFile );
				return TGA_INVALID_HEADER;
			}

			width = static_cast<unsigned short>( header[12] | ( header[13] << 8 ) );
			height = static_cast<unsigned short>( header[14] | ( header[15] << 8 ) );
			bitsPerPixel = header[16];
			if ( header[1] != 0 || !width || !height ||
				( bitsPerPixel != 24 && bitsPerPixel != 32 ) ||
				( header[2] != 2 && header[2] != 10 ) )
			{
				CloseHandle( hFile );
				return TGA_INVALID_HEADER;
			}

			LARGE_INTEGER fileSize;
			if ( !GetFileSizeEx( hFile, &fileSize ) )
			{
				CloseHandle( hFile );
				return TGA_INVALID_PIXEL_STREAM;
			}
			const unsigned __int64 pixelCount =
				static_cast<unsigned __int64>( width ) * height;
			const unsigned long bytesPerPixel = bitsPerPixel / 8;
			const unsigned __int64 pixelOffset = 18ULL + header[0];
			if ( static_cast<unsigned __int64>( fileSize.QuadPart ) < pixelOffset )
			{
				CloseHandle( hFile );
				return TGA_INVALID_PIXEL_STREAM;
			}

			if ( header[2] == 2 )
			{
				const bool complete = static_cast<unsigned __int64>( fileSize.QuadPart ) >=
					pixelOffset + pixelCount * bytesPerPixel;
				CloseHandle( hFile );
				return complete ? TGA_VALID : TGA_INVALID_PIXEL_STREAM;
			}

			LARGE_INTEGER streamPosition;
			streamPosition.QuadPart = pixelOffset;
			if ( !SetFilePointerEx( hFile, streamPosition, NULL, FILE_BEGIN ) )
			{
				CloseHandle( hFile );
				return TGA_INVALID_PIXEL_STREAM;
			}

			unsigned __int64 decodedPixels = 0;
			unsigned __int64 consumedBytes = pixelOffset;
			BufferedFileReader reader( hFile );
			while ( decodedPixels < pixelCount )
			{
				unsigned char packetHeader = 0;
				if ( !reader.ReadByte( packetHeader ) )
				{
					CloseHandle( hFile );
					return TGA_INVALID_PIXEL_STREAM;
				}
				++consumedBytes;
				const unsigned long packetPixels = ( packetHeader & 0x7F ) + 1;
				const unsigned long rowRemaining = width -
					static_cast<unsigned long>( decodedPixels % width );
				if ( packetPixels > rowRemaining ||
					decodedPixels + packetPixels > pixelCount )
				{
					CloseHandle( hFile );
					return TGA_INVALID_PIXEL_STREAM;
				}
				const unsigned long packetBytes = packetHeader & 0x80 ?
					bytesPerPixel : packetPixels * bytesPerPixel;
				if ( consumedBytes + packetBytes >
					static_cast<unsigned __int64>( fileSize.QuadPart ) )
				{
					CloseHandle( hFile );
					return TGA_INVALID_PIXEL_STREAM;
				}
				if ( !reader.Skip( packetBytes ) )
				{
					CloseHandle( hFile );
					return TGA_INVALID_PIXEL_STREAM;
				}
				consumedBytes += packetBytes;
				decodedPixels += packetPixels;
			}
			CloseHandle( hFile );
			return TGA_VALID;
		}

		bool ContainsFrame( const std::vector<int> &frames, int frame )
		{
			return std::find( frames.begin(), frames.end(), frame ) != frames.end();
		}

		void BuildExpectedFilePath( const ValidationSnapshot &snapshot,
			char *pOutput, size_t outputBytes, int passIndex, int frame )
		{
			if ( snapshot.takePrefix[0] )
			{
				Q_snprintf( pOutput, static_cast<int>( outputBytes ), "%s\\%s\\%s_%s_%04d.tga",
					snapshot.takeAbsolutePath, kPasses[passIndex].name,
					snapshot.takePrefix, kPasses[passIndex].name, frame );
			}
			else
			{
				Q_snprintf( pOutput, static_cast<int>( outputBytes ), "%s\\%s\\%s_%04d.tga",
					snapshot.takeAbsolutePath, kPasses[passIndex].name,
					kPasses[passIndex].name, frame );
			}
		}

		unsigned __int64 FileSizeFromFindData( const WIN32_FIND_DATAA &data )
		{
			return ( static_cast<unsigned __int64>( data.nFileSizeHigh ) << 32 ) |
				static_cast<unsigned __int64>( data.nFileSizeLow );
		}

		void UpdateSizeRange( ArtValidationResult &result, unsigned __int64 bytes )
		{
			if ( !result.scannedFiles || bytes < result.smallestFileBytes )
				result.smallestFileBytes = bytes;
			if ( bytes > result.largestFileBytes )
				result.largestFileBytes = bytes;
		}

		void ScanPassDirectory( const ValidationSnapshot &snapshot,
			ArtValidationResult &result, int passIndex )
		{
			char directory[MAX_PATH];
			Q_snprintf( directory, sizeof( directory ), "%s\\%s",
				snapshot.takeAbsolutePath, kPasses[passIndex].name );
			const DWORD attributes = GetFileAttributesA( directory );
			if ( attributes == INVALID_FILE_ATTRIBUTES || !( attributes & FILE_ATTRIBUTE_DIRECTORY ) )
			{
				++result.directoryErrors;
				return;
			}

			char pattern[MAX_PATH];
			Q_snprintf( pattern, sizeof( pattern ), "%s\\*.tga", directory );
			WIN32_FIND_DATAA data;
			HANDLE hFind = FindFirstFileA( pattern, &data );
			std::vector<ActualFrame> actualFrames;
			unsigned short referenceWidth = 0;
			unsigned short referenceHeight = 0;
			unsigned char referenceBits = 0;

			if ( hFind != INVALID_HANDLE_VALUE )
			{
				do
				{
					if ( data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY )
						continue;

					const unsigned __int64 bytes = FileSizeFromFindData( data );
					UpdateSizeRange( result, bytes );
					++result.scannedFiles;
					result.totalBytes += bytes;
					if ( bytes < snapshot.minimumFileBytes )
						++result.undersizedFiles;

					int frame = -1;
					if ( ParseFrameNumber( data.cFileName, frame ) )
					{
						ActualFrame actual = { frame, bytes };
						actualFrames.push_back( actual );
					}
					else if ( snapshot.expectedFrameHistoryAvailable )
					{
						++result.unexpectedFiles;
					}

					char path[MAX_PATH];
					Q_snprintf( path, sizeof( path ), "%s\\%s", directory, data.cFileName );
					unsigned short width = 0;
					unsigned short height = 0;
					unsigned char bits = 0;
					const TgaValidationStatus tgaStatus =
						ValidateTgaFile( path, width, height, bits );
					if ( tgaStatus == TGA_INVALID_HEADER )
					{
						++result.invalidHeaders;
					}
					else if ( tgaStatus == TGA_INVALID_PIXEL_STREAM )
					{
						++result.invalidPixelData;
					}
					else if ( !referenceWidth )
					{
						referenceWidth = width;
						referenceHeight = height;
						referenceBits = bits;
					}
					else if ( width != referenceWidth || height != referenceHeight || bits != referenceBits )
					{
						++result.inconsistentDimensions;
					}
					InterlockedIncrement( &g_ArtValidationProgress.completedFiles );
				}
				while ( FindNextFileA( hFind, &data ) );
				FindClose( hFind );
			}
			else if ( GetLastError() != ERROR_FILE_NOT_FOUND )
			{
				++result.directoryErrors;
			}

			const std::vector<int> &expectedFrames = snapshot.expectedFrames[passIndex];
			result.expectedFiles += static_cast<unsigned long>( expectedFrames.size() );
			if ( snapshot.expectedFrameHistoryAvailable )
			{
				for ( size_t i = 0; i < expectedFrames.size(); ++i )
				{
					char expectedPath[MAX_PATH];
					BuildExpectedFilePath( snapshot, expectedPath,
						sizeof( expectedPath ), passIndex, expectedFrames[i] );
					const DWORD expectedAttributes = GetFileAttributesA( expectedPath );
					if ( expectedAttributes == INVALID_FILE_ATTRIBUTES ||
						( expectedAttributes & FILE_ATTRIBUTE_DIRECTORY ) )
						++result.missingFiles;
				}
				for ( size_t i = 0; i < actualFrames.size(); ++i )
				{
					if ( !ContainsFrame( expectedFrames, actualFrames[i].frame ) )
						++result.unexpectedFiles;
				}
			}
			else if ( actualFrames.size() > 1 )
			{
				std::sort( actualFrames.begin(), actualFrames.end(),
					[]( const ActualFrame &left, const ActualFrame &right ) { return left.frame < right.frame; } );
				for ( size_t i = 1; i < actualFrames.size(); ++i )
				{
					if ( actualFrames[i].frame > actualFrames[i - 1].frame + 1 )
						result.sequenceGaps +=
							static_cast<unsigned long>( actualFrames[i].frame - actualFrames[i - 1].frame - 1 );
				}
			}
		}

		unsigned long CountTgaFiles( const ValidationSnapshot &snapshot )
		{
			unsigned long count = 0;
			for ( int passIndex = 0; passIndex < ART_CAPTURE_PASS_COUNT; ++passIndex )
			{
				char pattern[MAX_PATH];
				Q_snprintf( pattern, sizeof( pattern ), "%s\\%s\\*.tga",
					snapshot.takeAbsolutePath, kPasses[passIndex].name );
				WIN32_FIND_DATAA data;
				HANDLE hFind = FindFirstFileA( pattern, &data );
				if ( hFind == INVALID_HANDLE_VALUE )
					continue;
				do
				{
					if ( !( data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY ) &&
						count < 0x7FFFFFFFUL )
						++count;
				}
				while ( FindNextFileA( hFind, &data ) );
				FindClose( hFind );
			}
			return count;
		}

		DWORD WINAPI ArtValidationWorker( void *pParameter )
		{
			ValidationSnapshot *pSnapshot =
				static_cast<ValidationSnapshot *>( pParameter );
			ArtValidationResult result = {};
			Q_strncpy( result.takePath, pSnapshot->takeDisplayPath,
				sizeof( result.takePath ) );

			InterlockedExchange( &g_ArtValidationProgress.phase,
				ART_VALIDATION_DISCOVERING );
			const unsigned long totalFiles = CountTgaFiles( *pSnapshot );
			InterlockedExchange( &g_ArtValidationProgress.totalFiles,
				static_cast<LONG>( totalFiles ) );
			InterlockedExchange( &g_ArtValidationProgress.phase,
				ART_VALIDATION_CHECKING_FILES );
			for ( int i = 0; i < ART_CAPTURE_PASS_COUNT; ++i )
				ScanPassDirectory( *pSnapshot, result, i );

			InterlockedExchange( &g_ArtValidationProgress.phase,
				ART_VALIDATION_FINALIZING );
			const bool sizeFailure = pSnapshot->checkFileSize &&
				result.undersizedFiles != 0;
			const bool droppedFailure = pSnapshot->checkDroppedFrames &&
				( result.missingFiles != 0 ||
					result.unexpectedFiles != 0 ||
					result.sequenceGaps != 0 );
			result.passed = !sizeFailure && !droppedFailure &&
				result.invalidHeaders == 0 &&
				result.invalidPixelData == 0 &&
				result.inconsistentDimensions == 0 &&
				result.directoryErrors == 0;
			result.hasResult = true;
			g_ArtValidationResult = result;
			delete pSnapshot;

			InterlockedExchange( &g_ArtValidationProgress.completionPending, TRUE );
			InterlockedExchange( &g_ArtValidationProgress.running, FALSE );
			InterlockedExchange( &g_ArtValidationProgress.phase, ART_VALIDATION_IDLE );
			return 0;
		}
	}

	void FormatArtTakeManifestPath( char *pOutput, size_t outputBytes )
	{
		if ( !pOutput || !outputBytes )
			return;
		pOutput[0] = '\0';
		if ( !g_ArtRecordingStats.takeAbsolutePath[0] ||
			!g_ArtRecordingStats.takeName[0] )
			return;
		const int written = Q_snprintf( pOutput, static_cast<int>( outputBytes ),
			"%s\\%s.json", g_ArtRecordingStats.takeAbsolutePath,
			g_ArtRecordingStats.takeName );
		if ( written < 0 || written >= static_cast<int>( outputBytes ) )
			pOutput[0] = '\0';
	}

	bool WriteArtTakeManifest( bool force )
	{
		if ( !force && !g_ArtRecordingStats.takeManifestEnabled )
			return false;
		char manifestPath[MAX_PATH];
		FormatArtTakeManifestPath( manifestPath, sizeof( manifestPath ) );
		if ( !manifestPath[0] )
			return false;

		std::string json;
		json.reserve( 12288 );
		json += "{\n";
		json += "  \"schema\": \"css-v34-art.take\",\n";
		json += "  \"schema_version\": 1,\n";
		json += "  \"generator\": {\n";
		json += "    \"name\": \"CS:S V34 ADVANCED RECORDING TOOLS\",\n";
		json += "    \"abbreviation\": \"ART\",\n";
		json += "    \"version\": ";
		AppendJsonEscaped( json, V34_ART_VERSION_STRING );
		json += ",\n";
		json += "    \"target\": \"Counter-Strike: Source v34 (build 4044)\"\n";
		json += "  },\n";
		json += "  \"take\": {\n";
		json += "    \"name\": ";
		AppendJsonEscaped( json, g_ArtRecordingStats.takeName );
		json += ",\n    \"status\": ";
		AppendJsonEscaped( json, g_ArtRecordingStats.takeActive ? "recording" :
			g_ArtRecordingStats.takeAborted ? "aborted" : "completed" );
		json += ",\n    \"started_utc\": ";
		AppendUtcTimestamp( json, g_ArtRecordingStats.takeStartedUtc );
		json += ",\n    \"finished_utc\": ";
		AppendUtcTimestamp( json, g_ArtRecordingStats.takeFinishedUtc );
		AppendJsonFormat( json, ",\n    \"duration_seconds\": %.6f,\n    \"display_folder\": ",
			GetArtRecordingElapsedMs() / 1000.0 );
		AppendJsonEscaped( json, g_ArtRecordingStats.takeDisplayPath );
		json += ",\n    \"absolute_folder\": ";
		AppendJsonEscaped( json, g_ArtRecordingStats.takeAbsolutePath );
		json += ",\n    \"manifest_file\": ";
		char manifestName[96];
		Q_snprintf( manifestName, sizeof( manifestName ), "%s.json",
			g_ArtRecordingStats.takeName );
		AppendJsonEscaped( json, manifestName );
		json += "\n  },\n";

		json += "  \"source\": {\n";
		json += "    \"map\": ";
		AppendJsonEscaped( json, g_ArtRecordingStats.takeMapName );
		AppendJsonFormat( json,
			",\n    \"demo_playback\": %s,\n    \"engine_build\": %u,\n    \"game_directory\": ",
			g_ArtRecordingStats.takeDemoPlayback ? "true" : "false",
			g_ArtRecordingStats.takeEngineBuild );
		AppendJsonEscaped( json, g_ArtRecordingStats.takeGameDirectory );
		json += "\n  },\n";

		AppendJsonFormat( json,
			"  \"capture\": {\n"
			"    \"width\": %d,\n"
			"    \"height\": %d,\n"
			"    \"aspect_ratio\": %.6f,\n"
			"    \"resolution_changes\": %lu,\n"
			"    \"frame_rate\": %.6f,\n"
			"    \"frame_rate_mode\": \"%s\",\n"
			"    \"frames\": %lu,\n"
			"    \"files\": %lu,\n"
			"    \"bytes\": %I64u,\n"
			"    \"record_mask\": %ld,\n"
			"    \"hud_mask\": %ld,\n"
			"    \"depth_start\": %.6f,\n"
			"    \"depth_end\": %.6f\n"
			"  },\n",
			g_ArtRecordingStats.takeWidth, g_ArtRecordingStats.takeHeight,
			g_ArtRecordingStats.takeAspectRatio,
			g_ArtRecordingStats.takeResolutionChanges,
			g_ArtRecordingStats.takeHostFramerate,
			g_ArtRecordingStats.takeHostFramerate > 0.0f ? "fixed" : "real-time",
			g_ArtRecordingStats.takeFrames, g_ArtRecordingStats.takeFiles,
			g_ArtRecordingStats.takeBytes, g_ArtRecordingStats.takeRecordMask,
			g_ArtRecordingStats.takeHudMask,
			g_ArtRecordingStats.takeDepthStart, g_ArtRecordingStats.takeDepthEnd );
		AppendJsonFormat( json,
			"  \"camera\": {\n"
			"    \"configured\": {\n"
			"      \"global_fov_enabled\": %s,\n"
			"      \"global_fov_4_3_degrees\": %.6f,\n"
			"      \"preserve_zoom\": %s,\n"
			"      \"minimum_unzoomed_fov\": %.6f,\n"
			"      \"viewmodel_fov_4_3_degrees\": %.6f\n"
			"    },\n"
			"    \"source_engine_actual\": {\n"
			"      \"description\": \"Source stores horizontal FOV for a 4:3 frame; widescreen horizontal FOV is expanded for the recorded aspect ratio.\",\n"
			"      \"camera_base_horizontal_4_3_degrees\": %.6f,\n"
			"      \"camera_horizontal_degrees\": %.6f,\n"
			"      \"camera_vertical_degrees\": %.6f,\n"
			"      \"camera_base_range_degrees\": [%.6f, %.6f],\n"
			"      \"camera_horizontal_range_degrees\": [%.6f, %.6f],\n"
			"      \"viewmodel_base_horizontal_4_3_degrees\": %.6f,\n"
			"      \"viewmodel_horizontal_degrees\": %.6f,\n"
			"      \"viewmodel_vertical_degrees\": %.6f,\n"
			"      \"changes\": %lu\n"
			"    }\n"
			"  },\n",
			g_ArtRecordingStats.takeGlobalFovEnabled ? "true" : "false",
			g_ArtRecordingStats.takeGlobalFov,
			g_ArtRecordingStats.takeGlobalFovHandleZoom ? "true" : "false",
			g_ArtRecordingStats.takeGlobalFovMinUnzoomedFov,
			g_ArtRecordingStats.takeViewmodelFov,
			g_ArtRecordingStats.takeEngineCameraFov4x3,
			g_ArtRecordingStats.takeEngineCameraFovHorizontal,
			g_ArtRecordingStats.takeEngineCameraFovVertical,
			g_ArtRecordingStats.takeEngineCameraFov4x3Minimum,
			g_ArtRecordingStats.takeEngineCameraFov4x3Maximum,
			g_ArtRecordingStats.takeEngineCameraFovHorizontalMinimum,
			g_ArtRecordingStats.takeEngineCameraFovHorizontalMaximum,
			g_ArtRecordingStats.takeEngineViewmodelFov4x3,
			g_ArtRecordingStats.takeEngineViewmodelFovHorizontal,
			g_ArtRecordingStats.takeEngineViewmodelFovVertical,
			g_ArtRecordingStats.takeEngineFovChanges );

		json += "  \"passes\": [\n";
		for ( int i = 0; i < ART_CAPTURE_PASS_COUNT; ++i )
			AppendPassManifest( json, i );
		json += "\n  ],\n";

		AppendJsonFormat( json,
			"  \"pipeline\": {\n"
			"    \"tga_compression\": \"%s\",\n"
			"    \"queue_limits\": {\n"
			"      \"max_files\": %ld,\n"
			"      \"max_megabytes\": %ld,\n"
			"      \"reserve_megabytes\": %ld\n"
			"    },\n"
			"    \"queue_peak\": {\n"
			"      \"files\": %lu,\n"
			"      \"bytes\": %I64u\n"
			"    },\n"
			"    \"flushes\": %lu,\n"
			"    \"allocation_retries\": %lu,\n"
			"    \"allocation_failures\": %lu,\n"
			"    \"uncompressed_bytes\": %I64u,\n"
			"    \"output_bytes\": %I64u,\n"
			"    \"timings\": {\n",
			ArtTgaCompressionModeName( g_ArtRecordingStats.takeTgaCompressionMode ),
			g_ArtRecordingStats.takeQueueMaxFiles,
			g_ArtRecordingStats.takeQueueMaxMegabytes,
			g_ArtRecordingStats.takeQueueReserveMegabytes,
			g_ArtPipelineStats.takePeakFiles, g_ArtPipelineStats.takePeakBytes,
			g_ArtPipelineStats.takeFlushes, g_ArtPipelineStats.takeAllocationRetries,
			g_ArtPipelineStats.takeAllocationFailures,
			g_ArtPipelineStats.takeUncompressedBytes, g_ArtPipelineStats.takeOutputBytes );
		static const char *timingNames[ART_TIMING_COUNT] =
			{ "render", "read", "encode", "write", "queue" };
		for ( int i = 0; i < ART_TIMING_COUNT; ++i )
		{
			if ( i )
				json += ",\n";
			json += "      ";
			const ArtStageTimingStatistics &timing = g_ArtPipelineStats.stages[i];
			AppendJsonEscaped( json, timingNames[i] );
			AppendJsonFormat( json,
				": {\n"
				"        \"total_microseconds\": %I64u,\n"
				"        \"samples\": %lu,\n"
				"        \"average_microseconds\": %.3f,\n"
				"        \"maximum_microseconds\": %lu\n"
				"      }",
				timing.takeTotalMicroseconds, timing.takeSamples,
				timing.takeSamples ? static_cast<double>( timing.takeTotalMicroseconds ) /
					timing.takeSamples : 0.0,
				timing.takeMaxMicroseconds );
		}
		json += "\n    }\n  },\n";

		AppendJsonFormat( json,
			"  \"validation\": {\n"
			"    \"automatic\": %s,\n"
			"    \"file_size_check\": %s,\n"
			"    \"dropped_frames_check\": %s,\n"
			"    \"minimum_file_bytes\": %ld",
			InterlockedCompareExchange( &g_ArtValidationOptions.autoValidate, 0, 0 ) ? "true" : "false",
			InterlockedCompareExchange( &g_ArtValidationOptions.checkFileSize, 0, 0 ) ? "true" : "false",
			InterlockedCompareExchange( &g_ArtValidationOptions.checkDroppedFrames, 0, 0 ) ? "true" : "false",
			InterlockedCompareExchange( &g_ArtValidationOptions.minimumFileBytes, 0, 0 ) );
		if ( g_ArtValidationResult.hasResult )
		{
			AppendJsonFormat( json,
				",\n    \"result\": \"%s\",\n"
				"    \"scanned_files\": %lu,\n"
				"    \"expected_files\": %lu,\n"
				"    \"missing_files\": %lu,\n"
				"    \"unexpected_files\": %lu,\n"
				"    \"sequence_gaps\": %lu,\n"
				"    \"invalid_headers\": %lu,\n"
				"    \"invalid_pixel_data\": %lu,\n"
				"    \"inconsistent_dimensions\": %lu",
				g_ArtValidationResult.passed ? "pass" : "fail",
				g_ArtValidationResult.scannedFiles, g_ArtValidationResult.expectedFiles,
				g_ArtValidationResult.missingFiles, g_ArtValidationResult.unexpectedFiles,
				g_ArtValidationResult.sequenceGaps, g_ArtValidationResult.invalidHeaders,
				g_ArtValidationResult.invalidPixelData,
				g_ArtValidationResult.inconsistentDimensions );
		}
		json += "\n  }\n}\n";

		char temporaryPath[MAX_PATH];
		const int temporaryWritten = Q_snprintf( temporaryPath, sizeof( temporaryPath ),
			"%s.tmp", manifestPath );
		if ( temporaryWritten < 0 || temporaryWritten >= sizeof( temporaryPath ) )
			return false;
		HANDLE hFile = CreateFileA( temporaryPath, GENERIC_WRITE, 0, NULL,
			CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL );
		if ( hFile == INVALID_HANDLE_VALUE )
		{
			LogMessage( "TAKE JSON: unable to open temporary manifest path='%s' error=%lu",
				temporaryPath, GetLastError() );
			return false;
		}
		DWORD written = 0;
		const BOOL writeOk = json.size() <= 0xFFFFFFFFULL &&
			WriteFile( hFile, json.data(), static_cast<DWORD>( json.size() ),
				&written, NULL ) &&
			written == json.size();
		const BOOL flushOk = writeOk && FlushFileBuffers( hFile );
		CloseHandle( hFile );
		if ( !writeOk || !flushOk ||
			!MoveFileExA( temporaryPath, manifestPath,
				MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH ) )
		{
			const DWORD error = GetLastError();
			DeleteFileA( temporaryPath );
			LogMessage( "TAKE JSON: write failed path='%s' bytes=%Iu error=%lu",
				manifestPath, json.size(), error );
			return false;
		}
		LogMessage( "TAKE JSON: wrote path='%s' bytes=%Iu status='%s'",
			manifestPath, json.size(), g_ArtRecordingStats.takeActive ? "recording" :
			g_ArtRecordingStats.takeAborted ? "aborted" : "completed" );
		return true;
	}

	void BeginArtRecordingStatistics( LONG recordMask, LONG hudMask )
	{
		ResetArtPipelineTakeStatistics();
		for ( int i = 0; i < ART_CAPTURE_PASS_COUNT; ++i )
		{
			g_ExpectedFrames[i].clear();
			g_ArtRecordingStats.passes[i].files = 0;
			g_ArtRecordingStats.passes[i].bytes = 0;
		}
		g_bExpectedFrameHistoryAvailable = true;
		g_ArtRecordingStats.takeFrames = 0;
		g_ArtRecordingStats.takeFiles = 0;
		g_ArtRecordingStats.takeBytes = 0;
		g_ArtRecordingStats.takeStartTick = GetTickCount();
		g_ArtRecordingStats.takeElapsedMs = 0;
		g_ArtRecordingStats.takeRecordMask = recordMask;
		g_ArtRecordingStats.takeHudMask = hudMask;
		g_ArtRecordingStats.takeWidth = 0;
		g_ArtRecordingStats.takeHeight = 0;
		g_ArtRecordingStats.takeResolutionChanges = 0;
		g_LastCaptureWidth = 0;
		g_LastCaptureHeight = 0;
		g_LastFovFrame = -1;
		g_LastCameraFov = 0.0f;
		g_LastViewmodelFov = 0.0f;
		g_LastFovAspectRatio = 0.0f;
		ConVar *pHostFramerate = g_pCvar ? g_pCvar->FindVar( "host_framerate" ) : NULL;
		g_ArtRecordingStats.takeHostFramerate =
			pHostFramerate ? pHostFramerate->GetFloat() : 0.0f;
		g_ArtRecordingStats.takeGlobalFovEnabled =
			InterlockedCompareExchange( &g_bGlobalFovOverride, 0, 0 ) != FALSE;
		g_ArtRecordingStats.takeGlobalFov = g_flGlobalFov;
		g_ArtRecordingStats.takeGlobalFovHandleZoom =
			InterlockedCompareExchange( &g_bGlobalFovHandleZoom, 0, 0 ) != FALSE;
		g_ArtRecordingStats.takeGlobalFovMinUnzoomedFov =
			g_flGlobalFovMinUnzoomedFov;
		g_ArtRecordingStats.takeViewmodelFov = g_flViewmodelFov;
		g_ArtRecordingStats.takeAspectRatio = 0.0f;
		g_ArtRecordingStats.takeEngineCameraFov4x3 = 0.0f;
		g_ArtRecordingStats.takeEngineCameraFovHorizontal = 0.0f;
		g_ArtRecordingStats.takeEngineCameraFovVertical = 0.0f;
		g_ArtRecordingStats.takeEngineCameraFov4x3Minimum = 0.0f;
		g_ArtRecordingStats.takeEngineCameraFov4x3Maximum = 0.0f;
		g_ArtRecordingStats.takeEngineCameraFovHorizontalMinimum = 0.0f;
		g_ArtRecordingStats.takeEngineCameraFovHorizontalMaximum = 0.0f;
		g_ArtRecordingStats.takeEngineViewmodelFov4x3 = 0.0f;
		g_ArtRecordingStats.takeEngineViewmodelFovHorizontal = 0.0f;
		g_ArtRecordingStats.takeEngineViewmodelFovVertical = 0.0f;
		g_ArtRecordingStats.takeEngineFovChanges = 0;
		g_ArtRecordingStats.takeDepthStart = art_depth_start.GetFloat();
		g_ArtRecordingStats.takeDepthEnd = art_depth_end.GetFloat();
		g_ArtRecordingStats.takeViewmodelColor[0] = g_nViewmodelBackgroundRed;
		g_ArtRecordingStats.takeViewmodelColor[1] = g_nViewmodelBackgroundGreen;
		g_ArtRecordingStats.takeViewmodelColor[2] = g_nViewmodelBackgroundBlue;
		g_ArtRecordingStats.takePlayersColor[0] = g_nPlayersBackgroundRed;
		g_ArtRecordingStats.takePlayersColor[1] = g_nPlayersBackgroundGreen;
		g_ArtRecordingStats.takePlayersColor[2] = g_nPlayersBackgroundBlue;
		GetArtObjectIdCategoryColors( g_ArtRecordingStats.takeObjectIdColors );
		g_ArtRecordingStats.takeTgaCompressionMode =
			InterlockedCompareExchange( &g_nArtTgaCompressionMode, 0, 0 );
		g_ArtRecordingStats.takeQueueMaxFiles =
			InterlockedCompareExchange( &g_ArtQueueOptions.maxFiles, 0, 0 );
		g_ArtRecordingStats.takeQueueMaxMegabytes =
			InterlockedCompareExchange( &g_ArtQueueOptions.maxMegabytes, 0, 0 );
		g_ArtRecordingStats.takeQueueReserveMegabytes =
			InterlockedCompareExchange( &g_ArtQueueOptions.reserveMegabytes, 0, 0 );
		g_ArtRecordingStats.takeEngineBuild = 4044;
		g_ArtRecordingStats.takeDemoPlayback =
			g_pEngine && g_pEngine->IsPlayingDemo();
		g_ArtRecordingStats.takeManifestEnabled =
			InterlockedCompareExchange( &g_bArtTakeManifestEnabled, 0, 0 ) != FALSE;
		g_ArtRecordingStats.takeActive = true;
		g_ArtRecordingStats.takeAborted = false;
		GetSystemTime( &g_ArtRecordingStats.takeStartedUtc );
		ZeroMemory( &g_ArtRecordingStats.takeFinishedUtc,
			sizeof( g_ArtRecordingStats.takeFinishedUtc ) );
		FormatTakeDisplayPath( g_ArtRecordingStats.takeDisplayPath,
			sizeof( g_ArtRecordingStats.takeDisplayPath ) );
		BuildTakeAbsolutePath( g_ArtRecordingStats.takeAbsolutePath,
			sizeof( g_ArtRecordingStats.takeAbsolutePath ) );
		CopyTakeName( g_ArtRecordingStats.takeAbsolutePath,
			g_ArtRecordingStats.takeName, sizeof( g_ArtRecordingStats.takeName ) );
		CopyMapName( g_pEngine ? g_pEngine->GetLevelName() : NULL,
			g_ArtRecordingStats.takeMapName, sizeof( g_ArtRecordingStats.takeMapName ) );
		Q_strncpy( g_ArtRecordingStats.takeGameDirectory,
			g_pEngine ? g_pEngine->GetGameDirectory() : "",
			sizeof( g_ArtRecordingStats.takeGameDirectory ) );
		Q_strncpy( g_ArtRecordingStats.takePrefix, g_szCapturePrefix,
			sizeof( g_ArtRecordingStats.takePrefix ) );
		++g_ArtRecordingStats.sessionTakesStarted;
		ZeroMemory( &g_ArtValidationResult, sizeof( g_ArtValidationResult ) );
		WriteArtTakeManifest( false );
	}

	void FinishArtRecordingStatistics( bool aborted )
	{
		if ( !g_ArtRecordingStats.takeActive )
			return;
		g_ArtRecordingStats.takeElapsedMs = GetTickCount() - g_ArtRecordingStats.takeStartTick;
		g_ArtRecordingStats.takeActive = false;
		g_ArtRecordingStats.takeAborted = aborted;
		GetSystemTime( &g_ArtRecordingStats.takeFinishedUtc );
		if ( aborted )
			++g_ArtRecordingStats.sessionTakesAborted;
		else
			++g_ArtRecordingStats.sessionTakesCompleted;
		WriteArtTakeManifest( false );
	}

	void RecordArtCapturedFile( const char *pPassName, int frame, unsigned long bytes,
		int width, int height, float cameraFov, float viewmodelFov )
	{
		const int passIndex = FindPassIndex( pPassName );
		if ( passIndex < 0 )
			return;
		++g_ArtRecordingStats.takeFiles;
		g_ArtRecordingStats.takeBytes += bytes;
		++g_ArtRecordingStats.sessionFiles;
		g_ArtRecordingStats.sessionBytes += bytes;
		++g_ArtRecordingStats.passes[passIndex].files;
		g_ArtRecordingStats.passes[passIndex].bytes += bytes;
		g_ExpectedFrames[passIndex].push_back( frame );
		if ( width > 0 && height > 0 )
		{
			if ( !g_ArtRecordingStats.takeWidth || !g_ArtRecordingStats.takeHeight )
			{
				g_ArtRecordingStats.takeWidth = width;
				g_ArtRecordingStats.takeHeight = height;
			}
			else if ( g_LastCaptureWidth &&
				( width != g_LastCaptureWidth || height != g_LastCaptureHeight ) )
				++g_ArtRecordingStats.takeResolutionChanges;
			g_LastCaptureWidth = width;
			g_LastCaptureHeight = height;
		}
		if ( frame != g_LastFovFrame && width > 0 && height > 0 )
		{
			const float aspectRatio = static_cast<float>( width ) / height;
			const float cameraHorizontal =
				logic::CalculateWidescreenHorizontalFov( cameraFov, aspectRatio );
			const float cameraVertical =
				logic::CalculateVerticalFov( cameraHorizontal, aspectRatio );
			const float viewmodelHorizontal =
				logic::CalculateWidescreenHorizontalFov( viewmodelFov, aspectRatio );
			const float viewmodelVertical =
				logic::CalculateVerticalFov( viewmodelHorizontal, aspectRatio );
			if ( g_LastFovFrame < 0 )
			{
				g_ArtRecordingStats.takeEngineCameraFov4x3Minimum = cameraFov;
				g_ArtRecordingStats.takeEngineCameraFov4x3Maximum = cameraFov;
				g_ArtRecordingStats.takeEngineCameraFovHorizontalMinimum = cameraHorizontal;
				g_ArtRecordingStats.takeEngineCameraFovHorizontalMaximum = cameraHorizontal;
			}
			else
			{
				if ( fabsf( cameraFov - g_LastCameraFov ) > 0.001f ||
					fabsf( viewmodelFov - g_LastViewmodelFov ) > 0.001f ||
					fabsf( aspectRatio - g_LastFovAspectRatio ) > 0.0001f )
					++g_ArtRecordingStats.takeEngineFovChanges;
				if ( cameraFov < g_ArtRecordingStats.takeEngineCameraFov4x3Minimum )
					g_ArtRecordingStats.takeEngineCameraFov4x3Minimum = cameraFov;
				if ( cameraFov > g_ArtRecordingStats.takeEngineCameraFov4x3Maximum )
					g_ArtRecordingStats.takeEngineCameraFov4x3Maximum = cameraFov;
				if ( cameraHorizontal < g_ArtRecordingStats.takeEngineCameraFovHorizontalMinimum )
					g_ArtRecordingStats.takeEngineCameraFovHorizontalMinimum = cameraHorizontal;
				if ( cameraHorizontal > g_ArtRecordingStats.takeEngineCameraFovHorizontalMaximum )
					g_ArtRecordingStats.takeEngineCameraFovHorizontalMaximum = cameraHorizontal;
			}
			g_ArtRecordingStats.takeAspectRatio = aspectRatio;
			g_ArtRecordingStats.takeEngineCameraFov4x3 = cameraFov;
			g_ArtRecordingStats.takeEngineCameraFovHorizontal = cameraHorizontal;
			g_ArtRecordingStats.takeEngineCameraFovVertical = cameraVertical;
			g_ArtRecordingStats.takeEngineViewmodelFov4x3 = viewmodelFov;
			g_ArtRecordingStats.takeEngineViewmodelFovHorizontal = viewmodelHorizontal;
			g_ArtRecordingStats.takeEngineViewmodelFovVertical = viewmodelVertical;
			g_LastFovFrame = frame;
			g_LastCameraFov = cameraFov;
			g_LastViewmodelFov = viewmodelFov;
			g_LastFovAspectRatio = aspectRatio;
		}
	}

	void RecordArtCompletedFrame()
	{
		++g_ArtRecordingStats.takeFrames;
		++g_ArtRecordingStats.sessionFrames;
	}

	DWORD GetArtRecordingElapsedMs()
	{
		if ( g_ArtRecordingStats.takeActive )
			return GetTickCount() - g_ArtRecordingStats.takeStartTick;
		return g_ArtRecordingStats.takeElapsedMs;
	}

	void FormatArtByteCount( unsigned __int64 bytes, char *pOutput, size_t outputBytes )
	{
		if ( !pOutput || !outputBytes )
			return;
		const double value = bytes >= 1024ULL * 1024ULL * 1024ULL ?
			static_cast<double>( bytes ) / ( 1024.0 * 1024.0 * 1024.0 ) :
			bytes >= 1024ULL * 1024ULL ?
			static_cast<double>( bytes ) / ( 1024.0 * 1024.0 ) :
			bytes >= 1024ULL ? static_cast<double>( bytes ) / 1024.0 : static_cast<double>( bytes );
		const char *pUnit = bytes >= 1024ULL * 1024ULL * 1024ULL ? "GiB" :
			bytes >= 1024ULL * 1024ULL ? "MiB" : bytes >= 1024ULL ? "KiB" : "B";
		Q_snprintf( pOutput, static_cast<int>( outputBytes ), "%.2f %s", value, pUnit );
	}

	void PrintArtRecordingStatistics()
	{
		char takeBytes[48];
		char sessionBytes[48];
		FormatArtByteCount( g_ArtRecordingStats.takeBytes, takeBytes, sizeof( takeBytes ) );
		FormatArtByteCount( g_ArtRecordingStats.sessionBytes, sessionBytes, sizeof( sessionBytes ) );
		ArtConsoleMessage( "ART recording statistics:\n" );
		ArtConsoleMessage( "  current/latest take: %s\n",
			g_ArtRecordingStats.takeDisplayPath[0] ? g_ArtRecordingStats.takeDisplayPath : "<none recorded this session>" );
		ArtConsoleMessage( "  state: %s%s; frames: %lu; files: %lu; encoded size: %s; elapsed: %.2f s\n",
			g_ArtRecordingStats.takeActive ? "recording" : "idle",
			g_ArtRecordingStats.takeAborted ? " (aborted)" : "",
			g_ArtRecordingStats.takeFrames, g_ArtRecordingStats.takeFiles, takeBytes,
			GetArtRecordingElapsedMs() / 1000.0f );
		ArtConsoleMessage( "  capture: %dx%d; host_framerate %g; resolution changes %lu; manifest %s\n",
			g_ArtRecordingStats.takeWidth, g_ArtRecordingStats.takeHeight,
			g_ArtRecordingStats.takeHostFramerate,
			g_ArtRecordingStats.takeResolutionChanges,
			g_ArtRecordingStats.takeManifestEnabled ? "enabled" : "disabled" );
		ArtConsoleMessage( "  Viewmodel RGB: %d %d %d; Players RGB: %d %d %d\n",
			g_ArtRecordingStats.takeViewmodelColor[0],
			g_ArtRecordingStats.takeViewmodelColor[1],
			g_ArtRecordingStats.takeViewmodelColor[2],
			g_ArtRecordingStats.takePlayersColor[0],
			g_ArtRecordingStats.takePlayersColor[1],
			g_ArtRecordingStats.takePlayersColor[2] );
		for ( int i = 0; i < ART_CAPTURE_PASS_COUNT; ++i )
		{
			char passBytes[48];
			FormatArtByteCount( g_ArtRecordingStats.passes[i].bytes, passBytes, sizeof( passBytes ) );
			ArtConsoleMessage( "  %-17s %lu files, %s\n", kPasses[i].name,
				g_ArtRecordingStats.passes[i].files, passBytes );
		}
		ArtConsoleMessage( "  session totals: %lu started, %lu completed, %lu aborted; %lu frames; %lu files; %s\n",
			g_ArtRecordingStats.sessionTakesStarted, g_ArtRecordingStats.sessionTakesCompleted,
			g_ArtRecordingStats.sessionTakesAborted, g_ArtRecordingStats.sessionFrames,
			g_ArtRecordingStats.sessionFiles, sessionBytes );
		PrintArtPipelineStatistics();
	}

	bool RunArtValidation()
	{
		if ( InterlockedCompareExchange( &g_bRecording, FALSE, FALSE ) )
		{
			ArtConsoleMessage( "art_validation: stop recording before validating the take.\n" );
			return false;
		}
		if ( !g_ArtRecordingStats.takeAbsolutePath[0] )
		{
			ArtConsoleMessage( "art_validation: no take has been recorded during this ART session.\n" );
			return false;
		}
		if ( InterlockedCompareExchange(
			&g_ArtValidationProgress.running, TRUE, FALSE ) != FALSE )
		{
			ArtConsoleMessage( "art_validation: already running (%ld/%ld files, %s).\n",
				InterlockedCompareExchange( &g_ArtValidationProgress.completedFiles, 0, 0 ),
				InterlockedCompareExchange( &g_ArtValidationProgress.totalFiles, 0, 0 ),
				ArtValidationPhaseName( InterlockedCompareExchange(
					&g_ArtValidationProgress.phase, 0, 0 ) ) );
			return false;
		}

		ValidationSnapshot *pSnapshot = new ( std::nothrow ) ValidationSnapshot;
		if ( !pSnapshot )
		{
			InterlockedExchange( &g_ArtValidationProgress.running, FALSE );
			ArtConsoleMessage( "art_validation: unable to allocate validation job.\n" );
			return false;
		}
		pSnapshot->checkFileSize = InterlockedCompareExchange(
			&g_ArtValidationOptions.checkFileSize, FALSE, FALSE ) != FALSE;
		pSnapshot->checkDroppedFrames = InterlockedCompareExchange(
			&g_ArtValidationOptions.checkDroppedFrames, FALSE, FALSE ) != FALSE;
		pSnapshot->minimumFileBytes = static_cast<unsigned long>(
			InterlockedCompareExchange( &g_ArtValidationOptions.minimumFileBytes, 0, 0 ) );
		pSnapshot->expectedFrameHistoryAvailable = g_bExpectedFrameHistoryAvailable;
		Q_strncpy( pSnapshot->takeDisplayPath, g_ArtRecordingStats.takeDisplayPath,
			sizeof( pSnapshot->takeDisplayPath ) );
		Q_strncpy( pSnapshot->takeAbsolutePath, g_ArtRecordingStats.takeAbsolutePath,
			sizeof( pSnapshot->takeAbsolutePath ) );
		Q_strncpy( pSnapshot->takePrefix, g_ArtRecordingStats.takePrefix,
			sizeof( pSnapshot->takePrefix ) );
		for ( int i = 0; i < ART_CAPTURE_PASS_COUNT; ++i )
			pSnapshot->expectedFrames[i] = g_ExpectedFrames[i];

		ZeroMemory( &g_ArtValidationResult, sizeof( g_ArtValidationResult ) );
		InterlockedExchange( &g_ArtValidationProgress.phase, ART_VALIDATION_DISCOVERING );
		InterlockedExchange( &g_ArtValidationProgress.completedFiles, 0 );
		InterlockedExchange( &g_ArtValidationProgress.totalFiles, 0 );
		InterlockedExchange( &g_ArtValidationProgress.completionPending, FALSE );
		g_ArtValidationProgress.startTick = GetTickCount();
		HANDLE hThread = CreateThread( NULL, 0, ArtValidationWorker, pSnapshot, 0, NULL );
		if ( !hThread )
		{
			const DWORD error = GetLastError();
			delete pSnapshot;
			InterlockedExchange( &g_ArtValidationProgress.phase, ART_VALIDATION_IDLE );
			InterlockedExchange( &g_ArtValidationProgress.running, FALSE );
			ArtConsoleMessage( "art_validation: unable to start background validation (Win32 error %lu).\n",
				error );
			return false;
		}
		CloseHandle( hThread );
		ArtConsoleMessage( "art_validation: background validation started for %s.\n",
			g_ArtRecordingStats.takeDisplayPath );
		return true;
	}

	void RunAutomaticArtValidation()
	{
		if ( !InterlockedCompareExchange( &g_ArtValidationOptions.autoValidate, FALSE, FALSE ) )
			return;
		ArtConsoleMessage( "art_validation: automatically validating completed take...\n" );
		RunArtValidation();
	}

	bool IsArtValidationRunning()
	{
		return InterlockedCompareExchange(
			&g_ArtValidationProgress.running, FALSE, FALSE ) != FALSE;
	}

	const char *ArtValidationPhaseName( LONG phase )
	{
		switch ( phase )
		{
		case ART_VALIDATION_DISCOVERING: return "discovering files";
		case ART_VALIDATION_CHECKING_FILES: return "checking TGA data";
		case ART_VALIDATION_FINALIZING: return "finalizing";
		default: return "idle";
		}
	}

	float GetArtValidationProgressFraction()
	{
		const LONG total = InterlockedCompareExchange(
			&g_ArtValidationProgress.totalFiles, 0, 0 );
		const LONG completed = InterlockedCompareExchange(
			&g_ArtValidationProgress.completedFiles, 0, 0 );
		if ( total <= 0 )
			return 0.0f;
		const float fraction = static_cast<float>( completed ) / total;
		return fraction < 0.0f ? 0.0f : fraction > 1.0f ? 1.0f : fraction;
	}

	DWORD GetArtValidationElapsedMs()
	{
		if ( !g_ArtValidationProgress.startTick )
			return 0;
		return GetTickCount() - g_ArtValidationProgress.startTick;
	}

	void PublishArtValidationCompletion()
	{
		if ( IsArtValidationRunning() )
			return;
		if ( InterlockedExchange(
			&g_ArtValidationProgress.completionPending, FALSE ) )
		{
			WriteArtTakeManifest( false );
			PrintArtValidationResult();
		}
	}

	void PrintArtValidationResult()
	{
		if ( IsArtValidationRunning() )
		{
			ArtConsoleMessage( "art_validation: %s - %ld/%ld files (%.1f%%, %.2f s).\n",
				ArtValidationPhaseName( InterlockedCompareExchange(
					&g_ArtValidationProgress.phase, 0, 0 ) ),
				InterlockedCompareExchange( &g_ArtValidationProgress.completedFiles, 0, 0 ),
				InterlockedCompareExchange( &g_ArtValidationProgress.totalFiles, 0, 0 ),
				GetArtValidationProgressFraction() * 100.0f,
				GetArtValidationElapsedMs() / 1000.0f );
			return;
		}
		InterlockedExchange( &g_ArtValidationProgress.completionPending, FALSE );
		if ( !g_ArtValidationResult.hasResult )
		{
			ArtConsoleMessage( "art_validation: no validation result is available.\n" );
			return;
		}
		char totalBytes[48];
		char smallestBytes[48];
		char largestBytes[48];
		FormatArtByteCount( g_ArtValidationResult.totalBytes, totalBytes, sizeof( totalBytes ) );
		FormatArtByteCount( g_ArtValidationResult.smallestFileBytes, smallestBytes, sizeof( smallestBytes ) );
		FormatArtByteCount( g_ArtValidationResult.largestFileBytes, largestBytes, sizeof( largestBytes ) );
		ArtConsoleMessage( "art_validation: %s - %s\n",
			g_ArtValidationResult.passed ? "PASS" : "FAIL", g_ArtValidationResult.takePath );
		ArtConsoleMessage( "  files: %lu scanned / %lu expected; size: %s; range: %s to %s\n",
			g_ArtValidationResult.scannedFiles, g_ArtValidationResult.expectedFiles,
			totalBytes, smallestBytes, largestBytes );
		ArtConsoleMessage( "  missing: %lu; unexpected: %lu; inferred gaps: %lu; undersized: %lu\n",
			g_ArtValidationResult.missingFiles, g_ArtValidationResult.unexpectedFiles,
			g_ArtValidationResult.sequenceGaps, g_ArtValidationResult.undersizedFiles );
		ArtConsoleMessage( "  invalid TGA headers: %lu; damaged pixel data: %lu; inconsistent dimensions: %lu; directory errors: %lu\n",
			g_ArtValidationResult.invalidHeaders, g_ArtValidationResult.invalidPixelData,
			g_ArtValidationResult.inconsistentDimensions, g_ArtValidationResult.directoryErrors );
	}
}
