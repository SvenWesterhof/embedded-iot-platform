#!/usr/bin/env python3
"""
Firmware Upload and OTA Notification Script

Automates the firmware release process for dual-platform (ESP32 and STM32):
1. Uploads firmware binary to AWS S3
2. Calculates checksums (SHA256, CRC32)
3. Applies platform-specific signing (RSA-3072 for ESP32, ED25519 for STM32)
4. Updates firmware manifest
5. Sends MQTT notification to devices
"""

import os
import sys
import json
import hashlib
import argparse
import subprocess
import base64
import binascii
from datetime import datetime, timezone
from pathlib import Path

# Install dependencies: pip install boto3 paho-mqtt cryptography
import boto3
from botocore.client import Config
import paho.mqtt.client as mqtt
from cryptography.hazmat.primitives import serialization
from cryptography.hazmat.primitives.asymmetric.ed25519 import Ed25519PrivateKey
from cryptography.hazmat.backends import default_backend

# Configuration (override with environment variables)
# AWS S3 Configuration (default)
AWS_REGION = os.getenv('AWS_REGION', 'us-east-1')
AWS_ACCESS_KEY = os.getenv('AWS_ACCESS_KEY_ID')  # Uses AWS credentials from ~/.aws/credentials if not set
AWS_SECRET_KEY = os.getenv('AWS_SECRET_ACCESS_KEY')
S3_BUCKET = os.getenv('S3_BUCKET', 'sw-embedded-iot-platform-project-firmware')
S3_ENDPOINT = os.getenv('S3_ENDPOINT', None)  # Set to MinIO URL for local testing: http://localhost:9000

# MQTT Configuration
MQTT_BROKER = os.getenv('MQTT_BROKER', 'broker.hivemq.com')
MQTT_PORT = int(os.getenv('MQTT_PORT', '1883'))
MQTT_TOPIC_ESP32_NOTIFY = os.getenv('MQTT_TOPIC_ESP32_NOTIFY', 'gateway/ota/notify')
MQTT_TOPIC_STM32_NOTIFY = os.getenv('MQTT_TOPIC_STM32_NOTIFY', 'gateway/stm32/ota/notify')

# Signing Keys Configuration
# ESP32: RSA-3072 key (used by espsecure.py)
# STM32: ED25519 key (verified by ESP32 before forwarding to STM32)
ESP32_SIGNING_KEY = Path(__file__).parent.parent / 'ESP32' / 'keys' / 'ota_signing_key.pem'
STM32_SIGNING_KEY = Path(__file__).parent.parent / 'ESP32' / 'keys' / 'stm32_signing_key.pem'

def extract_version_from_cmake():
    """Extract firmware version from ESP32/CMakeLists.txt"""
    # Look for CMakeLists.txt in ESP32 directory
    cmake_path = Path(__file__).parent.parent / 'ESP32' / 'CMakeLists.txt'

    if not cmake_path.exists():
        return None

    try:
        with open(cmake_path, 'r') as f:
            for line in f:
                # Look for: set(PROJECT_VER "1.2.3")
                if 'PROJECT_VER' in line and 'set(' in line:
                    # Extract version string between quotes
                    import re
                    match = re.search(r'"([0-9]+\.[0-9]+\.[0-9]+)"', line)
                    if match:
                        return match.group(1)
    except Exception as e:
        print(f"Warning: Could not extract version from CMakeLists.txt: {e}")

    return None

def detect_platform_from_filename(file_path):
    """Detect platform from firmware filename

    Convention:
    - esp32-*.bin -> ESP32
    - stm32-*.bin -> STM32

    Returns: 'esp32', 'stm32', or None if cannot detect
    """
    filename = Path(file_path).name.lower()
    if filename.startswith('esp32'):
        return 'esp32'
    elif filename.startswith('stm32'):
        return 'stm32'
    return None

def calculate_sha256(file_path):
    """Calculate SHA256 checksum of file"""
    sha256_hash = hashlib.sha256()
    with open(file_path, "rb") as f:
        for byte_block in iter(lambda: f.read(4096), b""):
            sha256_hash.update(byte_block)
    return sha256_hash.hexdigest()

def calculate_crc32(file_path):
    """Calculate CRC32 checksum of file (for STM32)"""
    crc = 0
    with open(file_path, "rb") as f:
        for byte_block in iter(lambda: f.read(4096), b""):
            crc = binascii.crc32(byte_block, crc)
    return crc & 0xffffffff  # Ensure unsigned 32-bit

