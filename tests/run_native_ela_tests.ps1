param(
    [string]$BuildDirectory = "build/ela-migration",
    [string]$OutputDirectory = "build/ela-migration/native-third"
)

$ErrorActionPreference = "Stop"
$root = Split-Path -Parent $PSScriptRoot
$executable = Join-Path $root "$BuildDirectory/tests/regmap_ela_remaining_tests.exe"
if (-not (Test-Path -LiteralPath $executable -PathType Leaf)) { throw "Missing executable: $executable" }
$outputRoot = [IO.Path]::GetFullPath((Join-Path $root $OutputDirectory))
New-Item -ItemType Directory -Path $outputRoot -Force | Out-Null
$saved = @{}
foreach ($name in @("QT_QPA_PLATFORM", "QT_SCALE_FACTOR", "QT_SCREEN_SCALE_FACTORS", "QT_FONT_DPI",
                    "QT_SCALE_FACTOR_ROUNDING_POLICY", "REGMAP_UI_STYLE", "REGMAP_UI_REVIEW",
                    "REGMAP_TEST_THEME", "REGMAP_UI_ARTIFACT_DIR", "REGMAP_EXPECTED_SCALE")) {
    $saved[$name] = [Environment]::GetEnvironmentVariable($name, "Process")
}
try {
    # QtTest events target temporary fixtures; this script does not inject system mouse input.
    $env:QT_QPA_PLATFORM = "windows"
    $env:QT_SCREEN_SCALE_FACTORS = "1"
    $env:QT_FONT_DPI = "96"
    $env:QT_SCALE_FACTOR_ROUNDING_POLICY = "PassThrough"
    $env:REGMAP_UI_STYLE = "ela"
    $env:REGMAP_UI_REVIEW = $null
    foreach ($scale in @("1", "1.25", "1.5", "2")) {
        $env:QT_SCALE_FACTOR = $scale
        $env:REGMAP_EXPECTED_SCALE = $scale
        foreach ($theme in @("light", "dark")) {
            $env:REGMAP_TEST_THEME = $theme
            $env:REGMAP_UI_ARTIFACT_DIR = Join-Path $outputRoot "$theme-$scale"
            $log = Join-Path $outputRoot "$theme-$scale.log"
            & $executable -o "$log,txt"
            if ($LASTEXITCODE -ne 0) { Get-Content -LiteralPath $log; throw "Native contracts failed: $theme, scale $scale" }
            if (-not (Select-String -LiteralPath $log -Pattern 'Totals: 7 passed, 0 failed, 0 skipped' -Quiet)) {
                throw "Incomplete native contracts: $theme, scale $scale"
            }
            Write-Output "Native contracts passed: $theme, scale $scale (7/7)"
        }
    }
} finally {
    foreach ($name in $saved.Keys) { [Environment]::SetEnvironmentVariable($name, $saved[$name], "Process") }
}
