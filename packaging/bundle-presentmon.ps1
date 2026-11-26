param(
    [string]$Runtime = (Join-Path $PSScriptRoot '..\dist\runtime')
)

$ErrorActionPreference = 'Stop'
$version = '2.6.0'
$digest = 'B2A706BC6AD475749E3B7E3409263AA1E6906D45BDCF993F6DBC0F660188F1AF'
$null = New-Item -ItemType Directory -Path $Runtime -Force
$exe = Join-Path $Runtime 'PresentMon.exe'
$license = Join-Path $Runtime 'PresentMon-LICENSE.txt'

if ((Test-Path -LiteralPath $exe) -and (Test-Path -LiteralPath $license) -and
    (Get-FileHash -LiteralPath $exe -Algorithm SHA256).Hash -eq $digest) {
    return
}

$tempExe = Join-Path $Runtime ('PresentMon-' + [guid]::NewGuid() + '.download')
try {
    $url = "https://github.com/GameTechDev/PresentMon/releases/download/v$version/PresentMon-$version-x64.exe"
    Invoke-WebRequest -Uri $url -OutFile $tempExe -TimeoutSec 30
    if ((Get-FileHash -LiteralPath $tempExe -Algorithm SHA256).Hash -ne $digest) {
        throw "PresentMon $version SHA-256 verification failed"
    }
    Invoke-WebRequest -Uri "https://raw.githubusercontent.com/GameTechDev/PresentMon/v$version/LICENSE.txt" -OutFile $license -TimeoutSec 30
    Move-Item -LiteralPath $tempExe -Destination $exe -Force
} finally {
    if (Test-Path -LiteralPath $tempExe) {
        Remove-Item -LiteralPath $tempExe
    }
}