def sign_esp32_firmware(file_path, key_path):
    """Sign ESP32 firmware using espsecure.py (RSA-3072)

    Automatically finds espsecure.py from ESP-IDF installation
    Returns: signature info string, or None if signing fails
    """
    if not key_path.exists():
        print(f"Warning: ESP32 signing key not found: {key_path}")
        print("  ESP32 firmware signing skipped")
        return None

    # Try to find espsecure.py
    espsecure_path = 'espsecure.py'  # Try PATH first

    # If not in PATH, try to find from ESP32 build directory
    try:
        project_desc = Path(__file__).parent.parent / 'ESP32' / 'build' / 'project_description.json'
        if project_desc.exists():
            with open(project_desc) as f:
                desc = json.load(f)
                idf_path = desc.get('idf_path')
                if idf_path:
                    # espsecure.py is in components/esptool_py/esptool/
                    espsecure_full = Path(idf_path) / 'components' / 'esptool_py' / 'esptool' / 'espsecure.py'
                    if espsecure_full.exists():
                        espsecure_path = str(espsecure_full)
                        print(f"  Using espsecure.py from: {idf_path}")
    except Exception as e:
        pass  # Fall back to PATH version

    # Create output signed file
    signed_file = Path(file_path).parent / f"{Path(file_path).stem}_signed.bin"

    try:
        # Try running as Python module first (if esptool is installed)
        cmd = [
            sys.executable, '-m', 'espsecure', 'sign_data',
            '--version', '2',
            '--keyfile', str(key_path),
            '--output', str(signed_file),
            str(file_path)
        ]

        result = subprocess.run(
            cmd,
            capture_output=True,
            text=True,
            check=False  # Don't raise on error, we'll check manually
        )

        # If module approach failed, try direct script execution
        if result.returncode != 0 and espsecure_path != 'espsecure.py':
            cmd = [
                sys.executable, espsecure_path, 'sign_data',
                '--version', '2',
                '--keyfile', str(key_path),
                '--output', str(signed_file),
                str(file_path)
            ]
            result = subprocess.run(
                cmd,
                capture_output=True,
                text=True,
                check=True
            )
        elif result.returncode != 0:
            raise subprocess.CalledProcessError(result.returncode, cmd, result.stdout, result.stderr)

        print(f"  ESP32 firmware signed successfully")
        print(f"  Signed file: {signed_file}")

        # For ESP32, the signature is embedded in the binary
        # We could upload the signed binary instead of the original
        return f"RSA-3072 signature embedded (signed file: {signed_file.name})"

    except FileNotFoundError:
        print(f"Warning: espsecure.py not found")
        print(f"  Searched in PATH and ESP-IDF installation")
        print(f"  ESP-IDF tools must be installed")
        return None
    except subprocess.CalledProcessError as e:
        print(f"Warning: ESP32 signing failed: {e}")
        if e.stderr:
            print(f"  stderr: {e.stderr}")
        return None

def sign_stm32_firmware(file_path, key_path):
    """Sign STM32 firmware using ED25519

    Args:
        file_path: Path to firmware binary
        key_path: Path to ED25519 private key (PEM format)

    Returns: signature as base64 string (64 bytes encoded)
    """
    if not key_path.exists():
        raise FileNotFoundError(f"STM32 signing key not found: {key_path}")

    # Load ED25519 private key
    with open(key_path, 'rb') as f:
        private_key = serialization.load_pem_private_key(
            f.read(),
            password=None,
            backend=default_backend()
        )

    # Read firmware data
    with open(file_path, 'rb') as f:
        firmware_data = f.read()

    # Sign the firmware
    signature = private_key.sign(firmware_data)

    # Return as base64 for JSON storage
    return base64.b64encode(signature).decode('ascii')

def verify_stm32_signature(file_path, signature_b64, public_key_path):
    """Verify STM32 firmware signature (for testing)

    Args:
        file_path: Path to firmware binary
        signature_b64: Base64-encoded signature
        public_key_path: Path to ED25519 public key

    Returns: True if valid, False otherwise
    """
    from cryptography.hazmat.primitives.asymmetric.ed25519 import Ed25519PublicKey
    from cryptography.exceptions import InvalidSignature

    with open(public_key_path, 'rb') as f:
        public_key = serialization.load_pem_public_key(
            f.read(),
            backend=default_backend()
        )

    with open(file_path, 'rb') as f:
        firmware_data = f.read()

    signature = base64.b64decode(signature_b64)

    try:
        public_key.verify(signature, firmware_data)
        return True
    except InvalidSignature:
        return False

