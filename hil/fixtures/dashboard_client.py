"""
Dashboard WebSocket client for HIL tests.

The dashboard uses a binary protocol over WebSocket:
  - Messages to ESP32:   first byte = DASH_MSG_* type, rest = payload
  - Messages from ESP32: first byte = DASH_RESP_* type, rest = payload

Message types mirror feat_dashboard_server.h.
"""

import struct
import threading
import time
import logging
from enum import IntEnum
from dataclasses import dataclass
from typing import Optional, Callable

import websocket

logger = logging.getLogger(__name__)


class DashMsg(IntEnum):
    HEARTBEAT         = 0x01
    REQUEST_HISTORY   = 0x02
    START_MEASUREMENT = 0x03
    STOP_MEASUREMENT  = 0x04
    GET_STATUS        = 0x05
    SET_CONFIG        = 0x06
    SYNC_TIME         = 0x07
    STM32_CMD         = 0x10


class DashResp(IntEnum):
    ACK         = 0x01
    ERROR       = 0x02
    STATUS      = 0x03
    DATA        = 0x04
    MEASUREMENT = 0x05
    HISTORY     = 0x06
    STM32       = 0x10


@dataclass
class DashPacket:
    resp_type: DashResp
    payload: bytes

    def __repr__(self):
        return f"DashPacket(type={self.resp_type.name}, payload_len={len(self.payload)})"


class DashboardClient:
    """
    Connects to the ESP32 dashboard WebSocket and provides a clean test API.
    """

    def __init__(self, esp32_ip: str, port: int = 80, path: str = "/ws",
                 connect_timeout: float = 10.0):
        self._url = f"ws://{esp32_ip}:{port}{path}"
        self._connect_timeout = connect_timeout
        self._ws: Optional[websocket.WebSocket] = None
        self._rx_thread: Optional[threading.Thread] = None
        self._running = False
        self._received: list[DashPacket] = []
        self._recv_lock = threading.Condition()
        self._disconnected = threading.Event()
        self._connected = threading.Event()

    def connect(self):
        self._ws = websocket.WebSocket()
        self._ws.connect(self._url, timeout=self._connect_timeout)
        self._running = True
        self._rx_thread = threading.Thread(target=self._rx_loop, daemon=True, name="dash-rx")
        self._rx_thread.start()
        logger.info("Connected to dashboard: %s", self._url)

    def disconnect(self):
        self._running = False
        if self._ws:
            try:
                self._ws.close()
            except Exception:
                pass
        if self._rx_thread:
            self._rx_thread.join(timeout=2.0)

    def __enter__(self):
        self.connect()
        return self

    def __exit__(self, *_):
        self.disconnect()

    # ------------------------------------------------------------------
    # Send helpers
    # ------------------------------------------------------------------

    def send(self, msg_type: DashMsg, payload: bytes = b""):
        data = bytes([msg_type]) + payload
        self._ws.send_binary(data)
        logger.debug("Sent %s payload_len=%d", msg_type.name, len(payload))

    def heartbeat(self):
        self.send(DashMsg.HEARTBEAT)

    def get_status(self):
        self.send(DashMsg.GET_STATUS)

    def request_history(self):
        self.send(DashMsg.REQUEST_HISTORY)

    def start_measurement(self, interval_ms: int = 1000):
        # payload: uint32 interval_ms (matches cmd_start_measurement_t)
        self.send(DashMsg.START_MEASUREMENT, struct.pack("<I", interval_ms))

    def stop_measurement(self):
        self.send(DashMsg.STOP_MEASUREMENT)

    def sync_time(self):
        self.send(DashMsg.SYNC_TIME)

    def stm32_cmd(self, cmd_id: int, payload: bytes = b""):
        """Forward a raw STM32 command through the dashboard (DASH_MSG_STM32_CMD)."""
        self.send(DashMsg.STM32_CMD, bytes([cmd_id]) + payload)

    # ------------------------------------------------------------------
    # Receive helpers
    # ------------------------------------------------------------------

    def wait_for(self, resp_type: DashResp, timeout: float = 5.0) -> DashPacket:
        """
        Wait until a packet of the given response type is received.
        Returns the packet or raises TimeoutError.
        Raises ConnectionError if the WebSocket disconnects while waiting.
        """
        deadline = time.monotonic() + timeout
        seen_up_to = 0
        with self._recv_lock:
            while True:
                if self._disconnected.is_set():
                    raise ConnectionError("WebSocket disconnected while waiting for response")
                new = self._received[seen_up_to:]
                seen_up_to = len(self._received)
                for pkt in new:
                    if pkt.resp_type == resp_type:
                        return pkt
                remaining = deadline - time.monotonic()
                if remaining <= 0:
                    break
                self._recv_lock.wait(timeout=remaining)
        raise TimeoutError(f"No {resp_type.name} response within {timeout}s")

    def wait_for_any(self, timeout: float = 5.0) -> DashPacket:
        """Wait for any packet to arrive."""
        deadline = time.monotonic() + timeout
        start_len = len(self._received)
        with self._recv_lock:
            while True:
                if self._disconnected.is_set():
                    raise ConnectionError("WebSocket disconnected while waiting for response")
                if len(self._received) > start_len:
                    return self._received[-1]
                remaining = deadline - time.monotonic()
                if remaining <= 0:
                    break
                self._recv_lock.wait(timeout=remaining)
        raise TimeoutError(f"No packet received within {timeout}s")

    def drain(self) -> list[DashPacket]:
        """Return and clear all received packets."""
        with self._recv_lock:
            pkts = list(self._received)
            self._received.clear()
        return pkts

    def received(self) -> list[DashPacket]:
        """Return a snapshot of all received packets."""
        with self._recv_lock:
            return list(self._received)

    # ------------------------------------------------------------------
    # Internal RX loop
    # ------------------------------------------------------------------

    def _rx_loop(self):
        while self._running:
            try:
                data = self._ws.recv()
            except Exception as e:
                if self._running:
                    logger.warning("WebSocket recv error: %s", e)
                self._disconnected.set()
                with self._recv_lock:
                    self._recv_lock.notify_all()
                break

            if not data:
                continue

            if isinstance(data, str):
                # Dashboard may send JSON text in some responses
                logger.debug("Text from dashboard: %s", data)
                continue

            raw = bytes(data)
            if len(raw) < 1:
                continue

            try:
                resp_type = DashResp(raw[0])
            except ValueError:
                logger.warning("Unknown dashboard resp type: 0x%02X", raw[0])
                continue

            pkt = DashPacket(resp_type, raw[1:])
            logger.debug("Received %s", pkt)
            with self._recv_lock:
                self._received.append(pkt)
                self._recv_lock.notify_all()
