#pragma once

#include "interface.h"
#include "view_shared.h"

#include <stddef.h>

namespace art
{
	struct ArtHlaeStatus
	{
		bool enabled;
		bool v34HookReady;
		bool campathEnabled;
		bool campathHold;
		bool campathDraw;
		bool campathDrawKeyAxis;
		bool campathDrawKeyCam;
		float campathDrawKeyIndex;
		size_t campathKeyframes;
		bool campathCanEval;
		int campathPositionInterp;
		int campathRotationInterp;
		int campathFovInterp;
		double campathOffset;
		double campathCurrentTime;
		double campathLowerBound;
		double campathUpperBound;
		bool inputCamera;
		bool inputHasCameraData;
		double inputCameraX;
		double inputCameraY;
		double inputCameraZ;
		double inputCameraPitch;
		double inputCameraYaw;
		double inputCameraRoll;
		double inputCameraFov;
		double inputMouseSensitivity;
		double inputKeyboardSensitivity;
		bool inputMouseMoveSupport;
		int inputOffsetMode;
		double inputStepFactor;
		bool inputRotLocalSpace;
		bool inputSmooth;
		bool inputSmoothRotShortestPath;
		double inputSmoothHalfTime;
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
		bool camioExporting;
		bool camioImporting;
		bool agrRecording;
		bool agrEnabled;
		bool agrRecordCamera;
		bool agrRecordPlayers;
		int agrRecordPlayerCameras;
		bool agrRecordWeapons;
		bool agrRecordProjectiles;
		int agrRecordViewModels;
		bool agrRecordInvisible;
		bool agrDebug;
		bool camexportRecording;
		bool camimportActive;
		bool fovOverride;
		double fov;
		bool fovHandleZoom;
		double fovMinUnzoomed;
		bool autoExportAgr;
		bool autoExportCamio;
		bool autoExportBvh;
		double autoExportBvhFps;
		char camioExportPath[MAX_PATH];
		char camioImportPath[MAX_PATH];
		char agrPath[MAX_PATH];
		char camexportPath[MAX_PATH];
		char camimportPath[MAX_PATH];
	};

	struct ArtHlaeCampathDrawPoint
	{
		float x;
		float y;
		float z;
		float pitch;
		float yaw;
		float roll;
		float fov;
		double time;
		bool selected;
	};

	bool InitializeArtHlae( CreateInterfaceFn clientFactory, CreateInterfaceFn engineFactory );
	bool HasExistingArtHlaeCommand();
	void ApplyArtHlaeView( CViewSetup &view );
	void BeginArtHlaeTakeExports();
	void EndArtHlaeTakeExports();
	void ShutdownArtHlae();
	void GetArtHlaeStatus( ArtHlaeStatus &status );
	bool IsArtHlaeCampathDrawingEnabled();
	size_t GetArtHlaeCampathDrawPoints( ArtHlaeCampathDrawPoint *pPoints,
		size_t pointCapacity, double &currentPathTime );
	size_t GetArtHlaeCampathTrajectoryPoints( ArtHlaeCampathDrawPoint *pPoints,
		size_t pointCapacity, double &currentPathTime );
	bool GetArtHlaeCampathCurrentCamera( ArtHlaeCampathDrawPoint &point,
		bool &campathEnabled );
	bool IsArtHlaeEnabled();
	bool CaptureArtHlaeCursorPosition( LONG &x, LONG &y );
	void NotifyArtHlaeCursorWarp( LONG x, LONG y );
	void SupplyArtHlaeRawMouseDelta( LONG x, LONG y );
	bool SupplyArtHlaeRawInput( RAWINPUT &rawInput );
	bool SupplyArtHlaeKeyEvent( bool down, WPARAM wParam, LPARAM lParam );
	bool SupplyArtHlaeCharEvent( WPARAM wParam, LPARAM lParam );
	bool SupplyArtHlaeMouseEvent( UINT message, WPARAM &wParam, LPARAM &lParam );
	void SupplyArtHlaeFocus( bool focused );
	bool IsArtHlaeInputActive();
	void PrintArtHlaeHelp();
}
