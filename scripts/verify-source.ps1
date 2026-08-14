[CmdletBinding()]
param()

$ErrorActionPreference = "Stop"
. (Join-Path $PSScriptRoot "common.ps1")

$repositoryRoot = Get-ArtRepositoryRoot
$failures = [System.Collections.Generic.List[string]]::new()
$legacyLoaderTerm = "inj" + "ector"
$legacyProductTerm = "strea" + "ms"

$requiredFiles = @(
	"README.md",
	"LICENSE",
	"build.bat",
	"THIRD_PARTY_NOTICES.md",
	"v34-art.sln",
	"dll\v34_art.cpp",
	"dll\art_commands.cpp",
	"dll\art_ffmpeg.cpp",
	"dll\art_ffmpeg.h",
	"dll\art_internal.h",
	"dll\art_logging.cpp",
	"dll\art_logic.cpp",
	"dll\art_logic.h",
	"dll\art_render.cpp",
	"dll\art_runtime.cpp",
	"dll\art_statistics.cpp",
	"dll\art_pipeline.cpp",
	"dll\art_gui.cpp",
	"dll\art_gui.h",
	"dll\v34_art.rc",
	"dll\v34_art.vcxproj",
	"dll\v34_art.vcxproj.filters",
	"include\v34_art_version.h",
	"loader\v34_art_loader.cpp",
	"loader\v34_art_loader.rc",
	"loader\v34_art_loader.vcxproj",
	"loader\v34_art_loader.vcxproj.filters",
	"assets\art_icon.ico",
	"assets\art_icon.png",
	"tools\after_effects\ART_Importer_v1.0.jsx",
	"tools\after_effects\ART_Camera_Baker_v1.0.jsx",
	"tools\after_effects\readme.txt",
	"packaging\readme.txt",
	"tests\art_logic_tests.cpp",
	"tests\art_logic_tests.vcxproj",
	"tests\art_logic_tests.vcxproj.filters",
	"tests\source_contracts.ps1",
	"scripts\common.ps1",
	"scripts\build.ps1",
	"scripts\build-dll.ps1",
	"scripts\build-loader.ps1",
	"scripts\package-release.ps1",
	"scripts\verify-source.ps1",
	"scripts\verify-release.ps1",
	"scripts\test.ps1",
	".github\workflows\source-checks.yml",
	"docs\COMMANDS.md",
	"docs\AFTER_EFFECTS.md",
	"docs\BUILDING.md",
	"docs\TROUBLESHOOTING.md",
	"cfg\art_gui\art_default.cfg",
	"third_party\README.md",
	"third_party\advancedfx\LICENSE.txt",
	"third_party\advancedfx\shared\AfxMath.cpp",
	"third_party\advancedfx\shared\AfxMath.h",
	"third_party\advancedfx\shared\CamPath.cpp",
	"third_party\advancedfx\shared\CamPath.h",
	"third_party\advancedfx\shared\MirvCampath.cpp",
	"third_party\advancedfx\shared\MirvCampath.h",
	"third_party\advancedfx\shared\MirvInput.cpp",
	"third_party\advancedfx\shared\MirvInput.h",
	"third_party\imgui\LICENSE.txt",
	"third_party\minhook\LICENSE.txt",
	"third_party\cssv34-sdk\README.md",
	"third_party\cssv34-sdk\UPSTREAM_README.md",
	"third_party\cssv34-sdk\public\cdll_int.h",
	"third_party\cssv34-sdk\public\tier1\interface.h",
	"third_party\cssv34-sdk\lib\public\tier0.lib",
	"third_party\cssv34-sdk\lib\public\vstdlib.lib",
	"third_party\cssv34-sdk\lib\public\tier1.lib",
	"third_party\cssv34-sdk\lib\public\tier2.lib",
	"third_party\cssv34-sdk\lib\public\mathlib.lib",
	"third_party\cssv34-sdk\lib\public\bitmap.lib"
)

foreach ($relativePath in $requiredFiles) {
	if (-not (Test-Path -LiteralPath (Join-Path $repositoryRoot $relativePath) -PathType Leaf)) {
		$failures.Add("Missing required file: $relativePath")
	}
}

