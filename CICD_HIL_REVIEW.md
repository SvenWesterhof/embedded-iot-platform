# CI/CD Pipeline & HIL Testing Review

**Date:** 2026-03-09
**Scope:** Full analysis of GitHub Actions workflows, build scripts, and Hardware-in-the-Loop test infrastructure
**Branch:** `feature/setup_hil_testing`

---

## Table of Contents

1. [Pipeline Architecture Overview](#1-pipeline-architecture-overview)
2. [CI/CD Pipeline Assessment](#2-cicd-pipeline-assessment)
3. [HIL Testing — Critical Issues](#3-hil-testing--critical-issues)
4. [HIL Testing — Misleading Tests](#4-hil-testing--misleading-tests)
5. [HIL Testing — Robustness Gaps](#5-hil-testing--robustness-gaps)
6. [Issue Summary Table](#6-issue-summary-table)
7. [Recommended Fixes](#7-recommended-fixes)

---

## 1. Pipeline Architecture Overview

The repository uses **GitHub Actions** with 8 workflows:

| Workflow | Trigger | Runner | Purpose |
|----------|---------|--------|---------|
| `esp32-ci.yml` | Push/PR (ESP32 files) | `ubuntu-latest` | Build, lint, RTOS review, size report |
| `stm32-ci.yml` | Push/PR (STM32 files) | `ubuntu-latest` | Build, lint, RTOS review, size report |
| `unit-tests.yml` | Push/PR (common/tests) | `ubuntu-latest` | Host-native unit tests + coverage |
| `hil-tests.yml` | PR to master/develop | `self-hosted, linux, hil` | Hardware-in-the-loop testing |
| `release.yml` | Tag `v*.*.*` | `ubuntu-latest` | Sign, upload to S3, GitHub Release |
| `release-please.yml` | Push to master | `ubuntu-latest` | Auto version bump + changelog |
| `ota-notify.yml` | Manual dispatch | `ubuntu-latest` | MQTT OTA notification |
| `secrets-scan.yml` | All branches | `ubuntu-latest` | Gitleaks secret detection |

**Strengths of the pipeline:**
- Dual-platform (ESP32 + STM32) with shared common code tested independently
- AI-powered RTOS review via Claude API with evidence grounding
- Binary size tracking with PR comments showing deltas
- Production release gate with manual approval
- Key shredding after signing operations
- Secret scanning on all branches

---

## 2. CI/CD Pipeline Assessment

### 2.1 Well-Implemented Aspects

- **Concurrency control:** All CI workflows cancel in-progress runs for the same ref; release workflow does not (correct — don't cancel active releases)
- **Caching strategy:** ccache keyed per SHA, ARM toolchain keyed by version, ESP-IDF toolchain cached with sdkconfig hash
- **Artifact lifecycle:** Build artifacts (14 days), HIL firmware (1 day), size metrics (30 days) — reasonable retention
- **Static analysis:** Both cppcheck and clang-tidy-17 with warnings-as-errors for security-critical checks
- **Path filtering:** Workflows only trigger on relevant file changes

### 2.2 Pipeline Concerns

**P1: Unit test coverage enforcement is lenient**
- Threshold is **60% line coverage** — low for safety-relevant embedded code
- Coverage parse failures use `continue-on-error` — a broken coverage report won't fail the build
- Branch coverage is reported but **not enforced**

**P2: RTOS review has inconsistent severity gates**
- ESP32: Fails on `CRITICAL + HIGH confidence` findings
- STM32: Fails on `CRITICAL` only (no confidence requirement)
- This inconsistency means the same bug in shared code could pass on one platform and fail on the other

**P3: HIL workflow has no retry mechanism**
- Hardware tests are inherently flaky (serial timing, WiFi, MQTT). A single failure fails the entire PR check
- No `retry-on-failure` or test-level flaky marking
- 60-minute job timeout is appropriate, but individual test timeout (60s default) may be tight for OTA tests that have 150-220s markers

**P4: Size report is non-blocking**
- Binary size regressions only post a comment — they don't fail the build
- A firmware that exceeds flash limits won't be caught until the `check_flash_sizes.py` script runs (which does fail)

**P5: HIL Docker runs in privileged mode with host networking**
- Required for OpenOCD/ST-Link USB access and LAN device communication
- But this means a compromised test could access the entire host network and USB bus
- Consider using more granular `--cap-add` and `--device` flags instead of full `--privileged`

---

## 3. HIL Testing — Critical Issues

### 3.1 SerialMonitor `wait_for()` Has a Read-Sleep Race Window

**File:** `hil/fixtures/device.py:63-82`

```python
def wait_for(self, pattern, timeout=30.0, since=0):
    while time.monotonic() < deadline:
        with self._lock:
            new_lines = self._lines[seen_up_to:]
            seen_up_to = len(self._lines)
        for line in new_lines:
            if rx.search(line):
                return line
        time.sleep(0.1)  # <-- 100ms blind window
```

The monitor checks buffered lines, releases the lock, then sleeps 100ms. Lines arriving during the sleep are captured on the next iteration — but the polling pattern introduces unnecessary latency and makes boot-time pattern matching unreliable when the device boots faster than expected.

**Evidence:** Commits `0f6ab85` and `876bcc8` directly addressed "mid-run monitor start" and "MQTT wait pattern" timing issues — symptoms of this architectural weakness.

**Fix:** Replace polling with a `threading.Condition` variable. The reader thread notifies the condition when new lines arrive, eliminating the blind sleep window.

---

### 3.2 DashboardClient RX Thread Dies Silently on Disconnect

**File:** `hil/fixtures/dashboard_client.py:175-205`

```python
def _rx_loop(self):
    while self._running:
        try:
            data = self._ws.recv()
        except Exception as e:
            if self._running:
                logger.warning("WebSocket recv error: %s", e)
            break  # <-- Thread exits, no reconnect, no notification
```

If the WebSocket connection drops (common during OTA reboots), the RX thread exits silently. Subsequent `wait_for()` calls in the test **hang until the test timeout** because no new packets are being collected, and the test has no way to detect the dead connection.

**Impact:** OTA tests (`test_05_ota.py`) have 150-220s timeouts. A dropped connection means the test hangs for the full timeout before failing — wasting CI minutes and giving no diagnostic information.

**Fix:** Set a `_disconnected` event on RX thread exit. Have `wait_for()` check this event and raise immediately with a clear "WebSocket disconnected" error.

---

### 3.3 `booted_esp32` Fixture Can Match Stale Log Lines

**File:** `hil/conftest.py:103-154`

The `booted_esp32` fixture waits for WiFi → STM32 protocol → MQTT in sequence. If the serial monitor starts mid-boot and the buffer contains lines from a **previous boot cycle**, `wait_for()` may match a stale line and return immediately — leaving the device in an unknown state for subsequent checks.

**Fix:** Record line count at fixture start and pass `since=` parameter to all `wait_for()` calls to only match lines received after the fixture began.

---

## 4. HIL Testing — Misleading Tests

### 4.1 `test_sequence_numbers_match` Doesn't Verify Sequence Numbers

**File:** `hil/tests/test_02_protocol.py:117-132`

```python
def test_sequence_numbers_match(dashboard):
    for _ in range(5):
        dashboard.stm32_cmd(CMD_GET_STATUS)
    # ... waits for 5 responses ...
    assert len(pkts) >= 5, f"Expected 5 responses, got {len(pkts)}"
```

Despite the name, this test **only checks that 5 responses arrived**. It never extracts or validates sequence numbers. A device that sends 5 identical responses with sequence number 0 would pass. A device that reorders responses would pass.

**Verdict:** Test name is misleading. Either rename to `test_five_responses_received` or implement actual sequence number validation.

---

### 4.2 `test_dashboard_server_reachable` Only Tests the Fixture

**File:** `hil/tests/test_01_boot.py:38-47`

```python
def test_dashboard_server_reachable(dashboard):
    dashboard.heartbeat()
    assert dashboard._ws is not None and dashboard._ws.connected
```

The `dashboard` fixture calls `client.connect()` in setup. If connection fails, the fixture raises before the test body runs. So reaching the assertion means the fixture succeeded — the test is just re-checking fixture state.

The heartbeat is sent but the response is **not validated** (comment says "no ACK is sent"). This test verifies "the WebSocket connection opened" — not "the dashboard server is functional."

**Verdict:** Rename to `test_dashboard_websocket_connects` or add actual response validation.

---

### 4.3 `test_measurement_rate` Uses Wall-Clock Timing with 50% Tolerance

**File:** `hil/tests/test_03_sensor_data.py:88-114`

```python
arrival_times.append(time.monotonic())  # wall-clock, not device timestamp
# ...
tolerance = expected_s * 0.5  # ±50% tolerance
```

**Problems:**
1. Measures Python's `time.monotonic()` after `wait_for()` returns, not the device's scheduling time. Network jitter, WiFi latency, and test machine load all affect the measurement.
2. 50% tolerance means a 1000ms interval passes with anything from 500ms to 1500ms — a device running at 2x speed would pass.
3. If two measurements arrive in rapid succession between `wait_for()` polls, `drain()` discards the second one, skewing the average.

**Verdict:** This test provides weak assurance about measurement timing. Consider using device-side timestamps from the measurement payload instead of wall-clock timing.

---

### 4.4 OTA Signature Rejection Test May Fail for Wrong Reason

**File:** `hil/tests/test_05_ota.py:167-201`

The `test_stm32_ota_invalid_signature_rejected` test publishes an OTA payload with a fake signature but **omits the `sha256` field** that the successful OTA test includes. If the device rejects the OTA due to the missing SHA256 (not the bad signature), the test still passes because the regex matches any `"OTA failed"` pattern.

**Verdict:** The test verifies the failure outcome but not the failure **reason**. Add the SHA256 field so the only difference from a valid OTA is the signature.

---

## 5. HIL Testing — Robustness Gaps

### 5.1 File Handle Leaks in OTA Tests

**File:** `hil/tests/test_05_ota.py:72, 170`

```python
fw_data = open(ota_bin, "rb").read()  # never closed
```

Used in `test_esp32_ota_success` and `test_stm32_ota_invalid_signature_rejected`. On the self-hosted runner (Linux), this is unlikely to cause issues due to GC. But it's sloppy and could cause problems on Windows or in long-running test sessions.

**Fix:** Use `pathlib.Path(ota_bin).read_bytes()` or a `with` block.

---

### 5.2 Missing Payload Length Validation Before Index Access

**File:** `hil/tests/test_03_sensor_data.py:133`

```python
status = pkt.payload[3]  # No length check
```

The helper `stm32_resp_status()` in `test_02_protocol.py:28-32` correctly asserts `len(pkt.payload) >= 4`, but several tests access payload bytes directly without this check. A truncated or corrupted response causes an `IndexError` crash instead of a clear assertion failure.

**Fix:** Use the `stm32_resp_status()` helper consistently, or add inline length assertions.

---

### 5.3 Inconsistent Measurement Cleanup Between Test Modules

`test_03_sensor_data.py` has an `autouse` fixture that stops measurement after every test:

```python
@pytest.fixture(autouse=True)
def stop_measurement_after(dashboard):
    yield
    dashboard.stop_measurement()
```

`test_02_protocol.py` does **not** have this cleanup. If test_02's `test_start_and_stop_measurement` leaves measurement state dirty and runs before test_03, interference is possible.

**Mitigation:** The `dashboard` fixture is function-scoped (new connection per test), so residual state should be limited. But the **device-side** measurement state persists across WebSocket connections — only an explicit stop command resets it.

**Fix:** Add measurement cleanup to test_02 protocol tests, or add a session-scoped cleanup in conftest.py.

---

### 5.4 Fragile IP Address Extraction

**File:** `hil/fixtures/device.py:84-94`

```python
ip_re = re.compile(r"(?:ip|IP)[:\s]+(\d{1,3}(?:\.\d{1,3}){3})")
```

Matches syntactically valid-looking but semantically invalid IPs like `999.999.999.999`. Also sensitive to log format changes.

**Impact:** Low — invalid IPs would cause network failures downstream, not silent passes. But the error message would be confusing ("connection refused to 999.999.999.999" vs "invalid IP extracted from boot log").

---

### 5.5 `test_buffer_data_returns_samples` Retry Hides Timing Issues

**File:** `hil/tests/test_03_sensor_data.py:117-141`

The test retries 3 times with 5s delays, total 15s. If the device's sample buffer is slow to fill, the test eventually passes — but the underlying timing issue is hidden. The retry pattern masks what might be a real firmware regression (buffer fill rate dropped from 1s to 12s).

---

### 5.6 No Test Isolation for Device State

Tests run sequentially in a single pytest session against the same physical hardware. There is no mechanism to reset device state between test modules (e.g., reboot the ESP32 between test_02 and test_03). If an earlier test leaves the device in an unexpected state (e.g., OTA in progress, measurement active, WiFi disconnected), later tests may fail or pass misleadingly.

**Fix:** Consider adding a device-reset fixture at the module level for test modules that require clean device state.

---

## 6. Issue Summary Table

| # | Issue | Severity | Category | Location |
|---|-------|----------|----------|----------|
| 3.1 | SerialMonitor polling race | **HIGH** | Race condition | `fixtures/device.py` |
| 3.2 | RX thread dies silently | **HIGH** | Reliability | `fixtures/dashboard_client.py` |
| 3.3 | Stale log line matching | **MEDIUM** | Race condition | `conftest.py` |
| 4.1 | Sequence test doesn't check sequences | **MEDIUM** | Misleading test | `test_02_protocol.py` |
| 4.2 | Dashboard test only tests fixture | **MEDIUM** | Misleading test | `test_01_boot.py` |
| 4.3 | Wall-clock timing with 50% tolerance | **MEDIUM** | Weak assertion | `test_03_sensor_data.py` |
| 4.4 | OTA rejection may fail for wrong reason | **MEDIUM** | Misleading test | `test_05_ota.py` |
| 5.1 | File handle leaks | **LOW** | Resource leak | `test_05_ota.py` |
| 5.2 | Missing payload length checks | **LOW** | Robustness | `test_02/03_*.py` |
| 5.3 | Inconsistent measurement cleanup | **MEDIUM** | State mgmt | `test_02_protocol.py` |
| 5.4 | Fragile IP regex | **LOW** | Input validation | `fixtures/device.py` |
| 5.5 | Retry hides timing regressions | **LOW** | Observability | `test_03_sensor_data.py` |
| 5.6 | No device state reset between modules | **MEDIUM** | Test isolation | `conftest.py` |
| 2.1 | Low coverage threshold (60%) | **LOW** | CI config | `unit-tests.yml` |
| 2.2 | Inconsistent RTOS review gates | **LOW** | CI config | `esp32-ci.yml` / `stm32-ci.yml` |
| 2.3 | No HIL test retry mechanism | **MEDIUM** | CI config | `hil-tests.yml` |
| 2.5 | Privileged Docker for HIL | **LOW** | Security | `docker-compose.yml` |

---

## 7. Recommended Fixes

### Priority 1 — Fix reliability issues that cause false passes or wasted CI time

1. **Replace `SerialMonitor.wait_for()` polling with `threading.Condition`** — eliminates 100ms blind windows, makes boot pattern matching deterministic
2. **Add disconnect detection to `DashboardClient`** — set `_disconnected` event when RX thread exits; raise immediately in `wait_for()` instead of hanging until timeout
3. **Rename or rewrite `test_sequence_numbers_match`** — either verify actual sequence numbers or rename to reflect what it truly tests

### Priority 2 — Reduce misleading test results

4. **Add `since=` parameter to `booted_esp32` fixture** — prevent matching stale log lines from previous boot cycles
5. **Fix OTA signature test** — include SHA256 field so the test isolates signature validation specifically
6. **Strengthen dashboard test** — verify an actual server response, not just that the fixture connected
7. **Add measurement cleanup to `test_02_protocol.py`** — match the cleanup pattern already used in test_03

### Priority 3 — Improve robustness and observability

8. **Use `pathlib.Path.read_bytes()`** in OTA tests to fix file handle leaks
9. **Use `stm32_resp_status()` helper consistently** instead of raw `pkt.payload[3]` access
10. **Add module-level device reset fixture** — reboot ESP32 between test modules for clean state
11. **Consider adding `pytest-rerunfailures`** to the HIL workflow for automatic retry of flaky hardware tests
12. **Tighten measurement rate tolerance** — use device timestamps if available, or reduce tolerance from 50% to 20-25%

---

*Generated by code review analysis. Issues identified through static analysis of test code, fixture implementations, and CI workflow configurations.*
