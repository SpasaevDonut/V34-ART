#pragma once

class IVModelRender;
struct ModelRenderInfo_t;
class CViewSetup;

namespace art
{
	typedef int ( __thiscall *ArtDrawModelExFn )( IVModelRender *, ModelRenderInfo_t & );

	// Registers the in-game control panel and installs its render/input hooks.
	bool InstallArtGui();

	// Switches all control-panel hooks to passthrough before Windows begins
	// destroying the game window and graphics device.
	void BeginArtGuiTermination();
	bool IsArtGuiTerminating();

	// Best-effort teardown for an explicit/manual DLL unload.
	void ShutdownArtGui();

	bool IsArtGuiVisible();
	bool IsArtGuiMirvInputPassthroughActive();
	void SetArtGuiVisible( bool visible );

	// Applies the global FOV override directly to the CViewSetup used by the
	// recorder render hook. Returns true when the view was modified.
	bool IsArtGlobalFovOverrideActive();
	bool ApplyArtGlobalFov( CViewSetup &view );
	bool PauseArtDemoAfterRecordingIfEnabled();
	bool ResumeArtDemoBeforeRecordingIfEnabled();

	// Applies no-flash and no-smoke state immediately before Source renders a view.
	// No-flash clears replicated player flash values; no-smoke suppresses only smoke
	// particle materials and restores their original NO_DRAW flags when disabled.
	void MaintainArtVisualEffectsForRender();

	// Applies or restores tint on the six active skybox face materials immediately
	// before Source renders a view. This never mutates material state from D3D hooks.
	void MaintainArtSkyboxChamsForRender();

	// Enables the lightweight ObjectID override around one pass render.
	// BSP/map geometry uses full-distance fog, the disabled skybox is replaced by
	// the clear color, and only players/viewmodel use the normal flat chams material.
	bool BeginArtObjectIdPass();
	void EndArtObjectIdPass();
	void GetArtObjectIdColors( int &worldRed, int &worldGreen, int &worldBlue,
		int &skyboxRed, int &skyboxGreen, int &skyboxBlue );
	void GetArtObjectIdCategoryColors( int colors[4][3] );

	// Players-pass world-weapon classification shared with the recorder's model hook.
	bool AreArtPlayersPassWorldWeaponsEnabled();
	bool IsArtWorldWeaponModel( const ModelRenderInfo_t &info );

	// Integrates visual model controls into the recorder's existing DrawModelEx
	// hook. Returns true when this function has already submitted or suppressed
	// the model and the caller must return result without drawing it again.
	bool HandleArtGuiDrawModelEx(
		IVModelRender *pThis,
		ModelRenderInfo_t &info,
		ArtDrawModelExFn pOriginalDrawModelEx,
		bool playerPassActive,
		bool playerPassEntity,
		int &result );
}
