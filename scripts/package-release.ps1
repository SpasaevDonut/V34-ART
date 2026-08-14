[CmdletBinding()]
param(
	[string]$PackageName,
	[string]$Configuration = "Release",
	# -StageBuildOutputs: stage direct build binaries into dist before archiving
	[switch]$StageBuildOutputs
)

$ErrorActionPreference = "Stop"
. (Join-Path $PSScriptRoot "common.ps1")

$repositoryRoot = Get-ArtRepositoryRoot
$distDirectory = Join-Path $repositoryRoot "dist"
$distributionBaseName = Get-ArtDistributionBaseName
$PackageName = if ($PackageName) { $PackageName } else { "v34-art-v$(Get-ArtVersion).zip" }
$packagePath = Join-Path $distDirectory $PackageName
$tempPackagePath = "$packagePath.tmp"
$distExe = Join-Path $distDirectory "$distributionBaseName.exe"
$distDll = Join-Path $distDirectory "$distributionBaseName.dll"

function Copy-ArtBuildOutput {
	param(
		[Parameter(Mandatory = $true)][string]$Source,
		[Parameter(Mandatory = $true)][string]$Destination
	)

	if (-not (Test-Path -LiteralPath $Source -PathType Leaf)) {
		throw "Compiled release input is missing: $Source"
	}
	Copy-Item -LiteralPath $Source -Destination $Destination -Force
}

New-Item -ItemType Directory -Force -Path $distDirectory | Out-Null

if ($StageBuildOutputs) {
	Copy-ArtBuildOutput `
		-Source (Join-Path $repositoryRoot "build\loader\$Configuration\v34_art.exe") `
		-Destination $distExe
	Copy-ArtBuildOutput `
		-Source (Join-Path $repositoryRoot "build\dll\$Configuration\v34_art.dll") `
		-Destination $distDll
}

$entries = [ordered]@{
	"$distributionBaseName.exe" = $distExe
	"$distributionBaseName.dll" = $distDll
	"readme.txt" = Join-Path $repositoryRoot "packaging\readme.txt"
	"Scripts/ART_Importer_v1.0.jsx" = Join-Path $repositoryRoot "tools\after_effects\ART_Importer_v1.0.jsx"
	"Scripts/ART_Camera_Baker_v1.0.jsx" = Join-Path $repositoryRoot "tools\after_effects\ART_Camera_Baker_v1.0.jsx"
	"Scripts/readme.txt" = Join-Path $repositoryRoot "tools\after_effects\readme.txt"
}

foreach ($sourcePath in $entries.Values) {
	if (-not (Test-Path -LiteralPath $sourcePath -PathType Leaf)) {
		throw "Release input is missing: $sourcePath"
	}
}

foreach ($path in @($packagePath, $tempPackagePath)) {
	if (Test-Path -LiteralPath $path) {
		Remove-Item -LiteralPath $path -Force
	}
}

Add-Type -AssemblyName System.IO.Compression
Add-Type -AssemblyName System.IO.Compression.FileSystem
$archive = [IO.Compression.ZipFile]::Open(
	$tempPackagePath,
	[IO.Compression.ZipArchiveMode]::Create
)
try {
	foreach ($entry in $entries.GetEnumerator()) {
		[IO.Compression.ZipFileExtensions]::CreateEntryFromFile(
			$archive,
			$entry.Value,
			$entry.Key,
			[IO.Compression.CompressionLevel]::Optimal
		) | Out-Null
	}
}
finally {
	$archive.Dispose()
}

Move-Item -LiteralPath $tempPackagePath -Destination $packagePath -Force
if (-not (Test-Path -LiteralPath $packagePath -PathType Leaf)) {
	throw "Release ZIP was not created: $packagePath"
}

$package = Get-Item -LiteralPath $packagePath
if ($package.Length -le 0) {
	throw "Release ZIP is empty: $packagePath"
}

Write-Host "Packaged release: $packagePath ($($package.Length) bytes)"
