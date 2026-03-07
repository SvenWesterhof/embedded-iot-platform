# HIL Testing — Integration Manual

This guide walks through setting up the Hardware-in-the-Loop (HIL) test infrastructure
on the Ubuntu server from scratch through to a passing CI run.

---

## 1. Hardware Setup

### Required connections

```
Ubuntu Server
├── USB-A port 1  →  ESP32-S3 dev board (built-in USB JTAG, Espressif 303a:1001)
│                    No external USB-UART chip needed — the S3 exposes a CDC ACM device
└── USB-A port 2  →  STM32 Nucleo board (ST-Link USB, STMicroelectronics 0483:374b)
                     Exposes: /dev/ttyACM1 = ST-Link VCP debug UART
                              ST-Link JTAG = used by OpenOCD for flashing

ESP32 GPIO16 (TX) ──────────────────────────────── STM32 USART2 PA3 (RX)
ESP32 GPIO17 (RX) ──────────────────────────────── STM32 USART2 PA2 (TX)
ESP32 GND         ──────────────────────────────── STM32 GND
```

After plugging in both boards, verify the devices appear:

```bash
ls /dev/ttyACM*
# Expected:
#   /dev/ttyACM0   ← ESP32-S3 built-in USB JTAG/CDC
#   /dev/ttyACM1   ← STM32 ST-Link VCP
```

> **Note:** The ESP32-S3 uses its built-in USB peripheral — it shows up as `ttyACM`,
> not `ttyUSB`. There is no external USB-UART chip (CP2102/CH340) needed.

If the numbers differ, update `hil/config.yaml` or set `HIL_ESP32_PORT` / `HIL_STM32_PORT`.

---

## 2. Ubuntu Server Prerequisites

### 2.1 System packages

```bash
sudo apt-get update
sudo apt-get install -y \
    docker.io \
    docker-compose-plugin \
    openocd \
    python3-pip \
    udev
```

### 2.2 esptool

```bash
pip3 install esptool
# Verify:
esptool.py version
```

### 2.3 User permissions for USB devices

The user that runs the tests (and the GitHub Actions runner) needs access to USB
serial ports and ST-Link.

```bash
sudo usermod -aG dialout $USER
sudo usermod -aG plugdev $USER

# Create udev rule for ST-Link (required for OpenOCD without root)
sudo tee /etc/udev/rules.d/99-stlink.rules <<'EOF'
SUBSYSTEM=="usb", ATTRS{idVendor}=="0483", ATTRS{idProduct}=="374b", MODE="0666", GROUP="plugdev"
SUBSYSTEM=="usb", ATTRS{idVendor}=="0483", ATTRS{idProduct}=="374e", MODE="0666", GROUP="plugdev"
EOF
sudo udevadm control --reload-rules && sudo udevadm trigger

# Log out and back in (or reboot) for group changes to take effect
```

### 2.4 Docker post-install

```bash
sudo systemctl enable --now docker
sudo usermod -aG docker $USER
# Log out and back in
docker run hello-world   # verify
```

---

## 3. Clone the Repository on the Server

Everything from here on runs on the Ubuntu server.

```bash
# Use SSH key or HTTPS — whichever you normally use with GitHub
git clone git@github.com:<your-org>/embedded-iot-platform.git
cd embedded-iot-platform
```

If you already have the repo cloned locally and just need to get it onto the server,
SSH in and clone there — the server needs its own copy because the self-hosted runner
checks out the repo fresh for each CI job into its own workspace.

---

## 4. Signing Keys

OTA tests require an RSA key pair:
- **Private key** — used by the test runner to sign firmware. Stays on the Ubuntu server, never committed.
- **Public key** — compiled into the firmware so the device can verify signatures. Must be committed to the repo.

### 4.1 Generate keys on the Ubuntu server

Run these commands **on the Ubuntu server**, in the repo root:

```bash
mkdir -p keys

# STM32 OTA signing key (RSA-2048)
openssl genrsa -out keys/ota_signing_key_stm32.pem 2048
openssl rsa -in keys/ota_signing_key_stm32.pem -pubout -out keys/ota_signing_key_stm32.pub.pem
```

`.gitignore` already excludes `keys/*.pem` (private) but allows `keys/*.pub.pem` (public).

### 3.2 Commit the public key

From the Ubuntu server (or any machine):

```bash
git add keys/ota_signing_key_stm32.pub.pem
git commit -m "feat(hil): add HIL test OTA signing public key"
git push
```

### 4.3 Embed the public key in firmware

