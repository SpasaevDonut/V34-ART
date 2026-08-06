[CmdletBinding()]
param(
	[string]$PackageName
)

$ErrorActionPreference = "Stop"
. (Join-Path $PSScriptRoot "common.ps1")

function Get-PeMachine {
	param([Parameter(Mandatory = $true)][string]$Path)

	$bytes = [IO.File]::ReadAllBytes($Path)
	if ($bytes.Length -lt 64 -or $bytes[0] -ne 0x4D -or $bytes[1] -ne 0x5A) {
		throw "Not a valid PE file: $Path"
	}
	$peOffset = [BitConverter]::ToInt32($bytes, 0x3C)
	if ($peOffset -lt 0 -or $peOffset + 6 -gt $bytes.Length) {
		throw "Invalid PE header offset: $Path"
	}
	return [BitConverter]::ToUInt16($bytes, $peOffset + 4)
}

$repositoryRoot = Get-ArtRepositoryRoot
$distDirectory = Join-Path $repositoryRoot "dist"
$PackageName = if ($PackageName) { $PackageName } else { "v34-art-v$(Get-ArtVersion).zip" }
$distributionBaseName = Get-ArtDistributionBaseName
$exeName = "$distributionBaseName.exe"
$dllName = "$distributionBaseName.dll"
$exePath = Join-Path $distDirectory $exeName
$dllPath = Join-Path $distDirectory $dllName
$packagePath = Join-Path $distDirectory $PackageName

foreach ($file in @($exePath, $dllPath, $packagePath)) {
	if (-not (Test-Path -LiteralPath $file -PathType Leaf)) {
		throw "Release file is missing: $file"
	}
}

foreach ($binary in @($exePath, $dllPath)) {
	$machine = Get-PeMachine -Path $binary
	if ($machine -ne 0x014C) {
		throw "Expected Win32 x86 PE machine 0x014C, got 0x$($machine.ToString('X4')): $binary"
	}
	$versionInfo = (Get-Item -LiteralPath $binary).VersionInfo
	if ($versionInfo.FileVersion -ne "1.0" -or $versionInfo.ProductVersion -ne "1.0") {
		throw "Expected file and product version 1.0: $binary"
	}
}

$exeText = [Text.Encoding]::Unicode.GetString([IO.File]::ReadAllBytes($exePath))
foreach ($loaderMarker in @("ART Loader v1.0", "[%d/4]", "PACKAGE", "INJECTION", "READY", "Press any key to close")) {
	if (-not $exeText.Contains($loaderMarker)) {
		throw "Styled Loader marker was not found in the EXE: $loaderMarker"
	}
}
$legacyLoaderTitle = "V34 ART Inj" + "ector"
if ($exeText.Contains($legacyLoaderTitle)) {
	throw "Legacy Loader terminology was found in the EXE."
}