def upload_to_storage(file_path, version, platform):
    """Upload firmware to AWS S3 or S3-compatible storage

    Args:
        file_path: Path to firmware binary
        version: Firmware version string
        platform: 'esp32' or 'stm32'

    Returns: (download_url, object_key)
    """
    print(f"Uploading {platform.upper()} firmware to S3...")

    # Initialize S3 client
    # If S3_ENDPOINT is set, use it (for MinIO/local testing)
    # Otherwise, use AWS S3 with credentials from environment or ~/.aws/credentials
    client_config = {
        'config': Config(signature_version='s3v4'),
        'region_name': AWS_REGION
    }

    if S3_ENDPOINT:
        # Local MinIO/S3-compatible storage
        client_config['endpoint_url'] = S3_ENDPOINT
        client_config['aws_access_key_id'] = AWS_ACCESS_KEY
        client_config['aws_secret_access_key'] = AWS_SECRET_KEY
        print(f"Using S3-compatible endpoint: {S3_ENDPOINT}")
    elif AWS_ACCESS_KEY and AWS_SECRET_KEY:
        # AWS S3 with explicit credentials
        client_config['aws_access_key_id'] = AWS_ACCESS_KEY
        client_config['aws_secret_access_key'] = AWS_SECRET_KEY
        print(f"Using AWS S3 in region: {AWS_REGION}")
    else:
        # AWS S3 with default credentials from ~/.aws/credentials
        print(f"Using AWS S3 with default credentials in region: {AWS_REGION}")

    s3_client = boto3.client('s3', **client_config)

    # Create bucket if it doesn't exist (for new setups)
    try:
        s3_client.head_bucket(Bucket=S3_BUCKET)
    except:
        print(f"Creating bucket: {S3_BUCKET}")
        try:
            # All regions except us-east-1 require LocationConstraint
            s3_client.create_bucket(
                Bucket=S3_BUCKET,
                CreateBucketConfiguration={'LocationConstraint': AWS_REGION}
            )
        except Exception as e:
            print(f"Note: {e}")
            print(f"Using existing bucket or insufficient permissions to create")

    # Platform-specific object naming and S3 paths
    if platform == 'esp32':
        object_key = f'firmware/esp32/esp32-gateway-v{version}.bin'
    elif platform == 'stm32':
        object_key = f'firmware/stm32/stm32-sensor-v{version}.bin'
    else:
        raise ValueError(f"Unknown platform: {platform}")

    s3_client.upload_file(
        file_path,
        S3_BUCKET,
        object_key,
        ExtraArgs={
            'ContentType': 'application/octet-stream'
            # Public access is controlled by bucket policy, not ACLs
        }
    )

    # Generate public URL
    if S3_ENDPOINT:
        # MinIO/local endpoint
        url = f"{S3_ENDPOINT}/{S3_BUCKET}/{object_key}"
    else:
        # AWS S3 public URL (us-east-1 uses different format)
        if AWS_REGION == 'us-east-1':
            url = f"https://{S3_BUCKET}.s3.amazonaws.com/{object_key}"
        else:
            url = f"https://{S3_BUCKET}.s3.{AWS_REGION}.amazonaws.com/{object_key}"

    print(f"Uploaded: {url}")
    return url, object_key

def update_manifest(version, url, file_size, platform, checksums, signature_info, changelog):
    """Update firmware manifest JSON with v2.0 schema (local record-keeping only)

    Args:
        version: Firmware version
        url: Download URL
        file_size: File size in bytes
        platform: 'esp32' or 'stm32'
        checksums: Dict with 'sha256' and optionally 'crc32'
        signature_info: Dict with 'algorithm' and 'value'
        changelog: String or list of changelog entries

    Returns: Path to manifest file
    """
    manifest_path = Path(__file__).parent / 'firmware_manifest.json'

    # Load existing manifest or create new
    if manifest_path.exists():
        with open(manifest_path, 'r') as f:
            manifest = json.load(f)
        # Upgrade to v2.0 if needed
        if manifest.get('manifest_version') == '1.0':
            manifest['manifest_version'] = '2.0'
            # Add platform field to existing releases (assume esp32)
            for release in manifest.get('firmware_releases', []):
                if 'platform' not in release:
                    release['platform'] = 'esp32'
    else:
        manifest = {
            'manifest_version': '2.0',
            'firmware_releases': []
        }

    # Build release entry with v2.0 schema
    release = {
        'platform': platform,
        'version': version,
        'release_date': datetime.now(timezone.utc).strftime('%Y-%m-%dT%H:%M:%SZ'),
        'download_url': url,
        'file_size': file_size,
        'checksum': checksums,
        'signature': signature_info,
        'changelog': changelog if isinstance(changelog, list) else [changelog]
    }

    # Add STM32-specific metadata if applicable
    if platform == 'stm32':
        release['target_hardware'] = 'STM32F767'

    manifest['firmware_releases'].insert(0, release)

    # Save manifest (local only - not uploaded to S3)
    with open(manifest_path, 'w') as f:
        json.dump(manifest, f, indent=2)

    print(f"Manifest updated: {manifest_path}")
    return manifest_path

