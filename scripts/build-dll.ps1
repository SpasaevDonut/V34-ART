[CmdletBinding()]
param(
	[string]$Configuration = "Release",
	[string]$SdkRoot,
	[string]$MSBuildPath
)

$ErrorActionPreference = "Stop"
. (Join-Path $PSScriptRoot "common.ps1")

$repositoryRoot = Get-ArtRepositoryRoot
$project = Join-Path $repositoryRoot "dll\v34_art.vcxproj"
$msbuild = Resolve-ArtMSBuild -RequestedPath $MSBuildPath
$sdk = Resolve-ArtCssSdk -RequestedPath $SdkRoot

$arguments = @(
	$project,
	"/m",
	"/p:Configuration=$Configuration",
	"/p:Platform=Win32",
	"/p:CssV34SdkRoot=$sdk",
	"/v:minimal"
)
Invoke-ArtProcess -FilePath $msbuild -Arguments $arguments -Description "ART DLL build"

$sourceDll = Join-Path $repositoryRoot "build\dll\$Configuration\v34_art.dll"
$distDirectory = Join-Path $repositoryRoot "dist"
$distDll = Join-Path $distDirectory "$(Get-ArtDistributionBaseName).dll"
New-Item -ItemType Directory -Force -Path $distDirectory | Out-Null
Copy-Item -LiteralPath $sourceDll -Destination $distDll -Force

Write-Host "Built DLL: $distDll"
