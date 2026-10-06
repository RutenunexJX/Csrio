#requires -Version 7.0
param(
    [Parameter(Mandatory)][string]$Directory,
    [Parameter(Mandatory)][string]$ExpectedVersion,
    [Parameter(Mandatory)][string]$ExpectedRevision,
    [ValidateSet('clean','dirty')][string]$ExpectedSourceState = 'clean',
    [ValidateSet('ON','OFF')][string]$ExpectedTests = 'OFF',
    [string]$ExpectedSuiteAppVersion
)
$ErrorActionPreference = 'Stop'
$packageRoot = (Resolve-Path -LiteralPath $Directory).Path
if ([IO.Path]::GetFileName($packageRoot) -cne 'Csrio') { throw 'Expected a Csrio package root' }
$repository = Split-Path -Parent $PSScriptRoot
$smokeRoot = Join-Path $repository "build/validation/directory-smoke/$([guid]::NewGuid().ToString('N'))"
$null = New-Item -ItemType Directory -Path $smokeRoot
$files = @(Get-ChildItem -LiteralPath $packageRoot -File -Recurse)
$sums = Join-Path $packageRoot 'SHA256SUMS.txt'
$checked = [Collections.Generic.HashSet[string]]::new([StringComparer]::OrdinalIgnoreCase)
foreach ($line in [IO.File]::ReadAllLines($sums)) {
    if ($line -notmatch '^([0-9a-fA-F]{64})  (.+)$') { throw "Invalid checksum line: $line" }
    $digest = $Matches[1]; $relative = $Matches[2]
    if ($relative -match '(^[/\\]|^[A-Za-z]:|(^|[/\\])\.\.([/\\]|$))' -or
        -not $checked.Add($relative)) { throw "Unsafe or repeated entry: $relative" }
    $file = Join-Path $packageRoot $relative
    if ((Get-FileHash -LiteralPath $file -Algorithm SHA256).Hash -ne $digest) {
        throw "Checksum mismatch: $relative"
    }
}
if ($checked.Count -ne $files.Count - 1) { throw 'Checksum inventory is incomplete' }
foreach ($file in $files) {
    $relative = [IO.Path]::GetRelativePath($packageRoot, $file.FullName).Replace('\', '/')
    if ($file.Extension -ieq '.exe' -and $relative -cnotin @('Csrio.exe', 'regmapc.exe')) {
        throw "Unexpected executable in Csrio runtime: $relative"
    }
    if ($relative -ne 'SHA256SUMS.txt' -and -not $checked.Contains($relative)) {
        throw "Unlisted file: $relative"
    }
    if ($relative -match '(?i)(^|/)(examples|\.git|tests)/|\.(pdb|obj|a)$') {
        throw "Development content in runtime: $relative"
    }
}
foreach ($relative in @('Csrio.exe', 'regmapc.exe', 'ElaWidgetTools.dll',
    'Qt6Core.dll', 'Qt6Gui.dll', 'Qt6Widgets.dll', 'plugins/platforms/qwindows.dll',
    'README.md', 'docs/cli.md', 'docs/ela-native-capabilities.md', 'BUILD-INFO.txt',
    'licenses/ElaWidgetTools/LICENSE', 'licenses/ElaWidgetTools/FontAwesome-LICENSE.txt',
    'licenses/ElaWidgetTools/ZeroSlack-Apache-2.0.txt', 'licenses/ElaWidgetTools/REGMAP-NOTICE.md',
    'licenses/ElaWidgetTools/patches/11-regmap-native-capabilities.patch',
    'licenses/Qt-LGPLv3.txt', 'licenses/GCC-COPYING.RUNTIME.txt', 'licenses/GCC-COPYING3.LIB.txt',
    'licenses/GCC-COPYING3.txt', 'licenses/MinGW-w64-COPYING.txt', 'licenses/QXlsx-MIT.txt',
    'licenses/winpthreads-COPYING.txt', 'licenses/yaml-cpp-MIT.txt')) {
    if (-not (Test-Path -LiteralPath (Join-Path $packageRoot $relative) -PathType Leaf)) {
        throw "Missing runtime or notice: $relative"
    }
}
$info = Get-Content -LiteralPath (Join-Path $packageRoot 'BUILD-INFO.txt') -Raw
foreach ($line in @('Csrio', "Version: $ExpectedVersion", "Revision: $ExpectedRevision", "Source state: $ExpectedSourceState",
    'Build type: Release', 'Platform: win64', 'Qt: 6.10.2', 'UI backend: ELA', "Tests: $ExpectedTests")) {
    if ($info -notmatch "(?m)^$([regex]::Escape($line))\r?$") { throw "Metadata mismatch: $line" }
}
if ($ExpectedSuiteAppVersion) {
    foreach ($line in @('SuiteApp: ON', "SuiteApp SDK version: $ExpectedSuiteAppVersion")) {
        if ($info -notmatch "(?m)^$([regex]::Escape($line))\r?$") { throw "SDK metadata mismatch: $line" }
    }
    if (-not (Test-Path -LiteralPath (Join-Path $packageRoot 'Qt6Network.dll') -PathType Leaf)) {
        throw 'SDK-enabled runtime is missing Qt6Network.dll'
    }
}
$resource = [Diagnostics.FileVersionInfo]::GetVersionInfo((Join-Path $packageRoot 'Csrio.exe'))
if ($resource.ProductName -cne 'Csrio' -or $resource.FileDescription -cne 'Csrio' -or
    $resource.OriginalFilename -cne 'Csrio.exe' -or $resource.ProductVersion -ne $ExpectedVersion) {
    throw 'Csrio executable version resources do not match the package'
}
function Invoke-Packaged([string]$Executable, [string[]]$Arguments, [string]$Name) {
    $start = [Diagnostics.ProcessStartInfo]::new()
    $start.FileName = Join-Path $packageRoot $Executable
    $start.WorkingDirectory = $smokeRoot
    $start.UseShellExecute = $false
    $start.CreateNoWindow = $true
    $start.RedirectStandardOutput = $true
    $start.RedirectStandardError = $true
    foreach ($argument in $Arguments) { $start.ArgumentList.Add($argument) }
    foreach ($key in @($start.Environment.Keys)) {
        if ($key -match '^(QT_|QML|REGMAP_|SUITEAPP_)') { $null = $start.Environment.Remove($key) }
    }
    $start.Environment['PATH'] = "$env:WINDIR\System32;$env:WINDIR"
    $start.Environment['QT_QPA_PLATFORM'] = 'windows'
    $start.Environment['APPDATA'] = Join-Path $smokeRoot 'profile/Roaming'
    $start.Environment['LOCALAPPDATA'] = Join-Path $smokeRoot 'profile/Local'
    $process = [Diagnostics.Process]::Start($start)
    try {
        $stdout = $process.StandardOutput.ReadToEndAsync()
        $stderr = $process.StandardError.ReadToEndAsync()
        if (-not $process.WaitForExit(30000)) {
            $process.Kill($true); throw "Packaged command timed out: $Name"
        }
        $output = $stdout.GetAwaiter().GetResult(); $errors = $stderr.GetAwaiter().GetResult()
        $output | Set-Content -LiteralPath (Join-Path $smokeRoot "$Name.stdout.log") -Encoding utf8
        $errors | Set-Content -LiteralPath (Join-Path $smokeRoot "$Name.stderr.log") -Encoding utf8
        if ($process.ExitCode -ne 0) { throw "Failed $Name ($($process.ExitCode)): $errors" }
        return $output
    } finally { $process.Dispose() }
}
if ((Invoke-Packaged 'Csrio.exe' @('--version') 'gui-version').Trim() -ne
    "Csrio $ExpectedVersion") { throw 'GUI version mismatch' }
if ((Invoke-Packaged 'Csrio.exe' @('--help') 'gui-help') -notmatch 'Usage:') {
    throw 'GUI help missing'
}
$version = Invoke-Packaged 'regmapc.exe' @('--json', 'version') 'cli-version'
$null = $version | ConvertFrom-Json
if ($version -notmatch [regex]::Escape($ExpectedVersion)) { throw 'CLI version mismatch' }
$project = Join-Path $smokeRoot 'fixture/device.regmap.yaml'
$null = New-Item -ItemType Directory -Path (Split-Path -Parent $project)
foreach ($command in @('init', 'validate', 'generate')) {
    $null = (Invoke-Packaged 'regmapc.exe' @('--json', $command, $project) "cli-$command") | ConvertFrom-Json
}
$null = (Invoke-Packaged 'regmapc.exe' @('--json', 'status', $project, '--require-current') 'cli-status') | ConvertFrom-Json
foreach ($extension in @('*.xlsx', '*.h', '*.md')) {
    if (-not (Get-ChildItem -LiteralPath (Split-Path -Parent $project) -Recurse -File -Filter $extension)) {
        throw "Missing generated output: $extension"
    }
}
"Directory smoke passed: $($checked.Count) file hashes; GUI version/help; CLI version/init/validate/generate/current"
'SDK-independent PATH: Windows system directories only; isolated profile and project'
"Evidence: $smokeRoot"
