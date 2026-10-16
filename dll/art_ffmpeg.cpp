// Direct FFmpeg video encoding pipeline and Win32 streaming pipes.

#include "art_ffmpeg.h"
#include "art_internal.h"
#include <string>
#include <vector>

// memdbgon must be the last include file in a .cpp file.
#include "tier0/memdbgon.h"

namespace art
{
	volatile LONG g_nArtOutputMode = ART_OUTPUT_MODE_TGA;
	volatile LONG g_nArtFfmpegPreset = ART_FFMPEG_PRESET_PRORES_422;
	char g_szArtFfmpegPath[MAX_PATH] = "ffmpeg.exe";
	char g_szArtFfmpegCustomArgs[1024] =
		"-y -f rawvideo -pix_fmt rgb24 -s {WIDTH}x{HEIGHT} -r {FPS} -i - -c:v prores_ks -profile:v 3 -pix_fmt yuv422p10le \"{OUTPUT_FILE}\"";

	volatile LONG g_ArtFfmpegOptions[ART_FFOPT_COUNT] =
	{
		16,	// x264 crf
		3,	// x264 speed ("faster")
		0,	// x264 tune ("none")
		9,	// nvenc h264 cq
		4,	// nvenc h264 preset ("p5")
		0,	// nvenc h264 tune
		18,	// nvenc hevc cq
		3,	// nvenc hevc preset ("p4")
		0,	// nvenc hevc tune
		3,	// prores 422 profile ("hq")
		0	// prores 4444 profile ("4444")
	};

	namespace
	{
		static const ArtFfmpegOptionInfo s_Options[ART_FFOPT_COUNT] =
		{
			{ ART_FFOPT_X264_CRF, "x264_crf", 0, 51, 16 },
			{ ART_FFOPT_X264_SPEED, "x264_speed", 0, ARRAYSIZE( kArtX264Speeds ) - 1, 3 },
			{ ART_FFOPT_X264_TUNE, "x264_tune", 0, ARRAYSIZE( kArtX264Tunes ) - 1, 0 },
			{ ART_FFOPT_NVH264_CQ, "nvh264_cq", 0, 51, 9 },
			{ ART_FFOPT_NVH264_PRESET, "nvh264_preset", 0, ARRAYSIZE( kArtNvencPresets ) - 1, 4 },
			{ ART_FFOPT_NVH264_TUNE, "nvh264_tune", 0, ARRAYSIZE( kArtNvencTunes ) - 1, 0 },
			{ ART_FFOPT_NVHEVC_CQ, "nvhevc_cq", 0, 51, 18 },
			{ ART_FFOPT_NVHEVC_PRESET, "nvhevc_preset", 0, ARRAYSIZE( kArtNvencPresets ) - 1, 3 },
			{ ART_FFOPT_NVHEVC_TUNE, "nvhevc_tune", 0, ARRAYSIZE( kArtNvencTunes ) - 1, 0 },
			{ ART_FFOPT_PRORES422_PROFILE, "prores422_profile", 0, ARRAYSIZE( kArtProres422Profiles ) - 1, 3 },
			{ ART_FFOPT_PRORES4444_PROFILE, "prores4444_profile", 0, ARRAYSIZE( kArtProres4444Profiles ) - 1, 0 }
		};

