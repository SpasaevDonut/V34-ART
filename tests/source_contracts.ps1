[CmdletBinding()]
param()

$ErrorActionPreference = "Stop"
$repositoryRoot = (Resolve-Path -LiteralPath (Join-Path $PSScriptRoot "..")).Path
$failures = [System.Collections.Generic.List[string]]::new()

function Require-Text {
	param(
		[Parameter(Mandatory = $true)][string]$Content,
		[Parameter(Mandatory = $true)][string]$Expected,
		[Parameter(Mandatory = $true)][string]$Description
	)
	if (-not $Content.Contains($Expected)) {
		$script:failures.Add($Description)
	}
}

function Require-NotText {
	param(
		[Parameter(Mandatory = $true)][string]$Content,
		[Parameter(Mandatory = $true)][string]$Unexpected,
		[Parameter(Mandatory = $true)][string]$Description
	)
	if ($Content.Contains($Unexpected)) {
		$script:failures.Add($Description)
	}
}

$entry = Get-Content -LiteralPath (Join-Path $repositoryRoot "dll\v34_art.cpp") -Raw
$commands = Get-Content -LiteralPath (Join-Path $repositoryRoot "dll\art_commands.cpp") -Raw
$logging = Get-Content -LiteralPath (Join-Path $repositoryRoot "dll\art_logging.cpp") -Raw
$logic = Get-Content -LiteralPath (Join-Path $repositoryRoot "dll\art_logic.cpp") -Raw
$render = Get-Content -LiteralPath (Join-Path $repositoryRoot "dll\art_render.cpp") -Raw
$runtime = Get-Content -LiteralPath (Join-Path $repositoryRoot "dll\art_runtime.cpp") -Raw
$statistics = Get-Content -LiteralPath (Join-Path $repositoryRoot "dll\art_statistics.cpp") -Raw
$pipeline = Get-Content -LiteralPath (Join-Path $repositoryRoot "dll\art_pipeline.cpp") -Raw
$hlae = Get-Content -LiteralPath (Join-Path $repositoryRoot "dll\art_hlae.cpp") -Raw
$gui = Get-Content -LiteralPath (Join-Path $repositoryRoot "dll\art_gui.cpp") -Raw
$loader = Get-Content -LiteralPath (Join-Path $repositoryRoot "loader\v34_art_loader.cpp") -Raw
$project = Get-Content -LiteralPath (Join-Path $repositoryRoot "dll\v34_art.vcxproj") -Raw
$versionHeader = Get-Content -LiteralPath (Join-Path $repositoryRoot "include\v34_art_version.h") -Raw
$commonScript = Get-Content -LiteralPath (Join-Path $repositoryRoot "scripts\common.ps1") -Raw
$afterEffectsImporter = Get-Content -LiteralPath (Join-Path $repositoryRoot "tools\after_effects\ART_Importer_v1.0.jsx") -Raw
$afterEffectsCameraBaker = Get-Content -LiteralPath (Join-Path $repositoryRoot "tools\after_effects\ART_Camera_Baker_v1.0.jsx") -Raw
$packageRelease = Get-Content -LiteralPath (Join-Path $repositoryRoot "scripts\package-release.ps1") -Raw
$buildScript = Get-Content -LiteralPath (Join-Path $repositoryRoot "scripts\build.ps1") -Raw
$buildBatch = Get-Content -LiteralPath (Join-Path $repositoryRoot "build.bat") -Raw
$loaderProject = Get-Content -LiteralPath (Join-Path $repositoryRoot "loader\v34_art_loader.vcxproj") -Raw
$camPathHeader = Get-Content -LiteralPath (Join-Path $repositoryRoot "third_party\advancedfx\shared\CamPath.h") -Raw
$mirvCampathSource = Get-Content -LiteralPath (Join-Path $repositoryRoot "third_party\advancedfx\shared\MirvCampath.cpp") -Raw

Require-Text $entry "static const bool kEnableDebugLoggingOnInjection = false;" "Emergency injection logging must default to off."
Require-Text $logging "volatile LONG g_bDebugLogging = FALSE;" "Runtime file logging must default to off."
Require-Text $logging "const DWORD creation = g_bLogOpenedOnce ? OPEN_ALWAYS : CREATE_ALWAYS;" "Logging reopen semantics changed."
Require-NotText $commands "Inspired by HLAE and sin1ster's DOF config." "Removed help attribution text remains."
Require-Text $commands 'V34_ART_PRODUCT_NAME " v" V34_ART_VERSION_STRING' "Help header does not use shared project metadata."
Require-Text $commands "__DATE__, __TIME__" "Help must include compile date and time."
Require-Text $versionHeader '#define V34_ART_VERSION_STRING "1.0"' "Shared display version must be 1.0."
Require-Text $versionHeader '#define V34_ART_DISTRIBUTION_BASENAME "v34_art_v1.0"' "Versioned distribution base name is incorrect."
Require-Text $versionHeader '#define V34_ART_PRODUCT_NAME "CS:S V34 ADVANCED RECORDING TOOLS"' "Shared product name is incorrect."
Require-Text $gui 'static const char *kGuiVersion = V34_ART_VERSION_STRING;' "GUI version is not tied to ART V1.0."
Require-Text $gui 'ImGui::Begin( V34_ART_PRODUCT_NAME " V" V34_ART_VERSION_STRING' "Main GUI title does not display the shared V1.0 version."
Require-Text $commonScript "function Get-ArtDistributionBaseName" "Build scripts cannot derive the versioned distribution name."

