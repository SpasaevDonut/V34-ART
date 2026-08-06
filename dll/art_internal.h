#pragma once

#include "../include/v34_art_version.h"

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stddef.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "interface.h"
#include "cdll_int.h"
#include "view_shared.h"
#include "filesystem.h"
#include "icvar.h"
#include "icliententitylist.h"
#include "iclientnetworkable.h"
#include "client_class.h"
#include "basehandle.h"
#include "ienginevgui.h"
#include "ivrenderview.h"
#include "engine/ivmodelrender.h"
#include "color.h"
#include "tier1/convar.h"
#include "tier1/strtools.h"
#include "tier1/utlbuffer.h"
#include "bitmap/tgawriter.h"
#include "materialsystem/imaterialsystem.h"
#include "art_logic.h"

abstract_class IGameConsole003 : public IBaseInterface
{
public:
	virtual void Activate() = 0;
	virtual void Initialize() = 0;
	virtual void Hide() = 0;
	virtual void Clear() = 0;
	virtual bool IsConsoleVisible() = 0;
	virtual void Printf( const char *pFormat, ... ) = 0;
	virtual void DPrintf( const char *pFormat, ... ) = 0;
	virtual void ColorPrintf( Color &color, const char *pFormat, ... ) = 0;
	virtual void SetParent( unsigned int parent ) = 0;
};

namespace art
{
	enum PreviewPass
	{
		PREVIEW_NONE = 0,
		PREVIEW_NORMAL,
		PREVIEW_CLEAR,
		PREVIEW_CLEAR_NOPLAYERS,
		PREVIEW_VIEWMODEL,
		PREVIEW_DEPTH,
		PREVIEW_PLAYERS,
		PREVIEW_OBJECTID
	};

	enum RecordBits
	{
		RECORD_CLEAR = ( 1 << 0 ),
		RECORD_VIEWMODEL = ( 1 << 1 ),
		RECORD_DEPTH = ( 1 << 2 ),
		RECORD_PLAYERS = ( 1 << 3 ),
		RECORD_NORMAL = ( 1 << 4 ),
		RECORD_CLEAR_NOPLAYERS = ( 1 << 5 ),
		RECORD_OBJECTID = ( 1 << 6 ),
		RECORD_ALL = RECORD_NORMAL | RECORD_CLEAR | RECORD_VIEWMODEL | RECORD_DEPTH |
			RECORD_PLAYERS | RECORD_CLEAR_NOPLAYERS | RECORD_OBJECTID
	};

	// Compatibility aliases retained for the existing build-4044 render path.
	static const LONG ART_PREVIEW_NONE = PREVIEW_NONE;
	static const LONG ART_PREVIEW_NORMAL = PREVIEW_NORMAL;
	static const LONG ART_PREVIEW_CLEAR = PREVIEW_CLEAR;
	static const LONG ART_PREVIEW_VIEWMODEL = PREVIEW_VIEWMODEL;
	static const LONG ART_PREVIEW_DEPTH = PREVIEW_DEPTH;
	static const LONG ART_PREVIEW_PLAYERS = PREVIEW_PLAYERS;
	static const LONG ART_PREVIEW_CLEAR_NOPLAYERS = PREVIEW_CLEAR_NOPLAYERS;
	static const LONG ART_PREVIEW_OBJECTID = PREVIEW_OBJECTID;
	static const LONG ART_RECORD_NORMAL = RECORD_NORMAL;
	static const LONG ART_RECORD_CLEAR = RECORD_CLEAR;
	static const LONG ART_RECORD_VIEWMODEL = RECORD_VIEWMODEL;
	static const LONG ART_RECORD_DEPTH = RECORD_DEPTH;
	static const LONG ART_RECORD_PLAYERS = RECORD_PLAYERS;
	static const LONG ART_RECORD_CLEAR_NOPLAYERS = RECORD_CLEAR_NOPLAYERS;
	static const LONG ART_RECORD_OBJECTID = RECORD_OBJECTID;
	static const LONG ART_RECORD_ALL = RECORD_ALL;

	// Engine modules and interfaces resolved during asynchronous initialization.
	extern HMODULE g_hThisModule;
	extern HMODULE g_hClientModule;
	extern HMODULE g_hEngineModule;
	extern IBaseClientDLL *g_pClient;
	extern IVEngineClient *g_pEngine;
	extern IClientEntityList *g_pEntityList;
	extern ICvar *g_pCvar;
	extern IMaterialSystem *g_pMaterials;
	extern IFileSystem *g_pFileSystem;
	extern IVRenderView *g_pRenderView;
	extern IVModelRender *g_pModelRender;
	extern IGameConsole003 *g_pGameConsole;

