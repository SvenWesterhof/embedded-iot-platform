#!/usr/bin/env python3
"""
Firmware Upload and OTA Notification Script

Automates the firmware release process:
1. Uploads firmware binary to AWS S3
2. Calculates SHA256 checksum
3. Updates firmware manifest
4. Sends MQTT notification to devices
"""

import os
import sys
import json
import hashlib
import argparse
from datetime import datetime, timezone
from pathlib import Path

# Install dependencies: pip install boto3 paho-mqtt
import boto3
from botocore.client import Config
import paho.mqtt.client as mqtt

# Configuration (override with environment variables)
# AWS S3 Configuration (default)
AWS_REGION = os.getenv('AWS_REGION', 'us-east-1')
AWS_ACCESS_KEY = os.getenv('AWS_ACCESS_KEY_ID')  # Uses AWS credentials from ~/.aws/credentials if not set
AWS_SECRET_KEY = os.getenv('AWS_SECRET_ACCESS_KEY')
S3_BUCKET = os.getenv('S3_BUCKET', 'sw-embedded-iot-platform-project-firmware')
S3_ENDPOINT = os.getenv('S3_ENDPOINT', None)  # Set to MinIO URL for local testing: http://localhost:9000

MQTT_BROKER = os.getenv('MQTT_BROKER', 'broker.hivemq.com')
MQTT_PORT = int(os.getenv('MQTT_PORT', '1883'))
MQTT_TOPIC_NOTIFY = os.getenv('MQTT_TOPIC_NOTIFY', 'gateway/ota/notify')

def extract_version_from_cmake():
    """Extract firmware version from CMakeLists.txt"""
    # Look for CMakeLists.txt in parent directory of tools/
    cmake_path = Path(__file__).parent.parent / 'CMakeLists.txt'

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

def calculate_sha256(file_path):
    """Calculate SHA256 checksum of file"""
    sha256_hash = hashlib.sha256()
    with open(file_path, "rb") as f:
        for byte_block in iter(lambda: f.read(4096), b""):
            sha256_hash.update(byte_block)
    return sha256_hash.hexdigest()

def upload_to_storage(file_path, version):
    """Upload firmware to AWS S3 or S3-compatible storage"""
    print(f"Uploading firmware to S3...")

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

    # Upload file with public-read ACL for firmware downloads
    object_key = f'esp32-gateway-v{version}.bin'
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

def update_manifest(version, url, file_size, checksum, changelog):
    """Update firmware manifest JSON"""
    manifest_path = Path(__file__).parent / 'firmware_manifest.json'

    # Load existing manifest or create new
    if manifest_path.exists():
        with open(manifest_path, 'r') as f:
            manifest = json.load(f)
    else:
        manifest = {
            'manifest_version': '1.0',
            'firmware_releases': []
        }

    # Add new release at the beginning
    release = {
        'version': version,
        'release_date': datetime.now(timezone.utc).strftime('%Y-%m-%dT%H:%M:%SZ'),
        'download_url': url,
        'file_size': file_size,
        'checksum': {
            'sha256': checksum
        },
        'changelog': changelog if isinstance(changelog, list) else [changelog]
    }

    manifest['firmware_releases'].insert(0, release)

    # Save manifest
    with open(manifest_path, 'w') as f:
        json.dump(manifest, f, indent=2)

    print(f"Manifest updated: {manifest_path}")
    return manifest_path

def send_mqtt_notification(version, url, file_size, checksum, auto_reboot=False):
    """Send OTA notification via MQTT"""
    print(f"Sending MQTT notification to {MQTT_BROKER}...")

    notification = {
        'version': version,
        'url': url,
        'size': file_size,
        'sha256': checksum,
        'auto_reboot': auto_reboot
    }

    payload = json.dumps(notification)

    # Connect and publish
    client = mqtt.Client(callback_api_version=mqtt.CallbackAPIVersion.VERSION2)

    def on_connect(client, userdata, flags, rc):
        if rc == 0:
            print(f"Connected to MQTT broker")
            client.publish(MQTT_TOPIC_NOTIFY, payload, qos=1)
            print(f"Published to topic: {MQTT_TOPIC_NOTIFY}")
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
    parser = argparse.ArgumentParser(description='Upload firmware and notify devices via MQTT')
    parser.add_argument('binary', help='Path to firmware binary (.bin file)')
    parser.add_argument('--version', help='Firmware version (e.g., 1.1.0). If not provided, extracts from CMakeLists.txt')
    parser.add_argument('--changelog', default='Firmware update', help='Release notes')
    parser.add_argument('--auto-reboot', action='store_true', help='Auto-reboot devices after update')
    parser.add_argument('--no-notify', action='store_true', help='Skip MQTT notification (upload only)')

    args = parser.parse_args()

    # Validate binary file
    if not os.path.exists(args.binary):
        print(f"Error: File not found: {args.binary}")
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
    print(f"\nFirmware Release Tool")
    print(f"Version: {version}")
    print(f"Binary: {args.binary}")
    print(f"Size: {file_size:,} bytes ({file_size/1024/1024:.2f} MB)")
    print()

    # Step 1: Calculate checksum
    print("Calculating SHA256 checksum...")
    checksum = calculate_sha256(args.binary)
    print(f"Checksum: {checksum}")

    # Step 2: Upload to storage
    url, object_key = upload_to_storage(args.binary, version)

    # Step 3: Update manifest
    manifest_path = update_manifest(version, url, file_size, checksum, args.changelog)

    # Step 4: Send MQTT notification
    if not args.no_notify:
        send_mqtt_notification(version, url, file_size, checksum, args.auto_reboot)
    else:
        print("Skipping MQTT notification (--no-notify)")

    print()
    print("Firmware release complete!")
    print(f"URL: {url}")
    print(f"Manifest: {manifest_path}")
    print()

    if not args.no_notify:
        print("Devices will be notified and can download the update.")
    else:
        print("Upload complete. Run without --no-notify to notify devices.")

if __name__ == '__main__':
    main()
