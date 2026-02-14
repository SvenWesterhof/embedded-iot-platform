# RSA Signature Verification Setup Guide

## Overview

This guide explains how to set up and test RSA-2048 signature verification for STM32 firmware updates via the ESP32 gateway.

**Architecture:**
- Python script signs STM32 firmware with RSA-2048 private key
- Uploads signed firmware to AWS S3
- Sends MQTT notification with signature to ESP32
- ESP32 downloads firmware and verifies RSA signature
- ESP32 forwards verified firmware to STM32 via UART

---

## 1. Generate RSA Keypair

### Option A: Windows (using batch file)

```bash
cd tools
generate_rsa_keys.bat
```

### Option B: Linux/Mac (using shell script)

```bash
cd tools
chmod +x generate_rsa_keys.sh
./generate_rsa_keys.sh
```

### Option C: Manual (using OpenSSL directly)

```bash
cd ESP32/keys

# Generate private key (KEEP SECRET!)
openssl genrsa -out stm32_private_key.pem 2048

# Extract public key
openssl rsa -in stm32_private_key.pem -pubout -out stm32_public_key.pem

# View public key
cat stm32_public_key.pem
```

Then manually copy the public key contents to `ESP32/Middleware/Services/stm32_public_key.h`

---

## 2. Verify Generated Files

After running the key generation script, you should have:

```
ESP32/keys/
├── stm32_private_key.pem    # Private key (SECRET - DO NOT COMMIT)
└── stm32_public_key.pem     # Public key (can be shared)

ESP32/Middleware/Services/
└── stm32_public_key.h       # Embedded public key (updated automatically)
```

**Verify the key:**
```bash
openssl rsa -in ESP32/keys/stm32_private_key.pem -check -noout
```

You should see: `RSA key ok`

---

## 3. Rebuild ESP32 Firmware

The ESP32 firmware needs to be rebuilt to include the new embedded public key:

```bash
cd ESP32
idf.py build
idf.py flash monitor
```

**Expected boot log:**
```
I (123) SIG_VERIFY: Initializing RSA signature verification service
I (124) SIG_VERIFY: RSA public key loaded: 2048 bits
I (125) SIG_VERIFY: Signature verification service initialized successfully
I (130) STM32_OTA: STM32 OTA Manager initialized
```

If you see `Failed to parse RSA public key`, the key format is incorrect.

---

## 4. Test Signature Verification (Local)

Before uploading to cloud, test signing locally:

```bash
cd tools

# Create a test firmware file
echo "Test firmware data" > test_firmware.bin

# Sign it with Python
python -c "
from cryptography.hazmat.primitives import serialization, hashes
from cryptography.hazmat.primitives.asymmetric import padding
from cryptography.hazmat.backends import default_backend
import base64

# Load private key
with open('../ESP32/keys/stm32_private_key.pem', 'rb') as f:
    private_key = serialization.load_pem_private_key(f.read(), password=None, backend=default_backend())

# Read test firmware
with open('test_firmware.bin', 'rb') as f:
    data = f.read()

# Sign with RSA-2048 + SHA256
signature = private_key.sign(data, padding.PKCS1v15(), hashes.SHA256())
signature_b64 = base64.b64encode(signature).decode('utf-8')

print('Signature (base64):')
print(signature_b64)
print(f'\nSignature length: {len(signature)} bytes')
"
```

Expected output:
```
Signature (base64):
<base64 string here>

Signature length: 256 bytes
```

---

## 5. Upload STM32 Firmware with Signature

### Prerequisites

1. **AWS S3 bucket configured** (see `upload_firmware.py` header for config)
2. **MQTT broker accessible** (default: broker.hivemq.com)
3. **Python dependencies installed:**
   ```bash
   pip install boto3 paho-mqtt cryptography
   ```

### Upload Command

```bash
cd tools

# Upload STM32 firmware (auto-detect version from CMakeLists.txt)
python upload_firmware.py stm32-sensor-v2.1.0.bin --platform stm32

# Or with explicit version
python upload_firmware.py stm32-sensor-v2.1.0.bin --platform stm32 --version 2.1.0

# Upload without sending MQTT notification (test upload only)
python upload_firmware.py stm32-sensor-v2.1.0.bin --platform stm32 --no-notify
```

### Expected Output

```
Dual-Platform Firmware Release Tool
Platform: STM32
Version: 2.1.0
Binary: stm32-sensor-v2.1.0.bin
Size: 245,760 bytes (0.23 MB)

Calculating checksums...
  SHA256: a1b2c3d4e5f6...
  CRC32:  0x12345678

Signing STM32 firmware (RSA-2048)...
  Signature: dGVzdCBzaWduYXR1cmU=... (344 chars)
  ✓ Signature verified successfully

Uploading STM32 firmware to S3...
Uploaded: https://your-bucket.s3.amazonaws.com/firmware/stm32/stm32-sensor-v2.1.0.bin

Sending MQTT notification to broker.hivemq.com...
Connected to MQTT broker
Published to topic: gateway/stm32/ota/notify
   Payload: {"target":"stm32","version":"2.1.0","url":"https://...","size":245760,"sha256":"...","crc32":305419896,"signature_rsa":"...","auto_apply":false}
Message published successfully

==============================================================
Firmware release complete!
==============================================================
Platform: STM32
Version:  2.1.0
URL:      https://your-bucket.s3.amazonaws.com/firmware/stm32/stm32-sensor-v2.1.0.bin
Manifest: tools/firmware_manifest.json

Devices will receive notification on topic: gateway/stm32/ota/notify
```

---

## 6. Monitor ESP32 OTA Process

Watch the ESP32 serial monitor for OTA progress:

```bash
cd ESP32
idf.py monitor
```