		static const ArtFfmpegPresetInfo s_Presets[] =
		{
			{
				ART_FFMPEG_PRESET_PRORES_422,
				"prores_422",
				"ProRes 422 HQ (.mov) - AE Recommended",
				"mov",
				"-y -f rawvideo -pix_fmt rgb24 -s {WIDTH}x{HEIGHT} -r {FPS} -i - -c:v prores_ks -profile:v 3 -pix_fmt yuv422p10le \"{OUTPUT_FILE}\"",
				"High-quality 10-bit editing codec. Fast encoding, visually lossless, ideal for After Effects."
			},
			{
				ART_FFMPEG_PRESET_PRORES_4444,
				"prores_4444",
				"ProRes 4444 (.mov) - 4:4:4 Maximum Quality",
				"mov",
				"-y -f rawvideo -pix_fmt rgb24 -s {WIDTH}x{HEIGHT} -r {FPS} -i - -c:v prores_ks -profile:v 4 -pix_fmt yuv444p10le \"{OUTPUT_FILE}\"",
				"Full 4:4:4 chroma sampling for highest color fidelity and compositing precision."
			},
			{
				ART_FFMPEG_PRESET_H264,
				"h264",
				"x264 (.mp4) - adjustable CRF / tune",
				"mp4",
				"",
				"Software H.264. CRF 0 is mathematically lossless (4:4:4); higher CRF means smaller files. Tune and speed are configurable."
			},
			{
				ART_FFMPEG_PRESET_NVENC_H264,
				"nvenc_h264",
				"NVIDIA NVENC H.264 (.mp4) - GPU, adjustable CQ",
				"mp4",
				"-y -f rawvideo -pix_fmt rgb24 -s {WIDTH}x{HEIGHT} -r {FPS} -i - -c:v h264_nvenc -preset p5 -cq 9 -pix_fmt yuv420p \"{OUTPUT_FILE}\"",
				"Hardware-accelerated NVIDIA GPU encoding with constant-quality (CQ) rate control. Near-zero CPU load."
			},
			{
				ART_FFMPEG_PRESET_NVENC_HEVC,
				"nvenc_hevc",
				"NVIDIA NVENC HEVC / H.265 (.mp4) - GPU, adjustable CQ",
				"mp4",
				"-y -f rawvideo -pix_fmt rgb24 -s {WIDTH}x{HEIGHT} -r {FPS} -i - -c:v hevc_nvenc -preset p4 -cq 18 -pix_fmt yuv420p \"{OUTPUT_FILE}\"",
				"High-efficiency H.265 NVIDIA GPU hardware encoding with constant-quality (CQ) rate control."
			},
			{
				ART_FFMPEG_PRESET_CUSTOM,
				"custom",
				"Custom Arguments (Configured below)",
				"mp4",
				"",
				"User-defined FFmpeg arguments template."
			}
		};

		ArtFfmpegPipe s_Pipes[ART_CAPTURE_PASS_COUNT] = {};

		bool FileExistsOnDisk( const char *pPath )
		{
			if ( !pPath || !pPath[0] )
				return false;
			const DWORD attributes = GetFileAttributesA( pPath );
			return attributes != INVALID_FILE_ATTRIBUTES && !( attributes & FILE_ATTRIBUTE_DIRECTORY );
		}

		void GetParentDirectory( const char *pFullPath, char *pOutDir, size_t outSize )
		{
			if ( !pFullPath || !pOutDir || outSize == 0 )
				return;
			Q_strncpy( pOutDir, pFullPath, static_cast<int>( outSize ) );
			char *pLastSlash = strrchr( pOutDir, '\\' );
			char *pLastForward = strrchr( pOutDir, '/' );
			char *pSep = pLastSlash > pLastForward ? pLastSlash : pLastForward;
			if ( pSep )
				*pSep = '\0';
			else
				pOutDir[0] = '\0';
		}

		void ReplaceSubstring( std::string &subject, const std::string &search, const std::string &replace )
		{
			if ( search.empty() )
				return;
			size_t pos = 0;
			while ( ( pos = subject.find( search, pos ) ) != std::string::npos )
			{
				subject.replace( pos, search.length(), replace );
				pos += replace.length();
			}
		}
	}

	const ArtFfmpegPresetInfo *GetArtFfmpegPresetInfo( int presetIndex )
	{
		if ( presetIndex >= 0 && presetIndex < ARRAYSIZE( s_Presets ) )
			return &s_Presets[presetIndex];
		return &s_Presets[0];
	}

	int GetArtFfmpegPresetCount()
	{
		return ARRAYSIZE( s_Presets );
	}

	const char *GetArtOutputModeName( LONG mode )
	{
		return mode == ART_OUTPUT_MODE_FFMPEG ? "ffmpeg" : "tga";
	}

	LONG ArtOutputModeFromName( const char *pName )
	{
		if ( !pName )
			return ART_OUTPUT_MODE_TGA;
		if ( !Q_stricmp( pName, "ffmpeg" ) || !Q_stricmp( pName, "video" ) || !Q_stricmp( pName, "stream" ) )
			return ART_OUTPUT_MODE_FFMPEG;
		return ART_OUTPUT_MODE_TGA;
	}

	const char *GetArtFfmpegPresetName( LONG preset )
	{
		if ( preset >= 0 && preset < ARRAYSIZE( s_Presets ) )
			return s_Presets[preset].name;
		return s_Presets[0].name;
	}