foreach ($command in @(
	"art_start", "art_stop", "art_status", "art_viewmodel_color", "art_players_color",
	"art_viewmodel_fov", "art_prefix", "art_record", "art_hud",
	"art_preview", "art_preview_next", "art_toggle", "art_stats",
	"art_queue", "art_tga_compression", "art_validation", "art_take_json", "art_help", "art_debug"
)) {
	Require-Text $commands ("ConCommand " + $command + "( `"" + $command + "`"") "Command registration missing: $command"
}

foreach ($module in @(
	"v34_art.cpp", "art_commands.cpp", "art_logging.cpp",
	"art_logic.cpp", "art_render.cpp", "art_runtime.cpp", "art_statistics.cpp",
	"art_pipeline.cpp", "art_hlae.cpp"
)) {
Require-Text $project $module "DLL project module missing: $module"
}
Require-Text $project '/PDBALTPATH:%_PDB%' "Release binaries must not embed machine-specific absolute PDB paths."

Require-Text $render "const int kViewRenderVtableIndex = 23;" "View_Render vtable contract changed."
Require-Text $render "const int kDrawModelExVtableIndex = 19;" "DrawModelEx vtable contract changed."
Require-Text $render "MODEL RENDER HOOK INSTALL COMPLETE" "Model-render hook validation marker is missing."
Require-Text $render "const int kHudProcessInputVtableIndex = 7;" "HudProcessInput vtable contract changed."
Require-Text $render "const int kClientModeGetViewmodelFovVtableIndex = 32;" "GetViewModelFOV vtable contract changed."

$passMarkers = @("pass='viewmodel'", "pass='players'", "pass='objectid'", "pass='depth'", "pass='clear'")
$previousIndex = -1
foreach ($marker in $passMarkers) {
	$index = $render.IndexOf($marker, [StringComparison]::Ordinal)
	if ($index -le $previousIndex) {
		$failures.Add("Render pass order changed near $marker.")
	}
	$previousIndex = $index
}

Require-Text $runtime 'ConVar art_depth_start( "art_depth_start", "150"' "Depth-start default must remain 150."
Require-Text $runtime 'ConVar art_depth_end( "art_depth_end", "800"' "Depth-end default must remain 800."
Require-Text $runtime 'volatile LONG g_nRecordMask = RECORD_NORMAL;' "Only the normal recording pass must default to on."
Require-Text $runtime 'volatile LONG g_nHudMask = RECORD_NORMAL | RECORD_CLEAR | RECORD_CLEAR_NOPLAYERS;' "Normal, clear, and clear-noplayers HUD must default to on."
Require-Text $runtime 'int g_nViewmodelBackgroundGreen = 255;' "Viewmodel chroma color default is missing."
Require-Text $runtime 'int g_nPlayersBackgroundGreen = 255;' "Players chroma color default is missing."
Require-Text $runtime 'CreatePassDirectory( "viewmodel" )' "Viewmodel output directory is missing."
Require-NotText $runtime 'CreatePassDirectory( "green" )' "Legacy Green output directory remains."
Require-Text $runtime 'method=min_rgb_grayscale' "Depth reflection cleanup contract is missing."
Require-Text $runtime "EnsureArtQueueCapacity" "Capture must apply queue backpressure before allocating frame buffers."
Require-Text $runtime "AllocateArtCaptureMemory" "Capture must flush and retry memory allocations."
Require-Text $runtime "EncodeArtTgaRle" "Capture does not expose TGA RLE encoding."
Require-Text $runtime "ART_TIMING_READ" "Framebuffer read timing is missing."
Require-Text $runtime "ART_TIMING_ENCODE" "TGA encode timing is missing."
Require-Text $runtime "ART_TIMING_WRITE" "Write-enqueue timing is missing."
Require-Text $render "ART_TIMING_RENDER" "Pass-render timing is missing."
Require-Text $pipeline "GlobalMemoryStatusEx" "Queue safety must preserve virtual address space."
Require-Text $pipeline "AsyncFinishAllWrites" "Queue backpressure must finish accepted asynchronous writes."
Require-Text $pipeline "ART_QUEUE_DEFAULT_MAX_FILES," "Bounded queue file default changed."
Require-Text $pipeline "ART_TGA_COMPRESSION_AUTO" "Automatic TGA RLE mode is missing."
Require-Text $logic "pDestination[2] = 10;" "RLE output must use TGA image type 10."
Require-Text $logic "const size_t rowEnd = sourcePixel + width;" "TGA RLE packets must be bounded to one scanline."
Require-Text $pipeline "ART_TIMING_QUEUE" "Queue wait timing is missing."

Require-Text $gui 'Players pass: render players through walls' "Players-pass wall-rendering label is missing."
Require-Text $gui 'Players pass: world weapon models' "Players-pass world-weapon option is missing."
Require-Text $gui 'art_players_world_weapons' "Players-pass world-weapon command is missing."
Require-Text $gui 'mode != 3 && mode != 4 && mode != 5' "Spectator controls must accept first person, third person, and free cam."
Require-Text $gui 'ApplyDemoSpectatorMode( 3, SelectedDemoPlayer() != NULL );' "First-person spectator mode must use spec_mode 3."
Require-Text $gui 'ApplyDemoSpectatorMode( 4, SelectedDemoPlayer() != NULL );' "Third-person spectator mode must use spec_mode 4."
Require-Text $gui 'ApplyDemoSpectatorMode( 5, false );' "Free-cam spectator mode must use spec_mode 5."
Require-Text $gui 'ImGui::Button( "Free cam"' "The Capture spectator section is missing its Free cam button."
Require-Text $gui 'IssueCommand( "spec_player \"%s\"", safeName );' "Selected-player spectating must use the exact quoted player name."
Require-Text $gui 'IssueCommand( "hidepanel specgui" );' "Spectator-bar control must hide the passive specgui panel."
Require-Text $gui 'IssueCommand( "hidepanel specmenu" );' "Spectator-bar control must hide the default camera menu panel."
Require-Text $gui 'IssueCommand( "spec_menu 0" );' "Spectator-bar control must close the default CS:S camera menu."
Require-Text $gui "MaintainHiddenSpectatorPanels();" "Hidden spectator panels must remain suppressed during demo loading."
Require-Text $gui "ScheduleDemoPlayerAutoRefresh" "Demo playback must schedule automatic player-list refresh."
Require-Text $gui "kDemoPlayerRefreshMaximumAttempts" "Demo player auto-refresh must be bounded."
Require-Text $gui 'art_chams skybox' "Skybox chams command is missing."
Require-Text $gui 'MaintainSkyboxChamsMaterials();' "Skybox material maintenance is missing."
Require-Text $gui 'g_SkyboxMaterialStates' "Skybox material restoration state is missing."
Require-Text $gui 'g_pCvar->FindVar( "sv_skyname" )' "Skybox lookup must use the active sv_skyname."
Require-Text $gui '"skybox/%s%s"' "Skybox lookup must target the six active face materials."
Require-Text $gui 'pMaterial->IncrementReferenceCount();' "Skybox material references must remain valid until restoration."
Require-Text $gui 'pMaterial->DecrementReferenceCount();' "Skybox material references must be released after restoration."
Require-Text $gui 'g_bSkyboxChamsUpdateActive' "Skybox maintenance must prevent reentrant state mutation."
Require-Text $gui 'ReapplySkyboxChamsTint();' "Skybox tint must be maintained every frame."
Require-Text $gui 'void MaintainArtSkyboxChamsForRender()' "Skybox render-stage maintenance entry point is missing."
Require-Text $render 'MaintainArtSkyboxChamsForRender();' "View_Render must maintain skybox chams before rendering."
Require-NotText $gui 'pMaterial->GetShaderName()' "Skybox chams must not call the crash-prone shader-name vtable method."
Require-NotText $gui 'SKYBOX CHAMS APPLIED: shader_aware=1' "Old shader-enumeration skybox implementation remains."
Require-Text $gui "BuildSafeTakePath" "Take management must revalidate targets under the output root."
Require-Text $gui "FILE_ATTRIBUTE_REPARSE_POINT" "Take management must reject reparse points."
Require-Text $gui "FOF_ALLOWUNDO" "Take deletion must request the Recycle Bin."
Require-Text $gui '"Confirm recycle"' "Take recycling must require explicit GUI confirmation."
Require-Text $gui "stop recording before renaming a take" "Take rename must be blocked while recording."
Require-Text $gui "Capture pipeline safety" "Output-page queue controls are missing."
Require-Text $gui 'Generate [take].json' "Output-page take JSON control is missing."
Require-Text $gui 'art_take_json on' "Take JSON must be enabled in the generated default config."
Require-Text $gui 'art_viewmodel_color 0 255 0' "Default Viewmodel chroma command is missing."
Require-Text $gui 'art_players_color 0 255 0' "Default Players chroma command is missing."
Require-NotText $gui 'MaintainSkyboxChamsMaterials();
		if ( !IsArtGuiVisible() )' "Skybox maintenance must not run from the D3D GUI hook."
foreach ($removedWorldChamsMarker in @(
    'g_bWorldChamsEnabled', 'g_nWorldChamsRed', 'g_nWorldChamsGreen', 'g_nWorldChamsBlue',
    'WorldMaterialState', 'g_WorldMaterialStates', 'MaintainWorldChamsMaterials',
    'ApplyWorldChamsMaterials', 'IsWorldSurfaceMaterial', 'IsMapPropModel',
    'art_chams world', 'art_chams world_color', 'World and map props',
    'World and props color', 'world_and_props='
)) {
    Require-NotText $gui $removedWorldChamsMarker "World/map chams code remains: $removedWorldChamsMarker"
}

if ($gui.Contains('g_bPropsChamsEnabled') -or $gui.Contains('g_nPropsChamsRed') -or
    $gui.Contains('world_remove_' + 'textures') -or $gui.Contains('Remove ' + 'textures') -or
    $gui.Contains('[v' + '17]')) {
    $failures.Add("Removed prop-chams, texture-removal or temporary build-marker code is still present.")
}

Require-NotText $commands 'art_chams world' "World chams command remains in art_commands.cpp."
Require-NotText $commands 'world_color' "World chams color remains in art_commands.cpp."
Require-NotText $render 'art_chams world' "World chams command remains in art_render.cpp."
Require-NotText $render 'world_color' "World chams color remains in art_render.cpp."

Require-Text $runtime 'RECORD_CLEAR_NOPLAYERS' "clear-noplayers record bit is missing."
Require-Text $gui 'host_framerate 0' "Default host_framerate must be zero."
Require-Text $gui 'hostFramerate( 0 )' "GUI host_framerate state must initialize to zero."
Require-Text $gui 'CS:S V34 ADVANCED RECORDING TOOLS' "Control panel title is missing."
Require-Text $gui '_strnicmp( pName, "art_", 4 )' "ART command-prefix filtering must use the four-character prefix length."
Require-NotText $gui '_strnicmp( pName, "art_", 8 )' "Legacy command-prefix length remains after the ART rename."
Require-Text $gui 'clear-noplayers' "Clear-no-players GUI pass is missing."
Require-Text $gui 'Open output folder' "Output page Open folder button is missing."
Require-Text $gui 'ConCommand g_ArtOpenFolderCommand( "art_open_folder"' "Open-folder console command is missing."
Require-Text $gui 'ConCommand g_ArtOverlayCommand( "art_overlay"' "Statistics-overlay console command is missing."
Require-Text $gui 'IssueCommand( "art_preview_next" );' "Next-preview GUI button is missing."
Require-Text $gui 'IM_COL32( 255, 58, 42, 255 )' "Recording overlay must use red text."
Require-Text $gui 'IM_COL32( 255, 145, 32, 255 )' "Idle overlay must use orange text."
Require-Text $gui 'DrawRecordingStatisticsOverlay();' "Recording statistics overlay draw call is missing."
Require-Text $gui '"Queue: %lu/%ld files' "Recording overlay must show bounded queue usage."
Require-Text $gui '"Average ms: render %.2f' "Recording overlay must show pipeline timing."
Require-Text $gui '"TGA: %s  |  %.1f%% saved' "Recording overlay must show compression savings."
Require-Text $gui 'art_gui_theme orange' "The generated GUI config must default to the orange theme."
Require-Text $gui 'art_validation auto off' "Automatic post-recording validation must default to off."
Require-Text $gui 'const float flatChamsHeight =' "Flat chams GUI section must derive its height from its content."
Require-Text $gui 'ImGui::BeginChild( "flat_chams", ImVec2( 0, flatChamsHeight )' "Flat chams GUI section must use its content-derived height."
foreach ($paletteLine in @(
	"art_gui_color accent 209 114 71 255",
	"art_gui_color window 14 10 9 200",
	"art_gui_color panel 20 15 13 150",
	"art_gui_color sidebar 10 8 6 235",
	"art_gui_color text 235 237 247 255",
	"art_gui_color muted 143 150 173 255",
	"art_gui_color border 88 52 35 217",
	"art_gui_color control 34 27 23 245",
	"art_gui_color selection 181 34 0 235"
)) {
	Require-Text $gui $paletteLine "A generated default color does not match the approved palette: $paletteLine"
}
Require-Text $gui 'In-game recording statistics overlay' "Overlay settings control is missing."
Require-Text $gui 'io.MouseDrawCursor = IsArtGuiVisible();' "ImGui cursor initialization must follow menu visibility."
Require-Text $gui 'ImGui::GetIO().MouseDrawCursor = mainVisible && !g_Gui.hlaeInputWhileGuiActive;' "Overlay-only frames and mirv_input passthrough must not draw the menu cursor."
Require-Text $gui 'HlaeToggleButton( "Start camera##mirv_input_camera"' "mirv_input Camera/End is not an active-state toggle."
Require-Text $gui 'ConCommand g_ArtHlaeInputHoldKeyCommand( "art_hlae_input_hold_key"' "Configurable mirv_input GUI hold-key command is missing."
Require-Text $gui 'RefreshHlaeInputWhileGuiActive( guiVisible );' "mirv_input GUI passthrough is not refreshed from the configured hold key."
Require-Text $gui 'SetHlaeInputWhileGuiActive( false );' "mirv_input GUI passthrough does not release held controls safely."
Require-Text $gui 'art_hlae_input_hold_key LMB' "mirv_input GUI hold key does not default to LMB."
Require-Text $hlae 'IsArtGuiVisible() && !IsArtGuiMirvInputPassthroughActive()' "MirvInput remains suspended while GUI passthrough is active."
Require-Text $gui 'ImGui::GetIO().MouseDrawCursor = false;' "Closing the menu must disable the software cursor immediately."
Require-Text $gui 'ShellExecuteA( NULL, "open", outputDirectory' "Output folder shell action is missing."
Require-Text $gui 'Prefix is optional text added before every pass filename.' "Output prefix definition is missing."
Require-NotText $gui 'ImGui::Button( "Set path"' "Output path still requires a Set path button."
Require-NotText $gui 'ImGui::Button( "Apply##prefix"' "Output prefix still requires an Apply button."
Require-Text $gui 'const bool outputPathFinished = ImGui::IsItemDeactivatedAfterEdit();' "Output path is not applied automatically after editing."
Require-Text $gui 'ImGui::InputTextWithHint( "##prefix", "default",' "Output prefix field must use a hidden ID so the visible label can be positioned explicitly."
Require-Text $gui 'static const char *pPrefixLabel = "Prefix";' "Output prefix label is not positioned beside the field."
Require-Text $gui 'const bool prefixFinished = ImGui::IsItemDeactivatedAfterEdit();' "Output prefix is not applied automatically after editing."
Require-Text $gui '"Global FOV", &globalFov, 0.1f, 1.0f, 179.0f, "%.2f"' "Global FOV must use the compact depth-style drag control."
Require-Text $gui '"Viewmodel FOV", &viewmodelFov, 0.1f, 1.0f, 179.0f, "%.2f"' "Viewmodel FOV must use the compact depth-style drag control."
Require-NotText $gui '##global_fov_exact' "The obsolete separate Global FOV exact input remains."
Require-NotText $gui '##viewmodel_fov_exact' "The obsolete separate viewmodel FOV exact input remains."
Require-Text $gui 'view.fovViewmodel = g_flGlobalFov;' "Recorded Global FOV must also override the viewmodel FOV."
Require-Text $render 'pSetup->fovViewmodel = g_flGlobalFov;' "Live Global FOV must also override the viewmodel FOV."
Require-Text $render 'return g_flGlobalFov;' "GetViewModelFOV must return Global FOV while its override is active."
Require-Text $gui 'Reset everything to default' "Config reset control is missing."
Require-Text $gui 'exec art_gui/art_default.cfg' "Config reset must execute the built-in default config."
Require-Text $gui 'ImGui::Begin( "Capture Help", &g_Gui.showCaptureHelp' "Capture Help must open as a separate in-game window."
Require-Text $gui 'g_Gui.showCaptureHelp = true;' "Capture Help button must request the help window."
Require-Text $gui 'IssueCommandWithSeparator( "art_help" );' "Capture Help button must also print help in the console."
Require-NotText $gui 'ImGui::Button( "Print status"' "Capture menu still contains the removed Print status button."
Require-Text $gui 'ImGui::Button( pausedDemo ? "Resume" : "Pause", ImVec2( 120, 0 ) )' "Capture demo playback does not use one Pause/Resume toggle button."
Require-Text $gui 'void ToggleDemoPlayback( bool paused )' "Shared demo Pause/Resume toggle behavior is missing."
Require-NotText $gui 'ImGui::Button( "Apply##fps"' "host_framerate still requires an Apply button."
Require-Text $gui 'if ( ImGui::InputInt( "##host_framerate", &g_Gui.hostFramerate, 0, 0 ) )' "host_framerate does not update dynamically through an unlabeled field."
Require-NotText $gui '"Value##host_framerate"' "host_framerate still displays the removed Value label."
Require-Text $gui 'const float hostFramerateControlHeight = ImGui::GetFrameHeight();' "host_framerate preset height is not derived from the input-field height."
Require-Text $gui 'hostFrameratePresets[i] >= 1000 ? 52.0f : 44.0f, hostFramerateControlHeight' "host_framerate preset buttons do not match the field height."
Require-Text $gui 'ImGuiChildFlags_Borders | ImGuiChildFlags_AutoResizeY' "Content-sized GUI sections do not use automatic vertical sizing."
Require-NotText $gui 'ImGui::TextUnformatted( "Demo info" )' "Demo info section must be removed from the Info page."
Require-Text $gui 'static const char *previewNames[] = { "Off", "Normal", "Clear", "Clear - no players", "Viewmodel", "Depth", "Players", "ObjectID" };' "Clear-no-players must follow Clear in Live preview."
Require-Text $render 'const bool worldWeaponModel = IsArtWorldWeaponModel( info );' "Players-pass world-weapon detection is missing."
Require-Text $render 'playerPassEntity = worldWeaponModel ? AreArtPlayersPassWorldWeaponsEnabled() : anyPlayerEntity;' "Players-pass world-weapon filtering is missing."
Require-Text $gui 'ImGui::Checkbox( "No flash", &noFlash )' "No-flash GUI option is missing."
Require-Text $gui 'ImGui::Checkbox( "No smoke", &noSmoke )' "No-smoke GUI option is missing."
Require-Text $gui 'ConCommand g_ArtNoFlashCommand( "art_noflash"' "No-flash command is missing."
Require-Text $gui 'ConCommand g_ArtNoSmokeCommand( "art_nosmoke"' "No-smoke command is missing."
Require-Text $gui '"m_flFlashDuration"' "No-flash duration netvar lookup is missing."
Require-Text $gui '"m_flFlashMaxAlpha"' "No-flash alpha netvar lookup is missing."
Require-Text $gui 'MATERIAL_VAR_NO_DRAW' "No-smoke material suppression is missing."
Require-Text $gui 'RestoreNoSmokeMaterials();' "No-smoke material restoration is missing."
Require-Text $gui 'art_noflash off' "No-flash must default to off."
Require-Text $gui 'art_nosmoke off' "No-smoke must default to off."
Require-Text $gui 'volatile LONG g_bForceRenderLodEnabled = FALSE;' "Forced r_lod must default to off."
Require-Text $gui 'volatile LONG g_nForcedRenderLodValue = -7;' "Forced r_lod default value must remain -7."
Require-Text $gui 'InstallGlobalChangeCallback( ArtRenderLodCvarChanged )' "The r_lod global cvar change hook is missing."
Require-Text $gui 'MaintainForcedRenderLod();' "The render-time r_lod fallback is missing."
Require-Text $gui 'ConCommand g_ArtForceRenderLodCommand( "art_force_r_lod"' "Forced r_lod command is missing."
Require-Text $gui 'ImGui::Checkbox( "Force r_lod", &forceRenderLod )' "Forced r_lod GUI toggle is missing."
Require-Text $gui 'ImGui::InputInt( "r_lod value", &forcedRenderLod )' "Forced r_lod GUI value field is missing."
Require-Text $gui '"art_force_r_lod value -7\r\n"' "Generated default config is missing the r_lod value."
Require-Text $gui '"art_force_r_lod off\r\n"' "Generated default config must keep r_lod forcing off."
Require-Text $runtime 'RECORD_OBJECTID' "ObjectID record bit is missing."
Require-Text $runtime 'case PREVIEW_OBJECTID: return "objectid";' "ObjectID preview name is missing."
Require-Text $gui 'ObjectID colors' "ObjectID GUI color controls are missing."
Require-Text $gui 'art_objectid_color' "ObjectID color command is missing."
Require-Text $gui 'CREATE_ALWAYS' "Generated default config must be refreshed after updates."
Require-Text $gui 'volatile LONG g_nObjectIdViewmodelRed = 255;' "Default ObjectID viewmodel red component is incorrect."
Require-Text $gui 'volatile LONG g_nObjectIdViewmodelGreen = 128;' "Default ObjectID viewmodel green component is incorrect."
Require-Text $gui 'volatile LONG g_nObjectIdViewmodelBlue = 0;' "Default ObjectID viewmodel blue component is incorrect."
Require-Text $gui 'volatile LONG g_nObjectIdPlayersRed = 0;' "Default ObjectID player red component is incorrect."
Require-Text $gui 'volatile LONG g_nObjectIdPlayersGreen = 0;' "Default ObjectID player green component is incorrect."
Require-Text $gui 'volatile LONG g_nObjectIdPlayersBlue = 255;' "Default ObjectID player blue component is incorrect."
Require-Text $gui 'volatile LONG g_nObjectIdWorldRed = 255;' "Default ObjectID world red component is incorrect."
Require-Text $gui 'volatile LONG g_nObjectIdWorldGreen = 255;' "Default ObjectID world green component is incorrect."
Require-Text $gui 'volatile LONG g_nObjectIdWorldBlue = 0;' "Default ObjectID world blue component is incorrect."
Require-Text $gui 'volatile LONG g_nObjectIdSkyboxRed = 255;' "Default ObjectID skybox red component is incorrect."
Require-Text $gui 'volatile LONG g_nObjectIdSkyboxGreen = 0;' "Default ObjectID skybox green component is incorrect."
Require-Text $gui 'volatile LONG g_nObjectIdSkyboxBlue = 0;' "Default ObjectID skybox blue component is incorrect."
Require-Text $gui 'BeginArtObjectIdPass' "ObjectID pass setup is missing."
Require-Text $gui 'ResolveFlatChamsMaterial' "ObjectID must reuse the working flat chams material."
Require-Text $gui 'fog_world=1 flat_players_viewmodel=1 clear_skybox=1' "Lightweight ObjectID pass marker is missing."
Require-Text $runtime 'Never toggle material-system quality cvars here' "ObjectID material-rebuild prevention is missing."
Require-Text $runtime 'vars.Set( "fog_color", worldColor );' "ObjectID world color must use full-distance fog."
Require-Text $render 'ClearColor4ub( skyboxRed, skyboxGreen, skyboxBlue, 255 )' "ObjectID skybox must use the selected clear color."
Require-Text $runtime 'vars.Set( "fog_start", "-10000" );' "ObjectID fog must cover the complete scene."
Require-Text $runtime 'vars.Set( "fog_maxdensity", "1" );' "ObjectID fog must reach an exact solid category color."
Require-NotText $runtime 'vars.Set( "mat_fullbright"' "ObjectID must not toggle mat_fullbright per frame."
Require-NotText $runtime 'vars.Set( "mat_bumpmap"' "ObjectID must not trigger material rebuilds per frame."
Require-Text $runtime 'vars.Set( "r_skybox", "0" );' "ObjectID skybox must be replaced by the clear color."
Require-NotText $gui 'PassiveWindowHookCleanupThread' "Unsafe asynchronous window-close cleanup worker remains."
Require-NotText $gui 'CreateThread( NULL, 0, PassiveWindowHookCleanupThread' "Window close must not spawn a hook-removal worker."
Require-Text $gui 'WINDOW CLOSE PREPARED: wndproc_restored=1 d3d_hooks_removed=1 source_hooks_removed=1' "Synchronous pre-teardown close cleanup marker is missing."
Require-Text $gui 'RemoveD3D9Hooks();' "Window close must restore D3D hooks before Source teardown."
Require-Text $gui 'RemoveViewRenderHook();' "Window close must restore the View_Render hook before Source teardown."
Require-Text $gui 'RemoveModelRenderHook();' "Window close must restore the model-render hook before Source teardown."
Require-Text $gui 'RemoveClientModeFovHook();' "Window close must restore the FOV hooks before Source teardown."
Require-Text $gui 'before forwarding WM_CLOSE to Source' "Close cleanup must run before the original Source window procedure."
Require-Text $gui 'TerminateProcess( GetCurrentProcess(), 0 )' "Abrupt window close must bypass the unsafe late Source/D3D detach sequence."
Require-Text $gui 'if ( message == WM_CLOSE )' "WM_CLOSE hard-exit guard is missing."
Require-Text $render 'IsArtGuiTerminating()' "Render hooks must switch to passthrough during shutdown."
Require-NotText $gui 'ObjectIdTextureState' "Crash-prone ObjectID texture state remains."
Require-NotText $gui 'ObjectIdMaterialState' "Crash-prone ObjectID material mutation state remains."
Require-NotText $gui 'ReplaceObjectIdTexture' "ObjectID must not rewrite live material textures."
Require-NotText $gui 'g_pObjectIdWhiteTexture' "ObjectID must not retain a live texture pointer."
Require-Text $render 'ART_PREVIEW_OBJECTID' "ObjectID live preview rendering is missing."
Require-Text $render 'ART_RECORD_OBJECTID' "ObjectID recording render is missing."
Require-Text $render 'MaintainArtVisualEffectsForRender();' "View_Render must maintain no-flash and no-smoke before rendering."
Require-Text $runtime 'RecordArtCapturedFile( pPassName, g_nFrame' "Successful asynchronous writes must update footage statistics."
Require-Text $render 'RecordArtCompletedFrame();' "Completed recording frames must update footage statistics."
Require-Text $statistics 'g_ExpectedFrames[ART_CAPTURE_PASS_COUNT]' "Validation must retain exact expected frames per pass."
Require-Text $statistics 'ValidateTgaFile' "Validation must inspect recorded TGA headers and pixel data."
Require-Text $statistics 'packetPixels > rowRemaining' "Validation must reject RLE packets that cross scanlines."
Require-Text $statistics 'g_ArtValidationResult.invalidPixelData' "Validation must report damaged TGA pixel data."
Require-Text $statistics 'RunAutomaticArtValidation' "Automatic post-recording validation is missing."
Require-Text $statistics 'volatile LONG g_bArtTakeManifestEnabled = TRUE;' "Per-take JSON must default to on."
Require-Text $statistics 'css-v34-art.take' "Take JSON schema marker is missing."
Require-Text $statistics 'filename_pattern' "Take JSON sequence filename pattern is missing."
Require-Text $statistics 'frame_ranges' "Take JSON compact frame ranges are missing."
Require-Text $statistics 'logic::CalculateWidescreenHorizontalFov' "Actual Source widescreen FOV recording is missing."
Require-Text $logic 'aspectRatio / baseAspect' "Source 4:3-to-recording-aspect FOV conversion changed."
Require-Text $statistics 'source_engine_actual' "Take JSON actual Source-engine FOV object is missing."
Require-Text $statistics 'camera_horizontal_degrees' "Take JSON actual horizontal camera FOV is missing."
Require-Text $statistics 'viewmodel_horizontal_degrees' "Take JSON actual horizontal viewmodel FOV is missing."
Require-Text $statistics 'category_colors' "Take JSON ObjectID category colors are missing."
Require-Text $statistics '"  \"generator\": {\n"' "Take JSON must be written as indented, human-readable JSON."
Require-Text $gui 'GetArtObjectIdCategoryColors' "ObjectID colors cannot be snapshotted for post-production metadata."
Require-Text $statistics 'MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH' "Take JSON finalization must atomically replace the manifest."
Require-Text $statistics 'WriteArtTakeManifest( false );' "Take JSON is not integrated into the recording lifecycle."
Require-Text $statistics 'class BufferedFileReader' "Deep validation must use buffered sequential file reads."
Require-Text $statistics 'm_Buffer[64 * 1024]' "Deep validation read buffer size changed."
Require-Text $statistics 'CreateThread( NULL, 0, ArtValidationWorker' "Validation must run outside the render/command thread."
Require-Text $statistics 'g_ArtValidationProgress.completedFiles' "Validation file progress tracking is missing."
Require-Text $commands 'RunAutomaticArtValidation();' "Normal recording stop must trigger automatic validation."
Require-Text $commands 'wait for validation to finish' "Recording start must not race a running validation."
Require-Text $render 'RunAutomaticArtValidation();' "Aborted recordings must trigger automatic validation."
Require-Text $gui 'ImGui::ProgressBar( GetArtValidationProgressFraction()' "Info-page validation progress bar is missing."
Require-Text $gui 'ART VALIDATING' "In-game validation progress overlay is missing."
Require-Text $statistics 'g_ArtValidationResult.missingFiles' "Validation must detect missing files."
Require-Text $statistics 'g_ArtValidationResult.undersizedFiles' "Validation must detect undersized files."
Require-Text $gui 'ImGui::PushStyleColor( ImGuiCol_Button, ImVec4( 0.72f, 0.08f, 0.06f, 1.0f ) );' "The active Stop recording button must be red."
Require-Text $gui 'bool IsArtGuiTerminating()' "GUI termination-state accessor is missing."
Require-Text $render 'IsArtGuiTerminating()' "Render hooks do not use the exported termination-state accessor."
Require-NotText $render 'g_bArtGuiTerminating' "Render code must not reference the private GUI termination variable directly."
Require-Text $loader 'ART Loader v' "Styled Loader banner is missing."
Require-Text $loader 'PrintStep( 4, L"INJECTION"' "Loader does not expose the four-step injection flow."
Require-Text $loader 'READY' "Loader success state is missing."
Require-Text $loader 'Press any key to close' "Loader close interaction is missing."

$passesPageStart = $gui.IndexOf("void DrawPassesPage()", [StringComparison]::Ordinal)
$visualsPageStart = $gui.IndexOf("void DrawVisualsPage()", [StringComparison]::Ordinal)
$outputPageStart = $gui.IndexOf("void DrawOutputPage()", [StringComparison]::Ordinal)
if ($passesPageStart -lt 0 -or $visualsPageStart -le $passesPageStart -or $outputPageStart -le $visualsPageStart) {
	$failures.Add("Passes/Visuals/Output GUI page boundaries could not be found.")
}
else {
	$passesPage = $gui.Substring($passesPageStart, $visualsPageStart - $passesPageStart)
	$visualsPage = $gui.Substring($visualsPageStart, $outputPageStart - $visualsPageStart)
	foreach ($passControl in @("Viewmodel background", "Players background", "Depth start", "Depth end")) {
		Require-Text $passesPage $passControl "Pass-specific control is not on the Passes page: $passControl"
		Require-NotText $visualsPage $passControl "Pass-specific control remains on the Visuals page: $passControl"
	}
}

Require-Text $gui 'TextUnformattedWrapped( "About" );' "Appearance is missing its About section."
Require-NotText $gui 'MIT License - see LICENSE and THIRD_PARTY_NOTICES.md' "The removed MIT license line remains in About."
Require-Text $gui 'TextUnformattedWrapped( "Created by Contrastniy" );' "About is missing the creator."
Require-Text $gui 'https://www.youtube.com/@Contrastniy' "About is missing the creator YouTube channel."
Require-Text $gui 'ImGui::Text( "Build: %s %s", __DATE__, __TIME__ );' "About is missing build data."
Require-NotText $gui 'Interface version:' "Removed interface-version text remains in the GUI."
Require-NotText $gui 'Inspired by HLAE / advancedfx' "Removed program-information attribution remains."
Require-NotText $gui 'Windows x86' "Removed Windows architecture text remains in program information."
Require-Text $gui 'void TextUnformattedWrapped' "GUI plain text has no auto-wrapping helper."
Require-Text $gui 'void TextUnformattedSingleLine' "Aligned HLAE table labels have no single-line helper."
Require-Text $gui 'void TextDisabledWrapped' "GUI muted text has no auto-wrapping helper."
if (([regex]::Matches($gui, 'ImGui::TextUnformatted\(')).Count -ne 1) {
	$failures.Add("Every direct plain-text GUI call except the wrapping helper must use TextUnformattedWrapped.")
}
Require-NotText $gui 'ImGui::TextDisabled(' "Muted GUI text bypasses the auto-wrapping helper."
Require-Text $gui 'TextDisabledWrapped( "Global FOV overrides both the camera and viewmodel FOV.' "Global FOV definition must use muted wrapped text."
Require-Text $gui 'ImGui::SetNextWindowSize( ImVec2( 1220, 760 )' "The main GUI does not use the requested wider default size."
Require-Text $gui 'art_demo_pause_after_recording off' "Automatic demo pause must default to off."
Require-Text $gui 'Pause demo after recording stops (experimental)' "Experimental automatic demo-pause GUI option is missing."
Require-Text $gui 'ConCommand g_ArtDemoPauseAfterRecordingCommand' "Automatic demo-pause command is missing."
Require-Text $commands 'PauseArtDemoAfterRecordingIfEnabled();' "Recording stop does not request the configured demo pause."

$capturePageStart = $gui.IndexOf("void DrawCapturePage()", [StringComparison]::Ordinal)
$passesPageBoundary = $gui.IndexOf("void DrawPassesPage()", [StringComparison]::Ordinal)
if ($capturePageStart -lt 0 -or $passesPageBoundary -le $capturePageStart) {
	$failures.Add("Capture GUI page boundaries could not be found.")
}
else {
	$capturePage = $gui.Substring($capturePageStart, $passesPageBoundary - $capturePageStart)
	if ($capturePage.IndexOf('"##host_framerate"', [StringComparison]::Ordinal) -gt
		$capturePage.IndexOf('"capture_demo"', [StringComparison]::Ordinal)) {
		$failures.Add("host_framerate must appear in the recording section before Demo playback.")
	}
	Require-NotText $capturePage '"capture_timing"' "The removed standalone host_framerate section remains."
	if ($capturePage.IndexOf('TextDisabledWrapped( "Presets" );', [StringComparison]::Ordinal) -ge 0) {
		$failures.Add("host_framerate still displays the removed Presets label.")
	}
}

$infoPageStart = $gui.IndexOf("void DrawInfoPage()", [StringComparison]::Ordinal)
$configsPageStart = $gui.IndexOf("void DrawConfigsPage()", [StringComparison]::Ordinal)
if ($infoPageStart -lt 0 -or $configsPageStart -le $infoPageStart) {
	$failures.Add("Info GUI page boundaries could not be found.")
}
else {
	$infoPage = $gui.Substring($infoPageStart, $configsPageStart - $infoPageStart)
	if ($infoPage.IndexOf("DrawValidationInfoSection( recording );", [StringComparison]::Ordinal) -gt
		$infoPage.IndexOf('"info_recording"', [StringComparison]::Ordinal)) {
		$failures.Add("Validation must be the first section on the Info page.")
	}
	if ($infoPage.IndexOf('"Recorded footage statistics"', [StringComparison]::Ordinal) -lt
		$infoPage.IndexOf('"info_files"', [StringComparison]::Ordinal)) {
		$failures.Add("Recorded footage statistics must be the bottom section on the Info page.")
	}
}
Require-Text $gui 'ImGui::SetNextItemOpen( false, ImGuiCond_Once );' "Recorded footage statistics do not default to collapsed."
Require-Text $gui 'ImGui::CollapsingHeader( "Recorded footage statistics" )' "Recorded footage statistics are not presented as a collapsible tab."
Require-Text $gui 'ImGui::SetScrollY( 0.0f );' "Info does not reset its scroll position to the top."
Require-Text $gui 'g_Gui.resetInfoScroll = true;' "Selecting Info does not request a top-scroll reset."
Require-Text $gui 'ImGui::BeginChild( "info_validation", ImVec2( 0, 0 ),' "Validation does not use a content-sized child."
Require-Text $gui 'ImGuiChildFlags_Borders | ImGuiChildFlags_AutoResizeY );' "Content-sized GUI sections are missing AutoResizeY."

foreach ($command in @(
	"art_hlae", "mirv_campath", "mirv_input", "mirv_camio",
	"mirv_agr", "mirv_camexport", "mirv_camimport", "mirv_fov"
)) {
	Require-Text $hlae ('ConCommand ' + $command + '( "' + $command + '"') "HLAE command registration missing: $command"
}
Require-Text $hlae 'VCLIENTENGINETOOLS_INTERFACE_VERSION' "AGR does not integrate with the CS:S client-engine tools interface."
Require-Text $hlae 'VCLIENTTOOLS_INTERFACE_VERSION' "AGR does not integrate with the CS:S client-tools interface."
Require-Text $entry 'InitializeArtHlae( clientFactory, engineFactory )' "HLAE initialization does not receive the engine app-system factory."
Require-Text $hlae 'g_pHlaeEngineTools = engineFactory ?' "VCLIENTENGINETOOLS001 is not queried from the -afxV34 engine factory."
Require-Text $hlae 'destructor occupies slot 0' "The VCLIENTENGINETOOLS001 inherited-destructor slot offset is not documented."
Require-Text $hlae 'g_ppHlaePreRenderSlot = &pVtable[5];' "The -afxV34 PreRenderAllTools hook must account for IBaseInterface's virtual destructor."
Require-Text $hlae 'g_ppHlaePostRenderSlot = &pVtable[6];' "The -afxV34 PostRenderAllTools hook must account for IBaseInterface's virtual destructor."
Require-Text $hlae 'g_ppHlaePostMessageSlot = &pVtable[7];' "The -afxV34 PostToolMessage hook must account for IBaseInterface's virtual destructor."
Require-Text $hlae 'g_ppHlaeAdjustViewportSlot = &pVtable[8];' "The -afxV34 AdjustEngineViewport hook must account for IBaseInterface's virtual destructor."
Require-Text $hlae 'g_ppHlaeSetupEngineViewSlot = &pVtable[9];' "The -afxV34 SetupEngineView hook must account for IBaseInterface's virtual destructor."
Require-Text $hlae 'g_ppHlaeFrameStageNotifySlot = &pClientVtable[32];' "The CSS v34 AGR FrameStageNotify hook is missing."
Require-Text $hlae 'ApplyArtHlaeView( view );' "The HLAE camera pipeline is not connected to SetupEngineView."
Require-Text $hlae 'InterlockedExchange( &g_Hlae.inputMouseDeltaX, 0 )' "mirv_input does not consume accumulated relative mouse movement."
Require-Text $hlae 'CaptureArtHlaeCursorPosition' "mirv_input does not capture legacy cursor movement before Source recenters it."
Require-Text $hlae 'SupplyArtHlaeRawMouseDelta' "mirv_input does not accept WM_INPUT raw mouse movement."
Require-Text $gui 'CreateQueuedHook( g_pGetCursorPosTarget' "The GetCursorPos detour required by mirv_input is missing."
Require-Text $gui 'SupplyArtHlaeRawInput( rawInput )' "The window hook does not forward WM_INPUT to mirv_input."
Require-Text $gui 'FilterHlaeInputHoldKeyFromRawInput( rawInput );' "The GUI passthrough hold key leaks into mirv_input raw input."
Require-Text $gui 'FilterHlaeInputHoldMouseState( wParam )' "The GUI passthrough mouse hold state leaks into mirv_input normal mouse events."
Require-Text $gui 'SynchronizeHlaeInputCursorAnchor();' "GUI passthrough does not synchronize the AdvancedFX cursor anchor."
Require-Text $hlae 'if ( IsArtGuiVisible() )' "Legacy GetCursorPos movement is not disabled while GUI raw-input passthrough is active."
Require-Text $gui 'void DrawHlaeInputTableRow' "HLAE numeric controls do not use explicit aligned label/control rows."
Require-NotText $gui 'DrawHlaeInputTableValue(' "The old per-cell HLAE table layout remains and can vertically shift columns."
Require-Text $hlae 'g_ArtRecordingStats.takeAbsolutePath' "HLAE exports are not routed into ART take folders."
Require-Text $hlae 'g_ArtRecordingStats.takeActive' "HLAE relative exports do not distinguish an active take from the latest take."
Require-Text $hlae 'g_bRecordBaseAbsolute' "HLAE idle path resolution does not use the configured output mode."
Require-Text $hlae 'g_szRecordBase' "HLAE idle path resolution does not use the Output page folder."
Require-NotText $hlae '"%s\\art\\%s"' "HLAE still contains the hardcoded cstrike/art fallback."
Require-Text $hlae 'IsAbsoluteWindowsPath' "HLAE output paths do not preserve absolute paths."
Require-Text $hlae 'EnsureOutputParentDirectory' "HLAE absolute output folders are not created."
Require-Text $hlae '"advancedfx Cam\nversion 2\n"' "mirv_camio does not write the AdvancedFX CAM header."
Require-Text $hlae '"HIERARCHY\nROOT MdtCam\n' "mirv_camexport does not write the HLAE BVH root."
Require-Text $hlae 'fwrite( "afxGameRecord", 1, 13' "mirv_agr does not write the HLAE AGR magic."
Require-NotText $render 'ApplyArtHlaeView( *pSetup );' "The obsolete approximate IClientMode HLAE camera hook remains."
Require-Text $gui '"HLAE", "Configs"' "The HLAE GUI page is missing from the sidebar."
Require-Text $gui '\nCAM exp %s / imp %s | AGR %s | BVH exp %s / imp %s' "The in-game overlay does not report HLAE import/export activity."
Require-Text $gui '"Enable HLAE features globally"' "The HLAE global GUI switch is missing."
Require-Text $gui '"V34 HOOK READY", "V34 HOOK FAILED"' "The HLAE page does not expose -afxV34 hook status."
Require-Text $gui 'void HoverExplanation( const char *pFormat, ... )' "Reusable HLAE hover explanations are missing."
Require-Text $gui 'ImGui::TextWrapped( "%s", pHelp );' "Console-command hover help does not preserve a readable tooltip width."
Require-Text $gui 'HLAE cameras.' "HLAE controls do not include hover explanations."
Require-Text $gui 'void HlaeSameLineIfFits( float nextItemWidth )' "HLAE controls do not wrap responsive rows before the horizontal window boundary."
Require-Text $gui 'bool HlaeToggleButton' "HLAE start/stop activities do not use toggle buttons."
Require-Text $gui 'ImGui::Button( pausedDemo ? "Resume demo" : "Pause demo", ImVec2( 125, 30 ) )' "mirv_campath demo playback does not use one Pause/Resume toggle button."
Require-Text $gui '"Start at current time", ImVec2( 175, 0 )' "mirv_campath Start at current time button is too narrow."
Require-Text $gui 'ImGui::SetNextItemOpen( false, ImGuiCond_Once );' "HLAE dropdown sections do not default to closed."
Require-Text $gui 'art_hlae_input_while_gui off' "mirv_input GUI passthrough does not default to off."
Require-Text $gui 'IsPointInsideMainGuiWindow' "mirv_input GUI passthrough cannot distinguish outside-window mouse holds."
Require-Text $gui 'hostFrameratePresets[] = { 0, 150, 300, 600, 2000 }' "The host_framerate 2000 preset is missing."
Require-Text $gui '"art_hlae enabled 1' "HLAE features do not default to enabled."
Require-Text $hlae 'void BeginArtHlaeTakeExports()' "Automatic HLAE take exports are not started with ART recording."
Require-Text $hlae 'void EndArtHlaeTakeExports()' "Automatic HLAE take exports are not finalized with ART recording."
Require-Text $commands 'BeginArtHlaeTakeExports();' "art_start does not begin selected HLAE take exports."
Require-Text $commands 'EndArtHlaeTakeExports();' "art_stop does not finalize selected HLAE take exports."
Require-Text $gui '"Auto-export with ART takes"' "The HLAE GUI is missing automatic take-export options."
Require-Text $hlae '"remove"' "mirv_campath keyframe removal is missing."
Require-Text $hlae '"draw"' "mirv_campath draw command support is missing."
Require-Text $gui '"Draw path"' "The HLAE GUI is missing the campath draw control."
Require-Text $gui '"Remove keyframe"' "The HLAE GUI is missing the campath keyframe removal button."
Require-Text $gui 'IssueCommand( "mirv_campath offset none" );' "The campath offset slider does not reset the relative AdvancedFX offset before applying an absolute GUI value."
Require-Text $gui 'ProjectCampathWorldSegment' "Campath interpolation segments are not clipped and projected as world lines."
Require-Text $gui 'ClipCampathScreenLine' "Campath lines can disappear when an endpoint falls outside the screen."
Require-Text $gui 'ImGui::SetNextItemWidth( 180 );' "The BVH FPS field is not compact."
Require-Text $gui 'ImGui::DragFloat( "##campath_time_offset"' "HLAE time offset must use a drag control without +/- buttons."
Require-Text $gui '{ "Mouse sensitivity##mirv_input", "mirv_input cfg msens %.6f"' "HLAE mouse sensitivity must use the shared aligned drag row."
Require-Text $gui 'ImGui::DragInt( "##agr_player_cameras"' "HLAE player cameras must use the depth-style drag control."
Require-Text $gui 'ImGui::DragFloat( "Minimum unzoomed FOV"' "HLAE zoom threshold must use the depth-style drag control."
Require-NotText $gui 'ImGui::InputFloat( "Mouse sensitivity"' "HLAE mouse sensitivity still has InputFloat +/- buttons."
Require-Text $gui 'ImGui::DragInt( "Target tick##demo_tick"' "Capture demo tick must use a drag field."
Require-NotText $gui 'ImGui::InputInt( "Target tick##demo_tick"' "Capture demo tick still has InputInt +/- buttons."
Require-Text $gui 'ImGui::DragInt( "Target tick##campath_demo_tick"' "Campath demo tick must use a drag field."
Require-Text $gui 'IssueCommand( "demo_pause" );' "Campath demo pause control is missing."
Require-Text $gui 'IssueCommand( "demo_resume" );' "Campath demo resume control is missing."
Require-Text $gui 'SeekDemoTick( g_Gui.demoTick );' "Campath demo goto-tick control is missing."
Require-Text $hlae 'static const size_t kSamplesPerInterval = 1024;' "Campath drawer does not use the original AdvancedFX trajectory sampling density."
Require-Text $hlae 'static const double kReductionEpsilonSquared = 1.0;' "Campath drawer does not use the original one-unit reduction epsilon."
Require-Text $gui 'static const double kCameraRadius = 18.0;' "Campath camera geometry does not use the original radius."
Require-Text $gui 'const double axisRadius = 36.0;' "Campath key axes do not use the original radius."
Require-Text $gui 'DrawCampathWorldGradientLine( pDrawList, matrix, displaySize,' "Campath trajectory does not draw the interpolated line with per-vertex time colours."
Require-Text $gui '"mirv_input cfg smooth halfTimeVec %.6f"' "mirv_input vector smoothing control is missing."
Require-Text $gui '"mirv_input cfg smooth halfTimeAng %.6f"' "mirv_input angle smoothing control is missing."
Require-Text $gui '"mirv_input cfg smooth halfTimeFov %.6f"' "mirv_input FOV smoothing control is missing."
Require-Text $gui '"mirv_input cfg mBackSpeed %.6f"' "mirv_input mouse backward-speed control does not match the AdvancedFX parser."
Require-Text $gui '"mirv_input position %.6f %.6f %.6f"' "mirv_input direct position controls are missing."
Require-Text $gui '"mirv_input angles %.6f %.6f %.6f"' "mirv_input direct angle controls are missing."
Require-Text $gui '"mirv_input fov real %.6f"' "mirv_input real-FOV control is missing."
Require-Text $gui '"mirv_input mem store \"%s\""' "mirv_input named view-state storage is missing."
Require-Text $gui '"mirv_input mem use \"%s\" origin"' "mirv_input partial origin restore is missing."
Require-Text $gui '"mirv_input mem use \"%s\" angles"' "mirv_input partial angle restore is missing."
Require-Text $gui '"mirv_input mem use \"%s\" fov"' "mirv_input partial FOV restore is missing."
Require-Text $gui '"mirv_input mem save \"%s\""' "mirv_input XML state saving is missing."
Require-Text $gui '"mirv_input mem load \"%s\""' "mirv_input XML state loading is missing."
Require-Text $hlae 'status.inputHasCameraData = g_Hlae.hasLastCamera || g_Hlae.hasGameCamera;' "mirv_input GUI does not receive the current final camera state."
Require-Text $mirvCampathSource '#include "MirvCampath.h"' "Standalone MirvCampath source does not include its public header."
Require-NotText $mirvCampathSource 'MirvCamPath.h' "MirvCampath source still includes the missing upstream alias header."
Require-Text $camPathHeader 'Afx::Math::Quaternion R;' "CamPath quaternion storage is not namespace-qualified against the Source SDK Quaternion type."
Require-Text $camPathHeader 'CInterpolationMapView<CamPathValue, Afx::Math::Quaternion> m_RView;' "CamPath quaternion interpolation view is not namespace-qualified."
Require-Text $camPathHeader 'CInterpolation<Afx::Math::Quaternion> * m_RInterp;' "CamPath quaternion interpolation pointer is not namespace-qualified."
$updateInputStart = $hlae.IndexOf('void UpdateInput( CViewSetup &view )', [StringComparison]::Ordinal)
$updateInputEnd = $hlae.IndexOf('double XmlAttribute(', $updateInputStart, [StringComparison]::Ordinal)
if ($updateInputStart -lt 0 -or $updateInputEnd -le $updateInputStart) {
	$failures.Add("mirv_input UpdateInput boundaries could not be found.")
}
else {
	$updateInput = $hlae.Substring($updateInputStart, $updateInputEnd - $updateInputStart)
	if (([regex]::Matches($updateInput, '\bdouble\s+dt\b')).Count -ne 1) {
		$failures.Add("mirv_input UpdateInput must contain exactly one frame-time variable.")
	}
	if ($updateInput.Contains('g_Hlae.inputInitialized')) {
		$failures.Add("The obsolete local mirv_input fallback implementation remains after the AdvancedFX handler.")
	}
}
Require-NotText $gui 'ImGui::InputInt( "Player cameras"' "HLAE player cameras still has InputInt +/- buttons."
Require-Text $gui 'TextUnformattedSingleLine( "Player cameras" );' "AGR player-camera label is not in the aligned label row."
Require-Text $gui 'TextUnformattedSingleLine( "Viewmodels" );' "AGR viewmodel label is not in the aligned label row."

Require-Text $afterEffectsImporter '"TGA image sequences"' "After Effects importer has no image-sequence mode."
Require-Text $afterEffectsImporter '"Converted video files"' "After Effects importer has no converted-video mode."
Require-Text $afterEffectsImporter '"Linear Color Key"' "After Effects importer has no Linear Color Key option."
Require-Text $afterEffectsImporter '"Keylight (1.2)"' "After Effects importer has no Keylight option."
Require-Text $afterEffectsImporter 'ART Take Importer v" + SCRIPT_VERSION' "After Effects importer does not display version 1.0."
Require-Text $afterEffectsImporter '[ "Keylight (1.2)", "Linear Color Key", "None" ]' "Keylight must be the first auto-key option."
Require-Text $afterEffectsImporter '"Separate ObjectID into four category matte compositions"' "After Effects importer has no ObjectID separation option."
Require-Text $afterEffectsImporter 'options.sequence = true;' "After Effects importer does not import TGA image sequences as sequences."
Require-Text $afterEffectsImporter 'pass.category_colors' "After Effects importer does not consume ObjectID category colors."
Require-Text $afterEffectsImporter 'videoNameMatchesPass' "After Effects video discovery cannot distinguish overlapping pass names."
Require-Text $afterEffectsImporter '"ADBE Linear Color Key2"' "After Effects importer does not use the installed Linear Color Key match name."
Require-Text $afterEffectsImporter '"ADBE Linear Color Key2-0007"' "ObjectID Key Operation does not use the exact Adobe property match name."
Require-Text $afterEffectsImporter '"ADBE Linear Color Key2-0002"' "ObjectID View does not use the exact Adobe property match name."
Require-Text $afterEffectsImporter 'operation = effect.property( 7 )' "ObjectID Key Operation fallback does not target the correct Linear Color Key property."
Require-Text $afterEffectsImporter 'view = effect.property( 2 )' "ObjectID View fallback does not target the correct Linear Color Key property."
Require-NotText $afterEffectsImporter 'findNamedProperty( effect, [ "view" ] )' "Loose ObjectID View lookup can incorrectly select the Preview property."
Require-Text $afterEffectsImporter 'operation.setValue( 1 )' "ObjectID key operation is not set to Key Colors."
Require-Text $afterEffectsImporter 'view.setValue( 3 )' "ObjectID key view is not set to Matte Only."
Require-Text $afterEffectsImporter '[ "ADBE Invert", "Invert" ]' "ObjectID matte creation does not invert after Linear Color Key."
Require-Text $afterEffectsImporter '"objectid", "depth", "normal", "clear"' "ObjectID and depth are not created at the bottom of the master comp."
Require-Text $afterEffectsImporter 'masterComp.layers.precompose(' "Depth is not automatically pre-composed."
Require-Text $afterEffectsImporter 'layer.enabled = false;' "Depth and ObjectID layers are not disabled by default."
Require-Text $afterEffectsImporter '"Import mirv_camio camera (.cam)"' "After Effects importer has no mirv_camio camera option."
Require-Text $afterEffectsImporter '"Import mirv_camexport camera (.bvh)"' "After Effects importer has no mirv_camexport camera option."
Require-Text $afterEffectsImporter 'parseCamioCamera' "After Effects importer has no CAM parser."
Require-Text $afterEffectsImporter 'parseCamexportCamera' "After Effects importer has no BVH parser."
Require-Text $afterEffectsImporter '"ART Camera - mirv_camio"' "After Effects importer does not create a mirv_camio camera layer."
Require-Text $afterEffectsImporter '"ART Camera - mirv_camexport"' "After Effects importer does not create a mirv_camexport camera layer."
Require-Text $afterEffectsCameraBaker 'ART Camera Baker' "After Effects camera baker is missing its product name."
Require-Text $packageRelease 'ART_Importer_v1.0.jsx' "The release package omits the After Effects importer."
Require-Text $packageRelease 'ART_Camera_Baker_v1.0.jsx' "The release package omits the After Effects camera baker."
Require-Text $packageRelease '"Scripts/ART_Importer_v1.0.jsx"' "The release package does not use the regular After Effects Scripts folder."
Require-NotText $packageRelease 'ScriptUI Panels' "The obsolete ScriptUI Panels packaging workflow is still present."
Require-Text $packageRelease 'readme.txt' "The release package omits the plain-text installation guides."
Require-NotText $packageRelease '"LICENSE.txt"' "The release ZIP must not include the repository license file."
Require-NotText $packageRelease '"THIRD_PARTY_NOTICES.txt"' "The release ZIP must not include third-party notices."

Require-Text $packageRelease '-StageBuildOutputs' "Release packaging cannot stage direct Visual Studio build outputs."
Require-Text $packageRelease 'Move-Item -LiteralPath $tempPackagePath -Destination $packagePath -Force' "Release ZIP is not finalized atomically."
Require-Text $buildScript 'Build completed, but the release ZIP was not created' "The main build does not fail when packaging is missing."
Require-Text $buildBatch 'if not exist "%PACKAGE%"' "build.bat does not verify that the release ZIP exists."
Require-Text $loaderProject '<ProjectReference Include="..\dll\v34_art.vcxproj">' "The Loader project does not depend on the DLL before packaging."
Require-Text $loaderProject 'package-release.ps1" -Configuration "$(Configuration)" -StageBuildOutputs' "Visual Studio builds do not package the release ZIP."

if ($failures.Count -gt 0) {
	$failures | ForEach-Object { Write-Error $_ }
	throw "Source contract tests failed with $($failures.Count) issue(s)."
}

Write-Host "Source contract tests passed."
