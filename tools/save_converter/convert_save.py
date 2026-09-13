#!/usr/bin/env python3
"""Convert the 2021 re3/reVC 3DS ARM and PC save ABIs without touching game code."""

from __future__ import annotations

import argparse
import dataclasses
import hashlib
import struct
import sys
from pathlib import Path


U32 = struct.Struct("<I")
FIXED_FILE_SIZES = {"re3": 201820, "revc": 201828}
MEANINGFUL_BLOCKS = {"re3": 20, "revc": 23}
MAX_PADDING_PAYLOAD = 55000


class ConversionError(ValueError):
    pass


@dataclasses.dataclass(frozen=True)
class Block:
    offset: int
    data: bytes

    @property
    def outer_size(self) -> int:
        return len(self.data)

    def encoded(self) -> bytes:
        return U32.pack(len(self.data)) + self.data + bytes((-len(self.data)) & 3)

    def inner(self) -> bytes:
        if len(self.data) < 4:
            raise ConversionError(f"block at 0x{self.offset:X} has no inner size")
        size = U32.unpack_from(self.data)[0]
        if size > len(self.data) - 4:
            raise ConversionError(
                f"block at 0x{self.offset:X} declares inner size {size}, "
                f"but only {len(self.data) - 4} bytes remain"
            )
        return self.data[4 : 4 + size]


@dataclasses.dataclass(frozen=True)
class SaveFile:
    variant: str
    path: Path
    raw: bytes
    blocks: tuple[Block, ...]
    checksum: int


def align4(value: int) -> int:
    return (value + 3) & ~3


def checksum(data: bytes) -> int:
    return sum(data) & 0xFFFFFFFF