	bool TryParseArtFfmpegPresetName( const char *pName, LONG *pPresetOut )
	{
		if ( !pName || !pName[0] )
			return false;
		LONG result = -1;
		for ( int i = 0; i < ARRAYSIZE( s_Presets ); ++i )
		{
			if ( !Q_stricmp( pName, s_Presets[i].name ) )
				result = s_Presets[i].id;
		}
		if ( result < 0 )
		{
			if ( !Q_stricmp( pName, "prores" ) || !Q_stricmp( pName, "prores422" ) )
				result = ART_FFMPEG_PRESET_PRORES_422;
			else if ( !Q_stricmp( pName, "prores4444" ) )
				result = ART_FFMPEG_PRESET_PRORES_4444;
			else if ( !Q_stricmp( pName, "x264" ) || !Q_stricmp( pName, "mp4" ) ||
				!Q_stricmp( pName, "h264_hq" ) || !Q_stricmp( pName, "x264_hq" ) ||
				!Q_stricmp( pName, "h264_lossless" ) || !Q_stricmp( pName, "x264_lossless" ) )
				result = ART_FFMPEG_PRESET_H264;
			else if ( !Q_stricmp( pName, "nvenc" ) || !Q_stricmp( pName, "nvenc264" ) )
				result = ART_FFMPEG_PRESET_NVENC_H264;
			else if ( !Q_stricmp( pName, "hevc" ) || !Q_stricmp( pName, "nvenc265" ) )
				result = ART_FFMPEG_PRESET_NVENC_HEVC;
		}
		if ( result < 0 )
			return false;
		if ( pPresetOut )
			*pPresetOut = result;
		return true;
	}

	LONG ArtFfmpegPresetFromName( const char *pName )
	{
		LONG preset = ART_FFMPEG_PRESET_PRORES_422;
		TryParseArtFfmpegPresetName( pName, &preset );
		return preset;
	}

	const ArtFfmpegOptionInfo *GetArtFfmpegOptionInfo( int optionIndex )
	{
		if ( optionIndex >= 0 && optionIndex < ART_FFOPT_COUNT )
			return &s_Options[optionIndex];
		return NULL;
	}

	int FindArtFfmpegOptionByName( const char *pName )
	{
		if ( !pName )
			return -1;
		for ( int i = 0; i < ART_FFOPT_COUNT; ++i )
		{
			if ( !Q_stricmp( pName, s_Options[i].name ) )
				return i;
		}
		return -1;
	}

	int GetArtFfmpegOption( int optionIndex )
	{
		if ( optionIndex < 0 || optionIndex >= ART_FFOPT_COUNT )
			return 0;
		return static_cast<int>( InterlockedCompareExchange( &g_ArtFfmpegOptions[optionIndex], 0, 0 ) );
	}

	void SetArtFfmpegOption( int optionIndex, int value )
	{
		if ( optionIndex < 0 || optionIndex >= ART_FFOPT_COUNT )
			return;
		const ArtFfmpegOptionInfo &info = s_Options[optionIndex];
		if ( value < info.minValue )
			value = info.minValue;
		if ( value > info.maxValue )
			value = info.maxValue;
		InterlockedExchange( &g_ArtFfmpegOptions[optionIndex], value );
	}

	void ResetArtFfmpegOptions()
	{
		for ( int i = 0; i < ART_FFOPT_COUNT; ++i )
			InterlockedExchange( &g_ArtFfmpegOptions[i], s_Options[i].defaultValue );
	}

