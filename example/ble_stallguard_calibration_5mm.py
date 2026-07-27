"""Calibrate StallGuard over a 5 mm travel with a 2 mm/rev lead screw.

The syringe must have at least 5 mm of free forward travel. Remove any hard
obstacle from the calibration zone and keep the mechanism under its normal load.

Install: python -m pip install -r example/requirements.txt
Run:     python example/ble_stallguard_calibration_5mm.py
"""
import asyncio
import json

from bleak import BleakClient, BleakScanner

SERVICE = "7e400001-b5a3-f393-e0a9-e50e24dcca9e"
NUS_SERVICE = "6e400001-b5a3-f393-e0a9-e50e24dcca9e"
COMMAND = "7e400002-b5a3-f393-e0a9-e50e24dcca9e"
STATUS = "7e400003-b5a3-f393-e0a9-e50e24dcca9e"

SCREW_PITCH_MM_REV = 2.0
CALIBRATION_TRAVEL_MM = 5.0
CALIBRATION_SPEED_MM_S = 1.0


async def discover(timeout: float = 8.0):
    devices = await BleakScanner.discover(timeout=timeout, return_adv=True)
    for device, advertisement in devices.values():
        name = device.name or advertisement.local_name or ""
        if name.startswith("PasteDispenser-") and NUS_SERVICE in advertisement.service_uuids:
            print(f"Dispenser found: {name} ({device.address})")
            return device
    raise RuntimeError("PasteDispenser not found over BLE")


async def send(client: BleakClient, command: dict) -> None:
    payload = json.dumps(command, separators=(",", ":")).encode("utf-8")
    print("TX", payload.decode())
    await client.write_gatt_char(COMMAND, payload, response=True)
    await asyncio.sleep(0.25)


async def connect(device):
    client = BleakClient(
        device,
        timeout=15,
        services=[SERVICE],
        winrt={"use_cached_services": False},
    )
    await client.connect()
    return client


async def configure_mechanics() -> None:
    """Save the mechanical parameters, then reboot so they take full effect."""
    device = await discover()
    client = await connect(device)
    try:
        await send(
            client,
            {"command": "set_config", "parameter": "screw_pitch_mm", "value": SCREW_PITCH_MM_REV},
        )
        await send(
            client,
            {"command": "set_config", "parameter": "manual_speed_mm_s", "value": CALIBRATION_SPEED_MM_S},
        )
        print("Mechanical configuration saved; rebooting the dispenser...")
        await send(client, {"command": "reboot"})
    finally:
        if client.is_connected:
            await client.disconnect()
    await asyncio.sleep(3.0)


async def calibrate() -> None:
    device = await discover()
    client = await connect(device)
    latest = {}
    update = asyncio.Event()

    def status_callback(_, data: bytearray) -> None:
        try:
            latest.clear()
            latest.update(json.loads(bytes(data).decode("utf-8")))
            update.set()
        except (UnicodeDecodeError, json.JSONDecodeError):
            pass

    try:
        await client.start_notify(STATUS, status_callback)
        initial = json.loads(bytes(await client.read_gatt_char(STATUS)).decode("utf-8"))
        latest.update(initial)
        if initial.get("state") != "READY":
            raise RuntimeError(
                f"Calibration requires READY state; state={initial.get('state')}, fault={initial.get('fault')}"
            )
        if float(initial.get("remaining_course_mm", 0)) < CALIBRATION_TRAVEL_MM:
            raise RuntimeError("Less than 5 mm of forward travel remains")

        start_mm = float(initial["position_mm"])
        target_mm = start_mm + CALIBRATION_TRAVEL_MM
        await send(client, {"command": "sg_calibrate_start"})
        await send(client, {"command": "push_start"})
        print(f"Calibration movement: {start_mm:.3f} mm -> {target_mm:.3f} mm")

        deadline = asyncio.get_running_loop().time() + 10.0
        while float(latest.get("position_mm", start_mm)) < target_mm:
            if latest.get("state") == "FAULT":
                raise RuntimeError(f"Motor fault during calibration: {latest.get('fault')}")
            remaining = deadline - asyncio.get_running_loop().time()
            if remaining <= 0:
                raise TimeoutError("5 mm calibration movement timed out")
            update.clear()
            await asyncio.wait_for(update.wait(), min(remaining, 1.0))

        await send(client, {"command": "push_stop"})
        await asyncio.sleep(0.5)
        samples = int(latest.get("sg_samples", 0))
        if samples < 100:
            raise RuntimeError(f"Only {samples} StallGuard samples collected; at least 100 are required")
        await send(client, {"command": "sg_calibrate_finish"})
        await asyncio.sleep(0.5)
        final = json.loads(bytes(await client.read_gatt_char(STATUS)).decode("utf-8"))
        if final.get("sg_calibrating"):
            raise RuntimeError("StallGuard calibration did not finish")
        print(
            f"Calibration complete: travel={float(final['position_mm']) - start_mm:.3f} mm, "
            f"samples={samples}, sg_result={final.get('sg_result')}"
        )
    finally:
        if client.is_connected:
            # STOP is harmless when already stopped and protects against errors/timeouts.
            try:
                await send(client, {"command": "stop"})
            finally:
                await client.disconnect()


async def main() -> None:
    await configure_mechanics()
    await calibrate()


if __name__ == "__main__":
    asyncio.run(main())
