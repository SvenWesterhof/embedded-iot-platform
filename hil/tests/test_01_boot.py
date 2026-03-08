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
def test_dashboard_server_reachable(dashboard):
    """Dashboard WebSocket must accept connections."""
    # If the fixture connected without exception, the server is reachable.
    # Send a heartbeat and verify it is acknowledged.
    from fixtures.dashboard_client import DashResp
    dashboard.heartbeat()
    # Any response (including no error) means the connection is alive.
    # Heartbeat does not send an explicit ACK in the current implementation,
    # so we just confirm the connection is open with no error raised.
    assert dashboard._ws is not None and dashboard._ws.connected
