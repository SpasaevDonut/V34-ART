# Building ART V1.0

## Requirements

- Windows 10/11 x64.
- Visual Studio 2022 with **Desktop development with C++**.
- MSVC v143 and a Windows 10/11 SDK.
- PowerShell 5.1 or newer.

The repository already contains the required build-4044 SDK subset and x86 libraries.

## Build

Run from the repository root:

```text
build.bat
```

Equivalent PowerShell command:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File .\scripts\build.ps1
```

The scripts locate Visual Studio, build `Release|Win32`, copy the versioned EXE/DLL to `dist`, and create `dist/v34-art-v1.0.zip`.

Custom paths:

```powershell
.\scripts\build.ps1 -MSBuildPath "C:\...\MSBuild.exe"
.\scripts\build.ps1 -SdkRoot "D:\cssv34-sdk"
```

`SdkRoot` must contain `public` and `lib/public`. Building `v34-art.sln` in Visual Studio also creates the ZIP through the Loader post-build step.

## Verification

```powershell
.\scripts\verify-source.ps1
.\scripts\test.ps1
.\scripts\build.ps1
.\scripts\verify-release.ps1
```

`verify-release.ps1` checks Win32 PE metadata, required runtime markers, package hashes, and the exact ZIP layout. Generated `build`, `dist`, and temporary directories are ignored by Git.

## Release ZIP layout

```text
v34_art_v1.0.exe
v34_art_v1.0.dll
readme.txt
Scripts/
  ART_Importer_v1.0.jsx
  ART_Camera_Baker_v1.0.jsx
  readme.txt
```