	void BuildArtFfmpegArgumentTemplate( LONG presetId, char *pOut, size_t outSize )
	{
		if ( !pOut || outSize == 0 )
			return;

		static const char kInput[] = "-y -f rawvideo -pix_fmt rgb24 -s {WIDTH}x{HEIGHT} -r {FPS} -i - ";
		char encoder[512];
		encoder[0] = '\0';

		switch ( presetId )
		{
		case ART_FFMPEG_PRESET_CUSTOM:
			Q_strncpy( pOut, g_szArtFfmpegCustomArgs, static_cast<int>( outSize ) );
			return;

		case ART_FFMPEG_PRESET_PRORES_422:
		{
			const int profile = GetArtFfmpegOption( ART_FFOPT_PRORES422_PROFILE );
			Q_snprintf( encoder, sizeof( encoder ), "-c:v prores_ks -profile:v %d -pix_fmt yuv422p10le", profile );
			break;
		}

		case ART_FFMPEG_PRESET_PRORES_4444:
		{
			const int profile = GetArtFfmpegOption( ART_FFOPT_PRORES4444_PROFILE );
			Q_snprintf( encoder, sizeof( encoder ), "-c:v prores_ks -profile:v %d -pix_fmt yuv444p10le", 4 + profile );
			break;
		}

		case ART_FFMPEG_PRESET_H264:
		{
			const int crf = GetArtFfmpegOption( ART_FFOPT_X264_CRF );
			const int speed = GetArtFfmpegOption( ART_FFOPT_X264_SPEED );
			const int tune = GetArtFfmpegOption( ART_FFOPT_X264_TUNE );
			char tuneArg[48] = "";
			if ( tune > 0 )
				Q_snprintf( tuneArg, sizeof( tuneArg ), " -tune %s", kArtX264Tunes[tune] );
			// CRF 0 is only lossless when chroma is not subsampled.
			Q_snprintf( encoder, sizeof( encoder ), "-c:v libx264 -crf %d -preset %s%s -pix_fmt %s",
				crf, kArtX264Speeds[speed], tuneArg, crf == 0 ? "yuv444p" : "yuv420p" );
			break;
		}

		case ART_FFMPEG_PRESET_NVENC_H264:
		case ART_FFMPEG_PRESET_NVENC_HEVC:
		{
			const bool hevc = ( presetId == ART_FFMPEG_PRESET_NVENC_HEVC );
			const int cq = GetArtFfmpegOption( hevc ? ART_FFOPT_NVHEVC_CQ : ART_FFOPT_NVH264_CQ );
			const int preset = GetArtFfmpegOption( hevc ? ART_FFOPT_NVHEVC_PRESET : ART_FFOPT_NVH264_PRESET );
			const int tune = GetArtFfmpegOption( hevc ? ART_FFOPT_NVHEVC_TUNE : ART_FFOPT_NVH264_TUNE );
			char tuneArg[48] = "";
			if ( tune > 0 )
				Q_snprintf( tuneArg, sizeof( tuneArg ), " -tune %s", kArtNvencTunes[tune] );
			Q_snprintf( encoder, sizeof( encoder ), "-c:v %s -preset %s%s -rc vbr -cq %d -b:v 0 -pix_fmt yuv420p",
				hevc ? "hevc_nvenc" : "h264_nvenc", kArtNvencPresets[preset], tuneArg, cq );
			break;
		}

		default:
			Q_strncpy( encoder, "-c:v prores_ks -profile:v 3 -pix_fmt yuv422p10le", sizeof( encoder ) );
			break;
		}

		Q_snprintf( pOut, static_cast<int>( outSize ), "%s%s \"{OUTPUT_FILE}\"", kInput, encoder );
	}

	const char *GetArtFfmpegPassOutputExtension( LONG presetId )
	{
		if ( presetId >= 0 && presetId < ARRAYSIZE( s_Presets ) )
		{
			if ( presetId == ART_FFMPEG_PRESET_CUSTOM )
			{
				if ( strstr( g_szArtFfmpegCustomArgs, ".mov" ) ) return "mov";
				if ( strstr( g_szArtFfmpegCustomArgs, ".mkv" ) ) return "mkv";
				if ( strstr( g_szArtFfmpegCustomArgs, ".avi" ) ) return "avi";
				return "mp4";
			}
			return s_Presets[presetId].extension;
		}
		return "mov";
	}

