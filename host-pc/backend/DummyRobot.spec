# -*- mode: python ; coding: utf-8 -*-
"""PyInstaller spec for DummyRobot host-pc app.

Bundles:
  - FastAPI backend (`app/` package)
  - Vue frontend build (`../frontend/dist`) → mounted at runtime as `frontend_dist`

Build (from backend/ with an activated venv that has pyinstaller installed):

    pyinstaller --clean --noconfirm DummyRobot.spec

Output: ``dist/DummyRobot.exe`` (one-file).
"""
from pathlib import Path
from PyInstaller.utils.hooks import collect_submodules, collect_data_files

here = Path.cwd()
frontend_dist = (here / ".." / "frontend" / "dist").resolve()
assert frontend_dist.is_dir(), f"Build frontend first: cd frontend && npm run build  (missing: {frontend_dist})"

# uvicorn / fastapi / pydantic bring a number of lazy imports PyInstaller
# cannot see through; collect them explicitly.
hiddenimports = []
for pkg in ("uvicorn", "uvicorn.logging", "uvicorn.loops.auto", "uvicorn.loops.asyncio",
            "uvicorn.protocols", "uvicorn.protocols.http.auto", "uvicorn.protocols.http.h11_impl",
            "uvicorn.protocols.websockets.auto", "uvicorn.protocols.websockets.websockets_impl",
            "uvicorn.protocols.websockets.wsproto_impl", "uvicorn.lifespan.on",
            "fastapi", "pydantic_settings", "pydantic", "serial.tools.list_ports_windows"):
    hiddenimports += collect_submodules(pkg)

datas = [(str(frontend_dist), "frontend_dist")]

a = Analysis(
    ["app/main.py"],
    pathex=[str(here)],
    binaries=[],
    datas=datas,
    hiddenimports=hiddenimports,
    hookspath=[],
    hooksconfig={},
    runtime_hooks=[],
    excludes=["tkinter", "pytest", "unittest", "numpy", "matplotlib"],
    noarchive=False,
)
pyz = PYZ(a.pure)

exe = EXE(
    pyz,
    a.scripts,
    a.binaries,
    a.datas,
    [],
    name="DummyRobot",
    debug=False,
    bootloader_ignore_signals=False,
    strip=False,
    upx=False,
    upx_exclude=[],
    runtime_tmpdir=None,
    console=True,
    disable_windowed_traceback=False,
    argv_emulation=False,
    target_arch=None,
    codesign_identity=None,
    entitlements_file=None,
)
