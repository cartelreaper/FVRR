# FakeVR2 OpenVR Driver Installer
param([string]$InstallDir = "$PSScriptRoot")

$isAdmin = ([Security.Principal.WindowsPrincipal][Security.Principal.WindowsIdentity]::GetCurrent()).IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)
if (-not $isAdmin) { Write-Host "Run as Administrator!" -ForegroundColor Red; Read-Host; exit 1 }

$steamVR = "C:\Program Files (x86)\Steam\steamapps\common\SteamVR"
$driverSrc = Join-Path $InstallDir "fakevr2"
$driverDst = Join-Path $steamVR "drivers\fakevr2"

Write-Host "Installing FakeVR2 OpenVR Driver..." -ForegroundColor Cyan

# Stop SteamVR if running
Get-Process -Name "vrserver","vrmonitor" -ErrorAction SilentlyContinue | Stop-Process -Force
Start-Sleep -Seconds 2

# Copy driver folder
if (Test-Path $driverDst) { Remove-Item $driverDst -Recurse -Force }
Copy-Item $driverSrc $driverDst -Recurse
Write-Host "Copied driver to: $driverDst" -ForegroundColor Green

# Enable in steamvr.vrsettings
$cfg = "C:\Program Files (x86)\Steam\config\steamvr.vrsettings"
$json = Get-Content $cfg -Raw | ConvertFrom-Json
$json | Add-Member -NotePropertyName "driver_fakevr2" -NotePropertyValue ([PSCustomObject]@{ enable = $true }) -Force
$json | ConvertTo-Json -Depth 10 | Set-Content $cfg -Encoding UTF8
Write-Host "Enabled in steamvr.vrsettings" -ForegroundColor Green

Write-Host "`nDone! Start fakevr_companion.exe then SteamVR." -ForegroundColor Green
Read-Host "Press Enter to exit"
