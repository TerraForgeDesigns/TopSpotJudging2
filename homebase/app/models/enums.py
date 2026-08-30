import enum


class CarStatus(str, enum.Enum):
    UNJUDGED = "unjudged"
    JUDGED = "judged"
    FLAGGED_CONFLICT = "flagged_conflict"


class SubmissionStatus(str, enum.Enum):
    PENDING = "pending"
    ACCEPTED = "accepted"
    FLAGGED_DUPLICATE = "flagged_duplicate"
    REJECTED = "rejected"
    # Registration number didn't match any car in the roster at submission
    # time (e.g. a late-registered car judged before the handheld's roster
    # pull caught up). Held for host reconciliation — see PROTOCOL.md sync
    # notes in DECISIONS.md. Not a wire-protocol status: the API reports
    # these as "accepted" (see api/sync.py _wire_status), since the handheld
    # doesn't need to know or care that reconciliation is pending.
    UNMATCHED = "unmatched"


class PhotoType(str, enum.Enum):
    CAR = "car"
    JUDGE_SHEET = "judge_sheet"


class PhotoStatus(str, enum.Enum):
    MATCHED = "matched"  # normal case: resolved to a car, no conflict
    DUPLICATE = "duplicate"  # car+type slot was already filled — both kept, host resolves
    UNMATCHED = "unmatched"  # registration number didn't match any car — held for host to assign


class TransferMethod(str, enum.Enum):
    USB = "usb"
    WIFI = "wifi"
