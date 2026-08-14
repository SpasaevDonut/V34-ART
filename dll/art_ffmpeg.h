#pragma once

#include "art_internal.h"
#include <windows.h>

namespace art
{
	enum ArtOutputMode
	{
		ART_OUTPUT_MODE_TGA = 0,
		ART_OUTPUT_MODE_FFMPEG = 1
	};

	enum ArtFfmpegPresetId
	{
		ART_FFMPEG_PRESET_PRORES_422 = 0,
		ART_FFMPEG_PRESET_PRORES_4444,
		ART_FFMPEG_PRESET_H264_HQ,
		ART_FFMPEG_PRESET_H264_LOSSLESS,
		ART_FFMPEG_PRESET_NVENC_H264,
		ART_FFMPEG_PRESET_NVENC_HEVC,
		ART_FFMPEG_PRESET_CUSTOM,
		ART_FFMPEG_PRESET_COUNT
	};

	struct ArtFfmpegPresetInfo
	{
		ArtFfmpegPresetId id;
		const char *name;
		const char *displayName;
		const char *extension;
		const char *commandTemplate;
		const char *description;
	};

	struct ArtFfmpegPipe
	{
		bool active;
		char passName[32];
		char outputFilePath[MAX_PATH];
		HANDLE hPipeWrite;
		HANDLE hProcess;
		HANDLE hThread;
		DWORD processId;
		unsigned long framesWritten;
		unsigned __int64 bytesWritten;
	};

	extern volatile LONG g_nArtOutputMode;
	extern volatile LONG g_nArtFfmpegPreset;
	extern char g_szArtFfmpegPath[MAX_PATH];
	extern char g_szArtFfmpegCustomArgs[1024];

	// Preset definition queries
	const ArtFfmpegPresetInfo *GetArtFfmpegPresetInfo( int presetIndex );
	int GetArtFfmpegPresetCount();
	const char *GetArtOutputModeName( LONG mode );
	LONG ArtOutputModeFromName( const char *pName );
	const char *GetArtFfmpegPresetName( LONG preset );
	LONG ArtFfmpegPresetFromName( const char *pName );

	// Executable discovery (checks custom path -> beside DLL -> beside game -> system PATH)
	bool ResolveArtFfmpegExecutablePath( char *pOutPath, size_t outPathSize, bool *pFoundOnDisk = NULL );

	// Pipeline streaming
	bool StartArtFfmpegPipes( int width, int height, float fps, const char *pTakeFolder, LONG recordMask );
	bool WriteArtFfmpegFrame( const char *pPassName, const unsigned char *pPixels, int width, int height );
	void FinishArtFfmpegPipes( bool aborted );
	bool AreArtFfmpegPipesActive();
	const ArtFfmpegPipe *GetArtFfmpegPipe( const char *pPassName );
	const char *GetArtFfmpegPassOutputExtension( LONG presetId );

	// Diagnostic testing
	struct ArtFfmpegTestResult
	{
		bool executed;
		bool success;
		DWORD exitCode;
		char resolvedPath[MAX_PATH];
		char message[512];
	};
	bool RunArtFfmpegTest( ArtFfmpegTestResult &result );
}