	// Recorder state shared by commands, capture helpers, and render hooks.
	extern volatile LONG g_bRecording;
	extern volatile LONG g_bRenderingArt;
	extern volatile LONG g_nPreviewPass;
	extern volatile LONG g_nRecordMask;
	extern volatile LONG g_nHudMask;
	extern int g_nFrame;
	extern unsigned long g_nRenderHookCalls;
	extern unsigned long g_nPreviewRenderCalls;
	extern char g_szRecordBase[MAX_PATH];
	extern bool g_bRecordBaseAbsolute;
	extern char g_szCapturePrefix[48];
	extern char g_szTakeRoot[MAX_PATH];
	extern int g_nViewmodelBackgroundRed;
	extern int g_nViewmodelBackgroundGreen;
	extern int g_nViewmodelBackgroundBlue;
	extern int g_nPlayersBackgroundRed;
	extern int g_nPlayersBackgroundGreen;
	extern int g_nPlayersBackgroundBlue;
	extern const float kDefaultViewmodelFov;
	extern float g_flViewmodelFov;
	extern volatile LONG g_bGlobalFovOverride;
	extern float g_flGlobalFov;
	extern volatile LONG g_bGlobalFovHandleZoom;
	extern float g_flGlobalFovMinUnzoomedFov;
	extern ConVar art_depth_start;
	extern ConVar art_depth_end;

	// Console and optional diagnostic file logging.
	void ArtConsoleMessage( const char *pFormat, ... );
	void InitializeLogging();
	void ShutdownLogging();
	bool EnableDebugLogging( DWORD &error );
	void DisableDebugLogging();
	bool IsDebugLoggingEnabled();
	void FlushLog();
	void LogMessage( const char *pFormat, ... );
	const char *GetHostExecutablePath();
	const char *GetLogPath();

	// Configuration and output helpers.
	const char *PreviewName( LONG preview );
	LONG RecordBitFromName( const char *pName );
	LONG PassBitFromPreview( LONG preview );
	bool ShouldLogPassConVars();
	bool ParseViewmodelFov( const char *pValue, float &fov );
	bool ParseChromaColor( const char *pValue, int &red, int &green, int &blue );
	void GetViewmodelBackgroundColorString( char *pBuffer, int bufferSize );
	void GetPlayersBackgroundColorString( char *pBuffer, int bufferSize );
	bool IsSafeTakeName( const char *pName );
	const char *RecordPathId();
	void FormatConfiguredRecordPath( char *pOutput, size_t outputBytes );
	void FormatTakeDisplayPath( char *pOutput, size_t outputBytes );
	bool BuildCapturePath( char *pOutput, size_t outputBytes, const char *pPassName, int frame );
	bool SetRecordBasePath( const char *pRequestedPath, char *pError, size_t errorBytes );
	bool MakeTakeDirectories( const char *pRequestedName );
	bool CaptureTga( const CViewSetup &view, const char *pPassName );

	static const int ART_CAPTURE_PASS_COUNT = 7;
	static const int ART_QUEUE_DEFAULT_MAX_FILES = 16;
	static const int ART_QUEUE_DEFAULT_MAX_MEGABYTES = 256;
	static const int ART_QUEUE_DEFAULT_RESERVE_MEGABYTES = 256;

	enum ArtTimingStage
	{
		ART_TIMING_RENDER = 0,
		ART_TIMING_READ,
		ART_TIMING_ENCODE,
		ART_TIMING_WRITE,
		ART_TIMING_QUEUE,
		ART_TIMING_COUNT
	};

	enum ArtTgaCompressionMode
	{
		ART_TGA_COMPRESSION_OFF = 0,
		ART_TGA_COMPRESSION_AUTO,
		ART_TGA_COMPRESSION_RLE
	};

	struct ArtStageTimingStatistics
	{
		unsigned __int64 takeTotalMicroseconds;
		unsigned __int64 sessionTotalMicroseconds;
		unsigned long takeSamples;
		unsigned long sessionSamples;
		unsigned long takeMaxMicroseconds;
		unsigned long sessionMaxMicroseconds;
	};

