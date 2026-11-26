<#
Assemble a distributable single-exe build of Ripple RL into dist\:

  dist\ripple_rl.exe      the app  (the native backend is hosted in-process)
  dist\data\             catalog / feature-default data the backend reads
  dist\ripple\    the mitmproxy addon + the modules it imports (proxy only)
  dist\runtime\          embedded Python + mitmproxy  (the proxy stays Python)

The result is one executable plus a runtime\ folder — no separate backend
process, and no Python needed on the user's machine.

This assembles the proxy runtime from an existing Python 3.12 install: it copies
the interpreter + standard library (NOT that install's site-packages, which may be
huge and unrelated) and then copies exactly mitmproxy + pycryptodome and their
dependency closure (computed from installed package metadata by copy_deps.py).
It also fetches a pinned PresentMon console build for the FPS counter.

Prereqs: CMake 3.25+, the VS C++ toolchain, and a Python 3.12 with mitmproxy +
pycryptodome installed (the project's requirements).

Usage:
  packaging\bundle-runtime.ps1
  packaging\bundle-runtime.ps1 -PyHome C:\Python312
#>
param(
    [string]$Root = (Resolve-Path "$PSScriptRoot\.."),
    [string]$PyHome = "C:\Python312",
    [string]$Py = "py -3.12",
    [string]$VcVars = "C:\Program Files\Microsoft Visual Studio\18\Community\VC\Auxiliary\Build\vcvars64.bat"
)
$ErrorActionPreference = "Stop"
$dist = Join-Path $Root "dist"
$rt = Join-Path $dist "runtime"
$uiBuild = Join-Path $Root "ui-cpp\build"

Write-Host "== 1/4  Building ripple_rl.exe (Release, backend linked in) =="
$cfg = '"' + $VcVars + '" && cmake -S "' + (Join-Path $Root 'ui-cpp') + '" -B "' + $uiBuild + '" -G Ninja -DCMAKE_BUILD_TYPE=Release && cmake --build "' + $uiBuild + '"'
cmd /c $cfg
$exe = Join-Path $uiBuild "bin\ripple_rl.exe"
if (-not (Test-Path $exe)) { throw "ripple_rl.exe was not produced at $exe" }

Write-Host "== 2/4  Laying out dist\ =="
if (Test-Path $dist) { Remove-Item $dist -Recurse -Force }
New-Item -ItemType Directory -Force -Path $rt | Out-Null
Copy-Item $exe $dist
robocopy (Join-Path $Root "data") (Join-Path $dist "data") /E /NFL /NDL /NJH /NJS /NP | Out-Null
robocopy (Join-Path $Root "ripple") (Join-Path $dist "ripple") /E /XD __pycache__ /NFL /NDL /NJH /NJS /NP | Out-Null

Write-Host "== 3/4  Assembling runtime\ (interpreter + stdlib) =="
foreach ($f in "python.exe","pythonw.exe","python312.dll","python3.dll","vcruntime140.dll","vcruntime140_1.dll") {
    if (Test-Path "$PyHome\$f") { Copy-Item "$PyHome\$f" $rt }
}
robocopy "$PyHome\DLLs" "$rt\DLLs" /E /NFL /NDL /NJH /NJS /NP | Out-Null
# Copy the stdlib, but NOT the host install's site-packages.
robocopy "$PyHome\Lib" "$rt\Lib" /E /XD site-packages __pycache__ /NFL /NDL /NJH /NJS /NP | Out-Null
New-Item -ItemType Directory -Force -Path "$rt\Lib\site-packages" | Out-Null
Copy-Item (Join-Path $PSScriptRoot "mitmdump-launch.py") $rt

Write-Host "== 4/4  Copying mitmproxy + dependency closure =="
cmd /c "$Py `"$PSScriptRoot\copy_deps.py`" `"$rt\Lib\site-packages`""
& (Join-Path $PSScriptRoot "bundle-presentmon.ps1") -Runtime $rt

Write-Host ""
Write-Host "Done. Verifying the bundled proxy runtime:"
& "$rt\python.exe" "$rt\mitmdump-launch.py" --version
Write-Host ""
Write-Host "dist\ is ready. ProxyManager resolves runtime\python.exe + runtime\mitmdump-launch.py."
