"""
pytest fixtures for HIL testing.

Environment variables (override config.yaml defaults):
  HIL_ESP32_PORT      - ESP32 USB-UART port   (default: /dev/ttyUSB0)
  HIL_STM32_PORT      - STM32 debug UART port (default: /dev/ttyACM0)
  HIL_ESP32_IP        - ESP32 IP (auto-detected from boot log if empty)
  HIL_SERVER_IP       - Ubuntu server LAN IP (required for OTA tests)
  HIL_MQTT_BROKER     - MQTT broker host      (default: localhost)
  HIL_SKIP_FLASH      - Set to '1' to skip flashing (use already-running device)
  HIL_ESP32_FIRMWARE  - Path to ESP32 merged binary
  HIL_STM32_FIRMWARE  - Path to STM32 app binary
  HIL_STM32_BOOTLOADER - Path to STM32 bootloader binary
"""

import os
import time
import logging
import pytest
import yaml

from fixtures.device import SerialMonitor, flash_esp32, flash_stm32
from fixtures.dashboard_client import DashboardClient

logging.basicConfig(
    level=logging.DEBUG,
    format="%(asctime)s %(levelname)-7s %(name)s: %(message)s",
)
logger = logging.getLogger("conftest")


# ---------------------------------------------------------------------------
# Load config
# ---------------------------------------------------------------------------

def _load_config() -> dict:
    cfg_path = os.path.join(os.path.dirname(__file__), "config.yaml")
    with open(cfg_path) as f:
        return yaml.safe_load(f)


CFG = _load_config()


def _env(key: str, default: str = "") -> str:
    return os.environ.get(key, default)


# ---------------------------------------------------------------------------
# Session-scoped: flash once, boot once
# ---------------------------------------------------------------------------

@pytest.fixture(scope="session")
def esp32_port() -> str:
    return _env("HIL_ESP32_PORT", CFG["devices"]["esp32"]["port"])


@pytest.fixture(scope="session")
def stm32_port() -> str:
    return _env("HIL_STM32_PORT", CFG["devices"]["stm32"]["port"])


@pytest.fixture(scope="session")
def server_ip() -> str:
    ip = _env("HIL_SERVER_IP", CFG["network"].get("server_ip", ""))
    if not ip:
        pytest.fail("HIL_SERVER_IP environment variable must be set")
    return ip


@pytest.fixture(scope="session")
def flashed_devices(esp32_port, stm32_port):
    """
    Flash both devices at the start of the test session (unless HIL_SKIP_FLASH=1).
    """
    if _env("HIL_SKIP_FLASH") == "1":
        logger.info("HIL_SKIP_FLASH=1: skipping flash step")
        return

    esp32_fw = _env("HIL_ESP32_FIRMWARE")
    stm32_fw = _env("HIL_STM32_FIRMWARE")
    stm32_bl = _env("HIL_STM32_BOOTLOADER") or None

    if not esp32_fw:
        pytest.fail("HIL_ESP32_FIRMWARE env var must point to the merged ESP32 binary")
    if not stm32_fw:
        pytest.fail("HIL_STM32_FIRMWARE env var must point to the STM32 app binary")

    logger.info("Flashing STM32...")
    flash_stm32(stm32_fw, stm32_bl,
                CFG["devices"]["stm32"]["openocd_interface"],
                CFG["devices"]["stm32"]["openocd_target"])

    logger.info("Flashing ESP32...")
    flash_esp32(esp32_fw, esp32_port,
                CFG["devices"]["esp32"]["flash_baud"],
                CFG["devices"]["esp32"]["chip"])

    # Give devices time to boot before tests start
    time.sleep(2)


@pytest.fixture(scope="session")
def esp32_monitor(esp32_port, flashed_devices):
    """Serial monitor on ESP32 UART — captures all boot/runtime logs."""
    monitor = SerialMonitor(esp32_port, CFG["devices"]["esp32"]["baud"])
    monitor.start()
    yield monitor
    monitor.stop()


@pytest.fixture(scope="session")
def stm32_monitor(stm32_port, flashed_devices):
    """Serial monitor on STM32 debug UART (ST-Link VCP)."""
    monitor = SerialMonitor(stm32_port, CFG["devices"]["stm32"]["baud"])
    monitor.start()
    yield monitor
    monitor.stop()


@pytest.fixture(scope="session")
def booted_esp32(esp32_monitor):
    """
    Wait for ESP32 to fully boot: WiFi connected + MQTT connected + STM32 protocol ready.
    Returns the detected ESP32 IP address.
    """
    timeout = CFG["timeouts"]["boot_s"]
    logger.info("Waiting for ESP32 boot (timeout=%ds)...", timeout)

    esp32_monitor.wait_for(r"WiFi connected|wifi_connect.*success|IP address|WiFi: Connected", timeout=timeout)
    ip = esp32_monitor.extract_ip()

    # Allow override via environment variable
    if not ip:
        ip = _env("HIL_ESP32_IP")
    if not ip:
        pytest.fail("Could not determine ESP32 IP from boot log. Set HIL_ESP32_IP.")

    logger.info("ESP32 booted with IP: %s", ip)

    # Wait for STM32 protocol to be ready (logged by ESP32)
    esp32_monitor.wait_for(r"STM32 protocol feature started|Protocol task started|STM32_PROTO.*Heartbeat", timeout=30)

    return ip


@pytest.fixture(scope="session")
def esp32_ip(booted_esp32) -> str:
    return booted_esp32


# ---------------------------------------------------------------------------
# Function-scoped: fresh dashboard client per test
# ---------------------------------------------------------------------------

@pytest.fixture
def dashboard(esp32_ip):
    """Fresh WebSocket connection to the ESP32 dashboard for each test."""
    port = CFG["network"]["dashboard_port"]
    path = CFG["network"]["dashboard_path"]
    timeout = CFG["timeouts"]["websocket_s"]
    client = DashboardClient(esp32_ip, port, path, connect_timeout=timeout)
    client.connect()
    yield client
    client.disconnect()
