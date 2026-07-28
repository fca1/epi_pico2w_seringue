"""Place a running PasteDispenser in RP2350 BOOTSEL mode over USB."""
import ctypes
import os
import sys
import time

import serial
from serial.tools import list_ports


RESET_GUID = "{bc7398c1-73cd-4cb7-98b8-913a8fca7bf6}"


def bootsel_drive_present() -> bool:
    return any(os.path.isfile(f"{letter}:\\INFO_UF2.TXT") for letter in "DEFGHIJKLMNOPQRSTUVWXYZ")


def winusb_reset_paths() -> list[str]:
    if sys.platform != "win32":
        return []
    import winreg

    key_name = rf"SYSTEM\CurrentControlSet\Control\DeviceClasses\{RESET_GUID}"
    paths = []
    try:
        with winreg.OpenKey(winreg.HKEY_LOCAL_MACHINE, key_name) as key:
            for index in range(winreg.QueryInfoKey(key)[0]):
                name = winreg.EnumKey(key, index)
                if "#VID_2E8A&PID_0009&MI_02#" in name.upper():
                    paths.append("\\\\?\\" + name[4:])
    except FileNotFoundError:
        pass
    return paths


def request_bootsel_via_winusb() -> bool:
    """Use the Pico reset interface even when another process owns the CDC port."""
    if sys.platform != "win32":
        return False
    from ctypes import wintypes

    kernel32 = ctypes.WinDLL("kernel32", use_last_error=True)
    winusb = ctypes.WinDLL("winusb", use_last_error=True)
    kernel32.CreateFileW.argtypes = [wintypes.LPCWSTR, wintypes.DWORD, wintypes.DWORD,
                                     wintypes.LPVOID, wintypes.DWORD, wintypes.DWORD,
                                     wintypes.HANDLE]
    kernel32.CreateFileW.restype = wintypes.HANDLE
    kernel32.CloseHandle.argtypes = [wintypes.HANDLE]
    winusb.WinUsb_Initialize.argtypes = [wintypes.HANDLE, ctypes.POINTER(wintypes.HANDLE)]
    winusb.WinUsb_Initialize.restype = wintypes.BOOL
    winusb.WinUsb_Free.argtypes = [wintypes.HANDLE]

    class SetupPacket(ctypes.Structure):
        _fields_ = [("request_type", ctypes.c_ubyte), ("request", ctypes.c_ubyte),
                    ("value", ctypes.c_ushort), ("index", ctypes.c_ushort),
                    ("length", ctypes.c_ushort)]

    winusb.WinUsb_ControlTransfer.argtypes = [wintypes.HANDLE, SetupPacket, ctypes.c_void_p,
                                               wintypes.ULONG, ctypes.POINTER(wintypes.ULONG),
                                               ctypes.c_void_p]
    winusb.WinUsb_ControlTransfer.restype = wintypes.BOOL
    invalid_handle = ctypes.c_void_p(-1).value
    for path in winusb_reset_paths():
        handle = kernel32.CreateFileW(path, 0xC0000000, 3, None, 3, 0x40000080, None)
        if handle in (None, invalid_handle):
            continue
        interface = wintypes.HANDLE()
        try:
            if not winusb.WinUsb_Initialize(handle, ctypes.byref(interface)):
                continue
            transferred = wintypes.ULONG()
            # bmRequestType: host-to-device, class, interface; request 1: BOOTSEL.
            winusb.WinUsb_ControlTransfer(interface, SetupPacket(0x21, 1, 0, 2, 0),
                                           None, 0, ctypes.byref(transferred), None)
        finally:
            if interface.value:
                winusb.WinUsb_Free(interface)
            kernel32.CloseHandle(handle)
        for _ in range(20):
            if bootsel_drive_present():
                return True
            time.sleep(0.1)
    return False


def main() -> None:
    if request_bootsel_via_winusb():
        return
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