The ESP32 firmware reads the STM32 OTA public key from
`ESP32/Middleware/Services/stm32_public_key.h` — a C header with the PEM string
hardcoded as a `static const char[]`. Replace the placeholder with your generated key.

Print the key on the server:

```bash
cat keys/ota_signing_key_stm32.pub.pem
```

Open `ESP32/Middleware/Services/stm32_public_key.h` and replace the placeholder
PEM body with your key content. Keep each line as a quoted C string ending with `\n`:

```c
static const char stm32_public_key_pem[] =
"-----BEGIN PUBLIC KEY-----\n"
"MIIBIjANBgkqhkiG9w0BAQEFAAOCAQ8AMIIBCgKCAQEA...\n"
"...\n"
"-----END PUBLIC KEY-----\n";
```

Commit the updated header (the public key is safe to commit):

```bash
git add ESP32/Middleware/Services/stm32_public_key.h
git commit -m "feat(hil): embed HIL OTA signing public key in firmware"
git push
```

### 4.4 Set the GitHub Secret

In **Settings → Secrets → Actions**, add:

| Secret | Value (example) |
|--------|----------------|
| `HIL_STM32_SIGNING_KEY_PATH` | `/home/ci/actions-runner/_work/embedded-iot-platform/embedded-iot-platform/keys/ota_signing_key_stm32.pem` |

This is the absolute path to the private key **on the Ubuntu server**. The key itself never leaves the server.

---

## 5. GitHub Actions Self-Hosted Runner

### 5.1 Register the runner

In the GitHub repository:
**Settings → Actions → Runners → New self-hosted runner**

Select **Linux / x64**, then follow the displayed commands on the Ubuntu server.
When prompted for labels, enter:

```
self-hosted,linux,hil
```

### 5.2 Install as a service

After registration:

```bash
cd ~/actions-runner
sudo ./svc.sh install
sudo ./svc.sh start
sudo ./svc.sh status   # should show "active (running)"
```

### 5.3 Verify the runner appears in GitHub

**Settings → Actions → Runners** — the runner should show as **Idle**.

---

## 6. GitHub Secrets

Add the following secrets in **Settings → Secrets and variables → Actions**:

| Secret | Value | Used by |
|--------|-------|---------|
| `HIL_WIFI_SSID` | 2.4 GHz SSID the ESP32 connects to | Build (credentials.h injection) |
| `HIL_WIFI_PASSWORD` | WiFi password | Build |
| `HIL_SERVER_IP` | Ubuntu server LAN IP (e.g. `192.168.1.50`) | HIL runner + OTA tests |
| `HIL_STM32_SIGNING_KEY_PATH` | Absolute path to `keys/ota_signing_key_stm32.pem` on the runner | T5 OTA tests |

> `HIL_STM32_SIGNING_KEY_PATH` is a **path on the self-hosted runner**, not a key value.
> The private key never leaves the server. Example value: `/home/ci/actions-runner/_work/embedded-iot-platform/embedded-iot-platform/keys/ota_signing_key_stm32.pem`

---

## 7. Local First Run (Manual Verification)

Before relying on CI, verify everything works manually on the server.

### 7.1 Build firmware locally

**STM32:**
```bash
export PATH="/opt/arm-toolchain/bin:$PATH"
cd STM32
cmake --preset CI
cmake --build --preset CI --parallel $(nproc)
# Output: STM32/build/CI/sensor_node.bin + STM32/build/CI/Bootloader/bootloader.bin
cd ..
```

**ESP32** (requires Docker with ESP-IDF):
```bash
cp ESP32/Application/credentials.h.template ESP32/Application/credentials.h
# Edit credentials.h with your WiFi SSID, password, and MQTT broker (localhost:1883)
# Then build using Docker:
docker run --rm -v $PWD/ESP32:/project \
    -e IDF_CCACHE_ENABLE=1 \
    espressif/idf:v5.5 \
    bash -c "cd /project && idf.py build"
# Output: ESP32/build/iot_gateway.bin
```

### 7.2 Flash both devices

```bash
# Flash STM32 (bootloader + app)
hil/scripts/flash_stm32.sh \
    STM32/build/CI/sensor_node.bin \
    STM32/build/CI/Bootloader/bootloader.bin

# Flash ESP32
hil/scripts/flash_esp32.sh ESP32/build/iot_gateway.bin
```

### 7.3 Start supporting services

```bash
cd hil
FW_DIR=../ESP32/build docker compose up -d mosquitto fw-server
docker compose ps   # both should show "running"
```

### 7.4 Run boot tests only

