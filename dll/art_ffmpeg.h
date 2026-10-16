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
		ART_FFMPEG_PRESET_H264,
		ART_FFMPEG_PRESET_NVENC_H264,
		ART_FFMPEG_PRESET_NVENC_HEVC,
		ART_FFMPEG_PRESET_CUSTOM,
		ART_FFMPEG_PRESET_COUNT
	};

	// Tunable encoder options (one table entry per option; values live in g_ArtFfmpegOptions).
	enum ArtFfmpegOptionId
	{
		ART_FFOPT_X264_CRF = 0,
		ART_FFOPT_X264_SPEED,
		ART_FFOPT_X264_TUNE,
		ART_FFOPT_NVH264_CQ,
		ART_FFOPT_NVH264_PRESET,
		ART_FFOPT_NVH264_TUNE,
		ART_FFOPT_NVHEVC_CQ,
		ART_FFOPT_NVHEVC_PRESET,
		ART_FFOPT_NVHEVC_TUNE,
		ART_FFOPT_PRORES422_PROFILE,
		ART_FFOPT_PRORES4444_PROFILE,
		ART_FFOPT_COUNT
	};

	struct ArtFfmpegOptionInfo
	{
		ArtFfmpegOptionId id;
		const char *name;
		int minValue;
		int maxValue;
		int defaultValue;
	};

	// Option value labels / ffmpeg argument values for enumerated options.
	static const char *const kArtX264Speeds[] = { "ultrafast", "superfast", "veryfast", "faster", "fast", "medium", "slow", "slower", "veryslow" };
	static const char *const kArtX264Tunes[] = { "none", "film", "animation", "grain", "stillimage", "fastdecode", "zerolatency" };
	static const char *const kArtNvencPresets[] = { "p1", "p2", "p3", "p4", "p5", "p6", "p7" };
	static const char *const kArtNvencTunes[] = { "none", "hq", "ll", "ull", "lossless" };
	static const char *const kArtProres422Profiles[] = { "proxy", "lt", "standard", "hq" };
	static const char *const kArtProres4444Profiles[] = { "4444", "4444xq" };

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
	extern volatile LONG g_ArtFfmpegOptions[ART_FFOPT_COUNT];

	// Preset definition queries
	const ArtFfmpegPresetInfo *GetArtFfmpegPresetInfo( int presetIndex );
	int GetArtFfmpegPresetCount();
	const char *GetArtOutputModeName( LONG mode );
	LONG ArtOutputModeFromName( const char *pName );
	const char *GetArtFfmpegPresetName( LONG preset );
	LONG ArtFfmpegPresetFromName( const char *pName );
	// Returns true if pName is a recognised preset name or legacy alias (h264_hq, x264_lossless, ...).
	bool TryParseArtFfmpegPresetName( const char *pName, LONG *pPresetOut );

	// Encoder option access (values are clamped to their table range).
	const ArtFfmpegOptionInfo *GetArtFfmpegOptionInfo( int optionIndex );
	int FindArtFfmpegOptionByName( const char *pName );
	int GetArtFfmpegOption( int optionIndex );
	void SetArtFfmpegOption( int optionIndex, int value );
	void ResetArtFfmpegOptions();
	// Builds the ffmpeg argument template (still containing {WIDTH}, {OUTPUT_FILE}, ... tags) for a preset.
	void BuildArtFfmpegArgumentTemplate( LONG presetId, char *pOut, size_t outSize );

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