def send_mqtt_notification(version, url, file_size, platform, checksums, signature_b64=None, auto_reboot=False):
    """Send OTA notification via MQTT to platform-specific topic

    Args:
        version: Firmware version
        url: Download URL
        file_size: File size in bytes
        platform: 'esp32' or 'stm32'
        checksums: Dict with 'sha256' and optionally 'crc32'
        signature_b64: Base64-encoded signature (for STM32)
        auto_reboot: Auto-reboot flag (ESP32 only)

    Returns: True if successful, False otherwise
    """
    print(f"Sending MQTT notification to {MQTT_BROKER}...")

    # Platform-specific topic selection
    if platform == 'esp32':
        topic = MQTT_TOPIC_ESP32_NOTIFY
        notification = {
            'version': version,
            'url': url,
            'size': file_size,
            'sha256': checksums['sha256'],
            'auto_reboot': auto_reboot
        }
    elif platform == 'stm32':
        topic = MQTT_TOPIC_STM32_NOTIFY
        notification = {
            'target': 'stm32',
            'version': version,
            'url': url,
            'size': file_size,
            'sha256': checksums['sha256'],
            'crc32': f"0x{checksums['crc32']:08X}",
            'auto_apply': auto_reboot  # STM32 uses 'auto_apply' instead of 'auto_reboot'
        }
        # Add ED25519 signature if available
        if signature_b64:
            notification['signature_ed25519'] = signature_b64
    else:
        print(f"Error: Unknown platform: {platform}")
        return False

    payload = json.dumps(notification)

    # Connect and publish
    client = mqtt.Client(callback_api_version=mqtt.CallbackAPIVersion.VERSION2)

    def on_connect(client, userdata, flags, rc):
        if rc == 0:
            print(f"Connected to MQTT broker")
            client.publish(topic, payload, qos=1)
            print(f"Published to topic: {topic}")
            print(f"   Payload: {payload}")
        else:
            print(f"MQTT connection failed: {rc}")

    def on_publish(client, userdata, mid):
        print(f"Message published successfully")
        client.disconnect()

    client.on_connect = on_connect
    client.on_publish = on_publish

    try:
        client.connect(MQTT_BROKER, MQTT_PORT, 60)
        client.loop_forever(timeout=10)
    except Exception as e:
        print(f"MQTT error: {e}")
        return False

    return True

