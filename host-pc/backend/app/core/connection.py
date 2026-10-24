"""ConnectionManager: holds the optional active Robot+Transport pair.

The backend starts with NO active connection. The frontend (or /docs) calls
``POST /api/connect`` with a port to bring it up, and ``POST /api/disconnect``
to tear it down. Re-connecting to a different port is supported.
"""
from __future__ import annotations

import asyncio
import logging
from dataclasses import dataclass
from typing import Optional

from serial.tools import list_ports

from app.core.robot import Robot
from app.core.transport import SerialTransport, TransportError

log = logging.getLogger(__name__)


@dataclass(frozen=True)
class PortInfo:
    device: str          # "COM5" / "/dev/ttyUSB0"
    description: str     # OS-supplied description
    hwid: str            # USB VID:PID etc.
    is_likely_robot: bool = False


def list_serial_ports() -> list[PortInfo]:
    """Enumerate available serial ports on this machine."""
    out: list[PortInfo] = []
    for p in list_ports.comports():
        desc = (p.description or "").lower()
        hwid = (p.hwid or "").lower()
        # Heuristics: STM32 USB-CDC, FTDI/CH340/CP210x adapters
        likely = any(s in desc or s in hwid for s in (
            "stm32", "stmicro", "usbmodem", "usb serial",
            "cdc", "ftdi", "ch340", "ch341", "cp210", "silicon labs",
        ))
        out.append(PortInfo(
            device=p.device,
            description=p.description or "",
            hwid=p.hwid or "",
            is_likely_robot=likely,
        ))
    # Sort: likely-robot first, then by name
    out.sort(key=lambda x: (not x.is_likely_robot, x.device))
    return out


class ConnectionManager:
    """Owns the optional (Robot, Transport) pair. Thread/async-safe."""

    def __init__(self, default_timeout: float = 5.0) -> None:
        self._lock = asyncio.Lock()
        self._robot: Optional[Robot] = None
        self._port: Optional[str] = None
        self._baud: Optional[int] = None
        self._default_timeout = default_timeout

    @property
    def robot(self) -> Optional[Robot]:
        return self._robot

    @property
    def is_connected(self) -> bool:
        return self._robot is not None and self._robot.state.connected

    @property
    def port(self) -> Optional[str]:
        return self._port

    @property
    def baud(self) -> Optional[int]:
        return self._baud

    async def connect(self, port: str, baud: int) -> None:
        async with self._lock:
            if self._robot is not None:
                raise RuntimeError("already connected; disconnect first")
            transport = SerialTransport(port, baud)
            robot = Robot(transport, default_timeout=self._default_timeout)
            try:
                await robot.start()
            except Exception as e:
                # Make sure we don't leak a half-open transport
                try:
                    await robot.stop()
                except Exception:  # noqa: BLE001
                    pass
                raise TransportError(f"failed to open {port}@{baud}: {e}") from e
            self._robot = robot
            self._port = port
            self._baud = baud
            log.info("connected to %s @ %d", port, baud)

    async def disconnect(self) -> None:
        async with self._lock:
            if self._robot is None:
                return
            try:
                await self._robot.stop()
            finally:
                self._robot = None
                self._port = None
                self._baud = None
                log.info("disconnected")
