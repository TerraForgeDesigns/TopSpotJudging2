"""
In-process, thread-safe status of the current/last photo import — polled
by the /photos UI (see web/photos.py) so the host can see a 200+ file
import actually progressing rather than wondering if it's frozen. One
process, one scan at a time, so a module-level singleton is enough —
no need for anything heavier.
"""
import threading
from dataclasses import dataclass
from datetime import datetime


@dataclass
class ImportStatus:
    state: str = "idle"  # "idle" | "scanning" | "done" | "error"
    source_label: str = ""  # e.g. "E:\\" or "WiFi upload"
    total_files: int = 0
    current_index: int = 0
    current_filename: str = ""
    imported: int = 0
    unmatched: int = 0
    duplicate: int = 0
    skipped: int = 0
    errors: int = 0
    error_message: str = ""
    started_at: datetime | None = None
    finished_at: datetime | None = None


_lock = threading.Lock()
_status = ImportStatus()


def get_status() -> ImportStatus:
    with _lock:
        return ImportStatus(**vars(_status))


def start_scan(source_label: str, total_files: int) -> None:
    with _lock:
        global _status
        _status = ImportStatus(
            state="scanning",
            source_label=source_label,
            total_files=total_files,
            started_at=datetime.now(),
        )


def report_progress(index: int, filename: str) -> None:
    with _lock:
        _status.current_index = index
        _status.current_filename = filename


def record_result(status: str) -> None:
    with _lock:
        if status == "matched":
            _status.imported += 1
        elif status == "unmatched":
            _status.unmatched += 1
        elif status == "duplicate":
            _status.duplicate += 1
        elif status == "skipped":
            _status.skipped += 1
        elif status == "error":
            _status.errors += 1


def finish_scan() -> None:
    with _lock:
        _status.state = "done"
        _status.finished_at = datetime.now()


def fail_scan(message: str) -> None:
    with _lock:
        _status.state = "error"
        _status.error_message = message
        _status.finished_at = datetime.now()
