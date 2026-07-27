"""Provision Wi-Fi over BLE, then configure and dose over Wi-Fi.

Default demonstration credentials requested for this example:
  SSID: EPI
  password: maistuasbu

Install: python -m pip install -r example/requirements.txt
Run:     python example/ble_wifi_dispenser_example.py
"""
import argparse
import asyncio
import json
import socket
import time

from bleak import BleakClient, BleakScanner

SERVICE = "7e400001-b5a3-f393-e0a9-e50e24dcca9e"
NUS_SERVICE = "6e400001-b5a3-f393-e0a9-e50e24dcca9e"
WIFI_SSID = "7e400004-b5a3-f393-e0a9-e50e24dcca9e"
WIFI_PASSWORD = "7e400005-b5a3-f393-e0a9-e50e24dcca9e"
WIFI_CONNECT = "7e400006-b5a3-f393-e0a9-e50e24dcca9e"
WIFI_STATUS = "7e400007-b5a3-f393-e0a9-e50e24dcca9e"
WIFI_IP = "7e400008-b5a3-f393-e0a9-e50e24dcca9e"

DEFAULT_SSID = "EPI"
DEFAULT_PASSWORD = "maistuasbu"
EXAMPLE_CONFIGURATION = {
    "dosing_speed_mm_s": 4.0,
    "trigger_dose_mm": 0.8,
    "retract_distance_mm": 0.1,
    "retract_speed_mm_s": 3.0,
}


async def discover(timeout: float):
    print("Searching for PasteDispenser over BLE...")
    devices = await BleakScanner.discover(timeout=timeout, return_adv=True)
    for device, advertisement in devices.values():
        name = device.name or advertisement.local_name or ""
        if name.startswith("PasteDispenser-") and NUS_SERVICE in advertisement.service_uuids:
            print(f"Found {name} ({device.address}, {advertisement.rssi} dBm)")
            return device
    raise RuntimeError("PasteDispenser not found")


async def provision_wifi(device, ssid: str, password: str, timeout: float) -> str:
    print(f"Connecting over BLE and selecting SSID {ssid!r}...")
    async with BleakClient(
        device,
        timeout=15,
        services=[SERVICE],
        winrt={"use_cached_services": False},
    ) as client:
        await client.write_gatt_char(WIFI_SSID, ssid.encode("utf-8"), response=True)
        await client.write_gatt_char(WIFI_PASSWORD, password.encode("utf-8"), response=True)
        print("Wi-Fi password transmitted over BLE (not displayed).")
        await client.write_gatt_char(WIFI_CONNECT, b"\x01", response=True)

        deadline = time.monotonic() + timeout
        last_status = ""
        while time.monotonic() < deadline:
            await asyncio.sleep(1)
            last_status = bytes(await client.read_gatt_char(WIFI_STATUS)).decode(
                "utf-8", errors="replace"
            )
            print("Wi-Fi status:", last_status)
            if last_status == "CONNECTED":
                ip = bytes(await client.read_gatt_char(WIFI_IP)).decode(
                    "utf-8", errors="replace"
                )
                if not ip:
                    raise RuntimeError("Wi-Fi connected but no IPv4 address was returned")
                print("IPv4 address:", ip)
                break
            if last_status in {"AUTH_FAILED", "NETWORK_NOT_FOUND", "TIMEOUT"}:
                raise RuntimeError(f"Wi-Fi provisioning failed: {last_status}")
        else:
            raise TimeoutError(f"Wi-Fi connection timeout; last status={last_status!r}")

    print("BLE disconnected. Continuing exclusively over Wi-Fi.")
    return ip


def post_command(ip: str, command: dict) -> dict:
    body = json.dumps(command, separators=(",", ":")).encode("utf-8")
    request = (
        f"POST /api/command HTTP/1.1\r\n"
        f"Host: {ip}\r\n"
        "Content-Type: application/json\r\n"
        f"Content-Length: {len(body)}\r\n"
        "Connection: close\r\n\r\n"
    ).encode("ascii") + body
    with socket.create_connection((ip, 80), timeout=5) as connection:
        # The embedded HTTP endpoint currently expects headers and body in the
        # same receive buffer, so deliberately transmit one compact request.
        connection.sendall(request)
        response = bytearray()
        while chunk := connection.recv(1024):
            response.extend(chunk)
    head, separator, payload = bytes(response).partition(b"\r\n\r\n")
    if not separator:
        raise RuntimeError("Malformed HTTP response")
    status_line = head.split(b"\r\n", 1)[0].decode("ascii", errors="replace")
    if " 200 " not in status_line:
        raise RuntimeError(f"{status_line}: {payload.decode(errors='replace')}")
    result = json.loads(payload.decode("utf-8"))
    if not result.get("ok"):
        raise RuntimeError(f"Command rejected: {command}")
    return result


async def wait_for_http(ip: str, timeout: float = 15) -> None:
    deadline = time.monotonic() + timeout
    while time.monotonic() < deadline:
        try:
            reader, writer = await asyncio.open_connection(ip, 80)
            writer.close()
            await writer.wait_closed()
            del reader
            return
        except OSError:
            await asyncio.sleep(0.5)
    raise TimeoutError(f"HTTP server at {ip}:80 did not become reachable")


async def run(args) -> None:
    device = await discover(args.scan_timeout)
    ip = await provision_wifi(device, args.ssid, args.password, args.wifi_timeout)
    await wait_for_http(ip)

    for parameter, value in EXAMPLE_CONFIGURATION.items():
        command = {"command": "set_config", "parameter": parameter, "value": value}
        print("Wi-Fi TX", json.dumps(command, separators=(",", ":")))
        await asyncio.to_thread(post_command, ip, command)

    dose = {
        "command": "dose",
        "distance_mm": args.distance,
        "speed_mm_s": EXAMPLE_CONFIGURATION["dosing_speed_mm_s"],
        "retract_mm": EXAMPLE_CONFIGURATION["retract_distance_mm"],
    }
    print("Wi-Fi TX", json.dumps(dose, separators=(",", ":")))
    await asyncio.to_thread(post_command, ip, dose)
    print("Configuration and controlled dispensing request accepted over Wi-Fi.")


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--ssid", default=DEFAULT_SSID)
    parser.add_argument("--password", default=DEFAULT_PASSWORD)
    parser.add_argument("--distance", type=float, default=0.8, help="plunger travel in mm")
    parser.add_argument("--scan-timeout", type=float, default=8.0)
    parser.add_argument("--wifi-timeout", type=float, default=30.0)
    args = parser.parse_args()
    if args.distance <= 0:
        parser.error("--distance must be positive")
    asyncio.run(run(args))


if __name__ == "__main__":
    main()
