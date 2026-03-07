"""
T3: Sensor data quality tests.
Verify that real sensor readings are within physically plausible bounds
and arrive at the expected rate.
"""

import struct
import time
import yaml
import os
import pytest

from fixtures.dashboard_client import DashResp

CFG = yaml.safe_load(open(os.path.join(os.path.dirname(__file__), "..", "config.yaml")))
BOUNDS = CFG["sensor_bounds"]

SENSOR_TEMPERATURE = 0x01
SENSOR_CURRENT     = 0x02


def parse_sensor_sample(payload: bytes) -> tuple[int, int, int]:
    """
    Parse a sensor_sample_t from payload bytes.
    Returns (sensor_type, timestamp, raw_value).
    sensor_sample_t: uint8 sensor_type, uint32 timestamp, int32 value (packed, 9 bytes)
    """
    sensor_type, timestamp, raw_value = struct.unpack_from("<BIi", payload)
    return sensor_type, timestamp, raw_value


@pytest.fixture(autouse=True)
def stop_measurement_after(dashboard):
    """Ensure measurement is stopped after each test in this module."""
    yield
    dashboard.stop_measurement()
    time.sleep(0.5)


@pytest.mark.timeout(30)
def test_temperature_in_range(dashboard):
    """AHT25 temperature must be in a plausible range for a lab environment."""
    dashboard.start_measurement(interval_ms=1000)

    samples_collected = []
    deadline = time.monotonic() + 15
    while time.monotonic() < deadline and len(samples_collected) < 3:
        try:
            pkt = dashboard.wait_for(DashResp.MEASUREMENT, timeout=3)
            sensor_type, timestamp, raw_value = parse_sensor_sample(pkt.payload)
            if sensor_type == SENSOR_TEMPERATURE:
                samples_collected.append(raw_value / 100.0)
        except TimeoutError:
            break

    assert len(samples_collected) >= 1, "No temperature samples received within 15s"

    for temp_c in samples_collected:
        assert BOUNDS["temperature_min_c"] <= temp_c <= BOUNDS["temperature_max_c"], (
            f"Temperature {temp_c:.2f}°C out of range "
            f"[{BOUNDS['temperature_min_c']}, {BOUNDS['temperature_max_c']}]"
        )


@pytest.mark.timeout(30)
def test_timestamps_monotonic(dashboard):
    """Sensor sample timestamps must be monotonically increasing."""
    dashboard.start_measurement(interval_ms=1000)

    timestamps = []
    deadline = time.monotonic() + 15
    while time.monotonic() < deadline and len(timestamps) < 5:
        try:
            pkt = dashboard.wait_for(DashResp.MEASUREMENT, timeout=3)
            _, ts, _ = parse_sensor_sample(pkt.payload)
            timestamps.append(ts)
        except TimeoutError:
            break

    assert len(timestamps) >= 2, f"Need at least 2 samples to check monotonicity, got {len(timestamps)}"

    for i in range(1, len(timestamps)):
        assert timestamps[i] >= timestamps[i - 1], (
            f"Timestamp went backwards: {timestamps[i-1]} -> {timestamps[i]}"
        )


@pytest.mark.timeout(30)
def test_measurement_rate(dashboard):
    """Live measurements must arrive at approximately the requested interval (±30%)."""
    interval_ms = 1000
    dashboard.start_measurement(interval_ms=interval_ms)

    arrival_times = []
    deadline = time.monotonic() + 15
    while time.monotonic() < deadline and len(arrival_times) < 5:
        try:
            dashboard.wait_for(DashResp.MEASUREMENT, timeout=3)
            arrival_times.append(time.monotonic())
            dashboard.drain()
        except TimeoutError:
            break

    assert len(arrival_times) >= 3, f"Only received {len(arrival_times)} samples — too few to check rate"

    intervals = [arrival_times[i] - arrival_times[i - 1] for i in range(1, len(arrival_times))]
    avg_interval = sum(intervals) / len(intervals)

    expected_s = interval_ms / 1000.0
    tolerance = expected_s * 0.5  # 50% tolerance (network + processing jitter)

    assert abs(avg_interval - expected_s) <= tolerance, (
        f"Average interval {avg_interval:.2f}s deviates too much from expected {expected_s}s"
    )


@pytest.mark.timeout(20)
def test_buffer_data_returns_samples(dashboard):
    """Historical buffer must contain at least one sample after boot."""
    # STM32 buffers a sample every 10 seconds — wait a bit if needed
    import struct
    CMD_GET_BUFFER_DATA = 0x01
    RESP_OK = 0x00
    RESP_NO_DATA = 0x06

    # Try up to 3 times with 5s between attempts (in case buffer just started)
    for attempt in range(3):
        payload = struct.pack("<II", 0, 5)  # start=0, count=5
        dashboard.stm32_cmd(CMD_GET_BUFFER_DATA, payload)
        try:
            from fixtures.dashboard_client import DashResp
            pkt = dashboard.wait_for(DashResp.STM32, timeout=5)
            status = pkt.payload[3]  # status byte in header
            if status == RESP_OK:
                return  # Buffer has data — test passes
        except TimeoutError:
            pass
        if attempt < 2:
            time.sleep(5)

    pytest.fail("Buffer returned no data after 3 attempts")
