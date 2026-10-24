"""FastAPI entry point. Run with:

    uvicorn app.main:app --reload --host 0.0.0.0 --port 8000

Or simply:  python -m app.main

When bundled as a PyInstaller one-file exe, the frontend static build
(``frontend_dist``) is embedded and auto-served; a browser tab is opened
to http://127.0.0.1:<port>/ on startup.
"""
from __future__ import annotations

import logging
import os
import sys
from contextlib import asynccontextmanager
from pathlib import Path

from fastapi import FastAPI
from fastapi.middleware.cors import CORSMiddleware
from fastapi.responses import FileResponse
from fastapi.staticfiles import StaticFiles

from app.api.connection import router as connection_router
from app.api.robot import router as robot_router
from app.api.ws import router as ws_router
from app.config import settings
from app.core.connection import ConnectionManager

logging.basicConfig(
    level=logging.INFO,
    format="%(asctime)s [%(levelname)s] %(name)s: %(message)s",
    datefmt="%H:%M:%S",
)
log = logging.getLogger("app")


@asynccontextmanager
async def lifespan(app: FastAPI):
    """Start with NO active connection. The user picks a port via
    GET /api/ports and POSTs /api/connect to bring it up."""
    app.state.manager = ConnectionManager(default_timeout=settings.cmd_timeout)
    log.info("backend ready (not connected). open /docs and POST /api/connect")
    try:
        yield
    finally:
        try:
            await app.state.manager.disconnect()
        except Exception as exc:  # noqa: BLE001
            log.warning("error during shutdown: %s", exc)


def _resolve_frontend_dist() -> Path | None:
    """Locate the bundled ``frontend_dist`` directory.

    Search order:
      1. ``sys._MEIPASS/frontend_dist``       (PyInstaller one-file runtime)
      2. ``<exe-dir>/frontend_dist``          (PyInstaller one-dir or sibling)
      3. ``<repo>/host-pc/frontend/dist``     (dev: run from source)
    """
    candidates: list[Path] = []
    meipass = getattr(sys, "_MEIPASS", None)
    if meipass:
        candidates.append(Path(meipass) / "frontend_dist")
    exe_dir = Path(sys.executable).resolve().parent if getattr(sys, "frozen", False) else None
    if exe_dir:
        candidates.append(exe_dir / "frontend_dist")
    here = Path(__file__).resolve().parent
    candidates.append(here.parent.parent / "frontend" / "dist")  # host-pc/frontend/dist
    for p in candidates:
        if p.is_dir() and (p / "index.html").is_file():
            return p
    return None


app = FastAPI(title="Dummy Robot Backend", version="0.1.0", lifespan=lifespan)
app.add_middleware(
    CORSMiddleware,
    allow_origins=["*"],
    allow_methods=["*"],
    allow_headers=["*"],
)
app.include_router(connection_router)
app.include_router(robot_router)
app.include_router(ws_router)


_FRONTEND_DIST = _resolve_frontend_dist()
if _FRONTEND_DIST is not None:
    log.info("serving frontend from %s", _FRONTEND_DIST)
    # assets / js / css / images
    app.mount(
        "/assets",
        StaticFiles(directory=str(_FRONTEND_DIST / "assets")),
        name="assets",
    )

    @app.get("/")
    async def _index():
        return FileResponse(_FRONTEND_DIST / "index.html")

    # Serve other top-level static files (favicon.ico, vite.svg, robots.txt, ...)
    @app.get("/{fname:path}")
    async def _spa_fallback(fname: str):
        # never shadow API / WS / docs routes (they're registered before this mount)
        candidate = _FRONTEND_DIST / fname
        if candidate.is_file():
            return FileResponse(candidate)
        # Fallback to SPA entry for client-side routes
        return FileResponse(_FRONTEND_DIST / "index.html")
else:
    @app.get("/")
    async def root():
        return {"name": "dummy-robot-backend", "docs": "/docs", "ws": "/ws"}


def _open_browser_soon(url: str, delay: float = 1.0) -> None:
    import threading
    import webbrowser

    def _go() -> None:
        try:
            webbrowser.open(url)
        except Exception as exc:  # noqa: BLE001
            log.warning("could not open browser: %s", exc)

    threading.Timer(delay, _go).start()


def main() -> None:
    import uvicorn

    host = settings.http_host
    port = settings.http_port
    # When frozen (exe), bind to loopback and pop the browser automatically.
    if getattr(sys, "frozen", False) and os.environ.get("ROBOT_NO_BROWSER") != "1":
        if host in ("0.0.0.0", ""):
            host = "127.0.0.1"
        _open_browser_soon(f"http://127.0.0.1:{port}/")

    # Import-string reload=False works both from source and inside PyInstaller.
    uvicorn.run(app, host=host, port=port, log_level="info")


if __name__ == "__main__":
    main()
