# After Effects tools V1.0

## Install

1. Copy both `.jsx` files from `tools/after_effects` or the release ZIP's `Scripts` folder to:

```text
Adobe After Effects <version>/Support Files/Scripts/
```

2. Restart After Effects.
3. Run the tools from **File > Scripts**. They open as floating script windows, not dockable ScriptUI panels.
4. When file access is blocked, enable **Allow Scripts to Write Files and Access Network** in **Preferences > Scripting & Expressions**.

You can also use **File > Scripts > Run Script File** without installing them.

## ART Take Importer

Select an ART `[take].json` manifest. The importer supports:

- TGA sequences, converted video, or multilayer OpenEXR from `<take>/EXR`.
- A master composition matching the take resolution, FPS, frame range, and duration.
- Optional editing precomp, Keylight/Linear Color Key, Depth, and ObjectID mattes.
- CAM/BVH cameras and lightweight or full AGR null tracks.

Camera and AGR searches prefer files associated with the selected take. AGR bone nulls use numeric indices because AGR does not store bone names or hierarchy.

## ART Camera Baker

Select one time-remapped precomp containing the imported camera, run the script, choose the source camera, range, and sampling rate, then click **Bake Camera**. The script creates a normal keyed camera and optional null layers in the edit composition. Source-FPS sampling is recommended for high-FPS captures.

## Credits

CAM conversion is adapted from Brett Anthony / xNWP's **HLAE CamIO To AE**. BVH conversion is adapted from msthavoc's **HLAE BVH to AE Cam**. AGR parsing follows the official AdvancedFX SFM importer format. See the script headers and [third-party notices](../THIRD_PARTY_NOTICES.md).
