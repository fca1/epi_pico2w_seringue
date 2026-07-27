"""Flash a PasteDispenser UF2 without requiring the BOOTSEL button."""
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
    firmware = Path(sys.argv[1] if len(sys.argv) > 1 else "build/paste_dispenser.uf2").resolve()
    if not firmware.is_file():
        raise SystemExit(f"Firmware not found: {firmware}")
    drive = bootsel_drive()
    if drive is None:
        helper = Path(__file__).with_name("enter_bootsel.py")
        subprocess.run([sys.executable, str(helper)], check=True, timeout=5)
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
