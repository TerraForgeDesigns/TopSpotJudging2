"""Builds firmware/data/vehicle_seed.bin from firmware/tools/
vehicle_seed_source.json (produced by merge_seed_source.py) — the
flash-mapped, memory-searched vehicle make/model database the handheld's
storage::vehicle_db module reads directly from flash at runtime, with no
heap allocation beyond a small bounded result list. See DECISIONS.md's F4
entry for the full binary format writeup; this file is the only place that
format is allowed to be defined — storage/vehicle_db.cpp reads exactly
what this script writes, byte for byte.

Format (little-endian throughout):
  Header (32B):
    magic       4s   b"VSD1"
    version     u16
    reserved0   u16  (padding, kept 0)
    makeCount   u32
    modelCount  u32
    makeTableOffset   u32
    modelTableOffset  u32
    stringBlobOffset  u32
    stringBlobSize    u32
  Make table (12B x makeCount), sorted by name (case-insensitive):
    nameOffset       u32  (offset into the string blob)
    firstModelIndex  u32  (index into the model table)
    modelCount       u16
    reserved         u16
  Model table (4B x modelCount), grouped contiguously by make (in make
  table order), sorted by name within each make's group:
    nameOffset  u32
  String blob: for every make name and every model name, in the order
  they're first referenced by the make/model tables above:
    len   u8
    bytes (ASCII, no null terminator)

Every offset a table entry might not need on its own two feet is spelled
out explicitly here rather than assumed at the read side, since a flash-
mapped struct read via memcpy has no type system backing it up once it
leaves this script.
"""
import json
import struct

MAX_NAME_LEN = 47  # matches storage::DraftCar.make/model — char[48]

HEADER_FMT = "<4sHHIIIIII"
MAKE_ENTRY_FMT = "<IIHH"
MODEL_ENTRY_FMT = "<I"


def main():
    src = json.load(open("firmware/tools/vehicle_seed_source.json", encoding="utf-8"))
    makes = sorted(src["makes"], key=lambda m: m["make"].lower())

    string_blob = bytearray()
    string_offsets: dict[str, int] = {}

    def intern(name: str) -> int:
        if name in string_offsets:
            return string_offsets[name]
        raw = name.encode("ascii")
        if len(raw) > MAX_NAME_LEN:
            raise ValueError(f"{name!r} exceeds {MAX_NAME_LEN} bytes — should have been filtered by merge_seed_source.py")
        offset = len(string_blob)
        string_blob.append(len(raw))
        string_blob.extend(raw)
        string_offsets[name] = offset
        return offset

    make_entries = bytearray()
    model_entries = bytearray()
    model_index = 0

    for make in makes:
        models = sorted(make["models"], key=lambda m: m["name"].lower())
        name_offset = intern(make["make"])
        first_model_index = model_index
        for model in models:
            model_offset = intern(model["name"])
            model_entries.extend(struct.pack(MODEL_ENTRY_FMT, model_offset))
            model_index += 1
        make_entries.extend(struct.pack(MAKE_ENTRY_FMT, name_offset, first_model_index, len(models), 0))

    header_size = struct.calcsize(HEADER_FMT)
    make_table_offset = header_size
    model_table_offset = make_table_offset + len(make_entries)
    string_blob_offset = model_table_offset + len(model_entries)

    header = struct.pack(
        HEADER_FMT,
        b"VSD1",
        1,
        0,
        len(makes),
        model_index,
        make_table_offset,
        model_table_offset,
        string_blob_offset,
        len(string_blob),
    )

    out = header + make_entries + model_entries + bytes(string_blob)

    import os
    os.makedirs("firmware/data", exist_ok=True)
    with open("firmware/data/vehicle_seed.bin", "wb") as f:
        f.write(out)

    print(f"vehicle_seed.bin: {len(makes)} makes, {model_index} models, {len(out)} bytes total")
    print(f"  header {header_size}B, make table {len(make_entries)}B, model table {len(model_entries)}B, string blob {len(string_blob)}B")


if __name__ == "__main__":
    main()
