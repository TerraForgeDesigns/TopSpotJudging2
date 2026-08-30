import enum


class ShowStatus(str, enum.Enum):
    """See DECISIONS.md — added ahead of HB7's finish-show gate so it
    doesn't need a second migration on top of HB1's clean baseline.
    materialize() (services/show_wizard.py) moves a new show straight
    from the column default SETUP to JUDGING the moment it's created —
    the wizard IS the setup phase, so there's nothing left in SETUP by
    the time a real Show row exists. The JUDGING -> FINISHED transition
    is HB7's finish-show gate, not built here."""

    SETUP = "setup"
    JUDGING = "judging"
    FINISHED = "finished"


class CarStatus(str, enum.Enum):
    UNJUDGED = "unjudged"
    JUDGED = "judged"
    FLAGGED_CONFLICT = "flagged_conflict"


class SubmissionStatus(str, enum.Enum):
    ACCEPTED = "accepted"
    FLAGGED_DUPLICATE = "flagged_duplicate"
    REJECTED = "rejected"
    # NOTE: no UNMATCHED here anymore. Under the old CSV-import model a
    # submission's registration number could arrive before the matching
    # car did. Under the Aug 2026 spec, entries 001..N are all created up
    # front at show creation — an entry_number a submission names either
    # resolves to an existing Car or it doesn't exist at all, which is a
    # wire-level "error" result (see services/sync.py), not a row held for
    # later reconciliation. See DECISIONS.md.


class PhotoType(str, enum.Enum):
    CAR = "car"
    JUDGE_SHEET = "judge_sheet"


class PhotoStatus(str, enum.Enum):
    MATCHED = "matched"  # normal case: resolved to a car, no conflict
    DUPLICATE = "duplicate"  # car+type slot was already filled — both kept, host resolves
    # Photos still arrive by filename (SD card/Wi-Fi), independent of the
    # pre-generated entry roster, so a typo'd or stale entry_number in a
    # photo filename can still miss — unlike submissions (see
    # SubmissionStatus above), this UNMATCHED case is unchanged.
    UNMATCHED = "unmatched"


class TransferMethod(str, enum.Enum):
    """The handheld cannot present itself as a USB drive — its USB-C is a
    CH340C serial bridge, and the ESP32-S3's native USB pins are already
    used by the touchscreen (see firmware/include/pins.h). Photos always
    arrive by physically moving the microSD card into this computer; Wi-Fi
    upload is the only fallback. There has never been a USB transfer path
    for photos — see DECISIONS.md."""

    SD_CARD = "sd_card"
    WIFI = "wifi"


class AwardRankingBasis(str, enum.Enum):
    """What decides a judge-chosen award's winner among nominated cars —
    see CONTEXT.md Awards section. Null on an Award row when judge_chosen
    is False (an organiser-chosen award has no ranking basis at all)."""

    TOTAL = "total"
    ENGINE = "engine"
    EXTERIOR = "exterior"
    INTERIOR = "interior"
    PAINT = "paint"
    WHEELS_TIRES = "wheels_tires"


class VehicleSource(str, enum.Enum):
    """Keeps installation-learned vehicle names separable from the
    bundled seed list so a future seed replacement can never destroy
    what judges have actually typed in at real shows — see CONTEXT.md
    and DECISIONS.md. Load-bearing: do not collapse into a boolean."""

    SEED = "seed"
    LEARNED = "learned"


class VehicleCandidateStatus(str, enum.Enum):
    PENDING = "pending"
    APPROVED = "approved"
    IGNORED = "ignored"
    MERGED = "merged"
