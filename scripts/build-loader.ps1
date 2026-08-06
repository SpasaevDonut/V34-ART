[CmdletBinding()]
param(
	[string]$Configuration = "Release",
	[string]$MSBuildPath
)

$ErrorActionPreference = "Stop"
. (Join-Path $PSScriptRoot "common.ps1")

$repositoryRoot = Get-ArtRepositoryRoot
$project = Join-Path $repositoryRoot "loader\v34_art_loader.vcxproj"
$msbuild = Resolve-ArtMSBuild -RequestedPath $MSBuildPath

$arguments = @(
	$project,
	"/m",
	"/p:Configuration=$Configuration",
	"/p:Platform=Win32",
	"/v:minimal"
)
Invoke-ArtProcess -FilePath $msbuild -Arguments $arguments -Description "ART Loader build"

$sourceExe = Join-Path $repositoryRoot "build\loader\$Configuration\v34_art.exe"
$distDirectory = Join-Path $repositoryRoot "dist"
$distExe = Join-Path $distDirectory "$(Get-ArtDistributionBaseName).exe"
New-Item -ItemType Directory -Force -Path $distDirectory | Out-Null
Copy-Item -LiteralPath $sourceExe -Destination $distExe -Force

Write-Host "Built Loader: $distExe"
