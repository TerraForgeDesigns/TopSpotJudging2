"""
OS-agnostic SD card watcher + scan orchestration. The handheld cannot
present itself as a USB drive — its USB-C port is a CH340C serial
programming bridge, and the ESP32-S3's native USB pins are already used
by the touchscreen (see firmware/include/pins.h) — so the only way a
photo gets from a handheld to this computer without Wi-Fi is physically:
the operator pulls the microSD card out of the handheld and puts it in
this computer's card reader. Windows reports that the same way it
reports any removable drive, which is what this module watches for.

Mount detection is delegated to a platform-specific module
(sd_card_windows.py today); this file never touches a Windows API
directly, so adding another OS later is a matter of writing that OS's
list_removable_drives() and branching in list_removable_drives() below —
the polling loop and everything that happens after a card is detected
stays untouched.
"""
import logging
import sys
import threading
import time
from pathlib import Path

from app.db import SessionLocal
from app.models import TransferMethod
from app.services import photo_import_status as status_service
from app.services.photo_ingest import find_candidate_photo_files, ingest_photo_file
from app.services.shows import get_active_show

logger = logging.getLogger(__name__)

POLL_INTERVAL_SECONDS = 2.0


def list_removable_drives() -> list[Path]:
    """Currently-mounted removable drives. Public so the web layer can
    offer a manual "scan now" trigger independent of the watcher's own
    poll cadence."""
    if sys.platform.startswith("win"):
        from app.services.sd_card_windows import list_removable_drives as _list_windows

        return _list_windows()
    return []  # no watcher support on this OS yet — see module docstring


def scan_and_ingest_drive(drive_root: Path, source_label: str) -> None:
    """Scans `drive_root` and `drive_root/photos` for matching files and
    runs each through the shared ingest path. Safe to call directly (e.g.
    a manual "scan now" trigger) as well as from the watcher."""
    candidates = find_candidate_photo_files(drive_root) + find_candidate_photo_files(drive_root / "photos")

    db = SessionLocal()
    try:
        show = get_active_show(db)
        if show is None:
            status_service.fail_scan("No active show — set one up in Shows before importing photos.")
            return

        status_service.start_scan(source_label, len(candidates))
        for index, source_path in enumerate(candidates, start=1):
            status_service.report_progress(index, source_path.name)
            result = ingest_photo_file(db, source_path, show.id, TransferMethod.SD_CARD)
            status_service.record_result(result.status)
        status_service.finish_scan()
    except Exception:  # noqa: BLE001 - a scan failing must be visible, never silent
        logger.exception("Photo import failed for %s", source_label)
        status_service.fail_scan(
            "Home Base could not finish importing these photos. "
            "Try removing and reinserting the memory card. If it keeps happening, restart Home Base."
        )
    finally:
        db.close()


class SdCardWatcherThread(threading.Thread):
    """Polls for newly-mounted removable drives (in practice: a judge's
    microSD card in a card reader) and scans each one once, the moment it
    appears. Daemon thread — dies with the process, no explicit shutdown
    needed for a desktop app's lifetime."""

    def __init__(self) -> None:
        super().__init__(daemon=True, name="sd-card-watcher")
        self._stop_event = threading.Event()
        self._known_drives: set[str] = set()

    def stop(self) -> None:
        self._stop_event.set()

    def run(self) -> None:
        # Don't treat drives already mounted at startup as "newly inserted" —
        # only react to ones that appear after the watcher starts.
        self._known_drives = {str(d) for d in list_removable_drives()}

        while not self._stop_event.is_set():
            time.sleep(POLL_INTERVAL_SECONDS)
            try:
                current = {str(d) for d in list_removable_drives()}
            except Exception:  # noqa: BLE001 - a detection hiccup shouldn't kill the watcher
                continue

            newly_mounted = current - self._known_drives
            self._known_drives = current

            for drive_str in newly_mounted:
                scan_and_ingest_drive(Path(drive_str), drive_str)
