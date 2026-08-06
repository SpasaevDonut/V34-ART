# Troubleshooting

| Problem | Check |
| --- | --- |
| Loader cannot find `hl2.exe` | Start 32-bit CS:S v34 first and wait for it to finish loading. |
| Matching DLL not found | Keep `v34_art_v1.0.exe` and `v34_art_v1.0.dll` in the same folder. |
| Access denied | Run the game and Loader at the same privilege level. |
| No `art_*` commands | Confirm build 4044 compatibility and restart the game before loading another ART build. |
| No output files | Enable at least one pass and verify `art_record path` is writable. |
| TGA errors | Run `art_validation`; test `art_tga_compression off`. |
| Capture allocation failure | Reduce resolution/pass count, close memory-heavy apps, lower queue limits, or restart the 32-bit game. |
| Players render through walls | Disable `art_players_through_walls`; chams use a separate through-walls option. |
| Depth looks nonlinear | Expected: the pass is fog-based. Adjust `art_depth_start` and `art_depth_end`. |
| `mirv_input` moves but will not rotate | Close the Source console, test GUI input passthrough, and tune `mirv_input cfg msens`. |
| CAM/BVH/AGR file not found | Use an absolute path or place the file in the active take/current output folder. |
| AE script is missing | Copy the `.jsx` files into `Support Files/Scripts`, restart AE, then use **File > Scripts**. |
| OpenEXR import fails | Confirm `<take>/EXR` contains a sequence and After Effects' native OpenEXR/EXtractoR components are available. |
| Camera Baker refuses the layer | Select one time-remapped precomp that contains at least one camera. |

Enable diagnostics only while reproducing a problem:

```text
art_debug on
```

The log is written next to `hl2.exe` as `v34_art.log`. Useful reports also include `art_stats`, the take JSON, validation output, CS:S build, resolution, FPS, and exact reproduction steps.