foreach ($project in @(
	"dll\v34_art.vcxproj",
	"dll\v34_art.vcxproj.filters",
	"loader\v34_art_loader.vcxproj",
	"loader\v34_art_loader.vcxproj.filters",
	"tests\art_logic_tests.vcxproj",
	"tests\art_logic_tests.vcxproj.filters"
)) {
	try {
		[xml](Get-Content -LiteralPath (Join-Path $repositoryRoot $project) -Raw) | Out-Null
	}
	catch {
		$failures.Add("Invalid project XML: $project ($($_.Exception.Message))")
	}
}

$gitCommand = Get-Command git -ErrorAction SilentlyContinue
$isGitRepository = $false
if ($gitCommand) {
	$gitRepositoryResult = & $gitCommand.Source -C $repositoryRoot rev-parse --is-inside-work-tree 2>$null
	$isGitRepository = $LASTEXITCODE -eq 0 -and $gitRepositoryResult -eq "true"
}

if ($isGitRepository) {
	$trackedFiles = @(& $gitCommand.Source -C $repositoryRoot ls-files)
	if ($LASTEXITCODE -ne 0) {
		$failures.Add("git ls-files failed.")
	}
	else {
		foreach ($trackedFile in $trackedFiles) {
			$trackedPath = Join-Path $repositoryRoot $trackedFile
			if (-not (Test-Path -LiteralPath $trackedPath)) {
				continue
			}
			$generatedPathPattern = "^(build|dist|tmp|_vendor|\." + [regex]::Escape($legacyProductTerm) + "_[^/]+_backup)/"
			if ($trackedFile -match $generatedPathPattern) {
				$failures.Add("Generated, backup, or local dependency path is tracked: $trackedFile")
			}
			if ($trackedFile -match ("(?i)" + [regex]::Escape($legacyLoaderTerm))) {
				$failures.Add("Legacy Loader terminology remains in a tracked path: $trackedFile")
			}
			if ($trackedFile -match "(?i)\.(dll|exe|tga|zip|obj|pdb)$") {
				$failures.Add("Generated binary is tracked: $trackedFile")
			}
			if ((Get-Item -LiteralPath $trackedPath).Length -gt 90MB) {
				$failures.Add("Tracked file is larger than the repository limit (90 MB): $trackedFile")
			}
		}
	}
}

$forbiddenPaths = @(
	"CHANGELOG.md",
	"CONTRIBUTING.md",
	"SECURITY.md",
	"docs\GUI.md",
	"docs\ARCHITECTURE.md",
	"docs\RELEASING.md",
	$legacyLoaderTerm,
	("." + $legacyProductTerm + "_gui_backup"),
	("." + $legacyProductTerm + "_imgui_backup"),
	"symbolize-address.obj",
	"dll\art_gui.cpp.icvar-backup",
	"src\css_art.cpp",
	"src\css_art.h",
	("scripts\build-" + $legacyLoaderTerm + ".ps1"),
	("loader\v34_art_" + $legacyLoaderTerm + ".cpp"),
	("loader\v34_art_" + $legacyLoaderTerm + ".vcxproj")
)
foreach ($relativePath in $forbiddenPaths) {
	if (Test-Path -LiteralPath (Join-Path $repositoryRoot $relativePath)) {
		$failures.Add("Obsolete path still exists: $relativePath")
	}
}

$textExtensions = @(".bat", ".cpp", ".c", ".h", ".jsx", ".md", ".txt", ".ps1", ".rc", ".sln", ".vcxproj", ".filters", ".yml", ".yaml", ".cfg")
$textFiles = Get-ChildItem -LiteralPath $repositoryRoot -Recurse -File |
	Where-Object {
		$textExtensions -contains $_.Extension -and
		$_.FullName -notmatch "[\\/](\.git|\.[^\\/]*_backup|_vendor|build|dist|third_party|tmp)[\\/]"
	}

$renameAuditFiles = Get-ChildItem -LiteralPath $repositoryRoot -Force -Recurse -File |
	Where-Object {
		$_.FullName -notmatch "[\\/](\.git|\.[^\\/]*_backup|_vendor|build|dist|third_party|tmp)[\\/]"
	}
