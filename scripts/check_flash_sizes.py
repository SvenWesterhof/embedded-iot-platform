#!/usr/bin/env python3
"""Validate firmware binary sizes against flash partition limits.

Usage:
  python3 scripts/check_flash_sizes.py esp32 <binary> [--partition-csv <csv>]
  python3 scripts/check_flash_sizes.py stm32 <app_binary> [--boot-binary <bin>]

Exit codes:
  0  All binaries fit within their flash limits
  1  One or more binaries exceed their flash limit
  2  Script error (missing file, bad arguments)

Prints GitHub Actions warning/error annotations when run in CI.
"""

import sys
import os
import argparse

# STM32 flash layout (from MEMORY.md / STM32F767 dual-bank configuration).
# nDBANK=0 (dual-bank enabled), nDBOOT=1 (always boot Bank 1).
STM32_LIMITS = {
    "bootloader":  0x00008000,  # 32 KB  — Bank 1 sectors 0-1 (0x08000000-0x08007FFF)
    "application": 0x000F8000,  # 992 KB — Bank 1 sectors 2-11 (0x08008000-0x080FFFFF)
}

# Warn when a partition is this percentage full.
WARN_THRESHOLD_PCT = 90.0
IN_CI = "GITHUB_ACTIONS" in os.environ


def _ci_warning(msg: str) -> None:
    if IN_CI:
        print(f"::warning::{msg}")


def _ci_error(msg: str) -> None:
    if IN_CI:
        print(f"::error::{msg}")


def _progress_bar(size: int, limit: int, width: int = 30) -> str:
    pct = size / limit * 100
    filled = int(width * min(pct, 100) / 100)
    bar = "█" * filled + "░" * (width - filled)
    return f"[{bar}] {pct:5.1f}%"


def _print_row(label: str, size: int, limit: int, status: str) -> None:
    bar = _progress_bar(size, limit)
    print(f"  [{status}] {label:<16} {size:>9,} / {limit:>9,} bytes  {bar}")


def check_esp32(binary_path: str, partition_csv: str) -> list[str]:
    """Parse partition table CSV and verify the binary fits in every app partition."""
    for path, label in [(binary_path, "binary"), (partition_csv, "partition CSV")]:
        if not os.path.isfile(path):
            msg = f"{label} not found: {path}"
            _ci_error(msg)
            print(f"  ERROR: {msg}", file=sys.stderr)
            sys.exit(2)

    binary_size = os.path.getsize(binary_path)
    errors: list[str] = []

    with open(partition_csv) as f:
        for raw in f:
            line = raw.strip()
            if not line or line.startswith("#"):
                continue
            parts = [p.strip() for p in line.split(",")]
            if len(parts) < 5 or parts[1] != "app":
                continue
            name = parts[0]
            size_str = parts[4]
            try:
                limit = int(size_str, 16) if size_str.startswith("0x") else int(size_str)
            except ValueError:
                continue

            pct = binary_size / limit * 100
            if binary_size > limit:
                status = "FAIL"
                msg = (
                    f"Flash overflow: {os.path.basename(binary_path)} "
                    f"({binary_size:,} B) exceeds '{name}' partition ({limit:,} B)"
                )
                errors.append(msg)
                _ci_error(msg)
            elif pct >= WARN_THRESHOLD_PCT:
                status = "WARN"
                _ci_warning(
                    f"ESP32 '{name}' partition is {pct:.1f}% full "
                    f"({binary_size:,} / {limit:,} B) — approaching limit"
                )
            else:
                status = " OK "

            _print_row(f"esp32/{name}", binary_size, limit, status)

    return errors


def check_stm32(app_binary: str, boot_binary: str) -> list[str]:
    """Verify STM32 app and bootloader binaries fit within known flash regions."""
    targets = []
    if app_binary:
        targets.append(("application", app_binary, STM32_LIMITS["application"]))
    if boot_binary:
        targets.append(("bootloader", boot_binary, STM32_LIMITS["bootloader"]))

    errors: list[str] = []

    for region, path, limit in targets:
        if not os.path.isfile(path):
            print(f"  [SKIP] stm32/{region:<12} file not found: {path}")
            continue

        size = os.path.getsize(path)
        pct = size / limit * 100

        if size > limit:
            status = "FAIL"
            msg = (
                f"Flash overflow: {os.path.basename(path)} "
                f"({size:,} B) exceeds '{region}' limit ({limit:,} B)"
            )
            errors.append(msg)
            _ci_error(msg)
        elif pct >= WARN_THRESHOLD_PCT:
            status = "WARN"
            _ci_warning(
                f"STM32 '{region}' is {pct:.1f}% full "
                f"({size:,} / {limit:,} B) — approaching limit"
            )
        else:
            status = " OK "

        _print_row(f"stm32/{region}", size, limit, status)

    return errors


def main() -> None:
    parser = argparse.ArgumentParser(
        description="Validate firmware binary sizes against flash partition limits."
    )
    sub = parser.add_subparsers(dest="platform", required=True)

    esp = sub.add_parser("esp32", help="Check ESP32 firmware")
    esp.add_argument("binary", help="Path to iot_gateway.bin")
    esp.add_argument(
        "--partition-csv",
        default="ESP32/partitions_ota.csv",
        help="Partition table CSV (default: ESP32/partitions_ota.csv)",
    )

    stm = sub.add_parser("stm32", help="Check STM32 firmware")
    stm.add_argument("app_binary", help="Path to sensor_node.bin")
    stm.add_argument(
        "--boot-binary",
        default="",
        help="Path to bootloader.bin (optional but recommended)",
    )

    args = parser.parse_args()

    print(f"\nFlash size validation — {args.platform.upper()}")
    print("─" * 72)

    if args.platform == "esp32":
        errors = check_esp32(args.binary, args.partition_csv)
    else:
        errors = check_stm32(args.app_binary, args.boot_binary)

    print("─" * 72)

    if errors:
        print(f"\n✗ {len(errors)} flash overflow error(s) detected:")
        for e in errors:
            print(f"  • {e}")
        sys.exit(1)
    else:
        print("\n✓ All binaries fit within flash limits")
        sys.exit(0)


if __name__ == "__main__":
    main()
