"""Place a running PasteDispenser in RP2350 BOOTSEL mode over USB CDC."""
import os
import time

import serial
from serial.tools import list_ports


def main() -> None:
    port = next(
        (item.device for item in list_ports.comports() if item.vid == 0x2E8A and item.pid == 0x0009),
        None,
    )
    if port is None:
        raise SystemExit("PasteDispenser USB serial port not found")
    connection = serial.Serial(port, 115200, timeout=0.2, write_timeout=1)
    connection.write(b"BOOTSEL\n")
    # write() is synchronous on the Windows backend. Do not call flush(): the
    # device intentionally disappears while handling this line and some CDC
    # drivers then block forever in FlushFileBuffers.
    time.sleep(0.2)
    # Closing a CDC handle can block forever on Windows after the USB identity
    # changes. Process termination releases the handle without another I/O.
    os._exit(0)


if __name__ == "__main__":
    main()
