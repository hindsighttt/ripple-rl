param([string]$Python = 'python.exe')

$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
$build = Join-Path $root 'ui-cpp/build/portable'
$dist = Join-Path $root 'dist'
$package = Join-Path $env:TEMP ('RippleRL-portable-' + [guid]::NewGuid().ToString('N'))

cmake -S "$root/ui-cpp" -B $build -G Ninja -DCMAKE_BUILD_TYPE=Release `
    -DCMAKE_MSVC_RUNTIME_LIBRARY=MultiThreaded -DRIPPLE_RL_USE_LOCAL_BAKED_FONTS=OFF
if ($LASTEXITCODE) { throw 'Portable build configuration failed.' }
cmake --build $build
if ($LASTEXITCODE) { throw 'Portable build failed.' }

$exe = Join-Path $build 'bin/ripple_rl.exe'
$version = (Get-Item -LiteralPath $exe).VersionInfo.ProductVersion
if ($version -notmatch '^\d+\.\d+\.\d+$') { throw 'Invalid executable version.' }
$null = New-Item -ItemType Directory -Path $package, $dist -Force
try {
    Copy-Item -LiteralPath $exe -Destination $package
    & $Python -I -B "$PSScriptRoot/portable-files.py" $package
    if ($LASTEXITCODE) { throw 'Could not bundle the offline package writer.' }
    & "$PSScriptRoot/bundle-presentmon.ps1" -Runtime "$package/runtime"
    $zip = Join-Path $dist "Ripple-RL-v$version-Windows-x64.zip"
    Compress-Archive -Path "$package/*" -DestinationPath $zip -CompressionLevel Optimal -Force
    Get-FileHash -LiteralPath $zip -Algorithm SHA256
} finally {
    $tempRoot = (Resolve-Path -LiteralPath $env:TEMP).Path
    $resolvedPackage = (Resolve-Path -LiteralPath $package).Path
    if ((Split-Path -Parent $resolvedPackage) -eq $tempRoot) {
        Remove-Item -LiteralPath $resolvedPackage -Recurse -Force
    }
}
