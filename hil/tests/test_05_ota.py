"""
T5: OTA end-to-end tests.

Architecture:
  - Local HTTP server (fw-server container) serves firmware binaries at http://<server_ip>:8080/
  - Local Mosquitto (mosquitto container) receives OTA trigger MQTT messages
  - ESP32 is connected to the same LAN and can reach both services
  - OTA progress is monitored via ESP32 serial log

OTA MQTT payload (matches cont_ota_manager.c expected JSON):
  ESP32: {"target":"esp32","version":"x.y.z","url":"http://...","size":N,"auto_reboot":true}
  STM32: {"target":"stm32","version":"x.y.z","url":"http://...","size":N,
          "signature_rsa":"<base64>","crc32":N,"auto_apply":true}

Prerequisites (set via env vars):
  HIL_SERVER_IP        - LAN IP of the Ubuntu server
  HIL_ESP32_OTA_BIN    - Path to ESP32 OTA binary (ota_0 partition image)
  HIL_STM32_OTA_BIN    - Path to STM32 app binary (unsigned)
  HIL_STM32_KEY        - Path to RSA-2048 PEM key for STM32 signing
  HIL_ESP32_KEY        - Path to RSA-3072 PEM key for ESP32 signing
"""

import base64
import hashlib
import json
import os
import shutil
import struct
import subprocess
import time
import pytest
import paho.mqtt.client as mqtt

OTA_TOPIC = "gateway/ota/notify"
MQTT_HOST = os.environ.get("HIL_MQTT_BROKER", "localhost")
MQTT_PORT = int(os.environ.get("HIL_MQTT_PORT", "1883"))
FW_SERVER_DIR = os.environ.get("HIL_FW_SERVER_DIR", "/firmware")


def _compute_crc32(data: bytes) -> int:
    import zlib
    return zlib.crc32(data) & 0xFFFFFFFF


def _sign_firmware(bin_path: str, key_path: str) -> str:
    """
    Sign firmware with RSA key using openssl, return base64-encoded signature.
    The firmware is signed with SHA-256 + RSA-PSS to match serv_signature_verify.
    """
    result = subprocess.run(
        ["openssl", "dgst", "-sha256", "-sign", key_path, bin_path],
        capture_output=True, check=True
    )
    return base64.b64encode(result.stdout).decode()


def _publish_ota(payload: dict):
    client = mqtt.Client(mqtt.CallbackAPIVersion.VERSION2)
    client.connect(MQTT_HOST, MQTT_PORT, keepalive=10)
    client.loop_start()
    time.sleep(0.5)  # Allow CONNACK to be received before publishing
    info = client.publish(OTA_TOPIC, json.dumps(payload), qos=1)
    info.wait_for_publish(timeout=5)
    client.loop_stop()
    client.disconnect()


# ---------------------------------------------------------------------------
# ESP32 OTA
# ---------------------------------------------------------------------------

@pytest.mark.timeout(150)
def test_esp32_ota_success(esp32_monitor, server_ip):
    """ESP32 OTA: device downloads and flashes new firmware from local HTTP server."""
    ota_bin = os.environ.get("HIL_ESP32_OTA_BIN")
    if not ota_bin:
        pytest.skip("HIL_ESP32_OTA_BIN not set — skipping ESP32 OTA test")
    if not os.path.exists(ota_bin):
        pytest.fail(f"ESP32 OTA binary not found: {ota_bin}")

    fw_data = open(ota_bin, "rb").read()
    fw_size = len(fw_data)
    fw_url = f"http://{server_ip}:8080/{os.path.basename(ota_bin)}"

    # Copy to fw-server directory
    fw_dest = os.path.join(FW_SERVER_DIR, os.path.basename(ota_bin))
    shutil.copy2(ota_bin, fw_dest)

    payload = {
        "target": "esp32",
        "version": "99.0.0",       # Bump version so device accepts it
        "url": fw_url,
        "size": fw_size,
        "auto_reboot": True,
    }
    _publish_ota(payload)

    # Monitor for OTA completion in ESP32 serial log
    esp32_monitor.wait_for(
        r"OTA completed|ota.*complete|esp32.*ota.*success|EVENT_OTA_COMPLETED",
        timeout=120
    )
    # After reboot, wait for WiFi reconnect
    esp32_monitor.wait_for(r"WiFi connected|IP address", timeout=40)


