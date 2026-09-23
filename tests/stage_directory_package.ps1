#requires -Version 7.0
param([string]$BuildDirectory = 'build/ela-release-0.3.5')

$ErrorActionPreference = 'Stop'
$repository = Split-Path -Parent $PSScriptRoot
$buildRoot = [IO.Path]::GetFullPath((Join-Path $repository $BuildDirectory))
$cache = Get-Content -LiteralPath (Join-Path $buildRoot 'CMakeCache.txt') -Raw
foreach ($entry in @('CMAKE_BUILD_TYPE:STRING=Release', 'REGMAP_UI_BACKEND:STRING=ELA', 'REGMAP_BUILD_TESTS:BOOL=OFF')) {
    if ($cache -notmatch "(?m)^$([regex]::Escape($entry))\r?$") { throw "Required configuration: $entry" }
}
if ($cache -notmatch '(?m)^CMAKE_HOME_DIRECTORY:INTERNAL=(.+)\r?$' -or
    [IO.Path]::GetFullPath($Matches[1].Trim()) -ne [IO.Path]::GetFullPath($repository)) {
    throw 'Build cache does not belong to this checkout'
}
if ($cache -notmatch '(?m)^CMAKE_PROJECT_VERSION:STATIC=([^\r\n]+)') { throw 'Version missing from cache' }
$version = $Matches[1]
if ($cache -notmatch '(?m)^CMAKE_COMMAND:INTERNAL=([^\r\n]+)') { throw 'CMake executable missing from cache' }
$cmake = $Matches[1]
$revision = (& git -C $repository rev-parse HEAD).Trim()
if ($LASTEXITCODE -ne 0 -or $revision -notmatch '^[0-9a-f]{40}$') { throw 'Cannot identify source revision' }
$status = & git -C $repository status --porcelain --untracked-files=normal
if ($LASTEXITCODE -ne 0 -or $status) { throw 'Directory releases require a clean source checkout' }
$stage = Join-Path $repository "out/RegMapWorkbench-$version-$($revision.Substring(0,7))-staging/RegMapWorkbench"
if (Test-Path -LiteralPath $stage) { throw "Staging already exists; it will not be overwritten: $stage" }
& $cmake --build $buildRoot --parallel 4
if ($LASTEXITCODE -ne 0) { throw 'Release build failed' }
& $cmake --install $buildRoot --config Release --component Runtime --prefix $stage
if ($LASTEXITCODE -ne 0) { throw 'Runtime installation failed' }
$checksumLines = foreach ($file in Get-ChildItem -LiteralPath $stage -Recurse -File | Sort-Object FullName) {
    $relative = [IO.Path]::GetRelativePath($stage, $file.FullName).Replace('\', '/')
    '{0}  {1}' -f (Get-FileHash -LiteralPath $file.FullName -Algorithm SHA256).Hash.ToLowerInvariant(), $relative
}
$checksumLines | Set-Content -LiteralPath (Join-Path $stage 'SHA256SUMS.txt') -Encoding utf8NoBOM
& (Join-Path $PSScriptRoot 'check_directory_package.ps1') -Directory $stage -ExpectedVersion $version -ExpectedRevision $revision
if ($LASTEXITCODE -ne 0) { throw 'Directory smoke test failed' }
"Staging: $stage"
"Revision: $revision"
Get-FileHash -LiteralPath (Join-Path $stage 'SHA256SUMS.txt') -Algorithm SHA256
