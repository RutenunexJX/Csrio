param(
    [string]$BuildDirectory = "build/ela-migration",
    [string]$OutputDirectory = "build/ela-migration/second-states"
)

$ErrorActionPreference = "Stop"
$root = Split-Path -Parent $PSScriptRoot
$outputRoot = [IO.Path]::GetFullPath((Join-Path $root $OutputDirectory))
$controls = Join-Path $root "$BuildDirectory/tests/regmap_ela_control_tests.exe"
$application = Join-Path $root "$BuildDirectory/tests/regmap_suiteui_control_test.exe"
$remaining = Join-Path $root "$BuildDirectory/tests/regmap_ela_remaining_tests.exe"
foreach ($executable in @($controls, $application)) {
    if (-not (Test-Path -LiteralPath $executable -PathType Leaf)) { throw "Missing executable: $executable" }
}
New-Item -ItemType Directory -Path $outputRoot -Force | Out-Null
$saved = @{}
foreach ($name in @("QT_QPA_PLATFORM", "QT_SCALE_FACTOR", "QT_SCREEN_SCALE_FACTORS", "QT_FONT_DPI",
                    "QT_QPA_FONTDIR", "QT_SCALE_FACTOR_ROUNDING_POLICY", "REGMAP_UI_STYLE",
                    "REGMAP_TEST_THEME", "REGMAP_UI_ARTIFACT_DIR")) {
    $saved[$name] = [Environment]::GetEnvironmentVariable($name, "Process")
}
try {
    $env:QT_QPA_PLATFORM = "offscreen"
    $env:QT_SCREEN_SCALE_FACTORS = "1"
    $env:QT_FONT_DPI = "96"
    $env:QT_QPA_FONTDIR = Join-Path $env:WINDIR "Fonts"
    $env:QT_SCALE_FACTOR_ROUNDING_POLICY = "PassThrough"
    $env:REGMAP_UI_STYLE = "ela"
    foreach ($scale in @("1", "1.25", "1.5", "2")) {
        $env:QT_SCALE_FACTOR = $scale
        $env:REGMAP_UI_ARTIFACT_DIR = Join-Path $outputRoot "scale-$scale/feedback"
        & $controls -o "$(Join-Path $outputRoot "feedback-$scale.log"),txt"
        if ($LASTEXITCODE -ne 0) { throw "Feedback contracts failed at scale $scale" }
        foreach ($theme in @("light", "dark")) {
            $env:REGMAP_TEST_THEME = $theme
            $env:REGMAP_UI_ARTIFACT_DIR = Join-Path $outputRoot "scale-$scale/$theme"
            & $application -o "$(Join-Path $outputRoot "$theme-$scale.log"),txt"
            if ($LASTEXITCODE -ne 0) { throw "Application contracts failed: $theme, scale $scale" }
            if (Test-Path -LiteralPath $remaining -PathType Leaf) {
                $env:REGMAP_UI_ARTIFACT_DIR = Join-Path $outputRoot "scale-$scale/remaining-$theme"
                & $remaining -o "$(Join-Path $outputRoot "remaining-$theme-$scale.log"),txt"
                if ($LASTEXITCODE -ne 0) { throw "Remaining-control contracts failed: $theme, scale $scale" }
            }
        }
    }
    $captures = foreach ($file in Get-ChildItem -LiteralPath $outputRoot -Filter *.png -File -Recurse) {
        $png = [IO.File]::ReadAllBytes($file.FullName)
        [pscustomobject]@{
            File = $file.FullName.Substring($outputRoot.Length + 1)
            PixelWidth = ([int]$png[16] -shl 24) -bor ([int]$png[17] -shl 16) -bor ([int]$png[18] -shl 8) -bor [int]$png[19]
            PixelHeight = ([int]$png[20] -shl 24) -bor ([int]$png[21] -shl 16) -bor ([int]$png[22] -shl 8) -bor [int]$png[23]
            Sha256 = (Get-FileHash -LiteralPath $file.FullName -Algorithm SHA256).Hash
        }
    }
    $captures | ConvertTo-Json | Set-Content -LiteralPath (Join-Path $outputRoot "manifest.json") -Encoding utf8
    Write-Output "State PNG captures: $($captures.Count)"
} finally {
    foreach ($name in $saved.Keys) { [Environment]::SetEnvironmentVariable($name, $saved[$name], "Process") }
}
