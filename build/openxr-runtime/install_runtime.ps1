# FakeVR Runtime Installer
# Run as Administrator: powershell -ExecutionPolicy Bypass -File install_runtime.ps1

param(
    [string]$InstallDir = "C:\FakeVR"
)

$ErrorActionPreference = "Stop"

Write-Host "=== FakeVR OpenXR Runtime Installer ===" -ForegroundColor Cyan

# Check admin
$currentUser = [Security.Principal.WindowsIdentity]::GetCurrent()
$principal   = [Security.Principal.WindowsPrincipal]$currentUser
if (-not $principal.IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)) {
    Write-Host "ERROR: Please run as Administrator!" -ForegroundColor Red
    Write-Host "Right-click PowerShell -> Run as Administrator, then run this script again."
    exit 1
}

# Create install directory
if (-not (Test-Path $InstallDir)) {
    New-Item -ItemType Directory -Path $InstallDir | Out-Null
    Write-Host "Created directory: $InstallDir" -ForegroundColor Green
}

# Copy files from script directory
$scriptDir = Split-Path -Parent $MyInvocation.MyCommand.Path
$files = @("fakevr_runtime.dll", "fakevr_runtime.json")

foreach ($file in $files) {
    $src = Join-Path $scriptDir $file
    $dst = Join-Path $InstallDir $file
    if (Test-Path $src) {
        Copy-Item -Path $src -Destination $dst -Force
        Write-Host "Copied: $file -> $dst" -ForegroundColor Green
    } else {
        Write-Host "WARNING: $file not found in $scriptDir, skipping copy" -ForegroundColor Yellow
    }
}

# Update JSON to use absolute path for DLL
$jsonPath = Join-Path $InstallDir "fakevr_runtime.json"
$dllPath  = Join-Path $InstallDir "fakevr_runtime.dll"

# Use forward slashes in JSON as required by OpenXR loader
$dllPathJson = $dllPath.Replace("\", "/")

$jsonContent = @"
{
    "file_format_version": "1.0.0",
    "runtime": {
        "name": "FakeVR Runtime",
        "library_path": "$dllPathJson"
    }
}
"@

Set-Content -Path $jsonPath -Value $jsonContent -Encoding UTF8
Write-Host "Written runtime manifest: $jsonPath" -ForegroundColor Green

# Register in Windows registry
$regKey  = "HKLM:\SOFTWARE\Khronos\OpenXR\1"
$regName = "ActiveRuntime"
$regValue = $jsonPath

try {
    if (-not (Test-Path $regKey)) {
        New-Item -Path $regKey -Force | Out-Null
    }
    Set-ItemProperty -Path $regKey -Name $regName -Value $regValue -Type String
    Write-Host ""
    Write-Host "Registry set:" -ForegroundColor Green
    Write-Host "  $regKey" -ForegroundColor White
    Write-Host "  $regName = $regValue" -ForegroundColor White
} catch {
    Write-Host "ERROR setting registry: $_" -ForegroundColor Red
    exit 1
}

# Also set for current user (fallback)
$regKeyUser = "HKCU:\SOFTWARE\Khronos\OpenXR\1"
try {
    if (-not (Test-Path $regKeyUser)) {
        New-Item -Path $regKeyUser -Force | Out-Null
    }
    Set-ItemProperty -Path $regKeyUser -Name $regName -Value $regValue -Type String
    Write-Host "  Also set HKCU key (user fallback)" -ForegroundColor Green
} catch {
    Write-Host "  Note: Could not set HKCU key: $_" -ForegroundColor Yellow
}

Write-Host ""
Write-Host "=== Installation complete! ===" -ForegroundColor Cyan
Write-Host ""
Write-Host "FakeVR Runtime is now the active OpenXR runtime." -ForegroundColor Green
Write-Host "Start fakevr_companion.exe first, then launch Roblox in VR mode." -ForegroundColor White
Write-Host ""
Write-Host "To uninstall, run:" -ForegroundColor Yellow
Write-Host '  Remove-ItemProperty -Path "HKLM:\SOFTWARE\Khronos\OpenXR\1" -Name "ActiveRuntime"' -ForegroundColor White
Write-Host ""

# Verify
$check = Get-ItemProperty -Path $regKey -Name $regName -ErrorAction SilentlyContinue
if ($check -and $check.ActiveRuntime -eq $regValue) {
    Write-Host "Verification: OK - runtime registered correctly" -ForegroundColor Green
} else {
    Write-Host "Verification: WARNING - registry value may not be set correctly" -ForegroundColor Yellow
}
