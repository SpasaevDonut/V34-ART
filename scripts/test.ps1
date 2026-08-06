[CmdletBinding()]
param(
	[string]$Configuration = "Release",
	[string]$MSBuildPath
)

$ErrorActionPreference = "Stop"
. (Join-Path $PSScriptRoot "common.ps1")

$repositoryRoot = Get-ArtRepositoryRoot
& (Join-Path $PSScriptRoot "verify-source.ps1")
& (Join-Path $repositoryRoot "tests\source_contracts.ps1")

$msbuild = Resolve-ArtMSBuild -RequestedPath $MSBuildPath
$project = Join-Path $repositoryRoot "tests\art_logic_tests.vcxproj"
$arguments = @(
	$project,
	"/m",
	"/p:Configuration=$Configuration",
	"/p:Platform=Win32",
	"/v:minimal"
)
Invoke-ArtProcess -FilePath $msbuild -Arguments $arguments -Description "ART logic test build"

$testExecutable = Join-Path $repositoryRoot "build\tests\$Configuration\art_logic_tests.exe"
& $testExecutable
if ($LASTEXITCODE -ne 0) {
	throw "ART logic tests failed with exit code $LASTEXITCODE."
}
Write-Host "All automated tests passed."
