"""Serial transport layer.

Runs a background reader thread that emits each newline-terminated line into
an asyncio.Queue. A pub/sub fan-out lets any number of consumers (Robot core
+ WebSocket clients) listen to the same RX stream concurrently.
"""
from __future__ import annotations

import asyncio
import logging
import threading
import time
from abc import ABC, abstractmethod
from typing import Optional

import serial  # pyserial

log = logging.getLogger(__name__)


class TransportError(RuntimeError):
    pass


class _Subscriber:
    """An async queue consumer of RX lines."""

    def __init__(self, maxsize: int = 256):
        self.queue: asyncio.Queue[str] = asyncio.Queue(maxsize=maxsize)


class Transport(ABC):
    """Base transport: line-oriented full-duplex pipe."""

    def __init__(self) -> None:
        self._loop: Optional[asyncio.AbstractEventLoop] = None
        self._reader_thread: Optional[threading.Thread] = None
        self._stop = threading.Event()
        self._subs: list[_Subscriber] = []
        self._tx_lock = threading.Lock()
        self._connected = False

    # ---- abstract ----
    @abstractmethod
    def _open(self) -> None: ...
    @abstractmethod
    def _close(self) -> None: ...
    @abstractmethod
    def _write_bytes(self, data: bytes) -> None: ...
    @abstractmethod
    def _read_line(self) -> Optional[str]:
        """Return one line (without trailing newline) or None on timeout."""

    # ---- lifecycle ----
    def start(self, loop: asyncio.AbstractEventLoop) -> None:
        if self._reader_thread and self._reader_thread.is_alive():
            return
        self._loop = loop
        self._stop.clear()
        self._open()
        self._connected = True
        self._reader_thread = threading.Thread(target=self._reader_loop, name="rx", daemon=True)
        self._reader_thread.start()
        log.info("transport started")

    def stop(self) -> None:
        self._stop.set()
        try:
            self._close()
        except Exception:  # noqa: BLE001
            pass
        self._connected = False
        if self._reader_thread:
            self._reader_thread.join(timeout=1.0)
        log.info("transport stopped")

    @property
    def connected(self) -> bool:
        return self._connected

    # ---- TX ----
    def write_line(self, line: str) -> None:
        if not self._connected:
            raise TransportError("transport not connected")
        payload = (line.rstrip("\r\n") + "\n").encode("ascii", errors="replace")
        with self._tx_lock:
            self._write_bytes(payload)
        log.debug("TX: %s", line.strip())

    # ---- RX pub/sub ----
    def subscribe(self) -> _Subscriber:
        sub = _Subscriber()
        self._subs.append(sub)
        return sub

    def unsubscribe(self, sub: _Subscriber) -> None:
        try:
            self._subs.remove(sub)
        except ValueError:
            pass

    @staticmethod
    def _safe_put(q: "asyncio.Queue[str]", line: str) -> None:
        """Runs in the asyncio loop — catches QueueFull where it actually fires."""
        try:
            q.put_nowait(line)
        except asyncio.QueueFull:
            log.warning("subscriber queue full, dropping line: %s", line[:80])

    def _publish(self, line: str) -> None:
        if self._loop is None:
            return
        # NOTE: ``call_soon_threadsafe`` only schedules the callback — any
        # exception raised by ``put_nowait`` later in the loop tick would be
        # swallowed by asyncio's default exception handler. Wrap the put in
        # ``_safe_put`` so QueueFull is caught at the real call-site.
        for sub in list(self._subs):
            self._loop.call_soon_threadsafe(self._safe_put, sub.queue, line)

    # ---- background reader ----
    def _reader_loop(self) -> None:
        while not self._stop.is_set():
            try:
                line = self._read_line()
                if line is None:
                    continue
                line = line.rstrip("\r\n")
                if not line:
                    continue
                log.debug("RX: %s", line)
                self._publish(line)
            except Exception as exc:  # noqa: BLE001
                log.error("reader error: %s", exc)
                self._connected = False
                time.sleep(0.5)


class SerialTransport(Transport):
    def __init__(self, port: str, baud: int) -> None:
        super().__init__()
        self._port_name = port
        self._baud = baud
        self._ser: Optional[serial.Serial] = None
        self._buf = b""

    def _open(self) -> None:
        self._ser = serial.Serial(self._port_name, self._baud, timeout=0.2)
        self._buf = b""

    def _close(self) -> None:
        if self._ser and self._ser.is_open:
            self._ser.close()

    def _write_bytes(self, data: bytes) -> None:
        assert self._ser is not None
        self._ser.write(data)

    def _read_line(self) -> Optional[str]:
        assert self._ser is not None
        # 关键修复：优先消费 self._buf 里已缓冲的完整行。
        # 固件对 #ONLINE / #GET_DCE 这种多行响应会一次 burst 发 8-15 行，
        # pyserial 的 read(256) 可能一次把全部字节拿到，buf 里会有多个 '\n'。
        # 原实现每次 _read_line 只 partition 出第一行就 return，后续行要等
        # 下次 read() 有新字节时才会被处理——但 reader_loop 下一次迭代的
        # read() 若暂时为空就 return None，导致 buf 里剩下的 6-14 行永远
        # 不被发布，host-pc 端看起来就只有第一行响应到达。
        if b"\n" in self._buf:
            line, _, self._buf = self._buf.partition(b"\n")
            return self._decode_line(line)
        chunk = self._ser.read(256)
        if not chunk:
            return None
        self._buf += chunk
        if b"\n" not in self._buf:
            return None
        line, _, self._buf = self._buf.partition(b"\n")
        return self._decode_line(line)

    @staticmethod
    def _decode_line(line: bytes) -> str:
        # ESP32 ROM bootloader prints at 74880 baud; when we read at 115200
        # those bytes are garbage. Decode permissively and strip non-printable
        # control chars so we don't spam U+FFFD diamonds into the UI.
        decoded = line.decode("utf-8", errors="ignore")
        decoded = "".join(c for c in decoded if c == "\t" or c.isprintable())
        return decoded