foreach ($file in $renameAuditFiles) {
	$relative = $file.FullName.Substring($repositoryRoot.Length + 1)
	if ($relative.IndexOf($legacyProductTerm, [StringComparison]::OrdinalIgnoreCase) -ge 0) {
		$failures.Add("Legacy product name remains in an active path: $relative")
	}

	if ($textExtensions -contains $file.Extension -or
		$file.Name -in @(".gitignore", ".gitattributes", ".editorconfig") -or
		$file.Name.EndsWith(".icvar-backup", [StringComparison]::OrdinalIgnoreCase)) {
		$content = Get-Content -LiteralPath $file.FullName -Raw
		if ($content.IndexOf($legacyProductTerm, [StringComparison]::OrdinalIgnoreCase) -ge 0) {
			$failures.Add("Legacy product name remains in active content: $relative")
		}
	}
}

foreach ($file in $textFiles) {
	$lineNumber = 0
	foreach ($line in [IO.File]::ReadLines($file.FullName)) {
		++$lineNumber
		if ($line -match "[ \t]+$") {
			$relative = $file.FullName.Substring($repositoryRoot.Length + 1)
			$failures.Add("Trailing whitespace: ${relative}:$lineNumber")
		}
	}
}

$markdownFiles = $textFiles | Where-Object { $_.Extension -eq ".md" }
foreach ($file in $markdownFiles) {
	$content = Get-Content -LiteralPath $file.FullName -Raw
	$linkTargets = [regex]::Matches($content, "\[[^\]]+\]\(([^)]+)\)") |
		ForEach-Object { $_.Groups[1].Value }
	$htmlTargets = [regex]::Matches($content, "(?:href|src)=`"([^`"]+)`"") |
		ForEach-Object { $_.Groups[1].Value }

	foreach ($target in @($linkTargets) + @($htmlTargets)) {
		if (-not $target -or $target.StartsWith("#") -or $target -match "^[a-zA-Z][a-zA-Z0-9+.-]*:") {
			continue
		}
		$pathPart = ($target -split "#", 2)[0].Trim("<", ">")
		if (-not $pathPart) {
			continue
		}
		$resolvedTarget = Join-Path $file.DirectoryName $pathPart
		if (-not (Test-Path -LiteralPath $resolvedTarget)) {
			$relative = $file.FullName.Substring($repositoryRoot.Length + 1)
			$failures.Add("Broken relative Markdown link in ${relative}: $target")
		}
	}
}

$oldStartupMessage = "v34_art: standalone build-4044 four-pass recorder loaded"
$dllSourceFiles = @(
	"dll\v34_art.cpp",
	"dll\art_commands.cpp",
	"dll\art_logging.cpp",
	"dll\art_logic.cpp",
	"dll\art_render.cpp",
	"dll\art_runtime.cpp"
)
$activeSource = ($dllSourceFiles | ForEach-Object {
	Get-Content -LiteralPath (Join-Path $repositoryRoot $_) -Raw
}) -join "`n"
$entryPointSource = Get-Content -LiteralPath (Join-Path $repositoryRoot "dll\v34_art.cpp") -Raw
$commandsSource = Get-Content -LiteralPath (Join-Path $repositoryRoot "dll\art_commands.cpp") -Raw
$loggingSource = Get-Content -LiteralPath (Join-Path $repositoryRoot "dll\art_logging.cpp") -Raw
if ($activeSource.Contains($oldStartupMessage)) {
	$failures.Add("Removed startup console message returned to DLL source")
}
if (-not $activeSource.Contains('ConCommand art_viewmodel_fov( "art_viewmodel_fov"')) {
	$failures.Add("art_viewmodel_fov command registration is missing from DLL source")
}
if (-not $loggingSource.Contains('volatile LONG g_bDebugLogging = FALSE;')) {
	$failures.Add("Debug file logging is not disabled by default in art_logging.cpp")
}
if (-not $entryPointSource.Contains('static const bool kEnableDebugLoggingOnInjection = false;')) {
	$failures.Add("Emergency injection logging switch is missing or is not disabled by default")
}
if (-not $activeSource.Contains('ConCommand art_debug( "art_debug"')) {
	$failures.Add("art_debug command registration is missing from DLL source")
}
if (-not $entryPointSource.Contains('InitializeLogging();') -or $entryPointSource.Contains('CreateFileA(')) {
	$failures.Add("DLL entry point must initialize logging without directly opening the log file")
}
$expectedHelp = 'V34_ART_PRODUCT_NAME " v" V34_ART_VERSION_STRING'
if (-not $commandsSource.Contains($expectedHelp) -or
	-not $commandsSource.Contains('V34_ART_COMPANY_NAME') -or
	-not $commandsSource.Contains('__DATE__, __TIME__')) {
	$failures.Add("art_help attribution or compile date/time fields are incorrect")
}
$versionHeader = Get-Content -LiteralPath (Join-Path $repositoryRoot "include\v34_art_version.h") -Raw
if (-not $versionHeader.Contains('#define V34_ART_VERSION_STRING "1.0"') -or
	-not $versionHeader.Contains("#define V34_ART_VERSION_FILE 1,0,0,0") -or
	-not $versionHeader.Contains('#define V34_ART_DISTRIBUTION_BASENAME "v34_art_v1.0"') -or
	-not $versionHeader.Contains('#define V34_ART_PRODUCT_NAME "CS:S V34 ADVANCED RECORDING TOOLS"')) {
	$failures.Add("Shared project metadata is not set to ART V1.0")
}
$licenseText = Get-Content -LiteralPath (Join-Path $repositoryRoot "LICENSE") -Raw
if (-not $licenseText.StartsWith("MIT License") -or -not $licenseText.Contains("Copyright (c) 2026 Contrastniy")) {
	$failures.Add("LICENSE is not the expected MIT license for ART")
}
$guiSource = Get-Content -LiteralPath (Join-Path $repositoryRoot "dll\art_gui.cpp") -Raw
if (-not $guiSource.Contains('static const char *kGuiVersion = V34_ART_VERSION_STRING;')) {
	$failures.Add("GUI version is not tied to the shared ART V1.0 version")
}
if (-not $guiSource.Contains('ImGui::Begin( V34_ART_PRODUCT_NAME " V" V34_ART_VERSION_STRING')) {
	$failures.Add("Main GUI title does not display the shared ART V1.0 version")
}
if (-not $activeSource.Contains('ConVar art_depth_start( "art_depth_start", "150"') -or
	-not $activeSource.Contains('ConVar art_depth_end( "art_depth_end", "800"')) {
	$failures.Add("Depth defaults are not set to 150/800")
}
if ($activeSource.Contains("Based on the HLAE implementation")) {
	$failures.Add("Old art_help attribution remains in DLL source")
}
$entryPointLineCount = (Get-Content -LiteralPath (Join-Path $repositoryRoot "dll\v34_art.cpp")).Count
if ($entryPointLineCount -gt 400) {
	$failures.Add("DLL entry point grew beyond 400 lines; move feature code into its module")
}
$dllProjectText = Get-Content -LiteralPath (Join-Path $repositoryRoot "dll\v34_art.vcxproj") -Raw
foreach ($module in @("art_commands.cpp", "art_logging.cpp", "art_logic.cpp", "art_render.cpp", "art_runtime.cpp", "art_gui.cpp")) {
	if (-not $dllProjectText.Contains($module)) {
		$failures.Add("DLL project does not compile module: $module")
	}
}

foreach ($resourceScript in @("dll\v34_art.rc", "loader\v34_art_loader.rc")) {
	$resourceText = Get-Content -LiteralPath (Join-Path $repositoryRoot $resourceScript) -Raw
	if (-not $resourceText.Contains('V34_ART_ICON_RESOURCE_ID ICON "..\\assets\\art_icon.ico"')) {
		$failures.Add("Shared ART icon is not embedded by: $resourceScript")
	}
}

if ($isGitRepository) {
	$previousErrorAction = $ErrorActionPreference
	$ErrorActionPreference = "Continue"
	$diffCheck = & $gitCommand.Source -C $repositoryRoot diff --check 2>&1
	$diffExitCode = $LASTEXITCODE
	$ErrorActionPreference = $previousErrorAction
	if ($diffExitCode -ne 0) {
		$failures.Add("git diff --check failed:`n$($diffCheck -join [Environment]::NewLine)")
	}
}

if ($failures.Count -gt 0) {
	$failures | ForEach-Object { Write-Error $_ }
	throw "Source verification failed with $($failures.Count) issue(s)."
}

Write-Host "Source verification passed."
