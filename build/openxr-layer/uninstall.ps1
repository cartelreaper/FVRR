# FakeVR Uninstaller
param([string]$InstallDir = "$PSScriptRoot")
$ErrorActionPreference = "SilentlyContinue"

$isAdmin = ([Security.Principal.WindowsPrincipal][Security.Principal.WindowsIdentity]::GetCurrent()).IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)
if (-not $isAdmin) { Write-Host "Run as Administrator!" -ForegroundColor Red; Read-Host; exit 1 }

$jsonPath = Join-Path $InstallDir "fakevr_layer.json"
$regKey   = "HKLM:\SOFTWARE\Khronos\OpenXR\1\ApiLayers\Implicit"

Remove-ItemProperty -Path $regKey -Name $jsonPath -ErrorAction SilentlyContinue
Write-Host "FakeVR layer unregistered." -ForegroundColor Green
Read-Host "Press Enter to exit"
