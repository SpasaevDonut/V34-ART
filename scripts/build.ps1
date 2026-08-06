[CmdletBinding()]
param(
	[string]$Configuration = "Release",
	[string]$SdkRoot,
	[string]$MSBuildPath
)

$ErrorActionPreference = "Stop"
. (Join-Path $PSScriptRoot "common.ps1")

$repositoryRoot = Get-ArtRepositoryRoot
$distDirectory = Join-Path $repositoryRoot "dist"
$packageName = "v34-art-v$(Get-ArtVersion).zip"
$packagePath = Join-Path $distDirectory $packageName

if (Test-Path -LiteralPath $distDirectory -PathType Container) {
	Get-ChildItem -LiteralPath $distDirectory -File |
		Where-Object {
			$_.Name -match '^v34_art_v.+\.(dll|exe)$' -or
			$_.Name -match '^v34-art-v.+\.zip$' -or
			$_.Name -in @('v34_art.dll', 'v34_art.exe', 'v34-art-loader-build4044.zip')
		} |
		Remove-Item -Force
}

$dllParameters = @{ Configuration = $Configuration }
$loaderParameters = @{ Configuration = $Configuration }
if ($SdkRoot) { $dllParameters.SdkRoot = $SdkRoot }
if ($MSBuildPath) {
	$dllParameters.MSBuildPath = $MSBuildPath
	$loaderParameters.MSBuildPath = $MSBuildPath
}

& (Join-Path $PSScriptRoot "build-dll.ps1") @dllParameters
& (Join-Path $PSScriptRoot "build-loader.ps1") @loaderParameters
& (Join-Path $PSScriptRoot "package-release.ps1") -Configuration $Configuration -StageBuildOutputs -PackageName $packageName

if (-not (Test-Path -LiteralPath $packagePath -PathType Leaf)) {
	throw "Build completed, but the release ZIP was not created: $packagePath"
}

Write-Host "Build complete. Release ZIP: $packagePath"
