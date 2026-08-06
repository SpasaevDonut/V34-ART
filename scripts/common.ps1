Set-StrictMode -Version Latest

$script:ArtRepositoryRoot = (Resolve-Path -LiteralPath (Join-Path $PSScriptRoot "..")).Path

function Get-ArtRepositoryRoot {
	return $script:ArtRepositoryRoot
}

function Get-ArtVersion {
	$versionHeader = Join-Path $script:ArtRepositoryRoot "include\v34_art_version.h"
	$content = Get-Content -LiteralPath $versionHeader -Raw
	$match = [regex]::Match($content, '#define\s+V34_ART_VERSION_STRING\s+"([^"]+)"')
	if (-not $match.Success) {
		throw "Project version was not found in: $versionHeader"
	}
	return $match.Groups[1].Value
}

function Get-ArtDistributionBaseName {
	return "v34_art_v$(Get-ArtVersion)"
}

function Resolve-ArtMSBuild {
	param([string]$RequestedPath)

	if ($RequestedPath) {
		if (-not (Test-Path -LiteralPath $RequestedPath -PathType Leaf)) {
			throw "MSBuild was not found at: $RequestedPath"
		}
		return (Resolve-Path -LiteralPath $RequestedPath).Path
	}

	$programFilesX86 = [Environment]::GetFolderPath("ProgramFilesX86")
	$vswhere = Join-Path $programFilesX86 "Microsoft Visual Studio\Installer\vswhere.exe"
	if (Test-Path -LiteralPath $vswhere -PathType Leaf) {
		$detected = & $vswhere -latest -products * -requires Microsoft.Component.MSBuild -find "MSBuild\**\Bin\MSBuild.exe" |
			Select-Object -First 1
		if ($detected -and (Test-Path -LiteralPath $detected -PathType Leaf)) {
			return (Resolve-Path -LiteralPath $detected).Path
		}
	}

	$editions = @("Community", "Professional", "Enterprise", "BuildTools")
	foreach ($edition in $editions) {
		$candidate = Join-Path $programFilesX86 "Microsoft Visual Studio\2022\$edition\MSBuild\Current\Bin\MSBuild.exe"
		if (Test-Path -LiteralPath $candidate -PathType Leaf) {
			return (Resolve-Path -LiteralPath $candidate).Path
		}
	}

	throw "Visual Studio 2022 MSBuild was not found. Install 'Desktop development with C++' or pass -MSBuildPath."
}

function Resolve-ArtCssSdk {
	param([string]$RequestedPath)

	$repositoryRoot = Get-ArtRepositoryRoot
	$candidate = if ($RequestedPath) { $RequestedPath } else { Join-Path $repositoryRoot "third_party\cssv34-sdk" }
	$requiredFiles = @(
		"public\cdll_int.h",
		"public\tier1\interface.h",
		"lib\public\tier0.lib",
		"lib\public\vstdlib.lib",
		"lib\public\tier1.lib",
		"lib\public\tier2.lib",
		"lib\public\mathlib.lib",
		"lib\public\bitmap.lib"
	)

	if (-not (Test-Path -LiteralPath $candidate -PathType Container)) {
		throw "CSS v34 SDK not found at '$candidate'. See docs/BUILDING.md or pass -SdkRoot."
	}

	$resolved = (Resolve-Path -LiteralPath $candidate).Path
	foreach ($requiredFile in $requiredFiles) {
		if (-not (Test-Path -LiteralPath (Join-Path $resolved $requiredFile) -PathType Leaf)) {
			throw "'$resolved' is not the expected build-4044 SDK: missing $requiredFile"
		}
	}

	return $resolved
}

function Invoke-ArtProcess {
	param(
		[Parameter(Mandatory = $true)][string]$FilePath,
		[Parameter(Mandatory = $true)][string[]]$Arguments,
		[Parameter(Mandatory = $true)][string]$Description
	)

	& $FilePath @Arguments
	if ($LASTEXITCODE -ne 0) {
		throw "$Description failed with exit code $LASTEXITCODE."
	}
}
