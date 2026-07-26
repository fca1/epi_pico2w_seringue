"""Discover, configure and trigger a PasteDispenser over BLE.

Install the dependency with:  python -m pip install bleak
Run with:                     python example/ble_dispenser_example.py
"""
import argparse
import asyncio
import json

from bleak import BleakClient, BleakScanner

SERVICE = "7e400001-b5a3-f393-e0a9-e50e24dcca9e"
COMMAND = "7e400002-b5a3-f393-e0a9-e50e24dcca9e"
STATUS = "7e400003-b5a3-f393-e0a9-e50e24dcca9e"

EXAMPLE_CONFIGURATION = {
    "dosing_speed_mm_s": 4.0,
    "trigger_dose_mm": 0.8,
    "retract_distance_mm": 0.1,
    "retract_speed_mm_s": 3.0,
    "position_max_mm": 120.0,
}


async def discover(timeout: float):
    print("Searching for PasteDispenser...")
    devices = await BleakScanner.discover(timeout=timeout, return_adv=True)
    for device, advertisement in devices.values():
        name = device.name or advertisement.local_name or ""
        if name.startswith("PasteDispenser-") and SERVICE in advertisement.service_uuids:
            print(f"Found {name} ({device.address}, {advertisement.rssi} dBm)")
            return device
    raise RuntimeError("PasteDispenser not found")


async def write_command(client: BleakClient, command: dict) -> None:
    payload = json.dumps(command, separators=(",", ":")).encode("utf-8")
    print("TX", payload.decode())
    await client.write_gatt_char(COMMAND, payload, response=True)
    await asyncio.sleep(0.20)


async def run(args) -> None:
    device = await discover(args.scan_timeout)
    status_received = asyncio.Event()
    latest_status = {}

    def on_status(_, data: bytearray) -> None:
        text = bytes(data).decode("utf-8", errors="replace")
        print("RX", text)
        try:
            latest_status.clear()
            latest_status.update(json.loads(text))
        except json.JSONDecodeError:
            pass
        status_received.set()

    # Filtering the service also avoids stale Windows GATT caches after a firmware update.
    async with BleakClient(
        device,
        timeout=15,
        services=[SERVICE],
        winrt={"use_cached_services": False},
    ) as client:
        await client.start_notify(STATUS, on_status)
        for parameter, value in EXAMPLE_CONFIGURATION.items():
            await write_command(client, {"command": "set_config", "parameter": parameter, "value": value})

        print("Example configuration sent and saved in flash.")
        status_received.clear()
        await write_command(
            client,
            {
                "command": "dose",
                "distance_mm": args.distance,
                "speed_mm_s": EXAMPLE_CONFIGURATION["dosing_speed_mm_s"],
                "retract_mm": EXAMPLE_CONFIGURATION["retract_distance_mm"],
            },
        )
        print("Controlled dispensing command sent.")
        try:
            await asyncio.wait_for(status_received.wait(), timeout=3)
        except TimeoutError:
            print("No status notification received within 3 seconds.")
        if latest_status.get("state") == "FAULT":
            print(f"Warning: action cannot run while controller fault={latest_status.get('fault')} is active.")


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--distance", type=float, default=0.8, help="quantity expressed as plunger travel in mm")
    parser.add_argument("--scan-timeout", type=float, default=8.0, help="BLE scan duration in seconds")
    args = parser.parse_args()
    if args.distance <= 0:
        parser.error("--distance must be positive")
    asyncio.run(run(args))


if __name__ == "__main__":
    main()
