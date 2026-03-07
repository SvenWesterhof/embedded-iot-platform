"""
T4: Dashboard WebSocket API tests.
Verify client connection management, message handling, and broadcast behavior.
"""

import time
import threading
import pytest

from fixtures.dashboard_client import DashboardClient, DashMsg, DashResp


@pytest.mark.timeout(15)
def test_heartbeat_keeps_connection_alive(dashboard):
    """Send heartbeats and verify the connection remains open."""
    for _ in range(3):
        dashboard.heartbeat()
        time.sleep(0.5)
    assert dashboard._ws.connected


@pytest.mark.timeout(15)
def test_get_status_response(dashboard):
    """GET_STATUS must return a DASH_RESP_STATUS packet."""
    dashboard.get_status()
    pkt = dashboard.wait_for(DashResp.STATUS, timeout=5)
    assert len(pkt.payload) > 0, "Status payload should not be empty"


@pytest.mark.timeout(20)
def test_request_history(dashboard):
    """REQUEST_HISTORY must return a DASH_RESP_HISTORY packet."""
    dashboard.request_history()
    pkt = dashboard.wait_for(DashResp.HISTORY, timeout=8)
    # History may be empty (RESP_NO_DATA wrapped) but packet must arrive
    assert pkt is not None


@pytest.mark.timeout(20)
def test_start_stop_measurement_via_dashboard(dashboard):
    """Dashboard can start and stop live measurements."""
    dashboard.start_measurement(interval_ms=1000)
    pkt = dashboard.wait_for(DashResp.MEASUREMENT, timeout=5)
    assert pkt is not None

    dashboard.stop_measurement()
    time.sleep(2)
    dashboard.drain()
    time.sleep(2)

    remaining = [p for p in dashboard.received() if p.resp_type == DashResp.MEASUREMENT]
    assert len(remaining) == 0, "Measurements should have stopped"


@pytest.mark.timeout(30)
def test_multiple_clients_receive_broadcast(esp32_ip):
    """
    Up to 4 clients can connect simultaneously and all receive measurement broadcasts.
    """
    from conftest import CFG
    port = CFG["network"]["dashboard_port"]
    path = CFG["network"]["dashboard_path"]

    clients = []
    try:
        for i in range(3):  # Test with 3 clients (leave 1 slot spare)
            c = DashboardClient(esp32_ip, port, path, connect_timeout=10)
            c.connect()
            clients.append(c)

        # Start measurement from first client
        clients[0].start_measurement(interval_ms=1000)

        # All clients should receive the broadcast
        for i, c in enumerate(clients):
            try:
                pkt = c.wait_for(DashResp.MEASUREMENT, timeout=5)
                assert pkt is not None, f"Client {i} did not receive measurement"
            except TimeoutError:
                pytest.fail(f"Client {i} did not receive measurement broadcast")

        clients[0].stop_measurement()
    finally:
        for c in clients:
            c.disconnect()


@pytest.mark.timeout(20)
def test_max_clients_enforced(esp32_ip):
    """
    The 5th client connection should be rejected (max clients = 4).
    """
    from conftest import CFG
    port = CFG["network"]["dashboard_port"]
    path = CFG["network"]["dashboard_path"]

    clients = []
    try:
        # Connect 4 clients (max allowed)
        for _ in range(4):
            c = DashboardClient(esp32_ip, port, path, connect_timeout=10)
            c.connect()
            clients.append(c)

        # 5th connection should fail or be dropped immediately
        fifth = DashboardClient(esp32_ip, port, path, connect_timeout=5)
        connection_failed = False
        try:
            fifth.connect()
            # If we get here, try to send and see if it works
            fifth.heartbeat()
            time.sleep(1)
            # The server may accept then drop — check if it's still open
            if not fifth._ws.connected:
                connection_failed = True
        except Exception:
            connection_failed = True
        finally:
            try:
                fifth.disconnect()
            except Exception:
                pass

        assert connection_failed, "5th client should have been rejected but was accepted"
    finally:
        for c in clients:
            c.disconnect()
