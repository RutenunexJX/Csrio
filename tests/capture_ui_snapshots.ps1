param(
    [string]$BuildDirectory = "build/dev-debug",
    [string]$OutputDirectory = "out/ui-snapshots"
)

$ErrorActionPreference = "Stop"
$root = Split-Path -Parent $PSScriptRoot
$executable = Join-Path $root "$BuildDirectory/tests/regmap_ui_snapshot.exe"
$outputRoot = Join-Path $root $OutputDirectory

if (-not (Test-Path -LiteralPath $executable -PathType Leaf)) {
    throw "Build regmap_ui_snapshot before capturing screenshots: $executable"
}

$previousPlatform = $env:QT_QPA_PLATFORM
$previousScale = $env:QT_SCALE_FACTOR
$previousScreenScales = $env:QT_SCREEN_SCALE_FACTORS
$previousFontDpi = $env:QT_FONT_DPI
$previousFontDirectory = $env:QT_QPA_FONTDIR
$previousRoundingPolicy = $env:QT_SCALE_FACTOR_ROUNDING_POLICY
$fontDirectory = Join-Path $env:WINDIR "Fonts"
$env:QT_QPA_PLATFORM = "offscreen"
$env:QT_SCREEN_SCALE_FACTORS = "1"
$env:QT_FONT_DPI = "96"
$env:QT_QPA_FONTDIR = $fontDirectory
$env:QT_SCALE_FACTOR_ROUNDING_POLICY = "PassThrough"

try {
    foreach ($scale in @("1", "1.25", "1.5", "2")) {
        $env:QT_SCALE_FACTOR = $scale
        $scaleLabel = $scale.Replace(".", "-")
        foreach ($viewport in @(
            @{ Width = 960; Height = 720 },
            @{ Width = 1440; Height = 900 }
        )) {
            foreach ($theme in @("light", "dark")) {
                $name = "workbench-$theme-$($viewport.Width)x$($viewport.Height)-scale-$scaleLabel.png"
                $target = Join-Path $outputRoot $name
                & $executable `
                    --output $target `
                    --theme $theme `
                    --width $viewport.Width `
                    --height $viewport.Height
                if ($LASTEXITCODE -ne 0) {
                    throw "Snapshot failed with exit code ${LASTEXITCODE}: $name"
                }
            }
        }
    }
} finally {
    $env:QT_QPA_PLATFORM = $previousPlatform
    $env:QT_SCALE_FACTOR = $previousScale
    $env:QT_SCREEN_SCALE_FACTORS = $previousScreenScales
    $env:QT_FONT_DPI = $previousFontDpi
    $env:QT_QPA_FONTDIR = $previousFontDirectory
    $env:QT_SCALE_FACTOR_ROUNDING_POLICY = $previousRoundingPolicy
}
