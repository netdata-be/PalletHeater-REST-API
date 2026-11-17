#!/usr/bin/env python3
"""
BLE OTA upload for PalletHeater.

Usage:
    pip install bleak
    python ble_ota.py .pio/build/esp32doit-devkit-v1-ble-ota/firmware.bin

Protocol:
    CTRL write 0x01 + uint32LE(size)  → NOTIFY 0x01 ready | 0xFF error
    DATA write <chunk>                → NOTIFY 0x50 <pct%> every 4 KB
    CTRL write 0x02                   → NOTIFY 0x02 success (device reboots) | 0xFF error
    CTRL write 0x03                   → abort
"""

import asyncio
import struct
import sys
from bleak import BleakClient, BleakScanner

DEVICE_NAME  = "PalletHeater"
OTA_SVC_UUID = "a1b20010-1234-5678-abcd-ef0123456789"
CTRL_UUID    = "a1b20011-1234-5678-abcd-ef0123456789"
DATA_UUID    = "a1b20012-1234-5678-abcd-ef0123456789"
CHUNK_SIZE   = 512


async def ota_upload(firmware_path: str) -> None:
    with open(firmware_path, "rb") as f:
        firmware = f.read()
    total = len(firmware)
    print(f"Firmware: {firmware_path}  ({total:,} bytes)")

    print(f"Scanning for '{DEVICE_NAME}'...")
    device = await BleakScanner.find_device_by_name(DEVICE_NAME, timeout=10)
    if device is None:
        print("ERROR: device not found. Is it powered on and advertising?")
        sys.exit(1)
    print(f"Found {device.name} [{device.address}]")

    async with BleakClient(device) as client:
        try:
            await client.request_mtu(517)
        except Exception:
            pass  # not all platforms expose this

        ready  = asyncio.Event()
        done   = asyncio.Event()
        failed = asyncio.Event()

        def on_ctrl(_, data: bytearray) -> None:
            if not data:
                return
            code = data[0]
            if code == 0x01:
                ready.set()
            elif code == 0x02:
                done.set()
            elif code == 0x03:
                print("\nOTA aborted by device.")
                failed.set()
            elif code == 0x50 and len(data) >= 2:
                print(f"\r  uploading… {data[1]:3d}%", end="", flush=True)
            elif code == 0xFF:
                print("\nERROR reported by device.")
                failed.set()

        await client.start_notify(CTRL_UUID, on_ctrl)

        # START
        start_cmd = bytes([0x01]) + struct.pack("<I", total)
        await client.write_gatt_char(CTRL_UUID, start_cmd, response=True)
        try:
            await asyncio.wait_for(ready.wait(), timeout=8.0)
        except asyncio.TimeoutError:
            print("ERROR: device did not acknowledge start.")
            sys.exit(1)
        print("OTA session open, uploading…")

        # DATA
        offset = 0
        while offset < total:
            if failed.is_set():
                sys.exit(1)
            chunk = firmware[offset : offset + CHUNK_SIZE]
            await client.write_gatt_char(DATA_UUID, chunk, response=False)
            offset += len(chunk)
            print(f"\r  uploading… {offset * 100 // total:3d}%  ({offset:,}/{total:,} B)", end="", flush=True)
            await asyncio.sleep(0.01)

        if failed.is_set():
            sys.exit(1)

        # END
        print("\nAll data sent, waiting for verification…")
        await client.write_gatt_char(CTRL_UUID, bytes([0x02]), response=True)
        try:
            await asyncio.wait_for(done.wait(), timeout=15.0)
        except asyncio.TimeoutError:
            print("ERROR: timeout waiting for OTA completion.")
            sys.exit(1)

        if failed.is_set():
            print("OTA failed.")
            sys.exit(1)

        print("OTA successful — device is rebooting.")


if __name__ == "__main__":
    if len(sys.argv) != 2:
        print(f"Usage: python {sys.argv[0]} <firmware.bin>")
        sys.exit(1)
    asyncio.run(ota_upload(sys.argv[1]))