### Expected Log Sequence

```
I (12345) STM32_OTA: Received MQTT data, attempting to parse as STM32 OTA notification
I (12346) STM32_OTA: Parsed STM32 OTA notification:
I (12347) STM32_OTA:   Target: stm32
I (12348) STM32_OTA:   Version: 2.1.0
I (12349) STM32_OTA:   URL: https://your-bucket.s3.amazonaws.com/firmware/stm32/stm32-sensor-v2.1.0.bin
I (12350) STM32_OTA:   Size: 245760 bytes
I (12351) STM32_OTA:   Signature: dGVzdCBzaWduYXR1cmU=...
I (12352) STM32_OTA: STM32 OTA notification received via MQTT
I (12353) STM32_OTA: Triggering STM32 OTA update to version 2.1.0

I (12400) STM32_OTA: STM32 OTA task started
I (12401) STM32_OTA: URL: https://your-bucket.s3.amazonaws.com/firmware/stm32/stm32-sensor-v2.1.0.bin
I (12402) STM32_OTA: Size: 245760 bytes
I (12403) STM32_OTA: Version: 2.1.0
I (12404) STM32_OTA: Phase 1: Downloading firmware...

I (15234) HTTPS_DL: Starting HTTPS download from: https://...
I (17890) HTTPS_DL: Download complete: 245760 bytes
I (17891) STM32_OTA: Firmware downloaded: 245760 bytes

I (17892) STM32_OTA: Phase 2: Verifying RSA signature...
I (17893) SIG_VERIFY: Verifying RSA signature for 245760 bytes of firmware
I (17894) SIG_VERIFY: Decoded signature: 256 bytes
I (18125) SIG_VERIFY: ✓ RSA signature verification successful
I (18126) STM32_OTA: ✓ RSA signature verification successful

I (18127) STM32_OTA: Phase 3: Transferring firmware to STM32...
W (18128) STM32_OTA: UART transfer not yet implemented
I (18129) STM32_OTA: STM32 OTA completed successfully
```

---

## 7. Troubleshooting

### Error: "Failed to parse RSA public key"

**Cause:** Public key format is incorrect in `stm32_public_key.h`

**Fix:**
1. Regenerate keys using `generate_rsa_keys.bat` or `.sh`
2. Verify public key PEM format:
   ```bash
   cat ESP32/keys/stm32_public_key.pem
   ```
   Should start with `-----BEGIN PUBLIC KEY-----`
3. Rebuild and reflash ESP32

---

### Error: "RSA signature verification failed"

**Cause:** Signature doesn't match firmware (wrong key, corrupted download)

**Possible fixes:**
1. Ensure you're using the same private key for signing that matches the public key on ESP32
2. Check firmware wasn't corrupted during upload/download
3. Verify SHA256 checksum matches:
   ```bash
   sha256sum stm32-sensor-v2.1.0.bin
   ```

---

### Error: "Invalid signature length"

**Cause:** Signature is not 256 bytes (wrong algorithm or truncated)

**Fix:**
1. Verify Python script is using RSA-2048 (not RSA-3072 or ED25519)
2. Check base64 encoding is correct
3. Signature should be exactly 344 characters in base64 (256 bytes * 4/3)

---

### No MQTT notification received

**Cause:** MQTT connection failed or wrong topic

**Debug:**
1. Check MQTT broker is accessible:
   ```bash
   mosquitto_sub -h broker.hivemq.com -t "gateway/stm32/ota/notify" -v
   ```
2. Verify ESP32 subscribed to correct topic (check logs for "Subscribed to MQTT topic")
3. Check WiFi and MQTT connection status on ESP32

---

## 8. Security Best Practices

### Private Key Security

✅ **DO:**
- Keep `stm32_private_key.pem` in secure location
- Back up to encrypted storage
- Use different keys for development vs production
- Rotate keys periodically (e.g., yearly)

❌ **DON'T:**
- Commit private key to git (already in .gitignore)
- Share private key via email/Slack
- Store in cloud without encryption
- Use same key for multiple products

### Verification Checklist

Before deploying to production:

- [ ] Generated unique RSA keypair
- [ ] Backed up private key securely
- [ ] Embedded correct public key in ESP32
- [ ] Tested signature verification with known-good firmware
- [ ] Tested signature verification rejects tampered firmware
- [ ] Configured HTTPS URLs (not HTTP)
- [ ] Set appropriate AWS S3 bucket permissions

---

## 9. File Reference

### Modified ESP32 Files

| File | Change | Purpose |
|------|--------|---------|
| `serv_signature_verify.h/c` | ED25519 → RSA-2048 | Signature verification service |
| `stm32_public_key.h` | Embedded RSA public key | Key for verification |
| `cont_stm32_ota_manager.h/c` | Added `target` field, RSA signature | OTA manager updates |
| `cont_ota_manager.c` | Check target=="esp32" | Distinguish ESP32/STM32 |

### Modified Python Files

| File | Change | Purpose |
|------|--------|---------|
| `upload_firmware.py` | RSA signing, target field | Upload and sign firmware |
| `generate_rsa_keys.sh/.bat` | NEW | Generate RSA keypair |

---

## 10. Next Steps

After successful signature verification:

1. **Implement UART transfer** (Phase 3 in `cont_stm32_ota_manager.c`)
2. **Add CRC32 verification** before UART transfer
3. **Add SHA256 verification** as additional check
4. **Test with actual STM32 hardware**
5. **Implement rollback mechanism** if STM32 update fails

---

## Questions?

- Check ESP32 logs: `idf.py monitor`
- Review MQTT payload format above
- Verify AWS S3 bucket permissions
- Test signature locally before uploading

**Ready to test!** 🚀