	bool ResolveArtFfmpegExecutablePath( char *pOutPath, size_t outPathSize, bool *pFoundOnDisk )
	{
		if ( pFoundOnDisk )
			*pFoundOnDisk = false;
		if ( !pOutPath || outPathSize == 0 )
			return false;

		pOutPath[0] = '\0';

		// 1. Explicitly configured path if non-default and valid
		if ( g_szArtFfmpegPath[0] && Q_stricmp( g_szArtFfmpegPath, "default" ) && Q_stricmp( g_szArtFfmpegPath, "ffmpeg.exe" ) )
		{
			if ( FileExistsOnDisk( g_szArtFfmpegPath ) )
			{
				Q_strncpy( pOutPath, g_szArtFfmpegPath, static_cast<int>( outPathSize ) );
				if ( pFoundOnDisk ) *pFoundOnDisk = true;
				return true;
			}
		}

		char candidate[MAX_PATH];

		// 2. Beside the injected DLL (v34_art_v1.0.dll) or in its bin/ folder
		if ( g_hThisModule )
		{
			char dllPath[MAX_PATH];
			if ( GetModuleFileNameA( g_hThisModule, dllPath, sizeof( dllPath ) ) )
			{
				char dllDir[MAX_PATH];
				GetParentDirectory( dllPath, dllDir, sizeof( dllDir ) );
				if ( dllDir[0] )
				{
					Q_snprintf( candidate, sizeof( candidate ), "%s\\ffmpeg.exe", dllDir );
					if ( FileExistsOnDisk( candidate ) )
					{
						Q_strncpy( pOutPath, candidate, static_cast<int>( outPathSize ) );
						if ( pFoundOnDisk ) *pFoundOnDisk = true;
						return true;
					}

					Q_snprintf( candidate, sizeof( candidate ), "%s\\bin\\ffmpeg.exe", dllDir );
					if ( FileExistsOnDisk( candidate ) )
					{
						Q_strncpy( pOutPath, candidate, static_cast<int>( outPathSize ) );
						if ( pFoundOnDisk ) *pFoundOnDisk = true;
						return true;
					}
				}
			}
		}

		// 3. Beside host executable (hl2.exe) or in its bin/ folder
		char exePath[MAX_PATH];
		if ( GetModuleFileNameA( NULL, exePath, sizeof( exePath ) ) )
		{
			char exeDir[MAX_PATH];
			GetParentDirectory( exePath, exeDir, sizeof( exeDir ) );
			if ( exeDir[0] )
			{
				Q_snprintf( candidate, sizeof( candidate ), "%s\\ffmpeg.exe", exeDir );
				if ( FileExistsOnDisk( candidate ) )
				{
					Q_strncpy( pOutPath, candidate, static_cast<int>( outPathSize ) );
					if ( pFoundOnDisk ) *pFoundOnDisk = true;
					return true;
				}

				Q_snprintf( candidate, sizeof( candidate ), "%s\\bin\\ffmpeg.exe", exeDir );
				if ( FileExistsOnDisk( candidate ) )
				{
					Q_strncpy( pOutPath, candidate, static_cast<int>( outPathSize ) );
					if ( pFoundOnDisk ) *pFoundOnDisk = true;
					return true;
				}
			}
		}

		// 4. In game directory (cstrike/)
		if ( g_pEngine )
		{
			const char *pGameDir = g_pEngine->GetGameDirectory();
			if ( pGameDir && pGameDir[0] )
			{
				Q_snprintf( candidate, sizeof( candidate ), "%s\\ffmpeg.exe", pGameDir );
				if ( FileExistsOnDisk( candidate ) )
				{
					Q_strncpy( pOutPath, candidate, static_cast<int>( outPathSize ) );
					if ( pFoundOnDisk ) *pFoundOnDisk = true;
					return true;
				}

				Q_snprintf( candidate, sizeof( candidate ), "%s\\bin\\ffmpeg.exe", pGameDir );
				if ( FileExistsOnDisk( candidate ) )
				{
					Q_strncpy( pOutPath, candidate, static_cast<int>( outPathSize ) );
					if ( pFoundOnDisk ) *pFoundOnDisk = true;
					return true;
				}
			}
		}

		// 5. In system PATH
		char systemFound[MAX_PATH];
		const DWORD searchLen = SearchPathA( NULL, "ffmpeg.exe", NULL, sizeof( systemFound ), systemFound, NULL );
		if ( searchLen > 0 && searchLen < sizeof( systemFound ) && FileExistsOnDisk( systemFound ) )
		{
			Q_strncpy( pOutPath, systemFound, static_cast<int>( outPathSize ) );
			if ( pFoundOnDisk ) *pFoundOnDisk = true;
			return true;
		}

		// 6. Default fallback
		Q_strncpy( pOutPath, g_szArtFfmpegPath[0] ? g_szArtFfmpegPath : "ffmpeg.exe", static_cast<int>( outPathSize ) );
		if ( pFoundOnDisk ) *pFoundOnDisk = false;
		return true;
	}