def sha256(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest()


def infer_variant(path: Path, data: bytes) -> str:
    lower = path.name.lower()
    if lower.startswith("gta3sf"):
        return "re3"
    if lower.startswith("gtavcsf"):
        return "revc"
    matches = [name for name, size in FIXED_FILE_SIZES.items() if len(data) == size]
    if len(matches) == 1:
        return matches[0]
    raise ConversionError("cannot infer game; use a GTA3sf*.b or GTAVCsf*.b filename")


def parse_save(path: Path) -> SaveFile:
    data = path.read_bytes()
    variant = infer_variant(path, data)
    expected_size = FIXED_FILE_SIZES[variant]
    if len(data) != expected_size:
        raise ConversionError(
            f"{variant} save must be exactly {expected_size} bytes, got {len(data)}"
        )

    stored = U32.unpack_from(data, len(data) - 4)[0]
    actual = checksum(data[:-4])
    if stored != actual:
        raise ConversionError(
            f"checksum mismatch: stored 0x{stored:08X}, calculated 0x{actual:08X}"
        )

    blocks: list[Block] = []
    offset = 0
    body_end = len(data) - 4
    while offset < body_end:
        if offset + 4 > body_end:
            raise ConversionError(f"truncated outer block header at 0x{offset:X}")
        size = U32.unpack_from(data, offset)[0]
        end = offset + 4 + align4(size)
        if end > body_end:
            raise ConversionError(
                f"outer block at 0x{offset:X} overruns file: size {size}"
            )
        blocks.append(Block(offset, data[offset + 4 : offset + 4 + size]))
        offset = end
    if offset != body_end:
        raise ConversionError("outer blocks do not end at the checksum")
    if len(blocks) < MEANINGFUL_BLOCKS[variant]:
        raise ConversionError(f"too few blocks for {variant}: {len(blocks)}")

    return SaveFile(variant, path, data, tuple(blocks), stored)


def nested_block(inner: bytes) -> Block:
    data = U32.pack(len(inner)) + inner + bytes((-len(inner)) & 3)
    return Block(0, data)


def expect_size(label: str, data: bytes, expected: int) -> None:
    if len(data) != expected:
        raise ConversionError(f"{label}: expected {expected} bytes, got {len(data)}")


PED_LAYOUTS = {
    "re3": {
        "raw_size": 1448,
        "pc_size": 1520,
        "model_name": 24,
        "weapon_count": 13,
        "raw": {
            "position": 52,
            "created_by": 349,
            "health": 684,
            "armour": 688,
            "weapons": 840,
            "max_weapon": 1102,
            "max_stamina": 1284,
            "targets": 1316,
        },
        "pc": {
            "position": 52,
            "created_by": 352,
            "health": 704,
            "armour": 708,
            "weapons": 860,
            "max_weapon": 1177,
            "max_stamina": 1356,
            "targets": 1388,
        },
    },
    "revc": {
        "raw_size": 1692,
        "pc_size": 1752,
        "model_name": 21,
        "weapon_count": 10,
        "raw": {
            "position": 52,
            "created_by": 350,
            "health": 836,
            "armour": 840,
            "weapons": 1016,
            "max_stamina": 1480,
            "targets": 1512,
        },
        "pc": {
            "position": 52,
            "created_by": 352,
            "health": 852,
            "armour": 856,
            "weapons": 1032,
            "max_stamina": 1540,
            "targets": 1572,
        },
    },
}


def convert_weapon_3ds_to_pc(raw: bytes) -> bytes:
    expect_size("raw weapon", raw, 20)
    out = bytearray(24)
    U32.pack_into(out, 0, raw[0])
    U32.pack_into(out, 4, raw[1])
    out[8:20] = raw[4:16]
    out[20] = raw[16]
    return bytes(out)


def convert_weapon_pc_to_3ds(raw: bytes) -> bytes:
    expect_size("PC weapon", raw, 24)
    out = bytearray(20)
    out[0] = raw[0]
    out[1] = raw[4]
    out[4:16] = raw[8:20]
    out[16] = raw[20]
    return bytes(out)


def convert_player_object_3ds_to_pc(variant: str, raw: bytes) -> bytes:
    layout = PED_LAYOUTS[variant]
    expect_size("raw player object", raw, layout["raw_size"])
    ro = layout["raw"]
    po = layout["pc"]
    out = bytearray(layout["pc_size"])

    for key, width in (
        ("position", 12),
        ("created_by", 1),
        ("health", 4),
        ("armour", 4),
        ("max_stamina", 4),
        ("targets", 16),
    ):
        out[po[key] : po[key] + width] = raw[ro[key] : ro[key] + width]

    if variant == "re3":
        out[po["max_weapon"]] = raw[ro["max_weapon"]]

    raw_weapons = ro["weapons"]
    pc_weapons = po["weapons"]
    for index in range(layout["weapon_count"]):
        converted = convert_weapon_3ds_to_pc(raw[raw_weapons + index * 20 : raw_weapons + (index + 1) * 20])
        out[pc_weapons + index * 24 : pc_weapons + (index + 1) * 24] = converted
    return bytes(out)


def convert_player_object_pc_to_3ds(variant: str, raw: bytes) -> bytes:
    layout = PED_LAYOUTS[variant]
    expect_size("PC player object", raw, layout["pc_size"])
    ro = layout["raw"]
    po = layout["pc"]
    out = bytearray(layout["raw_size"])

    for key, width in (
        ("position", 12),
        ("created_by", 1),
        ("health", 4),
        ("armour", 4),
        ("max_stamina", 4),
        ("targets", 16),
    ):
        out[ro[key] : ro[key] + width] = raw[po[key] : po[key] + width]

    if variant == "re3":
        out[ro["max_weapon"]] = raw[po["max_weapon"]]

    raw_weapons = ro["weapons"]
    pc_weapons = po["weapons"]
    for index in range(layout["weapon_count"]):
        converted = convert_weapon_pc_to_3ds(raw[pc_weapons + index * 24 : pc_weapons + (index + 1) * 24])
        out[raw_weapons + index * 20 : raw_weapons + (index + 1) * 20] = converted
    return bytes(out)


def convert_ped_pool(variant: str, block: Block, direction: str) -> Block:
    inner = block.inner()
    layout = PED_LAYOUTS[variant]
    source_size = layout["raw_size"] if direction == "3ds-to-pc" else layout["pc_size"]
    expected = 4 + 10 + source_size + 8 + layout["model_name"]
    expect_size(f"{direction} ped pool", inner, expected)
    count = U32.unpack_from(inner)[0]
    if count != 1:
        raise ConversionError(f"ped pool: expected exactly one player, got {count}")
    prefix = inner[:14]
    player_size = source_size
    raw_player = inner[14 : 14 + player_size]
    suffix = inner[14 + player_size :]
    converted_player = (
        convert_player_object_3ds_to_pc(variant, raw_player)
        if direction == "3ds-to-pc"
        else convert_player_object_pc_to_3ds(variant, raw_player)
    )
    converted = prefix + converted_player + suffix
    return nested_block(converted)


def vehicle_counts(variant: str, block: Block) -> tuple[int, ...]:
    inner = block.inner()
    count_len = 8 if variant == "re3" else 12
    if len(inner) < count_len:
        raise ConversionError("vehicle pool is too short")
    return struct.unpack_from("<" + "I" * (count_len // 4), inner)


# The PC vehicle-pool record uses the fixed 1448-byte compatible-save layout,
# while the 2021 3DS target's non-COMPATIBLE_SAVES CAutomobile is 1436 bytes.
# The meaningful fields consumed by the 3DS raw loader all precede the final
# 12-byte ABI tail.  That tail is reserved/skipped by the compatible Save/Load
# path, so it must not be interpreted as a second copy of gameplay state.
RE3_CAR_PC_SIZE = 1448
RE3_CAR_3DS_SIZE = 1436
RE3_ZONE_COUNT = 50


def convert_re3_car_pc_to_3ds(raw: bytes) -> bytes:
    expect_size("PC re3 CAutomobile", raw, RE3_CAR_PC_SIZE)
    return raw[:RE3_CAR_3DS_SIZE]


def convert_re3_car_3ds_to_pc(raw: bytes) -> bytes:
    expect_size("3DS re3 CAutomobile", raw, RE3_CAR_3DS_SIZE)
    return raw + bytes(RE3_CAR_PC_SIZE - RE3_CAR_3DS_SIZE)


def convert_re3_vehicle_pool(block: Block, direction: str) -> Block:
    """Convert persisted re3 mission-car records between PC and ARM ABIs.

    The save pool header is two uint32 counts followed by a 10-byte record
    header (type, model, pool slot) and a raw CAutomobile object.  re3's pool
    can also contain boats; those still need a separate ABI mapping, so fail
    explicitly instead of dropping them.
    """

    source = block.inner()
    if len(source) < 8:
        raise ConversionError("re3 vehicle pool is too short")
    car_count, boat_count = struct.unpack_from("<II", source)
    if boat_count:
        raise ConversionError(
            "re3 vehicle pool contains persisted mission boats "
            f"(counts={(car_count, boat_count)}); only mission-marked cars are supported"
        )

    source_object_size = RE3_CAR_3DS_SIZE if direction == "3ds-to-pc" else RE3_CAR_PC_SIZE
    target_object_size = RE3_CAR_PC_SIZE if direction == "3ds-to-pc" else RE3_CAR_3DS_SIZE
    source_record_size = 10 + source_object_size
    expected_size = 8 + car_count * source_record_size
    expect_size(f"{direction} re3 vehicle pool", source, expected_size)

    out = bytearray(source[:8])
    cursor = 8
    for index in range(car_count):
        record_header = source[cursor : cursor + 10]
        vehicle_type = U32.unpack_from(record_header, 0)[0]
        if vehicle_type != 0:  # VEHICLE_TYPE_CAR
            raise ConversionError(
                f"re3 vehicle record {index} has unsupported type {vehicle_type}; expected car"
            )
        object_start = cursor + 10
        object_end = object_start + source_object_size
        raw_object = source[object_start:object_end]
        converted_object = (
            convert_re3_car_3ds_to_pc(raw_object)
            if direction == "3ds-to-pc"
            else convert_re3_car_pc_to_3ds(raw_object)
        )
        expect_size("converted re3 CAutomobile", converted_object, target_object_size)
        out += record_header + converted_object
        cursor = object_end
    return nested_block(bytes(out))


def convert_vehicle_pool(variant: str, block: Block, direction: str) -> Block:
    counts = vehicle_counts(variant, block)
    if variant == "re3":
        return convert_re3_vehicle_pool(block, direction)
    if any(counts):
        raise ConversionError(
            "this converter currently refuses reVC saves containing persisted mission vehicles "
            f"(counts={counts}); reVC vehicle records still need a separate ABI mapping"
        )
    return block


def convert_phone_record_3ds_to_pc(raw: bytes) -> bytes:
    expect_size("3DS phone record", raw, 48)
    out = bytearray(52)
    out[:44] = raw[:44]
    U32.pack_into(out, 44, raw[44])
    out[48] = raw[45]
    return bytes(out)


def convert_phone_record_pc_to_3ds(raw: bytes) -> bytes:
    expect_size("PC phone record", raw, 52)
    out = bytearray(48)
    out[:44] = raw[:44]
    out[44] = raw[44]
    out[45] = raw[48]
    return bytes(out)


def convert_phones(block: Block, direction: str) -> Block:
    inner = block.inner()
    source_record_size = 48 if direction == "3ds-to-pc" else 52
    expect_size(f"{direction} phone block", inner, 8 + 50 * source_record_size)
    out = bytearray(inner[:8])
    for index in range(50):
        start = 8 + index * source_record_size
        raw = inner[start : start + source_record_size]
        out += (
            convert_phone_record_3ds_to_pc(raw)
            if direction == "3ds-to-pc"
            else convert_phone_record_pc_to_3ds(raw)
        )
    return nested_block(bytes(out))


# The garage block is deceptively important: the cars parked in the hideouts
# are serialized here, not in the persisted mission-vehicle pool.  A 3DS
# CStoredCar is 36 bytes while the PC-compatible record is 40 bytes.  The
# trailing CStoredCar inside each CGarage therefore shifts every following
# garage by four bytes as well.  Rebuilding this block is what keeps hidden
# garage cars from disappearing after a conversion.
GARAGE_LAYOUTS = {
    "re3": {
        "inner_size": 5484,
        "header": 40,
        "stored_count": 18,
        "pc_garage_stride": 140,
        "raw_garage_stride": 136,
        "stored_offset": 100,
    },
    "revc": {
        "inner_size": 7876,
        "header": 44,
        "stored_count": 48,
        "pc_garage_stride": 168,
        "raw_garage_stride": 164,
        "stored_offset": 128,
    },
}


def convert_stored_car_pc_to_3ds(raw: bytes) -> bytes:
    expect_size("PC stored car", raw, 40)
    out = bytearray(36)
    out[:28] = raw[:28]
    # PC stores the four-byte enum/bitfield as a word.  The ARM build has the
    # same value at the first byte and six one-byte attributes after it.
    out[28] = raw[28]
    out[29:35] = raw[32:38]
    return bytes(out)


def convert_stored_car_3ds_to_pc(raw: bytes) -> bytes:
    expect_size("3DS stored car", raw, 36)
    out = bytearray(40)
    out[:28] = raw[:28]
    U32.pack_into(out, 28, raw[28])
    out[32:38] = raw[29:35]
    return bytes(out)


def convert_garages(variant: str, block: Block, direction: str) -> Block:
    layout = GARAGE_LAYOUTS[variant]
    inner = block.inner()
    expect_size(f"{direction} garage block", inner, layout["inner_size"])
    source_car_stride = 36 if direction == "3ds-to-pc" else 40
    target_car_stride = 40 if direction == "3ds-to-pc" else 36
    source_garage_stride = (
        layout["raw_garage_stride"]
        if direction == "3ds-to-pc"
        else layout["pc_garage_stride"]
    )
    target_garage_stride = (
        layout["pc_garage_stride"]
        if direction == "3ds-to-pc"
        else layout["raw_garage_stride"]
    )

    cursor = layout["header"]
    out = bytearray(inner[: layout["header"]])
    car_converter = (
        convert_stored_car_3ds_to_pc
        if direction == "3ds-to-pc"
        else convert_stored_car_pc_to_3ds
    )
    for _ in range(layout["stored_count"]):
        out += car_converter(inner[cursor : cursor + source_car_stride])
        cursor += source_car_stride

    for _ in range(32):
        source = inner[cursor : cursor + source_garage_stride]
        expect_size("garage record", source, source_garage_stride)
        target = bytearray(target_garage_stride)
        offset = layout["stored_offset"]
        target[:offset] = source[:offset]
        target[offset : offset + target_car_stride] = car_converter(
            source[offset : offset + source_car_stride]
        )
        out += target
        cursor += source_garage_stride

    if cursor > len(inner):
        raise ConversionError(f"garage block parser stopped at {cursor}, expected {len(inner)}")
    target_tail_size = layout["inner_size"] - len(out)
    if target_tail_size < 0:
        raise ConversionError("converted garage block grew beyond its fixed size")
    # The save routine reserves a fixed garage block.  After the structured
    # arrays, the remaining bytes are only compiler/save-buffer reservation;
    # the loader never reads them.  Rebuild that tail as clean zero padding
    # instead of carrying stale bytes from the other ABI.
    out += bytes(target_tail_size)
    expect_size("converted garage block", out, layout["inner_size"])
    return nested_block(bytes(out))


def garage_car_records(variant: str, block: Block, direction: str) -> list[bytes]:
    """Return all stored-car records in canonical PC form for verification/UI."""
    layout = GARAGE_LAYOUTS[variant]
    inner = block.inner()
    source_stride = 36 if direction == "3ds-to-pc" else 40
    garage_stride = layout["raw_garage_stride"] if direction == "3ds-to-pc" else layout["pc_garage_stride"]
    cursor = layout["header"]
    records: list[bytes] = []
    for _ in range(layout["stored_count"]):
        raw = inner[cursor : cursor + source_stride]
        if direction == "3ds-to-pc":
            records.append(convert_stored_car_3ds_to_pc(raw))
        else:
            # The final two bytes of the 40-byte PC struct are compiler
            # padding and can contain stale save data.  Ignore them when
            # comparing semantic hidden-car records.
            records.append(raw[:38] + b"\0\0")
        cursor += source_stride
    for _ in range(32):
        raw = inner[cursor + layout["stored_offset"] : cursor + layout["stored_offset"] + source_stride]
        if direction == "3ds-to-pc":
            records.append(convert_stored_car_3ds_to_pc(raw))
        else:
            records.append(raw[:38] + b"\0\0")
        cursor += garage_stride
    return records


def convert_re3_zone(raw: bytes) -> bytes:
    expect_size("re3 3DS zone", raw, 52)
    out = bytearray(56)
    out[:32] = raw[:32]
    U32.pack_into(out, 32, raw[32])
    U32.pack_into(out, 36, raw[33])
    out[40:44] = raw[34:38]
    out[44:56] = raw[40:52]
    return bytes(out)


def convert_re3_zone_pc_to_3ds(raw: bytes) -> bytes:
    expect_size("re3 PC zone", raw, 56)
    out = bytearray(52)
    out[:32] = raw[:32]
    out[32] = raw[32]
    out[33] = raw[36]
    out[34:38] = raw[40:44]
    out[40:52] = raw[44:56]
    return bytes(out)


def convert_revc_zone(raw: bytes) -> bytes:
    expect_size("reVC 3DS zone", raw, 50)
    out = bytearray(56)
    out[:32] = raw[:32]
    U32.pack_into(out, 32, raw[32])
    U32.pack_into(out, 36, raw[33])
    out[40:56] = raw[34:50]
    return bytes(out)


def convert_revc_zone_pc_to_3ds(raw: bytes) -> bytes:
    expect_size("reVC PC zone", raw, 56)
    out = bytearray(50)
    out[:32] = raw[:32]
    out[32] = raw[32]
    out[33] = raw[36]
    out[34:50] = raw[40:56]
    return bytes(out)


def set_save_header_length(payload: bytearray) -> None:
    if payload[:4] != b"ZNS\0":
        raise ConversionError(f"zone block has unexpected tag {payload[:4]!r}")
    U32.pack_into(payload, 4, len(payload) - 8)


def convert_re3_zones(block: Block) -> Block:
    inner = block.inner()
    expect_size("re3 3DS zones", inner, 9797)
    if inner[:4] != b"ZNS\0":
        raise ConversionError("re3 zone header is missing")

    # The ARM serializer uses short enums and a one-byte alignment pad in the
    # header.  Both the normal zone array and the map-zone array contain 50
    # and 25 CZone records respectively; the old converter accidentally
    # treated the first array as 75 records, shifting every field after it.
    out = bytearray(inner[:12])
    out += U32.pack(inner[12])
    out += inner[13:15]
    out += b"\0\0"

    raw_zone_start = 16
    for index in range(RE3_ZONE_COUNT):
        start = raw_zone_start + index * 52
        out += convert_re3_zone(inner[start : start + 52])

    raw_info_start = raw_zone_start + RE3_ZONE_COUNT * 52
    raw_info_end = raw_info_start + 100 * 58
    out += inner[raw_info_start:raw_info_end]
    out += inner[raw_info_end : raw_info_end + 4]  # TotalNumberOfZones/Infos

    raw_map_start = raw_info_end + 4
    for index in range(25):
        start = raw_map_start + index * 52
        out += convert_re3_zone(inner[start : start + 52])

    raw_after_map = raw_map_start + 25 * 52
    out += inner[raw_after_map:-1]  # audio-zone array and final counters
    set_save_header_length(out)
    expect_size("re3 PC zones", out, 10100)
    return nested_block(bytes(out))


def convert_revc_zones(block: Block) -> Block:
    inner = block.inner()
    expect_size("reVC 3DS zones", inner, 34437)
    if inner[:4] != b"ZNS\0":
        raise ConversionError("reVC zone header is missing")

    out = bytearray(inner[:8])
    out += U32.pack(inner[8])
    out += inner[9:11]
    out += b"\0\0"
    cursor = 13
    for _ in range(20 + 169):
        out += convert_revc_zone(inner[cursor : cursor + 50])
        cursor += 50

    zone_info_bytes = 338 * 68
    out += inner[cursor : cursor + zone_info_bytes]
    cursor += zone_info_bytes
    out += inner[cursor : cursor + 8]
    cursor += 8

    for _ in range(39):
        out += convert_revc_zone(inner[cursor : cursor + 50])
        cursor += 50
    out += inner[cursor:]
    set_save_header_length(out)
    expect_size("reVC PC zones", out, 35808)
    return nested_block(bytes(out))


def convert_re3_zones_pc_to_3ds(block: Block) -> Block:
    inner = block.inner()
    expect_size("re3 PC zones", inner, 10100)
    if inner[:4] != b"ZNS\0":
        raise ConversionError("re3 zone header is missing")

    # The ARM serializer had a one-byte enum and two bytes of short-enum
    # header where the PC serializer writes two full words.  The two bytes
    # which were never serialized are deterministic padding.
    out = bytearray(inner[:12])
    out += inner[12:13]
    out += inner[16:18]
    out += b"\0"

    pc_zone_start = 20
    for index in range(RE3_ZONE_COUNT):
        start = pc_zone_start + index * 56
        out += convert_re3_zone_pc_to_3ds(inner[start : start + 56])

    pc_info_start = pc_zone_start + RE3_ZONE_COUNT * 56
    pc_info_end = pc_info_start + 100 * 58
    out += inner[pc_info_start:pc_info_end]
    out += inner[pc_info_end : pc_info_end + 4]  # TotalNumberOfZones/Infos

    pc_map_start = pc_info_end + 4
    for index in range(25):
        start = pc_map_start + index * 56
        out += convert_re3_zone_pc_to_3ds(inner[start : start + 56])

    pc_after_map = pc_map_start + 25 * 56
    out += inner[pc_after_map:]
    out += b"\0"
    set_save_header_length(out)
    expect_size("re3 3DS zones", out, 9797)
    return nested_block(bytes(out))


def convert_revc_zones_pc_to_3ds(block: Block) -> Block:
    inner = block.inner()
    expect_size("reVC PC zones", inner, 35808)
    if inner[:4] != b"ZNS\0":
        raise ConversionError("reVC zone header is missing")

    out = bytearray(inner[:8])
    out += inner[8:9]
    out += inner[12:14]
    out += b"\0\0"
    cursor = 16
    for _ in range(20 + 169):
        out += convert_revc_zone_pc_to_3ds(inner[cursor : cursor + 56])
        cursor += 56

    zone_info_bytes = 338 * 68
    out += inner[cursor : cursor + zone_info_bytes]
    cursor += zone_info_bytes
    out += inner[cursor : cursor + 8]
    cursor += 8

    for _ in range(39):
        out += convert_revc_zone_pc_to_3ds(inner[cursor : cursor + 56])
        cursor += 56
    out += inner[cursor:]
    set_save_header_length(out)
    expect_size("reVC 3DS zones", out, 34437)
    return nested_block(bytes(out))


PARTICLE_LAYOUTS = {
    "re3": {
        "raw_size": 128,
        "pc_size": 136,
        "raw": (52, 88, 92, 93, 94, 95, 96, 98, 100, 112, 116, 120, 124, 125),
        "pc": (52, 88, 92, 96, 100, 101, 102, 104, 108, 120, 124, 128, 132, 133),
        "max": 100,
    },
    "revc": {
        "raw_size": 124,
        "pc_size": 132,
        "raw": (48, 84, 88, 89, 90, 91, 92, 94, 96, 108, 112, 116, 120, 121),
        "pc": (48, 84, 88, 92, 96, 97, 98, 100, 104, 116, 120, 124, 128, 129),
        "max": 70,
    },
}


def convert_particle_record(variant: str, raw: bytes, direction: str) -> bytes:
    layout = PARTICLE_LAYOUTS[variant]
    source_size = layout["raw_size"] if direction == "3ds-to-pc" else layout["pc_size"]
    target_size = layout["pc_size"] if direction == "3ds-to-pc" else layout["raw_size"]
    expect_size(f"{direction} particle object", raw, source_size)
    ro = layout["raw"]
    po = layout["pc"]
    source_offsets, target_offsets = (ro, po) if direction == "3ds-to-pc" else (po, ro)
    out = bytearray(target_size)
    widths = (12, 4, 1, 1, 1, 1, 2, 2, 12, 4, 4, 4, 1, 1)
    for index, width in enumerate(widths):
        if index in (2, 3):
            out[target_offsets[index]] = raw[source_offsets[index]]
        else:
            out[target_offsets[index] : target_offsets[index] + width] = raw[
                source_offsets[index] : source_offsets[index] + width
            ]
    return bytes(out)


def convert_particles(variant: str, block: Block, direction: str) -> Block:
    inner = block.inner()
    if len(inner) < 4:
        raise ConversionError("particle block is too short")
    layout = PARTICLE_LAYOUTS[variant]
    count = U32.unpack_from(inner)[0]
    if count > layout["max"]:
        raise ConversionError(f"particle count {count} exceeds {variant} maximum")
    source_size = layout["raw_size"] if direction == "3ds-to-pc" else layout["pc_size"]
    target_size = layout["pc_size"] if direction == "3ds-to-pc" else layout["raw_size"]
    expected = 4 + (count + 1) * source_size
    expect_size(f"{direction} particle block", inner, expected)
    out = bytearray(U32.pack(count))
    cursor = 4
    for _ in range(count):
        out += convert_particle_record(variant, inner[cursor : cursor + source_size], direction)
        cursor += source_size
    out += bytes(target_size)
    return nested_block(bytes(out))


def preserve_player_info(variant: str, block: Block, direction: str) -> Block:
    inner = block.inner()
    target_size = 316 if variant == "re3" else 368
    if direction == "3ds-to-pc":
        expect_size(f"{variant} 3DS PlayerInfo", inner, target_size)
        return block
    # PC builds may reserve the larger GTA_PC class (re3: 360, reVC: 416),
    # while the ARM target reserves 316/368 bytes.  SavePlayerInfo only writes
    # a pointer-free prefix, so truncating the unused reservation is safe.
    if len(inner) not in {target_size, target_size + (44 if variant == "re3" else 48)}:
        raise ConversionError(
            f"{variant} PC PlayerInfo: expected {target_size} or a PC reservation, got {len(inner)}"
        )
    return nested_block(inner[:target_size])
    # SavePlayerInfo serializes only a small, pointer-free prefix and merely
    # reserves sizeof(CPlayerInfo) for the rest. PC loaders ignore the supplied
    # block size after reading that prefix. Keeping the 32-bit size matches
    # original-PC saves and is also accepted by the 64-bit re3/reVC builds.
    return block


def conversion_indices(variant: str) -> dict[str, int]:
    if variant == "re3":
        return {"ped": 1, "garage": 2, "vehicle": 3, "phones": 8, "zones": 11, "particles": 14, "player_info": 16}
    return {"ped": 1, "garage": 2, "vehicle": 4, "phones": 9, "zones": 12, "particles": 15, "player_info": 18}


def classify_format(save: SaveFile) -> str:
    idx = conversion_indices(save.variant)
    ped_size = len(save.blocks[idx["ped"]].inner())
    raw = 1494 if save.variant == "re3" else 1735
    pc = 1566 if save.variant == "re3" else 1795
    if ped_size == raw:
        return "3ds-arm"
    if ped_size == pc:
        return "pc-compatible"
    return f"unknown-ped-size-{ped_size}"


def convert(save: SaveFile, direction: str = "pc-to-3ds") -> bytes:
    fmt = classify_format(save)
    expected_source = "pc-compatible" if direction == "pc-to-3ds" else "3ds-arm"
    if fmt != expected_source:
        target_name = "3DS ARM" if direction == "pc-to-3ds" else "PC-compatible"
        raise ConversionError(f"input is {fmt}; {direction} expects {target_name} source data")

    variant = save.variant
    count = MEANINGFUL_BLOCKS[variant]
    blocks = list(save.blocks[:count])
    idx = conversion_indices(variant)
    blocks[idx["vehicle"]] = convert_vehicle_pool(variant, blocks[idx["vehicle"]], direction)
    blocks[idx["ped"]] = convert_ped_pool(variant, blocks[idx["ped"]], direction)
    blocks[idx["garage"]] = convert_garages(variant, blocks[idx["garage"]], direction)
    blocks[idx["phones"]] = convert_phones(blocks[idx["phones"]], direction)
    if variant == "re3":
        blocks[idx["zones"]] = (
            convert_re3_zones(blocks[idx["zones"]])
            if direction == "3ds-to-pc"
            else convert_re3_zones_pc_to_3ds(blocks[idx["zones"]])
        )
    else:
        blocks[idx["zones"]] = (
            convert_revc_zones(blocks[idx["zones"]])
            if direction == "3ds-to-pc"
            else convert_revc_zones_pc_to_3ds(blocks[idx["zones"]])
        )
    blocks[idx["particles"]] = convert_particles(variant, blocks[idx["particles"]], direction)
    blocks[idx["player_info"]] = preserve_player_info(variant, blocks[idx["player_info"]], direction)

    # Compare the complete set of hideout cars before encoding the file.  This
    # is deliberately byte-level for the canonical 40-byte PC record, so a
    # conversion can never silently drop a hidden garage vehicle.
    reverse = "pc-to-3ds" if direction == "3ds-to-pc" else "3ds-to-pc"
    if garage_car_records(variant, save.blocks[idx["garage"]], direction) != garage_car_records(
        variant, blocks[idx["garage"]], reverse
    ):
        raise ConversionError("garage hidden-car records changed during conversion")

    body = b"".join(block.encoded() for block in blocks)
    target_body_size = FIXED_FILE_SIZES[variant] - 4
    remaining = target_body_size - len(body)
    if remaining < 8 or remaining % 4:
        raise ConversionError(f"cannot represent {remaining} bytes of save padding")
    while remaining:
        payload = min(MAX_PADDING_PAYLOAD, remaining - 4)
        payload &= ~3
        if payload <= 4:
            raise ConversionError(f"invalid final padding payload {payload}")
        body += U32.pack(payload) + bytes(payload)
        remaining = target_body_size - len(body)

    if len(body) != target_body_size:
        raise ConversionError("rebuilt save has the wrong total size")
    result = body + U32.pack(checksum(body))
    verify_converted_bytes(save.path, result, variant, direction)
    return result


def verify_converted_bytes(path: Path, data: bytes, variant: str, direction: str = "3ds-to-pc") -> None:
    if len(data) != FIXED_FILE_SIZES[variant]:
        raise ConversionError("converted output length changed")
    stored = U32.unpack_from(data, len(data) - 4)[0]
    if checksum(data[:-4]) != stored:
        raise ConversionError("converted output checksum is invalid")

    # Parse through a temporary in-memory equivalent without writing a file.
    offset = 0
    blocks: list[Block] = []
    while offset < len(data) - 4:
        size = U32.unpack_from(data, offset)[0]
        blocks.append(Block(offset, data[offset + 4 : offset + 4 + size]))
        offset += 4 + align4(size)
    idx = conversion_indices(variant)
    raw_ped = 1494 if variant == "re3" else 1735
    pc_ped = 1566 if variant == "re3" else 1795
    raw_phone = 8 + 50 * 48
    pc_phone = 8 + 50 * 52
    raw_zones = 9797 if variant == "re3" else 34437
    pc_zones = 10100 if variant == "re3" else 35808
    raw_particle = {"re3": 128, "revc": 124}[variant]
    pc_particle = {"re3": 136, "revc": 132}[variant]
    expected = {
        "ped": pc_ped if direction == "3ds-to-pc" else raw_ped,
        "garage": GARAGE_LAYOUTS[variant]["inner_size"],
        "phones": pc_phone if direction == "3ds-to-pc" else raw_phone,
        "zones": pc_zones if direction == "3ds-to-pc" else raw_zones,
        "particles": None,
        "player_info": 316 if variant == "re3" else 368,
    }
    for name, size in expected.items():
        if size is None:
            particle_inner = blocks[idx[name]].inner()
            count = U32.unpack_from(particle_inner)[0]
            particle_size = pc_particle if direction == "3ds-to-pc" else raw_particle
            size = 4 + (count + 1) * particle_size
        actual = len(blocks[idx[name]].inner())
        if actual != size:
            raise ConversionError(f"converted {name} size is {actual}, expected {size}")

    counts = vehicle_counts(variant, blocks[idx["vehicle"]])
    if variant == "re3":
        cars, boats = counts
        if boats:
            raise ConversionError("converted re3 vehicle pool still contains unsupported boats")
        object_size = RE3_CAR_PC_SIZE if direction == "3ds-to-pc" else RE3_CAR_3DS_SIZE
        expected_vehicle = 8 + cars * (10 + object_size)
    else:
        expected_vehicle = 12
    actual_vehicle = len(blocks[idx["vehicle"]].inner())
    if actual_vehicle != expected_vehicle:
        raise ConversionError(
            f"converted vehicle pool size is {actual_vehicle}, expected {expected_vehicle}"
        )


def default_output(input_path: Path, direction: str = "pc-to-3ds") -> Path:
    suffix = ".3ds" if direction == "pc-to-3ds" else ".pc"
    return input_path.with_name(input_path.stem + suffix + input_path.suffix)


def print_inspection(save: SaveFile) -> None:
    idx = conversion_indices(save.variant)
    print(f"file: {save.path}")
    print(f"game: {save.variant}")
    print(f"format: {classify_format(save)}")
    print(f"size: {len(save.raw)}")
    print(f"sha256: {sha256(save.raw)}")
    print(f"checksum: 0x{save.checksum:08X} (valid)")
    print(f"blocks: {len(save.blocks)}")
    print(f"vehicle counts: {vehicle_counts(save.variant, save.blocks[idx['vehicle']])}")
    cars = garage_car_records(save.variant, save.blocks[idx["garage"]], "3ds-to-pc" if classify_format(save) == "3ds-arm" else "pc-to-3ds")
    print(f"garage stored-car records: {len(cars)} (non-empty: {sum(U32.unpack_from(car)[0] != 0 for car in cars)})")
    for name in ("ped", "phones", "zones", "particles", "player_info"):
        inner = save.blocks[idx[name]].inner()
        print(f"{name} inner size: {len(inner)}")


def build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(
        description="Convert 2021 re3/reVC PC and 3DS save ABIs"
    )
    parser.add_argument("input", type=Path, help="GTA3sf*.b or GTAVCsf*.b input")
    parser.add_argument(
        "--direction",
        choices=("pc-to-3ds", "3ds-to-pc"),
        default="pc-to-3ds",
        help="conversion direction (default: pc-to-3ds)",
    )
    parser.add_argument("-o", "--output", type=Path, help="output path (default: *.3ds.b or *.pc.b)")
    parser.add_argument("--inspect", action="store_true", help="inspect only; do not convert")
    parser.add_argument("--force", action="store_true", help="replace an existing output file")
    return parser


def main(argv: list[str] | None = None) -> int:
    args = build_parser().parse_args(argv)
    try:
        save = parse_save(args.input)
        if args.inspect:
            print_inspection(save)
            return 0

        output = args.output or default_output(args.input, args.direction)
        if output.exists() and not args.force:
            raise ConversionError(f"output already exists: {output} (use --force to replace)")
        result = convert(save, args.direction)
        output.parent.mkdir(parents=True, exist_ok=True)
        output.write_bytes(result)
        print(f"converted: {args.input} -> {output}")
        print(f"game: {save.variant}")
        print(f"size: {len(result)}")
        print(f"sha256: {sha256(result)}")
        print(f"checksum: 0x{U32.unpack_from(result, len(result) - 4)[0]:08X} (valid)")
        return 0
    except (ConversionError, OSError) as error:
        print(f"error: {error}", file=sys.stderr)
        return 2


if __name__ == "__main__":
    raise SystemExit(main())
