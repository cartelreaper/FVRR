# FakeVR Installer
# Run as Administrator!
# Registers the OpenXR API Layer in Windows Registry

param(
    [string]$InstallDir = "$PSScriptRoot"
)

$ErrorActionPreference = "Stop"

Write-Host "FakeVR OpenXR Layer Installer" -ForegroundColor Cyan
Write-Host "=============================" -ForegroundColor Cyan

# Check admin
$isAdmin = ([Security.Principal.WindowsPrincipal][Security.Principal.WindowsIdentity]::GetCurrent()).IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)
if (-not $isAdmin) {
    Write-Host "ERROR: Please run as Administrator!" -ForegroundColor Red
    Write-Host "Right-click install.ps1 -> Run with PowerShell as Administrator"
    Read-Host "Press Enter to exit"
    exit 1
}

# Resolve absolute paths
$dllPath  = Join-Path $InstallDir "fakevr_layer.dll"
$jsonPath = Join-Path $InstallDir "fakevr_layer.json"

if (-not (Test-Path $dllPath)) {
    Write-Host "ERROR: fakevr_layer.dll not found in $InstallDir" -ForegroundColor Red
    Read-Host "Press Enter to exit"
    exit 1
}
if (-not (Test-Path $jsonPath)) {
    Write-Host "ERROR: fakevr_layer.json not found in $InstallDir" -ForegroundColor Red
    Read-Host "Press Enter to exit"
    exit 1
}

# Update JSON to use absolute path to DLL
$jsonContent = Get-Content $jsonPath -Raw | ConvertFrom-Json
$jsonContent.api_layer.library_path = $dllPath
$jsonContent | ConvertTo-Json -Depth 10 | Set-Content $jsonPath -Encoding UTF8
Write-Host "Updated JSON with absolute DLL path: $dllPath" -ForegroundColor Green

# Registry key for OpenXR implicit API layers
$regKey = "HKLM:\SOFTWARE\Khronos\OpenXR\1\ApiLayers\Implicit"

# Create key if not exists
if (-not (Test-Path $regKey)) {
    New-Item -Path $regKey -Force | Out-Null
    Write-Host "Created registry key: $regKey" -ForegroundColor Green
}

# Register layer: value name = path to JSON, value = 0 (enabled)
Set-ItemProperty -Path $regKey -Name $jsonPath -Value 0 -Type DWord
Write-Host "Registered layer:" -ForegroundColor Green
Write-Host "  $jsonPath = 0" -ForegroundColor White

# Verify
$val = Get-ItemProperty -Path $regKey -Name $jsonPath -ErrorAction SilentlyContinue
if ($val) {
    Write-Host "`nInstallation SUCCESSFUL!" -ForegroundColor Green
} else {
    Write-Host "`nWARNING: Could not verify registration!" -ForegroundColor Yellow
}

Write-Host "`nInstall directory: $InstallDir" -ForegroundColor Cyan
Write-Host "Layer log file:    %TEMP%\fakevr_layer.log" -ForegroundColor Cyan
Write-Host "`nTo run:" -ForegroundColor Yellow
Write-Host "  1. Start fakevr_companion.exe FIRST"
Write-Host "  2. Launch Roblox VR"
Write-Host "  3. Press Tab to capture mouse, F1/F2/F3 to switch modes"
Write-Host ""
Read-Host "Press Enter to exit"
