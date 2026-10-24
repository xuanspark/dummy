"""Classify RX lines as 'boot/system noise' vs 'user-facing'.

ESP32 prints ~100 lines of bootloader / driver init noise on every reset.
This module flags those so the UI can hide them by default while still
preserving them for verbose-mode debugging.

Keep the patterns conservative: when in doubt, NOT noise. Anything tagged
noise is hidden by default but can be revealed via the Console "verbose"
toggle, so missing a line is more painful than including a useful one.
"""
from __future__ import annotations

import re

# Each pattern matches the start of a line that we consider noise.
_NOISE_PATTERNS = [
    # --- ROM bootloader (74880-baud-era, but the readable parts at 115200) ---
    r'ets ',
    r'rst:',
    r'configsip:',
    r'clk_drv:',
    r'mode:DIO',
    r'load:0x',
    r'entry 0x',

    # --- ESP-IDF 2nd-stage bootloader & system init ---
    r'I \(\d+\) boot[.: ]',
    r'I \(\d+\) esp_image:',
    r'I \(\d+\) cpu_start:',
    r'I \(\d+\) app_init:',
    r'I \(\d+\) efuse_init:',
    r'I \(\d+\) heap_init:',
    r'I \(\d+\) spi_flash:',
    r'I \(\d+\) main_task:',
    r'I \(\d+\) phy_init:',
    r'I \(\d+\) wifi_init:',
    r'W \(\d+\) spi_flash:',

    # --- WiFi driver state-machine spam (keep "Trying", "connected with",
    #     "security:", "IP ADDRESS", "Connected to SSID" — those don't match
    #     these patterns) ---
    r'I \(\d+\) wifi:wifi ',
    r'I \(\d+\) wifi:Init ',
    r'I \(\d+\) wifi:config ',
    r'I \(\d+\) wifi:mode : ',
    r'I \(\d+\) wifi:state: ',
    r'I \(\d+\) wifi:pm start',
    r'I \(\d+\) wifi:dp: ',
    r'I \(\d+\) wifi:new:',
    r'I \(\d+\) wifi:enable tsf',
    r'I \(\d+\) wifi:AP\'s beacon',
]

_NOISE_RE = re.compile(r'^(?:' + '|'.join(_NOISE_PATTERNS) + r')')


def is_boot_noise(line: str) -> bool:
    """True if the line is ESP32 boot/system noise (hidden by default)."""
    if not line:
        return False
    return _NOISE_RE.match(line) is not None
