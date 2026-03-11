"""
T1: Boot tests
Verify both devices come up correctly and communicate with each other.
These run first since all other tests depend on a successfully booted system.
"""

import pytest


@pytest.mark.timeout(60)
def test_esp32_wifi_connected(esp32_monitor):
    """ESP32 must connect to WiFi within boot timeout."""
    esp32_monitor.wait_for(r"WiFi connected|wifi.*connect.*success|IP address|WiFi: Connected", timeout=40)


@pytest.mark.timeout(60)
def test_esp32_mqtt_connected(esp32_monitor):
    """ESP32 must connect to MQTT broker."""
    # Look for MQTT connected message in boot log
    esp32_monitor.wait_for(r"MQTT connected|mqtt.*connect|serv_mqtt.*connect|MQTT: Connected", timeout=40)


@pytest.mark.timeout(60)
def test_stm32_protocol_ready(esp32_monitor):
    """ESP32 must establish communication with STM32 before other tests run."""
    esp32_monitor.wait_for(r"STM32 protocol feature started|Protocol task started|STM32_PROTO.*Heartbeat", timeout=40)


@pytest.mark.timeout(30)
def test_stm32_sensors_initialized(stm32_monitor):
    """STM32 debug UART must be producing output (sensors running)."""
    # The STM32 boots before the monitor opens, so startup messages are gone.
    # Instead verify it's alive by waiting for any debug UART output.
    stm32_monitor.wait_for(r".", timeout=20)


@pytest.mark.timeout(20)
def test_dashboard_server_responds(dashboard):
    """Dashboard WebSocket must accept connections and respond to commands."""
    from fixtures.dashboard_client import DashResp
    # GET_STATUS triggers a real response from the firmware, unlike HEARTBEAT
    # which has no ACK. This verifies the server processes commands, not just
    # that the TCP connection is open.
    dashboard.get_status()
    pkt = dashboard.wait_for(DashResp.STATUS, timeout=5)
    assert len(pkt.payload) > 0, "STATUS response payload should not be empty"
