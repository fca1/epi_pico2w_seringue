"""Flash a PasteDispenser UF2 without requiring the BOOTSEL button."""
import argparse
import shutil
import subprocess
import sys
import time
from pathlib import Path


def bootsel_drive() -> Path | None:
    for letter in "DEFGHIJKLMNOPQRSTUVWXYZ":
        root = Path(f"{letter}:\\")
        if (root / "INFO_UF2.TXT").is_file():
            return root
    return None


def main() -> None:
    repository = Path(__file__).resolve().parent.parent
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("firmware", nargs="?", type=Path, default=repository / "build/paste_dispenser.uf2")
    args = parser.parse_args()
    firmware = args.firmware.resolve()
    if not firmware.is_file():
        raise SystemExit(f"Firmware not found: {firmware}")
    drive = bootsel_drive()
    if drive is None:
        helper = Path(__file__).with_name("enter_bootsel.py")
        try:
            subprocess.run([sys.executable, str(helper)], check=True, timeout=5)
        except subprocess.TimeoutExpired as exc:
            raise SystemExit("BOOTSEL request timed out; close every serial terminal") from exc
        except subprocess.CalledProcessError as exc:
            raise SystemExit("BOOTSEL request failed; close every serial terminal") from exc
        deadline = time.monotonic() + 30
        while drive is None and time.monotonic() < deadline:
            time.sleep(0.25)
            drive = bootsel_drive()
    if drive is None:
        raise SystemExit("BOOTSEL volume not found after 30 seconds")
    destination = drive / "paste_dispenser.uf2"
    shutil.copyfile(firmware, destination)
    print(f"Firmware copied to {destination}")


if __name__ == "__main__":
    main()