```bash
cd hil
pip3 install -r requirements.txt

HIL_SERVER_IP=192.168.1.50 \
HIL_SKIP_FLASH=1 \
HIL_ESP32_FIRMWARE=../ESP32/build/iot_gateway.bin \
HIL_STM32_FIRMWARE=../STM32/build/CI/sensor_node.bin \
pytest tests/test_01_boot.py -v
```

Expected output:
```
tests/test_01_boot.py::test_esp32_wifi_connected          PASSED
tests/test_01_boot.py::test_esp32_mqtt_connected          PASSED
tests/test_01_boot.py::test_stm32_protocol_ready          PASSED
tests/test_01_boot.py::test_stm32_sensors_initialized     PASSED
tests/test_01_boot.py::test_dashboard_server_reachable    PASSED
```

### 7.5 Run the full suite

```bash
HIL_SERVER_IP=192.168.1.50 \
HIL_SKIP_FLASH=1 \
HIL_STM32_OTA_BIN=../STM32/build/CI/sensor_node.bin \
HIL_STM32_KEY=../keys/ota_signing_key_stm32.pem \
pytest tests/ -v --timeout=120
```

---

## 8. CI Run

Once the self-hosted runner is registered and secrets are configured, push to a
feature branch or open a PR touching `ESP32/**`, `STM32/**`, `common/**`, or `hil/**`.

The workflow `HIL Tests` will:

1. **build-esp32** (GitHub-hosted) — injects HIL WiFi credentials + local MQTT URL, builds, uploads artifact
2. **build-stm32** (GitHub-hosted) — builds app + bootloader, uploads artifact
3. **hil** (self-hosted runner) — downloads artifacts, flashes devices, runs pytest, uploads results

Check results in **Actions → HIL Tests → hil job → pytest output**.
JUnit XML and full logs are available as artifacts for 14 days.

You can also trigger a manual run without a PR via **Actions → HIL Tests → Run workflow**
with the option to skip flashing (useful when the device is already running the right firmware).

---

## 9. Troubleshooting

### ESP32 not detected (`/dev/ttyUSB0` missing)

```bash
lsusb | grep -i "cp210\|ch340\|ftdi"
dmesg | tail -20   # look for USB serial driver messages
```
Try a different USB cable — many phone cables are charge-only.

### OpenOCD fails to connect to ST-Link

```bash
openocd -f interface/stlink.cfg -f target/stm32f7x.cfg -c "init" -c "shutdown"
```
Common causes:
- ST-Link firmware outdated — update via STM32CubeProgrammer
- Missing udev rule (section 2.3 above) — verify with `ls -la /dev/bus/usb/...`
- Another process (STM32CubeIDE) has the ST-Link open

### ESP32 does not get an IP / cannot reach server

- Confirm the ESP32 is connecting to the 2.4 GHz network (it does not support 5 GHz)
- Check `credentials.h` has the correct SSID and password
- Verify the server IP is reachable from the ESP32 network:
  ```bash
  # From the server, check the ESP32 can ping it:
  # (read IP from serial monitor first)
  ping <esp32-ip>
  ```

### MQTT OTA trigger not received by ESP32

The HIL workflow points the ESP32 MQTT broker to `mqtt://<server_ip>:1883`.
Verify the local Mosquitto is running and listening:

```bash
docker compose ps
# Test publish/subscribe:
mosquitto_sub -h localhost -t "gateway/ota/notify" &
mosquitto_pub -h localhost -t "gateway/ota/notify" -m '{"test":1}'
```

### T5 OTA tests skipped

OTA tests require `HIL_STM32_OTA_BIN` and `HIL_STM32_KEY` environment variables.
If they are not set, the tests are skipped (not failed) so the suite still passes.
Set `HIL_STM32_SIGNING_KEY_PATH` as a GitHub Secret pointing to the key on the runner.

### Dashboard WebSocket connection refused

The ESP32 dashboard server starts after WiFi + MQTT are up. If T4 tests fail with
connection refused, increase `timeouts.boot_s` in `hil/config.yaml` or check
the ESP32 serial log for errors during `feat_dashboard_server_start`.

### Self-hosted runner shows "offline" in GitHub

```bash
sudo ~/actions-runner/svc.sh status
sudo ~/actions-runner/svc.sh start
```

---

## 9. Skipping OTA Tests

OTA tests take 2–3 minutes (STM32 bank erase) and require signing keys.
To run only T1–T4 (faster, ~3 min total):

```bash
pytest tests/ -v --ignore=tests/test_05_ota.py
```

Or in the workflow, add the `-k "not ota"` pytest flag.