	bool StartArtFfmpegPipes( int width, int height, float fps, const char *pTakeFolder, LONG recordMask )
	{
		FinishArtFfmpegPipes( false );

		char ffmpegExe[MAX_PATH];
		bool foundOnDisk = false;
		ResolveArtFfmpegExecutablePath( ffmpegExe, sizeof( ffmpegExe ), &foundOnDisk );

		const LONG presetId = InterlockedCompareExchange( &g_nArtFfmpegPreset, 0, 0 );
		const ArtFfmpegPresetInfo *pPreset = GetArtFfmpegPresetInfo( presetId );
		char builtTemplate[1536];
		if ( presetId == ART_FFMPEG_PRESET_CUSTOM && !g_szArtFfmpegCustomArgs[0] )
			BuildArtFfmpegArgumentTemplate( ART_FFMPEG_PRESET_PRORES_422, builtTemplate, sizeof( builtTemplate ) );
		else
			BuildArtFfmpegArgumentTemplate( presetId, builtTemplate, sizeof( builtTemplate ) );
		const char *pTemplate = builtTemplate;
		const char *pExtension = GetArtFfmpegPassOutputExtension( presetId );

		char fpsBuffer[32];
		if ( fps <= 0.0f )
			fps = 60.0f;
		if ( static_cast<float>( static_cast<int>( fps ) ) == fps )
			Q_snprintf( fpsBuffer, sizeof( fpsBuffer ), "%d", static_cast<int>( fps ) );
		else
			Q_snprintf( fpsBuffer, sizeof( fpsBuffer ), "%.3f", fps );

		char widthBuffer[32];
		char heightBuffer[32];
		Q_snprintf( widthBuffer, sizeof( widthBuffer ), "%d", width );
		Q_snprintf( heightBuffer, sizeof( heightBuffer ), "%d", height );

		static const struct
		{
			const char *name;
			LONG bit;
		} kPasses[ART_CAPTURE_PASS_COUNT] =
		{
			{ "normal", RECORD_NORMAL },
			{ "clear", RECORD_CLEAR },
			{ "clear-noplayers", RECORD_CLEAR_NOPLAYERS },
			{ "viewmodel", RECORD_VIEWMODEL },
			{ "depth", RECORD_DEPTH },
			{ "players", RECORD_PLAYERS },
			{ "objectid", RECORD_OBJECTID }
		};

		LogMessage( "FFMPEG START PIPES: exe='%s' found_on_disk=%d preset='%s' fps=%s dimensions=%dx%d mask=0x%lX",
			ffmpegExe, foundOnDisk ? 1 : 0, pPreset->name, fpsBuffer, width, height, recordMask );

		bool anyStarted = false;

		for ( int i = 0; i < ART_CAPTURE_PASS_COUNT; ++i )
		{
			if ( !( recordMask & kPasses[i].bit ) )
				continue;

			ArtFfmpegPipe &pipe = s_Pipes[i];
			memset( &pipe, 0, sizeof( pipe ) );
			Q_strncpy( pipe.passName, kPasses[i].name, sizeof( pipe.passName ) );

			// Build output video file path
			char fileName[128];
			if ( g_szCapturePrefix[0] )
				Q_snprintf( fileName, sizeof( fileName ), "%s_%s.%s", g_szCapturePrefix, kPasses[i].name, pExtension );
			else
				Q_snprintf( fileName, sizeof( fileName ), "%s.%s", kPasses[i].name, pExtension );

			char fullOutputPath[MAX_PATH];
			if ( g_bRecordBaseAbsolute )
				Q_snprintf( fullOutputPath, sizeof( fullOutputPath ), "%s\\%s", g_szTakeRoot, fileName );
			else
			{
				const char *pGameDir = g_pEngine ? g_pEngine->GetGameDirectory() : "cstrike";
				Q_snprintf( fullOutputPath, sizeof( fullOutputPath ), "%s\\%s\\%s", pGameDir, g_szTakeRoot, fileName );
			}
			Q_strncpy( pipe.outputFilePath, fullOutputPath, sizeof( pipe.outputFilePath ) );

			// Create anonymous pipe with inheritable read handle
			SECURITY_ATTRIBUTES saAttr = {};
			saAttr.nLength = sizeof( SECURITY_ATTRIBUTES );
			saAttr.bInheritHandle = TRUE;
			saAttr.lpSecurityDescriptor = NULL;

			HANDLE hPipeRead = NULL;
			HANDLE hPipeWrite = NULL;
			if ( !CreatePipe( &hPipeRead, &hPipeWrite, &saAttr, 0 ) )
			{
				const DWORD error = GetLastError();
				LogMessage( "FFMPEG PIPE ERROR: pass='%s' CreatePipe failed (error %lu)", kPasses[i].name, error );
				ArtConsoleMessage( "art: FFmpeg pipe creation failed for pass '%s' (Win32 error %lu).\n", kPasses[i].name, error );
				FinishArtFfmpegPipes( true );
				return false;
			}

			// Ensure write handle is NOT inherited
			SetHandleInformation( hPipeWrite, HANDLE_FLAG_INHERIT, 0 );

			// Expand argument template
			std::string cmdTemplate = pTemplate;
			ReplaceSubstring( cmdTemplate, "{WIDTH}", widthBuffer );
			ReplaceSubstring( cmdTemplate, "{HEIGHT}", heightBuffer );
			ReplaceSubstring( cmdTemplate, "{FPS}", fpsBuffer );
			ReplaceSubstring( cmdTemplate, "{PASS}", kPasses[i].name );
			ReplaceSubstring( cmdTemplate, "{OUTPUT_FILE}", fullOutputPath );
			ReplaceSubstring( cmdTemplate, "{TAKE_DIR}", pTakeFolder ? pTakeFolder : g_szTakeRoot );
			ReplaceSubstring( cmdTemplate, "{TAKE_NAME}", g_ArtRecordingStats.takeName );

			std::string fullCommandLine;
			if ( cmdTemplate.find( "{FFMPEG}" ) != std::string::npos )
			{
				std::string quotedExe = std::string( "\"" ) + ffmpegExe + "\"";
				ReplaceSubstring( cmdTemplate, "{FFMPEG}", quotedExe );
				fullCommandLine = cmdTemplate;
			}
			else
			{
				fullCommandLine = std::string( "\"" ) + ffmpegExe + "\" " + cmdTemplate;
			}

			LogMessage( "FFMPEG LAUNCH: pass='%s' cmd='%s'", kPasses[i].name, fullCommandLine.c_str() );

			STARTUPINFOA si = {};
			si.cb = sizeof( STARTUPINFOA );
			si.dwFlags = STARTF_USESTDHANDLES;
			si.hStdInput = hPipeRead;
			si.hStdOutput = INVALID_HANDLE_VALUE;
			si.hStdError = INVALID_HANDLE_VALUE;

			PROCESS_INFORMATION pi = {};
			std::vector<char> cmdBuffer( fullCommandLine.begin(), fullCommandLine.end() );
			cmdBuffer.push_back( '\0' );

			const BOOL processCreated = CreateProcessA(
				NULL,
				cmdBuffer.data(),
				NULL,
				NULL,
				TRUE,
				CREATE_NO_WINDOW,
				NULL,
				NULL,
				&si,
				&pi
			);

			// Parent closes read handle immediately
			CloseHandle( hPipeRead );

			if ( !processCreated )
			{
				const DWORD error = GetLastError();
				CloseHandle( hPipeWrite );
				LogMessage( "FFMPEG SPAWN FAILED: pass='%s' error=%lu exe='%s'", kPasses[i].name, error, ffmpegExe );
				ArtConsoleMessage( "art: failed to launch FFmpeg for pass '%s' (Win32 error %lu). Check executable path: '%s'\n",
					kPasses[i].name, error, ffmpegExe );
				FinishArtFfmpegPipes( true );
				return false;
			}

			pipe.active = true;
			pipe.hPipeWrite = hPipeWrite;
			pipe.hProcess = pi.hProcess;
			pipe.hThread = pi.hThread;
			pipe.processId = pi.dwProcessId;
			pipe.framesWritten = 0;
			pipe.bytesWritten = 0;
			anyStarted = true;

			LogMessage( "FFMPEG PIPE STARTED: pass='%s' pid=%lu out='%s'", kPasses[i].name, pi.dwProcessId, fullOutputPath );
		}

		return anyStarted;
	}