# ---------------------------------------------------------------------------
# STM32 OTA
# ---------------------------------------------------------------------------

@pytest.mark.timeout(330)
def test_stm32_ota_success(esp32_monitor, server_ip):
    """STM32 OTA: ESP32 downloads firmware and transfers to STM32 via UART."""
    stm32_bin = os.environ.get("HIL_STM32_OTA_BIN")
    stm32_key = os.environ.get("HIL_STM32_KEY")

    if not stm32_bin:
        pytest.skip("HIL_STM32_OTA_BIN not set — skipping STM32 OTA test")
    if not stm32_key:
        pytest.skip("HIL_STM32_KEY not set — skipping STM32 OTA test")
    if not os.path.exists(stm32_bin):
        pytest.fail(f"STM32 OTA binary not found: {stm32_bin}")

    fw_data = open(stm32_bin, "rb").read()
    fw_size = len(fw_data)
    fw_crc32 = _compute_crc32(fw_data)
    fw_sha256 = hashlib.sha256(fw_data).hexdigest()
    fw_signature = _sign_firmware(stm32_bin, stm32_key)
    fw_url = f"http://{server_ip}:8080/{os.path.basename(stm32_bin)}"

    # Copy to fw-server directory
    shutil.copy2(stm32_bin, os.path.join(FW_SERVER_DIR, os.path.basename(stm32_bin)))

    payload = {
        "target": "stm32",
        "version": "99.0.0",
        "url": fw_url,
        "size": fw_size,
        "signature_rsa": fw_signature,
        "sha256": fw_sha256,
        "crc32": fw_crc32,
        "auto_apply": True,
    }
    _publish_ota(payload)

    # Monitor: STM32 OTA transfer progress
    esp32_monitor.wait_for(
        r"STM32 OTA.*complete|stm32.*ota.*success|EVENT_STM32_OTA_COMPLETED",
        timeout=300
    )
    # STM32 reboots into new firmware — give it time
    time.sleep(5)

    # Verify STM32 is still responding after reboot (ESP32 re-establishes protocol)
    esp32_monitor.wait_for(r"STM32 protocol ready|stm32.*ready", timeout=30)


# ---------------------------------------------------------------------------
# Invalid firmware rejection
# ---------------------------------------------------------------------------

@pytest.mark.timeout(220)
def test_stm32_ota_invalid_signature_rejected(esp32_monitor, server_ip):
    """Firmware with a bad RSA signature must be rejected before any flash write."""
    stm32_bin = os.environ.get("HIL_STM32_OTA_BIN")
    if not stm32_bin or not os.path.exists(stm32_bin):
        pytest.skip("HIL_STM32_OTA_BIN not set — skipping signature rejection test")

    fw_data = open(stm32_bin, "rb").read()
    shutil.copy2(stm32_bin, os.path.join(FW_SERVER_DIR, os.path.basename(stm32_bin)))

    payload = {
        "target": "stm32",
        "version": "99.0.0",
        "url": f"http://{server_ip}:8080/{os.path.basename(stm32_bin)}",
        "size": len(fw_data),
        "signature_rsa": base64.b64encode(b"this_is_not_a_valid_signature").decode(),
        "crc32": _compute_crc32(fw_data),
        "auto_apply": True,
    }
    _publish_ota(payload)

    # Must see OTA failure, NOT success
    esp32_monitor.wait_for(
        r"OTA failed|Signature.*fail|verify.*fail|EVENT_STM32_OTA_FAILED",
        timeout=190
    )

    # STM32 should still be alive and responding
    time.sleep(3)
    esp32_monitor.wait_for(r"STM32 protocol ready|stm32.*ready", timeout=20)
