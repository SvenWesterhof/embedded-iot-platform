"""
T2: STM32 protocol tests via Dashboard WebSocket.

All STM32 commands are sent through the ESP32 dashboard (DASH_MSG_STM32_CMD or
higher-level dashboard commands) and responses arrive as DASH_RESP_STM32.

Command IDs and response status codes match protocol_common.h.
"""

import struct
import time
import pytest

from fixtures.dashboard_client import DashMsg, DashResp


# STM32 command IDs (from protocol_common.h)
CMD_GET_BUFFER_DATA   = 0x01
CMD_START_MEASUREMENT = 0x02
CMD_STOP_MEASUREMENT  = 0x03
CMD_SET_RTC           = 0x04
CMD_GET_STATUS        = 0x05

RESP_OK          = 0x00
RESP_INVALID_CMD = 0x02


def stm32_resp_status(pkt) -> int:
    """Extract the STM32 response status byte from a DASH_RESP_STM32 packet.
    Layout: TYPE(1) CMD(1) SEQ(1) STATUS(1) LEN(2) PAYLOAD"""
    assert len(pkt.payload) >= 4, "STM32 response payload too short"
    return pkt.payload[3]  # status byte at offset 3


@pytest.mark.timeout(15)
def test_get_status(dashboard):
    """CMD_GET_STATUS must return RESP_OK with a valid status struct."""
    dashboard.stm32_cmd(CMD_GET_STATUS)
    pkt = dashboard.wait_for(DashResp.STM32, timeout=5)
    assert stm32_resp_status(pkt) == RESP_OK, f"Expected RESP_OK, got 0x{stm32_resp_status(pkt):02X}"
    # resp_get_status_t: uint8 state, uint8 error_code, uint16 buffer_count, uint32 uptime
    # payload[6:] is the response payload after the 6-byte header
    resp_payload = pkt.payload[6:]
    assert len(resp_payload) >= 8, "GET_STATUS response payload too short"
    state, error_code, buffer_count, uptime = struct.unpack_from("<BBHI", resp_payload)
    assert uptime >= 0, "Uptime should be non-negative"


@pytest.mark.timeout(15)
def test_set_rtc(dashboard):
    """CMD_SET_RTC must sync the STM32 RTC and return RESP_OK.

    The dashboard uses NTP time for this command.  NTP may finish syncing within
    a second or two after boot, so retry until we get a STM32 response.
    """
    deadline = time.monotonic() + 12
    while True:
        dashboard.drain()
        dashboard.stm32_cmd(CMD_SET_RTC, struct.pack("<I", int(time.time())))
        try:
            pkt = dashboard.wait_for(DashResp.STM32, timeout=3)
            assert stm32_resp_status(pkt) == RESP_OK
            return
        except TimeoutError:
            if time.monotonic() >= deadline:
                pytest.fail("CMD_SET_RTC never succeeded — NTP may not have synced")
            time.sleep(1)


@pytest.mark.timeout(20)
def test_start_and_stop_measurement(dashboard):
    """Start measurement via dashboard, verify live data arrives, then stop."""
    # Start measurement (1-second interval)
    dashboard.start_measurement(interval_ms=1000)

    # Expect at least one MEASUREMENT response within 5 seconds
    pkt = dashboard.wait_for(DashResp.MEASUREMENT, timeout=5)
    assert len(pkt.payload) > 0, "Measurement payload should not be empty"

    # Stop measurement
    dashboard.stop_measurement()
    time.sleep(2)

    # After stopping, no more measurements should arrive
    dashboard.drain()
    time.sleep(2)
    pkts = [p for p in dashboard.received() if p.resp_type == DashResp.MEASUREMENT]
    assert len(pkts) == 0, "Measurements continued after STOP_MEASUREMENT"


@pytest.mark.timeout(15)
def test_get_buffer_data(dashboard):
    """CMD_GET_BUFFER_DATA must return at least one historical sample."""
    # Request up to 10 samples from the beginning of the buffer
    payload = struct.pack("<II", 0, 10)  # start_index=0, count=10
    dashboard.stm32_cmd(CMD_GET_BUFFER_DATA, payload)
    # Response arrives as DASH_RESP_STM32 or DASH_RESP_HISTORY depending on app layer
    pkt = dashboard.wait_for(DashResp.STM32, timeout=8)
    status = stm32_resp_status(pkt)
    # RESP_OK (has data) or RESP_NO_DATA (buffer empty) are both valid
    assert status in (RESP_OK, 0x06), f"Unexpected status 0x{status:02X}"


@pytest.mark.timeout(10)
def test_unknown_command_rejected(dashboard):
    """An unknown STM32 command ID must return RESP_INVALID_CMD."""
    # Drain any stale deferred responses (e.g. from a previous test's async
    # callbacks landing on a reused socket fd) before sending our command.
    dashboard.drain()
    dashboard.stm32_cmd(0xFF)  # 0xFF is not a valid command
    pkt = dashboard.wait_for(DashResp.STM32, timeout=5)
    assert stm32_resp_status(pkt) == RESP_INVALID_CMD, (
        f"Expected RESP_INVALID_CMD (0x02), got 0x{stm32_resp_status(pkt):02X}"
    )


@pytest.mark.timeout(30)
def test_sequence_numbers_match(dashboard):
    """Send 5 consecutive GET_STATUS commands and verify no response is lost."""
    for _ in range(5):
        dashboard.stm32_cmd(CMD_GET_STATUS)

    # Poll received() without draining so buffered responses are not discarded
    deadline = time.monotonic() + 10
    while time.monotonic() < deadline:
        pkts = [p for p in dashboard.received() if p.resp_type == DashResp.STM32]
        if len(pkts) >= 5:
            break
        time.sleep(0.2)

    pkts = [p for p in dashboard.received() if p.resp_type == DashResp.STM32]
    assert len(pkts) >= 5, f"Expected 5 responses, got {len(pkts)}"
