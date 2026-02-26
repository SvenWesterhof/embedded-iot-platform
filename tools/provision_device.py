#!/usr/bin/env python3
"""
Device Provisioning Script

Writes the IoT device private key into the ESP32's NVS partition.
Run once per physical device before deployment.

The private key is stored in NVS namespace 'iot_creds', key 'device_key'.
It is never embedded in the firmware binary — the binary is safe to publish.

Usage:
    python tools/provision_device.py --port COM3
    python tools/provision_device.py --port /dev/ttyUSB0 --key ESP32/keys/iot/device-private-key.pem
    python tools/provision_device.py --generate-only   # Generates NVS binary without flashing
"""

import argparse
import csv
import json
import os
import shutil
import subprocess
import sys
import tempfile
from pathlib import Path

# Defaults
REPO_ROOT       = Path(__file__).parent.parent
DEFAULT_KEY     = REPO_ROOT / 'ESP32' / 'keys' / 'iot' / 'device-private-key.pem'
NVS_PARTITION_OFFSET = '0x9000'  # From partitions_ota.csv
NVS_PARTITION_SIZE   = '0x6000'  # 24KB


def get_idf_path() -> str | None:
    """Resolve ESP-IDF root directory from env var or build artifacts."""
    # 1. IDF_PATH env var (set when running from ESP-IDF terminal)
    idf_path = os.environ.get('IDF_PATH')
    if idf_path:
        return idf_path

    # 2. Fallback: parse build/project_description.json (written by idf.py build)
    project_desc = REPO_ROOT / 'ESP32' / 'build' / 'project_description.json'
    if project_desc.exists():
        try:
            with open(project_desc) as f:
                return json.load(f).get('idf_path')
        except Exception:
            pass

    return None


def find_idf_tool(script_name: str, idf_subpath: str) -> str:
    """
    Find an ESP-IDF Python script.
    Search order:
      1. PATH (works when running from an ESP-IDF terminal)
      2. $IDF_PATH / idf_subpath
      3. build/project_description.json → idf_path / idf_subpath
    Exits with a clear message if not found.
    """
    # 1. PATH
    found = shutil.which(script_name)
    if found:
        return found

    # 2 & 3. IDF installation
    idf_path = get_idf_path()
    if idf_path:
        candidate = Path(idf_path) / idf_subpath
        if candidate.exists():
            return str(candidate)

    print(f"Error: {script_name} not found.")
    print("  Fix (choose one):")
    print("  A) Run this script from an ESP-IDF terminal (after export.ps1 / export.sh)")
    print("  B) Build the ESP32 firmware once first:  cd ESP32 && idf.py build")
    print("  C) Set IDF_PATH environment variable to your ESP-IDF installation")
    sys.exit(1)


def generate_nvs_binary(key_pem_path: Path, output_path: Path, broker_uri: str | None = None) -> None:
    """
    Use nvs_partition_gen.py (bundled with ESP-IDF) to create an NVS partition
    binary containing the device private key and optionally the MQTT broker URI.
    """
    key_pem = key_pem_path.read_bytes()

    # nvs_partition_gen.py takes a CSV describing the namespace/key/value layout.
    # type='file' + encoding='binary': the tool reads the file at the given path
    # and stores its raw bytes as an NVS blob entry.
    with tempfile.NamedTemporaryFile(mode='w', suffix='.csv', delete=False) as csv_file:
        csv_path = csv_file.name
        writer = csv.writer(csv_file)
        writer.writerow(['key', 'type', 'encoding', 'value'])
        writer.writerow(['iot_creds', 'namespace', '', ''])
        writer.writerow(['device_key', 'file', 'binary', str(key_pem_path.resolve())])
        if broker_uri:
            writer.writerow(['broker_uri', 'data', 'string', broker_uri])

    try:
        nvs_gen = find_idf_tool(
            'nvs_partition_gen.py',
            'components/nvs_flash/nvs_partition_generator/nvs_partition_gen.py'
        )

        cmd = [
            sys.executable, nvs_gen,
            'generate',
            csv_path,
            str(output_path),
            NVS_PARTITION_SIZE,
        ]

        print(f"Generating NVS partition binary...")
        print(f"  Tool: {nvs_gen}")
        print(f"  Key:  {key_pem_path} ({len(key_pem)} bytes)")
        print(f"  Out:  {output_path}")

        result = subprocess.run(cmd, capture_output=True, text=True)
        if result.returncode != 0:
            print(f"Error: nvs_partition_gen.py failed:")
            print(result.stderr)
            sys.exit(1)

        print(f"  NVS binary generated: {output_path} ({output_path.stat().st_size} bytes)")

    finally:
        os.unlink(csv_path)