	bool WriteArtFfmpegFrame( const char *pPassName, const unsigned char *pPixels, int width, int height )
	{
		if ( !pPassName || !pPixels || width <= 0 || height <= 0 )
			return false;

		for ( int i = 0; i < ART_CAPTURE_PASS_COUNT; ++i )
		{
			ArtFfmpegPipe &pipe = s_Pipes[i];
			if ( !pipe.active || !pipe.hPipeWrite || Q_stricmp( pipe.passName, pPassName ) )
				continue;

			const DWORD totalBytes = static_cast<DWORD>( width ) * static_cast<DWORD>( height ) * 3;
			DWORD bytesWritten = 0;
			const BOOL ok = WriteFile( pipe.hPipeWrite, pPixels, totalBytes, &bytesWritten, NULL );
			if ( !ok || bytesWritten != totalBytes )
			{
				const DWORD error = GetLastError();
				DWORD exitCode = 0;
				if ( pipe.hProcess )
					GetExitCodeProcess( pipe.hProcess, &exitCode );

				LogMessage( "FFMPEG WRITE ERROR: pass='%s' bytes_attempted=%lu bytes_written=%lu error=%lu process_exit_code=%lu",
					pPassName, totalBytes, bytesWritten, error, exitCode );
				ArtConsoleMessage( "art: FFmpeg pipe write failed for pass '%s' (error %lu, process code %lu).\n",
					pPassName, error, exitCode );
				return false;
			}

			++pipe.framesWritten;
			pipe.bytesWritten += bytesWritten;
			return true;
		}

		return true;
	}

	void FinishArtFfmpegPipes( bool aborted )
	{
		for ( int i = 0; i < ART_CAPTURE_PASS_COUNT; ++i )
		{
			ArtFfmpegPipe &pipe = s_Pipes[i];
			if ( !pipe.active )
				continue;

			LogMessage( "FFMPEG PIPE CLOSING: pass='%s' frames=%lu bytes=%I64u aborted=%d",
				pipe.passName, pipe.framesWritten, pipe.bytesWritten, aborted ? 1 : 0 );

			if ( pipe.hPipeWrite )
			{
				CloseHandle( pipe.hPipeWrite );
				pipe.hPipeWrite = NULL;
			}

			if ( pipe.hProcess )
			{
				const DWORD waitResult = WaitForSingleObject( pipe.hProcess, aborted ? 2000 : 15000 );
				DWORD exitCode = 0;
				GetExitCodeProcess( pipe.hProcess, &exitCode );

				if ( waitResult == WAIT_TIMEOUT )
				{
					LogMessage( "FFMPEG TIMEOUT: pass='%s' process did not exit within timeout; terminating", pipe.passName );
					TerminateProcess( pipe.hProcess, 1 );
				}

				LogMessage( "FFMPEG PIPE CLOSED: pass='%s' exit_code=%lu wait_result=%lu",
					pipe.passName, exitCode, waitResult );

				CloseHandle( pipe.hProcess );
				pipe.hProcess = NULL;
			}

			if ( pipe.hThread )
			{
				CloseHandle( pipe.hThread );
				pipe.hThread = NULL;
			}

			pipe.active = false;
		}
	}

