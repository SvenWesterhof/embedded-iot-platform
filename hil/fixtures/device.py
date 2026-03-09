"""
Serial monitor for ESP32 boot logs and utilities for flashing.
Used to determine when the ESP32 has fully booted and which IP it obtained.
"""

import re
import subprocess
import threading
import time
import logging
from pathlib import Path
from typing import Optional

import serial

logger = logging.getLogger(__name__)


class SerialMonitor:
    """
    Reads ESP32 (or STM32 debug) UART output in a background thread.
    Provides line-based log access and pattern waiting.
    """

    def __init__(self, port: str, baud: int = 115200):
        self._port = port
        self._baud = baud
        self._ser: Optional[serial.Serial] = None
        self._lines: list[str] = []
        self._lock = threading.Lock()
        self._thread: Optional[threading.Thread] = None
        self._running = False

    def start(self):
        self._ser = serial.Serial(self._port, self._baud, timeout=0.2)
        self._running = True
        self._thread = threading.Thread(target=self._read_loop, daemon=True)
        self._thread.start()

    def stop(self):
        self._running = False
        if self._thread:
            self._thread.join(timeout=2.0)
        if self._ser and self._ser.is_open:
            self._ser.close()

    def __enter__(self):
        self.start()
        return self

    def __exit__(self, *_):
        self.stop()

    def lines(self) -> list[str]:
        with self._lock:
            return list(self._lines)

    def current_line_count(self) -> int:
        """Return the current number of lines captured, useful as a baseline for wait_for."""
        with self._lock:
            return len(self._lines)

    def wait_for(self, pattern: str, timeout: float = 30.0, since: int = 0) -> str:
        """
        Block until a line matching the regex pattern is seen in the serial output.
        ``since`` sets the starting line index (use ``current_line_count()`` before an
        event to restrict matching to lines produced after that point).
        Returns the matching line. Raises TimeoutError on timeout.
        """
        rx = re.compile(pattern)
        deadline = time.monotonic() + timeout
        seen_up_to = since
        while time.monotonic() < deadline:
            with self._lock:
                new_lines = self._lines[seen_up_to:]
                seen_up_to = len(self._lines)
            for line in new_lines:
                if rx.search(line):
                    logger.debug("Pattern '%s' matched: %s", pattern, line.strip())
                    return line
            time.sleep(0.1)
        raise TimeoutError(f"Pattern '{pattern}' not seen within {timeout}s")

    def extract_ip(self) -> Optional[str]:
        """
        Parse the ESP32 IP address from boot log lines like:
          'ip: 192.168.1.42' or 'WiFi connected, IP: 192.168.1.42'
        """
        ip_re = re.compile(r"(?:ip|IP)[:\s]+(\d{1,3}(?:\.\d{1,3}){3})")
        for line in self.lines():
            m = ip_re.search(line)
            if m:
                return m.group(1)
        return None

    def _read_loop(self):
        buf = b""
        while self._running:
            try:
                chunk = self._ser.read(256)
            except Exception as e:
                if self._running:
                    logger.error("Serial read error: %s", e)
                break
            if not chunk:
                continue
            buf += chunk
            while b"\n" in buf:
                line, buf = buf.split(b"\n", 1)
                decoded = line.decode("utf-8", errors="replace").rstrip("\r")
                logger.debug("[UART] %s", decoded)
                with self._lock:
                    self._lines.append(decoded)


def flash_esp32(firmware: str, port: str = "/dev/ttyUSB0",
                flash_baud: int = 921600, chip: str = "esp32s3") -> None:
    """Flash ESP32 firmware via esptool."""
    cmd = [
        "esptool.py",
        "--chip", chip,
        "--port", port,
        "--baud", str(flash_baud),
        "--before", "default_reset",
        "--after", "hard_reset",
        "write_flash",
        "--flash_mode", "dio",
        "--flash_freq", "80m",
        "--flash_size", "8MB",
        "0x0", firmware,
    ]
    logger.info("Flashing ESP32: %s", " ".join(cmd))
    result = subprocess.run(cmd, capture_output=True, text=True, timeout=120)
    if result.returncode != 0:
        raise RuntimeError(f"ESP32 flash failed:\n{result.stderr}")
    logger.info("ESP32 flash complete")


def flash_stm32(app_bin: str, bootloader_bin: Optional[str] = None,
                openocd_interface: str = "interface/stlink.cfg",
                openocd_target: str = "target/stm32f7x.cfg") -> None:
    """Flash STM32 application (and optionally bootloader) via OpenOCD."""
    cmds = ["init", "reset halt"]
    if bootloader_bin:
        cmds.append(f'flash write_image erase "{bootloader_bin}" 0x08000000')
    cmds.append(f'flash write_image erase "{app_bin}" 0x08008000')
    cmds += ["reset run", "shutdown"]

    cmd = [
        "openocd",
        "-f", openocd_interface,
        "-f", openocd_target,
    ]
    for c in cmds:
        cmd += ["-c", c]

    logger.info("Flashing STM32 via OpenOCD")
    result = subprocess.run(cmd, capture_output=True, text=True, timeout=60)
    if result.returncode != 0:
        raise RuntimeError(f"STM32 flash failed:\n{result.stderr}")
    logger.info("STM32 flash complete")
