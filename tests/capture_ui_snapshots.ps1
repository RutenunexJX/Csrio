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
$captures = @()

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
                $png = [IO.File]::ReadAllBytes($target)
                $pixelWidth = ([int]$png[16] -shl 24) -bor ([int]$png[17] -shl 16) -bor ([int]$png[18] -shl 8) -bor [int]$png[19]
                $pixelHeight = ([int]$png[20] -shl 24) -bor ([int]$png[21] -shl 16) -bor ([int]$png[22] -shl 8) -bor [int]$png[23]
                $factor = [double]::Parse($scale, [Globalization.CultureInfo]::InvariantCulture)
                if ($pixelWidth -ne $viewport.Width * $factor -or $pixelHeight -ne $viewport.Height * $factor) {
                    throw "PNG pixel size mismatch: $name is ${pixelWidth}x${pixelHeight}"
                }
                $captures += [pscustomobject]@{
                    File = $name; Theme = $theme; Scale = $factor
                    LogicalWidth = $viewport.Width; LogicalHeight = $viewport.Height
                    PixelWidth = $pixelWidth; PixelHeight = $pixelHeight
                    Sha256 = (Get-FileHash -LiteralPath $target -Algorithm SHA256).Hash
                }
            }
        }
    }
    $captures | ConvertTo-Json | Set-Content -LiteralPath (Join-Path $outputRoot "manifest.json") -Encoding utf8
} finally {
    $env:QT_QPA_PLATFORM = $previousPlatform
    $env:QT_SCALE_FACTOR = $previousScale
    $env:QT_SCREEN_SCALE_FACTORS = $previousScreenScales
    $env:QT_FONT_DPI = $previousFontDpi
    $env:QT_QPA_FONTDIR = $previousFontDirectory
    $env:QT_SCALE_FACTOR_ROUNDING_POLICY = $previousRoundingPolicy
}