def flash_nvs_binary(nvs_bin_path: Path, port: str, baud: int = 921600) -> None:
    """Flash the NVS partition binary to the device using esptool."""
    esptool = find_idf_tool('esptool.py', 'components/esptool_py/esptool/esptool.py')

    cmd = [
        sys.executable, esptool,
        '--port', port,
        '--baud', str(baud),
        'write_flash',
        NVS_PARTITION_OFFSET,
        str(nvs_bin_path),
    ]

    print(f"\nFlashing NVS partition to device...")
    print(f"  Port:   {port}")
    print(f"  Offset: {NVS_PARTITION_OFFSET}")
    print(f"  Baud:   {baud}")

    result = subprocess.run(cmd)
    if result.returncode != 0:
        print("Error: esptool.py flash failed")
        sys.exit(1)

    print("\n[OK] Device provisioned successfully.")
    print("     The private key is now in NVS — it is NOT in the firmware binary.")


def main():
    parser = argparse.ArgumentParser(
        description='Provision ESP32 IoT device with private key via NVS',
        formatter_class=argparse.RawDescriptionHelpFormatter,
        epilog="""
Examples:
  # Provision connected device (Windows)
  python tools/provision_device.py --port COM3

  # Provision connected device (Linux/macOS)
  python tools/provision_device.py --port /dev/ttyUSB0

  # Use a different key file
  python tools/provision_device.py --port COM3 --key path/to/device-private-key.pem

  # Generate NVS binary only (flash manually later)
  python tools/provision_device.py --generate-only
        """
    )
    parser.add_argument('--port', help='Serial port (e.g., COM3 or /dev/ttyUSB0)')
    parser.add_argument('--baud', type=int, default=921600,
                        help='Baud rate for flashing (default: 921600)')
    parser.add_argument('--key', type=Path, default=DEFAULT_KEY,
                        help=f'Path to device private key PEM (default: {DEFAULT_KEY})')
    parser.add_argument('--output', type=Path, default=Path('nvs_iot_creds.bin'),
                        help='Output path for NVS binary (default: nvs_iot_creds.bin)')
    parser.add_argument('--broker-uri', type=str,
                        help='MQTT broker URI (e.g., "mqtts://xxx-ats.iot.eu-west-1.amazonaws.com:8883")')
    parser.add_argument('--generate-only', action='store_true',
                        help='Generate NVS binary without flashing')
    args = parser.parse_args()

    # Validate
    if not args.generate_only and not args.port:
        parser.error("--port is required unless --generate-only is set")

    if not args.key.exists():
        print(f"Error: Key file not found: {args.key}")
        print(f"  Expected: {DEFAULT_KEY}")
        sys.exit(1)

    print("ESP32 IoT Device Provisioner")
    print("=" * 40)

    # Step 1: Generate NVS binary
    generate_nvs_binary(args.key, args.output, args.broker_uri)

    if args.generate_only:
        print(f"\nGenerate-only mode. Flash manually with:")
        print(f"  esptool.py --port <PORT> write_flash {NVS_PARTITION_OFFSET} {args.output}")
        return

    # Step 2: Flash to device
    flash_nvs_binary(args.output, args.port, args.baud)

    # Cleanup
    if args.output == Path('nvs_iot_creds.bin'):
        args.output.unlink(missing_ok=True)


if __name__ == '__main__':
    main()
