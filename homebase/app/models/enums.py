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


class PhotoType(str, enum.Enum):
    CAR = "car"
    JUDGE_SHEET = "judge_sheet"


class TransferMethod(str, enum.Enum):
    USB = "usb"
    WIFI = "wifi"
