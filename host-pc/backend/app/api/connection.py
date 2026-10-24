"""Connection management endpoints: list ports, connect, disconnect, status."""
from __future__ import annotations

from fastapi import APIRouter, Depends, HTTPException
from pydantic import BaseModel, Field

from app.api.deps import get_manager
from app.config import settings
from app.core.connection import ConnectionManager, list_serial_ports
from app.core.transport import TransportError

router = APIRouter(prefix="/api", tags=["connection"])


class ConnectBody(BaseModel):
    port: str = Field(..., description="Serial device, e.g. COM5 or /dev/ttyUSB0")
    baud: int = 115200


@router.get("/ports")
async def list_ports():
    """Auto-detect available serial ports on this machine."""
    return {"ports": [p.__dict__ for p in list_serial_ports()]}


@router.get("/connection")
async def get_connection(mgr: ConnectionManager = Depends(get_manager)):
    return {
        "connected": mgr.is_connected,
        "port": mgr.port,
        "baud": mgr.baud,
    }


@router.get("/connection/defaults")
async def connection_defaults():
    """Defaults for the connect dialog (from .env)."""
    return {"port": settings.default_port, "baud": settings.default_baud}


@router.post("/connect")
async def connect(body: ConnectBody, mgr: ConnectionManager = Depends(get_manager)):
    if mgr.is_connected:
        raise HTTPException(status_code=409, detail=f"already connected to {mgr.port}; disconnect first")
    try:
        await mgr.connect(body.port, body.baud)
    except TransportError as e:
        raise HTTPException(status_code=400, detail=str(e)) from e
    except RuntimeError as e:
        raise HTTPException(status_code=409, detail=str(e)) from e
    return {"connected": True, "port": mgr.port, "baud": mgr.baud}


@router.post("/disconnect")
async def disconnect(mgr: ConnectionManager = Depends(get_manager)):
    await mgr.disconnect()
    return {"connected": False}