def main():
    parser = argparse.ArgumentParser(
        description='Upload firmware and notify devices via MQTT (supports ESP32 and STM32)',
        formatter_class=argparse.RawDescriptionHelpFormatter,
        epilog="""
Examples:
  # ESP32 firmware (auto-detect from filename)
  %(prog)s esp32-gateway-v1.2.0.bin --version 1.2.0

  # STM32 firmware with explicit platform
  %(prog)s stm32-sensor-v2.1.0.bin --platform stm32 --version 2.1.0

  # Upload only without notification
  %(prog)s firmware.bin --platform esp32 --version 1.2.0 --no-notify
        """
    )
    parser.add_argument('binary', help='Path to firmware binary (.bin file)')
    parser.add_argument('--platform', choices=['esp32', 'stm32'],
                       help='Target platform (auto-detected from filename if not specified)')
    parser.add_argument('--version', help='Firmware version (e.g., 1.1.0). If not provided, extracts from CMakeLists.txt')
    parser.add_argument('--changelog', default='Firmware update', help='Release notes')
    parser.add_argument('--auto-reboot', action='store_true',
                       help='Auto-reboot/apply after update (ESP32: reboot, STM32: auto-apply)')
    parser.add_argument('--no-notify', action='store_true', help='Skip MQTT notification (upload only)')

    args = parser.parse_args()

    # Validate binary file
    if not os.path.exists(args.binary):
        print(f"Error: File not found: {args.binary}")
        sys.exit(1)

    # Determine platform
    platform = args.platform
    if not platform:
        platform = detect_platform_from_filename(args.binary)
        if platform:
            print(f"Auto-detected platform from filename: {platform.upper()}")
        else:
            print("Error: Could not detect platform from filename")
            print("Please use --platform esp32 or --platform stm32")
            sys.exit(1)

    # Auto-extract version if not provided
    version = args.version
    if not version:
        version = extract_version_from_cmake()
        if version:
            print(f"Auto-detected version from CMakeLists.txt: {version}")
        else:
            print("Error: Could not extract version from CMakeLists.txt")
            print("Please provide version manually with --version")
            sys.exit(1)

    file_size = os.path.getsize(args.binary)

    # Validate file size constraints
    if platform == 'esp32' and file_size > 4 * 1024 * 1024:
        print(f"Warning: ESP32 firmware size ({file_size/1024/1024:.2f} MB) exceeds typical 4MB partition limit")
    elif platform == 'stm32' and file_size > 1.5 * 1024 * 1024:
        print(f"Error: STM32 firmware size ({file_size/1024/1024:.2f} MB) exceeds 1.5MB flash limit")
        sys.exit(1)

    print(f"\nDual-Platform Firmware Release Tool")
    print(f"Platform: {platform.upper()}")
    print(f"Version: {version}")
    print(f"Binary: {args.binary}")
    print(f"Size: {file_size:,} bytes ({file_size/1024/1024:.2f} MB)")
    print()

    # Step 1: Calculate checksums
    print("Calculating checksums...")
    sha256 = calculate_sha256(args.binary)
    print(f"  SHA256: {sha256}")

    checksums = {'sha256': sha256}
    if platform == 'stm32':
        crc32 = calculate_crc32(args.binary)
        checksums['crc32'] = crc32
        print(f"  CRC32:  0x{crc32:08X}")

    # Step 2: Sign firmware (platform-specific)
    signature_info = {}
    signature_b64 = None

    if platform == 'esp32':
        print("\nSigning ESP32 firmware (RSA-3072)...")
        sig_result = sign_esp32_firmware(args.binary, ESP32_SIGNING_KEY)
        if sig_result:
            signature_info = {
                'algorithm': 'RSA-3072',
                'value': sig_result
            }
        else:
            print("Warning: Continuing without signature")
            signature_info = {
                'algorithm': 'none',
                'value': None
            }

    elif platform == 'stm32':
        print("\nSigning STM32 firmware (ED25519)...")
        try:
            signature_b64 = sign_stm32_firmware(args.binary, STM32_SIGNING_KEY)
            signature_info = {
                'algorithm': 'ED25519',
                'value': signature_b64
            }
            print(f"  Signature: {signature_b64[:32]}... ({len(signature_b64)} chars)")

            # Verify signature for sanity check
            public_key_path = STM32_SIGNING_KEY.parent / 'stm32_public.pem'
            if public_key_path.exists():
                if verify_stm32_signature(args.binary, signature_b64, public_key_path):
                    print("  Signature verified successfully")
                else:
                    print("  ERROR: Signature verification failed!")
                    sys.exit(1)
        except Exception as e:
            print(f"Error: STM32 signing failed: {e}")
            sys.exit(1)

    # Step 3: Upload to storage
    print()
    url, object_key = upload_to_storage(args.binary, version, platform)

    # Step 4: Update manifest
    manifest_path = update_manifest(version, url, file_size, platform,
                                    checksums, signature_info, args.changelog)

    # Step 5: Send MQTT notification
    if not args.no_notify:
        print()
        send_mqtt_notification(version, url, file_size, platform, checksums,
                              signature_b64, args.auto_reboot)
    else:
        print("\nSkipping MQTT notification (--no-notify)")

    print()
    print("=" * 60)
    print("Firmware release complete!")
    print("=" * 60)
    print(f"Platform: {platform.upper()}")
    print(f"Version:  {version}")
    print(f"URL:      {url}")
    print(f"Manifest: {manifest_path}")
    print()

    if not args.no_notify:
        print(f"Devices will receive notification on topic: {MQTT_TOPIC_ESP32_NOTIFY if platform == 'esp32' else MQTT_TOPIC_STM32_NOTIFY}")
    else:
        print("Upload complete. Run without --no-notify to notify devices.")

if __name__ == '__main__':
    main()
