"""One-shot COM6 sniffer.

Opens COM6 @ 115200, asserts DTR/RTS to reset the ESP32, captures up to
~6 seconds of bytes, then prints:
  1) raw hex dump (per-line)
  2) UTF-8 decoded with errors=ignore + non-printable stripped (= what
     the patched backend would deliver)
"""
import serial, time, sys

PORT = "COM6"
BAUD = 115200
DURATION = 6.0

def is_keep(c: str) -> bool:
    return c == "\t" or c.isprintable()

def main() -> int:
    with serial.Serial(PORT, BAUD, timeout=0.05) as s:
        # Toggle DTR/RTS to reset ESP32 (ESP-IDF default: DTR=EN, RTS=IO0)
        s.setDTR(False); s.setRTS(True)
        time.sleep(0.1)
        s.setRTS(False)
        s.reset_input_buffer()

        buf = bytearray()
        t0 = time.time()
        while time.time() - t0 < DURATION:
            chunk = s.read(512)
            if chunk:
                buf.extend(chunk)
        print(f"== captured {len(buf)} bytes in {DURATION:.1f}s ==\n")

    lines = buf.split(b"\n")
    print("=== A. RAW HEX (per line, first 80 bytes) ===")
    for i, raw in enumerate(lines):
        if not raw:
            continue
        hexs = raw[:80].hex(" ")
        print(f"[{i:02d}] {hexs}")

    print("\n=== B. CLEANED OUTPUT (utf-8 ignore + printable filter) ===")
    for i, raw in enumerate(lines):
        if not raw:
            continue
        decoded = raw.decode("utf-8", errors="ignore")
        cleaned = "".join(c for c in decoded if is_keep(c))
        if cleaned.strip():
            print(f"[{i:02d}] {cleaned}")
    return 0

if __name__ == "__main__":
    sys.exit(main())