	struct ArtQueueOptions
	{
		volatile LONG maxFiles;
		volatile LONG maxMegabytes;
		volatile LONG reserveMegabytes;
	};

	struct ArtPipelineStatistics
	{
		unsigned long pendingFiles;
		unsigned __int64 pendingBytes;
		unsigned long takePeakFiles;
		unsigned __int64 takePeakBytes;
		unsigned long sessionPeakFiles;
		unsigned __int64 sessionPeakBytes;
		unsigned long takeFlushes;
		unsigned long sessionFlushes;
		unsigned long takeAllocationRetries;
		unsigned long sessionAllocationRetries;
		unsigned long takeAllocationFailures;
		unsigned long sessionAllocationFailures;
		unsigned __int64 takeUncompressedBytes;
		unsigned __int64 takeOutputBytes;
		unsigned __int64 sessionUncompressedBytes;
		unsigned __int64 sessionOutputBytes;
		ArtStageTimingStatistics stages[ART_TIMING_COUNT];
	};

	extern ArtQueueOptions g_ArtQueueOptions;
	extern volatile LONG g_nArtTgaCompressionMode;
	extern ArtPipelineStatistics g_ArtPipelineStats;
	unsigned __int64 BeginArtStageTiming();
	void EndArtStageTiming( ArtTimingStage stage, unsigned __int64 startCounter );
	void ResetArtPipelineTakeStatistics();
	bool EnsureArtQueueCapacity( size_t estimatedQueuedBytes, size_t estimatedAllocationBytes );
	void NoteArtQueuedWrite( unsigned long bytes );
	void FlushArtWriteQueue( const char *pReason, bool force );
	void *AllocateArtCaptureMemory( size_t bytes, const char *pBufferName );
	bool EncodeArtTgaRle( const unsigned char *pSource, size_t sourceBytes,
		unsigned char *pDestination, size_t destinationCapacity, size_t &destinationBytes );
	void RecordArtCompressionResult( unsigned long uncompressedBytes, unsigned long outputBytes );
	const char *ArtTgaCompressionModeName( LONG mode );
	void PrintArtQueueStatus();
	void PrintArtPipelineStatistics();

	struct ArtPassRecordingStatistics
	{
		unsigned long files;
		unsigned __int64 bytes;
	};

	struct ArtRecordingStatistics
	{
		unsigned long sessionTakesStarted;
		unsigned long sessionTakesCompleted;
		unsigned long sessionTakesAborted;
		unsigned long sessionFrames;
		unsigned long sessionFiles;
		unsigned __int64 sessionBytes;
		unsigned long takeFrames;
		unsigned long takeFiles;
		unsigned __int64 takeBytes;
		DWORD takeStartTick;
		DWORD takeElapsedMs;
		LONG takeRecordMask;
		LONG takeHudMask;
		int takeWidth;
		int takeHeight;
		unsigned long takeResolutionChanges;
		float takeHostFramerate;
		bool takeGlobalFovEnabled;
		float takeGlobalFov;
		bool takeGlobalFovHandleZoom;
		float takeGlobalFovMinUnzoomedFov;
		float takeViewmodelFov;
		float takeAspectRatio;
		float takeEngineCameraFov4x3;
		float takeEngineCameraFovHorizontal;
		float takeEngineCameraFovVertical;
		float takeEngineCameraFov4x3Minimum;
		float takeEngineCameraFov4x3Maximum;
		float takeEngineCameraFovHorizontalMinimum;
		float takeEngineCameraFovHorizontalMaximum;
		float takeEngineViewmodelFov4x3;
		float takeEngineViewmodelFovHorizontal;
		float takeEngineViewmodelFovVertical;
		unsigned long takeEngineFovChanges;
		float takeDepthStart;
		float takeDepthEnd;
		int takeViewmodelColor[3];
		int takePlayersColor[3];
		int takeObjectIdColors[4][3];
		LONG takeTgaCompressionMode;
		LONG takeQueueMaxFiles;
		LONG takeQueueMaxMegabytes;
		LONG takeQueueReserveMegabytes;
		unsigned int takeEngineBuild;
		bool takeDemoPlayback;
		bool takeManifestEnabled;
		bool takeActive;
		bool takeAborted;
		SYSTEMTIME takeStartedUtc;
		SYSTEMTIME takeFinishedUtc;
		char takeName[64];
		char takeMapName[128];
		char takeGameDirectory[MAX_PATH];
		char takeDisplayPath[MAX_PATH];
		char takeAbsolutePath[MAX_PATH];
		char takePrefix[48];
		ArtPassRecordingStatistics passes[ART_CAPTURE_PASS_COUNT];
	};

