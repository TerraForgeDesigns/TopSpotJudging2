"""PlatformIO extra_script (see platformio.ini's `extra_scripts`): adds a
`pio run -t upload-vehicle-seed` target that writes firmware/data/
vehicle_seed.bin directly to the `vehicle_seed` partition's flash offset —
independently of a full firmware upload, matching the principle
DECISIONS.md's F4 entry states for both this and the learned store: the
seed is a separate, independently-replaceable artifact. Regenerate
vehicle_seed.bin first (tools/merge_seed_source.py then
tools/build_vehicle_seed.py) if the source data changed; this script only
flashes whatever's already on disk.

The partition's offset is read from ../partitions.csv at run time rather
than duplicated here — one source of truth for that number.
"""
import csv
import os

Import("env")  # noqa: F821 — PlatformIO injects this at extra_scripts execution time


def _find_vehicle_seed_offset():
    partitions_csv = os.path.join(env.subst("$PROJECT_DIR"), "partitions.csv")  # noqa: F821
    with open(partitions_csv, newline="", encoding="utf-8") as f:
        for row in csv.reader(f):
            row = [c.strip() for c in row]
            if not row or row[0].startswith("#") or not row[0]:
                continue
            if row[0] == "vehicle_seed":
                return int(row[3], 0)  # Offset column, "0x..." or decimal
    raise RuntimeError("vehicle_seed partition not found in partitions.csv")


def upload_vehicle_seed(*_args, **_kwargs):
    project_dir = env.subst("$PROJECT_DIR")  # noqa: F821
    bin_path = os.path.join(project_dir, "data", "vehicle_seed.bin")
    if not os.path.isfile(bin_path):
        print(f"[upload-vehicle-seed] {bin_path} does not exist — run "
              "tools/merge_seed_source.py then tools/build_vehicle_seed.py first.")
        env.Exit(1)  # noqa: F821

    offset = _find_vehicle_seed_offset()
    port = env.subst("$UPLOAD_PORT") or env.AutodetectUploadPort()  # noqa: F821
    speed = env.subst("$UPLOAD_SPEED") or "921600"  # noqa: F821

    cmd = [
        env.subst("$PYTHONEXE"),  # noqa: F821
        "-m", "esptool",
        "--chip", "esp32s3",
        "--port", port,
        "--baud", speed,
        "write_flash", hex(offset), bin_path,
    ]
    print(f"[upload-vehicle-seed] {' '.join(cmd)}")
    env.Execute(" ".join(f'"{c}"' if " " in c else c for c in cmd))  # noqa: F821


env.AddCustomTarget(  # noqa: F821
    name="upload-vehicle-seed",
    dependencies=None,
    actions=[upload_vehicle_seed],
    title="Upload Vehicle Seed",
    description="Flash firmware/data/vehicle_seed.bin to the vehicle_seed partition, independent of the main firmware upload",
)