	bool AreArtFfmpegPipesActive()
	{
		for ( int i = 0; i < ART_CAPTURE_PASS_COUNT; ++i )
		{
			if ( s_Pipes[i].active )
				return true;
		}
		return false;
	}

	const ArtFfmpegPipe *GetArtFfmpegPipe( const char *pPassName )
	{
		if ( !pPassName )
			return NULL;
		for ( int i = 0; i < ART_CAPTURE_PASS_COUNT; ++i )
		{
			if ( s_Pipes[i].active && !Q_stricmp( s_Pipes[i].passName, pPassName ) )
				return &s_Pipes[i];
		}
		return NULL;
	}

	bool RunArtFfmpegTest( ArtFfmpegTestResult &result )
	{
		memset( &result, 0, sizeof( result ) );
		bool foundOnDisk = false;
		ResolveArtFfmpegExecutablePath( result.resolvedPath, sizeof( result.resolvedPath ), &foundOnDisk );

		char commandLine[MAX_PATH + 128];
		Q_snprintf( commandLine, sizeof( commandLine ), "\"%s\" -y -f rawvideo -pix_fmt rgb24 -s 64x64 -r 30 -i - -frames:v 1 -f null -",
			result.resolvedPath );

		SECURITY_ATTRIBUTES saAttr = {};
		saAttr.nLength = sizeof( SECURITY_ATTRIBUTES );
		saAttr.bInheritHandle = TRUE;

		HANDLE hPipeRead = NULL;
		HANDLE hPipeWrite = NULL;
		if ( !CreatePipe( &hPipeRead, &hPipeWrite, &saAttr, 0 ) )
		{
			result.executed = false;
			result.success = false;
			Q_snprintf( result.message, sizeof( result.message ), "Pipe creation failed (Win32 error %lu)", GetLastError() );
			return false;
		}

		SetHandleInformation( hPipeWrite, HANDLE_FLAG_INHERIT, 0 );

		STARTUPINFOA si = {};
		si.cb = sizeof( STARTUPINFOA );
		si.dwFlags = STARTF_USESTDHANDLES;
		si.hStdInput = hPipeRead;
		si.hStdOutput = INVALID_HANDLE_VALUE;
		si.hStdError = INVALID_HANDLE_VALUE;

		PROCESS_INFORMATION pi = {};
		const BOOL spawned = CreateProcessA(
			NULL, commandLine, NULL, NULL, TRUE, CREATE_NO_WINDOW, NULL, NULL, &si, &pi );

		CloseHandle( hPipeRead );

		if ( !spawned )
		{
			const DWORD error = GetLastError();
			CloseHandle( hPipeWrite );
			result.executed = false;
			result.success = false;
			result.exitCode = error;
			Q_snprintf( result.message, sizeof( result.message ),
				"Could not start FFmpeg (error %lu). Executable '%s' was %s.",
				error, result.resolvedPath, foundOnDisk ? "found on disk" : "NOT found on disk" );
			return false;
		}

		result.executed = true;

		// Feed 1 blank raw frame (64 * 64 * 3 bytes)
		unsigned char blankFrame[64 * 64 * 3] = {};
		DWORD written = 0;
		WriteFile( hPipeWrite, blankFrame, sizeof( blankFrame ), &written, NULL );
		CloseHandle( hPipeWrite );

		WaitForSingleObject( pi.hProcess, 5000 );
		DWORD exitCode = 1;
		GetExitCodeProcess( pi.hProcess, &exitCode );
		CloseHandle( pi.hProcess );
		CloseHandle( pi.hThread );

		result.exitCode = exitCode;
		result.success = ( exitCode == 0 );

		if ( result.success )
		{
			Q_snprintf( result.message, sizeof( result.message ),
				"FFmpeg verified successfully (exit code 0). Ready for live streaming." );
		}
		else
		{
			Q_snprintf( result.message, sizeof( result.message ),
				"FFmpeg process returned exit code %lu.", exitCode );
		}

		return result.success;
	}
}
