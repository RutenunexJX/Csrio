#requires -Version 7.0
param(
    [Parameter(Mandatory)][string]$Archive,
    [Parameter(Mandatory)][string]$ExpectedVersion,
    [Parameter(Mandatory)][string]$ExpectedRevision
)

$ErrorActionPreference = "Stop"
$archivePath = (Resolve-Path -LiteralPath $Archive).Path
$checksumPath = "$archivePath.sha256"
$actualHash = (Get-FileHash -LiteralPath $archivePath -Algorithm SHA256).Hash
$checksum = Get-Content -LiteralPath $checksumPath -Raw
if (($checksum -split '\s+')[0] -ne $actualHash) { throw "Archive checksum mismatch" }
if ([IO.Path]::GetFileName($archivePath) -ne "RegMapWorkbench-$ExpectedVersion-win64-$ExpectedRevision.zip") {
    throw "Expected a clean, revision-qualified release archive"
}

Add-Type -AssemblyName System.IO.Compression.FileSystem
$zip = [IO.Compression.ZipFile]::OpenRead($archivePath)
try {
    foreach ($entry in $zip.Entries) {
        if ($entry.FullName -match '(^[/\\]|^[A-Za-z]:|(^|[/\\])\.\.([/\\]|$))') {
            throw "Unsafe archive entry: $($entry.FullName)"
        }
        if ($entry.FullName -match '(?i)(^|/)(examples|\.git|tests)/|\.(pdb|obj|a)$') {
            throw "Unexpected development content: $($entry.FullName)"
        }
    }
} finally { $zip.Dispose() }

$repository = Split-Path -Parent $PSScriptRoot
$smokeRoot = Join-Path $repository ".tmp/portable-smoke/$([guid]::NewGuid().ToString('N'))"
New-Item -ItemType Directory -Path $smokeRoot | Out-Null
Expand-Archive -LiteralPath $archivePath -DestinationPath $smokeRoot
$roots = @(Get-ChildItem -LiteralPath $smokeRoot -Directory)
if ($roots.Count -ne 1) { throw "Expected one package root" }
$packageRoot = $roots[0].FullName
foreach ($relative in @("RegMapWorkbench.exe", "regmapc.exe", "ElaWidgetTools.dll",
        "Qt6Core.dll", "Qt6Gui.dll", "Qt6Widgets.dll", "plugins/platforms/qwindows.dll",
        "README.md", "docs/cli.md", "docs/ela-migration.md", "BUILD-INFO.txt",
        "licenses/ElaWidgetTools/LICENSE", "licenses/ElaWidgetTools/FontAwesome-LICENSE.txt",
        "licenses/ElaWidgetTools/ZeroSlack-Apache-2.0.txt", "licenses/ElaWidgetTools/REGMAP-NOTICE.md",
        "licenses/ElaWidgetTools/patches/10-regmap-remaining-surfaces.patch")) {
    if (-not (Test-Path -LiteralPath (Join-Path $packageRoot $relative) -PathType Leaf)) {
        throw "Missing runtime or notice: $relative"
    }
}
$buildInfo = Get-Content -LiteralPath (Join-Path $packageRoot "BUILD-INFO.txt") -Raw
foreach ($line in @("Version: $ExpectedVersion", "Revision: $ExpectedRevision", "Source state: clean",
                    "Build type: Release", "Platform: win64", "Qt: 6.10.2", "UI backend: ELA")) {
    if ($buildInfo -notmatch "(?m)^$([regex]::Escape($line))\r?$") { throw "Build metadata mismatch: $line" }
}

function Invoke-PackagedCommand([string]$Executable, [string[]]$Arguments, [string]$Name) {
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
    $start.Environment['REGMAP_UI_STYLE'] = 'ela'
    $start.Environment['APPDATA'] = Join-Path $smokeRoot 'profile/Roaming'
    $start.Environment['LOCALAPPDATA'] = Join-Path $smokeRoot 'profile/Local'
    $process = [Diagnostics.Process]::Start($start)
    try {
        $stdout = $process.StandardOutput.ReadToEndAsync()
        $stderr = $process.StandardError.ReadToEndAsync()
        if (-not $process.WaitForExit(30000)) {
            $process.Kill($true)
            throw "Packaged command timed out: $Name"
        }
        $output = $stdout.GetAwaiter().GetResult()
        $errors = $stderr.GetAwaiter().GetResult()
        $output | Set-Content -LiteralPath (Join-Path $smokeRoot "$Name.stdout.log") -Encoding utf8
        $errors | Set-Content -LiteralPath (Join-Path $smokeRoot "$Name.stderr.log") -Encoding utf8
        if ($process.ExitCode -ne 0) { throw "Packaged command failed: $Name ($($process.ExitCode)) $errors" }
        return $output
    } finally { $process.Dispose() }
}

$guiVersion = Invoke-PackagedCommand 'RegMapWorkbench.exe' @('--version') 'gui-version'
if ($guiVersion.Trim() -ne "Register Map Workbench $ExpectedVersion") { throw "GUI version mismatch" }
$guiHelp = Invoke-PackagedCommand 'RegMapWorkbench.exe' @('--help') 'gui-help'
if ($guiHelp -notmatch 'Usage:') { throw "GUI help missing" }
$cliVersion = Invoke-PackagedCommand 'regmapc.exe' @('--json', 'version') 'cli-version'
$null = $cliVersion | ConvertFrom-Json
if ($cliVersion -notmatch [regex]::Escape($ExpectedVersion)) { throw "CLI version mismatch" }
$project = Join-Path $smokeRoot 'fixture/device.regmap.yaml'
$null = New-Item -ItemType Directory -Path (Split-Path -Parent $project)
foreach ($command in @('init', 'validate', 'generate')) {
    $output = Invoke-PackagedCommand 'regmapc.exe' @('--json', $command, $project) "cli-$command"
    $null = $output | ConvertFrom-Json
}
$status = Invoke-PackagedCommand 'regmapc.exe' @('--json', 'status', $project, '--require-current') 'cli-status'
$null = $status | ConvertFrom-Json
foreach ($extension in @('*.xlsx', '*.h', '*.md')) {
    if (-not (Get-ChildItem -LiteralPath (Split-Path -Parent $project) -Recurse -File -Filter $extension)) {
        throw "Missing generated output: $extension"
    }
}
Write-Output "Portable smoke passed: GUI version/help; CLI version/init/validate/generate/current outputs"
Write-Output "SDK-independent PATH: Windows system directories only; no external Qt plugin paths"
Write-Output "SHA256: $actualHash"
Write-Output "Evidence: $smokeRoot"
