#requires -Version 5.1
$ErrorActionPreference = "Stop"

$root = $PSScriptRoot
$exe = Join-Path $root "dist\ripple_rl.exe"
if (-not (Test-Path -LiteralPath $exe)) {
    $exe = Join-Path $root "ui-cpp\build\bin\ripple_rl.exe"
    if (-not (Test-Path -LiteralPath $exe)) {
        & (Join-Path $root "tools\build_ui.bat")
        if ($LASTEXITCODE -ne 0 -or -not (Test-Path -LiteralPath $exe)) {
            throw "Ripple RL could not be built."
        }
    }
}

Start-Process -FilePath $exe -WorkingDirectory (Split-Path -Parent $exe) -Wait
