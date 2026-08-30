"""
Windows-only removable-drive detection. This is the ONLY module that
touches Windows-specific APIs — everything else (the polling loop in
usb_watcher.py, the scan/ingest logic in photo_ingest.py) is plain,
portable Python. Supporting another OS later means writing a sibling
module with the same list_removable_drives() signature and swapping it
in from usb_watcher.py; nothing else changes.

Uses ctypes against the Win32 API (stdlib — no pywin32/wmi/psutil
dependency, matching the "Pillow is the only new dependency" constraint).
"""
import ctypes
import string
from pathlib import Path

DRIVE_REMOVABLE = 2


def list_removable_drives() -> list[Path]:
    """Currently-mounted removable drives (SD cards / USB flash drives via
    a card reader or direct connection), e.g. [Path("E:\\\\")]."""
    kernel32 = ctypes.windll.kernel32  # type: ignore[attr-defined]
    bitmask = kernel32.GetLogicalDrives()

    drives: list[Path] = []
    for i, letter in enumerate(string.ascii_uppercase):
        if not (bitmask >> i) & 1:
            continue
        root = f"{letter}:\\"
        if kernel32.GetDriveTypeW(root) == DRIVE_REMOVABLE:
            drives.append(Path(root))
    return drives
