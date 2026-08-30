"""
Windows-only removable-drive detection. This is the ONLY module that
touches Windows-specific APIs — everything else (the polling loop in
sd_card_watcher.py, the scan/ingest logic in photo_ingest.py) is plain,
portable Python. Supporting another OS later means writing a sibling
module with the same list_removable_drives() signature and swapping it
in from sd_card_watcher.py; nothing else changes.

Named for what this system actually uses it for — a judge's microSD card,
pulled from the handheld and inserted into a card reader on this
computer, which Windows also reports as a removable drive. The Win32
API call itself doesn't know or care whether the media behind a drive
letter is an SD card or a USB flash drive; DRIVE_REMOVABLE covers both.

Uses ctypes against the Win32 API (stdlib — no pywin32/wmi/psutil
dependency, matching the "Pillow is the only new dependency" constraint).
"""
import ctypes
import string
from pathlib import Path

DRIVE_REMOVABLE = 2


def list_removable_drives() -> list[Path]:
    """Currently-mounted removable drives — in practice, a judge's
    microSD card in a card reader, e.g. [Path("E:\\\\")]."""
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
