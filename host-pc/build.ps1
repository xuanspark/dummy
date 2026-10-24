<#
.SYNOPSIS
    Build the host-pc app (backend + frontend) into a single DummyRobot.exe.

.DESCRIPTION
    Steps:
      1. `npm install` (if needed) + `npm run build` → frontend/dist
      2. Ensure backend venv has pyinstaller
      3. Run PyInstaller on backend/DummyRobot.spec
      4. Output: host-pc/backend/dist/DummyRobot.exe

.EXAMPLE
    powershell -ExecutionPolicy Bypass -File .\build.ps1
    powershell -ExecutionPolicy Bypass -File .\build.ps1 -SkipFrontend
#>
param(
    [switch]$SkipFrontend,
    [switch]$SkipInstall
)

$ErrorActionPreference = "Stop"
$Root = $PSScriptRoot
$FrontDir = Join-Path $Root "frontend"
$BackDir  = Join-Path $Root "backend"
$Venv     = Join-Path $BackDir ".venv\Scripts"

if (-not (Test-Path $Venv)) {
    throw "Backend venv missing: $Venv. Create it first:  py -m venv $BackDir\.venv"
}

$Py  = Join-Path $Venv "python.exe"
$Pip = Join-Path $Venv "pip.exe"

# ---- 1. frontend build ----
if (-not $SkipFrontend) {
    Write-Host "==> Building frontend..." -ForegroundColor Cyan
    Push-Location $FrontDir
    try {
        if (-not $SkipInstall -and -not (Test-Path "node_modules")) {
            npm install
        }
        npm run build
    } finally {
        Pop-Location
    }
    if (-not (Test-Path (Join-Path $FrontDir "dist\index.html"))) {
        throw "frontend build produced no dist/index.html"
    }
}

# ---- 2. backend deps + pyinstaller ----
if (-not $SkipInstall) {
    Write-Host "==> Installing backend + pyinstaller into venv..." -ForegroundColor Cyan
    & $Pip install --upgrade pip | Out-Null
    & $Pip install -e $BackDir
    & $Pip install pyinstaller
}

# ---- 3. pyinstaller ----
Write-Host "==> Running PyInstaller..." -ForegroundColor Cyan
Push-Location $BackDir
try {
    & $Py -m PyInstaller --clean --noconfirm DummyRobot.spec
} finally {
    Pop-Location
}

$exe = Join-Path $BackDir "dist\DummyRobot.exe"
if (-not (Test-Path $exe)) { throw "Build failed: $exe not found" }

Write-Host ""
Write-Host "==> SUCCESS" -ForegroundColor Green
Write-Host "    $exe"
Write-Host "    size: $([Math]::Round((Get-Item $exe).Length / 1MB, 1)) MB"
Write-Host ""
Write-Host "Double-click to launch. It opens http://127.0.0.1:8000/ automatically."
