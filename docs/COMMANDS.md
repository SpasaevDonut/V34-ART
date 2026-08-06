# Command reference

Run `art_help` for the in-game summary. Commands without required arguments normally print their current state.

## Recording

```text
art_start [take_name]
art_stop
art_toggle [take_name]
art_status
art_stats
art_record path <folder|default>
art_open_folder
art_prefix <prefix|default>
art_take_json <on|off|status|write>
art_debug <on|off>
```

Relative output paths resolve under `cstrike`; absolute drive and UNC paths are supported. Output path/prefix cannot change during recording.

## Passes and preview

```text
art_record <normal|clear|clear-noplayers|viewmodel|depth|players|objectid|all> <on|off>
art_hud    <normal|clear|clear-noplayers|viewmodel|depth|players|objectid|all> <on|off>
art_preview <normal|clear|clear-noplayers|viewmodel|depth|players|objectid|off>
art_preview_next
```

Only `normal` records by default. HUD defaults on for `normal`, `clear`, and `clear-noplayers`.

## Pass settings

```text
art_viewmodel_color <r> <g> <b>
art_players_color <r> <g> <b>
art_players_through_walls <on|off>
art_players_world_weapons <on|off>
art_objectid_color <viewmodel|players|world|skybox> <r> <g> <b>
art_depth_start <distance>    // default 150
art_depth_end <distance>      // default 800
```

ObjectID defaults: viewmodel `255 128 0`, players `0 0 255`, world `255 255 0`, skybox `255 0 0`.

## FOV and visuals

```text
art_fov <1-179|default>
art_fov handleZoom enabled <0|1>
art_fov handleZoom minUnzoomedFov <1-179>
art_viewmodel_fov <1-179|default>
art_visible <viewmodel|players> <on|off>
art_noflash <on|off>
art_nosmoke <on|off>
art_force_r_lod <on|off|status>
art_force_r_lod value <integer>
art_chams <players|viewmodel|skybox> <on|off>
art_chams <players_color|viewmodel_color|skybox_color> <r> <g> <b>
art_chams players_through_walls <on|off>
art_chams status
```

`art_force_r_lod` is off by default and stores `-7` as its default value. When enabled, it immediately restores that value after demo or console cvar changes.

## Queue, compression, and validation

```text
art_queue [status|flush|default]
art_queue max_files <1-512>
art_queue max_mb <16-1024>
art_queue reserve_mb <64-1024>
art_tga_compression <off|auto|rle>
art_validation [run|status]
art_validation auto <on|off>
art_validation file_size <on|off>
art_validation dropped_frames <on|off>
art_validation min_size <bytes>
```

Queue defaults: 16 files, 256 MiB queued output, and 256 MiB virtual-address-space reserve.

## GUI and configs

```text
art_gui [on|off|toggle|status]
art_gui_key <binding>
art_gui_theme <name>
art_gui_color <target> <r> <g> <b> [a]
art_gui_experimental <on|off>
art_overlay <on|off>
art_config save|load|delete|list [name]
art_demo_pause_after_recording <on|off>
```

Default GUI shortcut: `Shift+F3`. Configs are stored in `cstrike/cfg/art_gui`.

## HLAE bridge

```text
art_hlae enabled <0|1>
art_hlae autoExport <agr|camio|bvh> <0|1>
art_hlae autoExport bvhFps <fps>
art_hlae help
```

Absolute paths are used directly. Relative HLAE paths resolve to the active take while recording, otherwise to the configured output folder.

### Camera path and input

```text
mirv_campath add
mirv_campath remove <index>|all
mirv_campath clear
mirv_campath enable <0|1>
mirv_campath hold <0|1>
mirv_campath draw <0|1>
mirv_campath edit ...
mirv_campath save <file.xml>
mirv_campath load <file.xml>
mirv_campath print

mirv_input camera
mirv_input end
mirv_input position <x> <y> <z>
mirv_input angles <pitch> <yaw> <roll>
mirv_input fov <value>
mirv_input cfg ...
mirv_input mem ...
```

### CAM, AGR, BVH, and FOV

```text
mirv_camio export start <file.cam>
mirv_camio export end
mirv_camio import start <file.cam>
mirv_camio import end

mirv_agr enabled <0|1>
mirv_agr start <file.agr>
mirv_agr stop
mirv_agr recordCamera|recordPlayers|recordWeapons|recordProjectiles|recordViewModel|recordInvisible <0|1>

mirv_camexport start <file.bvh> <fps>
mirv_camexport stop
mirv_camexport timeinfo
mirv_camimport start <file.bvh>
mirv_camimport stop
mirv_camimport basetime <seconds>|current
mirv_camimport toCamPath <0|1> <fov>

mirv_fov [real] <fov>
mirv_fov default
mirv_fov handleZoom enabled <0|1>
mirv_fov handleZoom minUnzoomedFov current|[real] <fov>
```

Enable AGR before loading the demo when entity recording is required.
