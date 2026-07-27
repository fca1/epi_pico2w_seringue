"""Send one safe ASCII command over Nordic UART Service."""
import argparse
import asyncio

from bleak import BleakClient, BleakScanner

NUS_SERVICE = "6e400001-b5a3-f393-e0a9-e50e24dcca9e"
NUS_RX = "6e400002-b5a3-f393-e0a9-e50e24dcca9e"
NUS_TX = "6e400003-b5a3-f393-e0a9-e50e24dcca9e"


async def main(command: str) -> None:
    device = await BleakScanner.find_device_by_filter(
        lambda dev, adv: NUS_SERVICE in [uuid.lower() for uuid in adv.service_uuids],
        timeout=10,
    )
    if device is None:
        raise RuntimeError("PasteDispenser NUS introuvable")
    print(f"Connexion à {device.name or device.address} ({device.address})")
    async with BleakClient(
        device,
        timeout=15,
        winrt={"use_cached_services": False},
    ) as client:
        answer = asyncio.get_running_loop().create_future()

        def notified(_handle, data: bytearray) -> None:
            text = bytes(data).decode("utf-8", errors="replace")
            print(text, end="")
            if not answer.done():
                answer.set_result(text)

        await client.start_notify(NUS_TX, notified)
        await client.write_gatt_char(NUS_RX, (command + "\n").encode(), response=True)
        await asyncio.wait_for(answer, timeout=5)


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "command",
        nargs="?",
        default="STOP",
        help="commande ASCII ; STOP par défaut afin de ne provoquer aucun mouvement",
    )
    asyncio.run(main(parser.parse_args().command))
