# Third-party notices

The root MIT license applies only to original ART code and documentation. Vendored or adapted components retain their own terms.

- **AdvancedFX / HLAE:** camera path, input, CAM/BVH, AGR, and `-afxV34` behavior. Vendored subset: `third_party/advancedfx`; MIT, with CC0-marked interpolation sections in `AfxMath.cpp`.
- **After Effects references:** CAM conversion from Brett Anthony / xNWP's *HLAE CamIO To AE*; BVH rig/axis mapping from msthavoc's *HLAE BVH to AE Cam*; AGR parsing follows the official AdvancedFX SFM importer. Credits remain in `tools/after_effects/ART_Importer_v1.0.jsx`.
- **Dear ImGui 1.92.8:** MIT; see `third_party/imgui/LICENSE.txt`.
- **MinHook 1.3.4:** BSD 2-Clause; see `third_party/minhook/LICENSE.txt`.
- **Counter-Strike: Source v34 SDK subset:** build-4044 headers/libraries under `third_party/cssv34-sdk`. These files are outside the ART MIT license; the supplied upstream notice states **NOT FOR COMMERCIAL PURPOSES**.
- **Depth behavior:** adapted from a CS:S moviemaking DOF configuration shared by sin1ster; the configuration itself is not redistributed.

Counter-Strike, Counter-Strike: Source, Source, Steam, and Valve are trademarks of Valve Corporation. ART is independent and is not endorsed by Valve.