$dllText = [Text.Encoding]::ASCII.GetString([IO.File]::ReadAllBytes($dllPath))
$legacyProductTerm = "strea" + "ms"
$unprefixedDisplayNamePattern = "(?<!CS:S )V34 ADVANCED RECORDING TOOLS"
foreach ($binaryPath in @($exePath, $dllPath)) {
	$binaryBytes = [IO.File]::ReadAllBytes($binaryPath)
	$asciiText = [Text.Encoding]::ASCII.GetString($binaryBytes)
	$unicodeText = [Text.Encoding]::Unicode.GetString($binaryBytes)
	if ($asciiText.IndexOf($legacyProductTerm, [StringComparison]::OrdinalIgnoreCase) -ge 0 -or
		$unicodeText.IndexOf($legacyProductTerm, [StringComparison]::OrdinalIgnoreCase) -ge 0) {
		throw "Legacy product name was found in release binary content: $binaryPath"
	}
	if ([regex]::IsMatch($asciiText, $unprefixedDisplayNamePattern) -or
		[regex]::IsMatch($unicodeText, $unprefixedDisplayNamePattern)) {
		throw "Unprefixed ART display name was found in release binary content: $binaryPath"
	}
}
if (-not $dllText.Contains("CS:S V34 ADVANCED RECORDING TOOLS v1.0.")) {
	throw "Help attribution header was not found in the DLL."
}
if (-not $dllText.Contains("Built by Contrastniy. Build date:")) {
	throw "Updated help creator and build timestamp header were not found in the DLL."
}
if ($dllText.Contains("Inspired by HLAE and sin1ster's DOF config.")) {
	throw "Removed help attribution remains in the DLL."
}
if ($dllText.Contains("Based on the HLAE implementation")) {
	throw "Old help attribution was found in the DLL."
}
if ($dllText -notmatch "(?:Jan|Feb|Mar|Apr|May|Jun|Jul|Aug|Sep|Oct|Nov|Dec) [ 0-9][0-9] 20[0-9]{2}" -or
	$dllText -notmatch "[0-2][0-9]:[0-5][0-9]:[0-5][0-9]") {
	throw "Compiled __DATE__ or __TIME__ value was not found in the DLL."
}
if (-not $dllText.Contains("art_viewmodel_fov")) {
	throw "Viewmodel FOV command was not found in the DLL."
}
foreach ($requiredCommand in @(
	"art_open_folder", "art_preview_next", "art_toggle", "art_stats", "art_queue",
	"art_tga_compression", "art_validation", "art_take_json", "art_viewmodel_color",
	"art_players_color", "art_overlay", "art_demo_pause_after_recording", "art_hlae",
	"mirv_campath", "mirv_input", "mirv_camio", "mirv_agr", "mirv_camexport",
	"mirv_camimport", "mirv_fov"
)) {
	if (-not $dllText.Contains($requiredCommand)) {
		throw "Required ART command was not found in the DLL: $requiredCommand"
	}
}
foreach ($spectatorMarker in @(
	"hidepanel specgui", "hidepanel specmenu", "spec_menu 0",
	"Bars and the default CS:S camera menu."
)) {
	if (-not $dllText.Contains($spectatorMarker)) {
		throw "Spectator UI suppression marker was not found in the DLL: $spectatorMarker"
	}
}
if (-not $dllText.Contains("CLIENT MODE FOV HOOK INSTALL COMPLETE")) {
	throw "Global client-mode FOV hook was not found in the DLL."
}
if (-not $dllText.Contains("art_debug <on|off>")) {
	throw "Debug logging toggle help was not found in the DLL."
}
if (-not $dllText.Contains("DEBUG LOGGING ENABLED")) {
	throw "Debug logging enable path was not found in the DLL."
}
if ($dllText.Contains("standalone build-4044 four-pass recorder loaded")) {
	throw "Removed startup console message was found in the DLL."
}
foreach ($hlaeMarker in @(
	"advancedfx Cam", "HIERARCHY", "MdtCam", "afxGameRecord",
	"HLAE -afxV34 BRIDGE READY", "configured ART output folder",
	"art_hlae autoExport", "mirv_campath remove"
)) {
	if (-not $dllText.Contains($hlaeMarker)) {
		throw "HLAE compatibility marker was not found in the DLL: $hlaeMarker"
	}
}

Add-Type -AssemblyName System.IO.Compression.FileSystem
$expectedFiles = @(
	$exeName,
	$dllName,
	"readme.txt",
	"Scripts/ART_Importer_v1.0.jsx",
	"Scripts/ART_Camera_Baker_v1.0.jsx",
	"Scripts/readme.txt"
)

$sourceByName = @{
	$exeName = $exePath
	$dllName = $dllPath
	"readme.txt" = (Join-Path $repositoryRoot "packaging\readme.txt")
	"Scripts/ART_Importer_v1.0.jsx" = (Join-Path $repositoryRoot "tools\after_effects\ART_Importer_v1.0.jsx")
	"Scripts/ART_Camera_Baker_v1.0.jsx" = (Join-Path $repositoryRoot "tools\after_effects\ART_Camera_Baker_v1.0.jsx")
	"Scripts/readme.txt" = (Join-Path $repositoryRoot "tools\after_effects\readme.txt")
}

$zip = [IO.Compression.ZipFile]::OpenRead($packagePath)
try {
	$packageEntries = @($zip.Entries | Where-Object { -not $_.FullName.EndsWith("/") })
	$actualFiles = @($packageEntries | ForEach-Object { $_.FullName } | Sort-Object)
	$expectedSorted = @($expectedFiles | Sort-Object)
	if (($actualFiles -join "|") -ne ($expectedSorted -join "|")) {
		throw "Unexpected ZIP contents. Expected '$($expectedSorted -join ', ')'; got '$($actualFiles -join ', ')'."
	}
	if ($actualFiles | Where-Object { $_ -match "(?i)\.md$" }) {
		throw "Markdown files must not be included in the release ZIP."
	}

	foreach ($entry in $packageEntries) {
		$entryStream = $entry.Open()
		try {
			$zipHash = [Security.Cryptography.SHA256]::Create()
			try {
				$entryDigest = [BitConverter]::ToString($zipHash.ComputeHash($entryStream)).Replace("-", "")
			}
			finally {
				$zipHash.Dispose()
			}
		}
		finally {
			$entryStream.Dispose()
		}

		$sourceDigest = (Get-FileHash -Algorithm SHA256 -LiteralPath $sourceByName[$entry.FullName]).Hash
		if ($entryDigest -ne $sourceDigest) {
			throw "ZIP entry hash mismatch: $($entry.FullName)"
		}
	}
}
finally {
	$zip.Dispose()
}

Write-Host "Release verification passed."
Get-FileHash -Algorithm SHA256 -LiteralPath $exePath, $dllPath, $packagePath |
	ForEach-Object { Write-Host "$($_.Hash)  $($_.Path)" }