	struct ArtValidationOptions
	{
		volatile LONG checkFileSize;
		volatile LONG checkDroppedFrames;
		volatile LONG minimumFileBytes;
		volatile LONG autoValidate;
	};

	struct ArtValidationResult
	{
		bool hasResult;
		bool passed;
		unsigned long scannedFiles;
		unsigned long expectedFiles;
		unsigned long missingFiles;
		unsigned long unexpectedFiles;
		unsigned long sequenceGaps;
		unsigned long undersizedFiles;
		unsigned long invalidHeaders;
		unsigned long invalidPixelData;
		unsigned long inconsistentDimensions;
		unsigned long directoryErrors;
		unsigned __int64 totalBytes;
		unsigned __int64 smallestFileBytes;
		unsigned __int64 largestFileBytes;
		char takePath[MAX_PATH];
	};

	enum ArtValidationPhase
	{
		ART_VALIDATION_IDLE = 0,
		ART_VALIDATION_DISCOVERING,
		ART_VALIDATION_CHECKING_FILES,
		ART_VALIDATION_FINALIZING
	};

	struct ArtValidationProgress
	{
		volatile LONG running;
		volatile LONG phase;
		volatile LONG completedFiles;
		volatile LONG totalFiles;
		volatile LONG completionPending;
		DWORD startTick;
	};

	extern ArtRecordingStatistics g_ArtRecordingStats;
	extern volatile LONG g_bArtTakeManifestEnabled;
	extern ArtValidationOptions g_ArtValidationOptions;
	extern ArtValidationResult g_ArtValidationResult;
	extern ArtValidationProgress g_ArtValidationProgress;
	void BeginArtRecordingStatistics( LONG recordMask, LONG hudMask );
	void FinishArtRecordingStatistics( bool aborted );
	void RecordArtCapturedFile( const char *pPassName, int frame, unsigned long bytes,
		int width, int height, float cameraFov, float viewmodelFov );
	void RecordArtCompletedFrame();
	bool WriteArtTakeManifest( bool force );
	void FormatArtTakeManifestPath( char *pOutput, size_t outputBytes );
	DWORD GetArtRecordingElapsedMs();
	void FormatArtByteCount( unsigned __int64 bytes, char *pOutput, size_t outputBytes );
	void PrintArtRecordingStatistics();
	bool RunArtValidation();
	void RunAutomaticArtValidation();
	bool IsArtValidationRunning();
	const char *ArtValidationPhaseName( LONG phase );
	float GetArtValidationProgressFraction();
	DWORD GetArtValidationElapsedMs();
	void PublishArtValidationCompletion();
	void PrintArtValidationResult();

	struct SavedConVar
	{
		ConVar *pVar;
		bool restoreAsInt;
		int intValue;
		char value[128];
	};

	class ConVarRestore
	{
	public:
		ConVarRestore();
		~ConVarRestore();
		void Set( const char *pName, const char *pValue );
		void Set( const char *pName, float value );

	private:
		SavedConVar m_Saved[32];
		int m_nCount;
	};
	typedef ConVarRestore CConVarRestore;

	void ApplyUtilityBase( ConVarRestore &vars );
	void ApplyViewmodel( ConVarRestore &vars );
	void ApplyPlayers( ConVarRestore &vars );
	void ApplyObjectId( ConVarRestore &vars, int worldRed, int worldGreen, int worldBlue,
		int skyboxRed, int skyboxGreen, int skyboxBlue );
	void ApplyDepth( ConVarRestore &vars );
	int AddHudDrawFlag( int baseFlags, LONG hudMask, LONG passBit );
	void ApplyHudSetting( ConVarRestore &vars, LONG hudMask, LONG passBit );

	// Console command registration and help.
	bool HasExistingArtCommand();
	void RegisterArtCommands();
	void PrintArtHelp();

	// Build-4044 render hooks.
	bool InstallClientModeFovHook();
	void RemoveClientModeFovHook();
	bool InstallModelRenderHook();
	void RemoveModelRenderHook();
	bool InstallViewRenderHook();
	void RemoveViewRenderHook();
	bool IsViewRenderHookReady();
	bool IsModelRenderHookReady();
	bool IsClientModeFovHookReady();
	void LogRenderHookState();
}
