# re3/reVC PC ↔ 3DS save converter

This standalone tool converts the 2021 re3/reVC save ABI in either direction.
The primary path is PC → 3DS; the optional reverse path is 3DS → PC. It does
not modify either game's source code and never overwrites the input by default.
The browser version is `save_converter.html`: it is a single offline file with
the same conversion rules and does not upload the save anywhere.

## Why the unconverted saves crash

The 3DS builds have `COMPATIBLE_SAVES` disabled. Several save routines therefore
copy live C++ objects directly into the file. devkitARM uses a 32-bit ARM ABI
with short enums, while original PC and compatible re3/reVC saves use the PC
layout. The file checksum and total size can still be valid, but object records
have different sizes and offsets. A PC loader then reads later fields from the
wrong positions.

Known 3DS-to-PC record changes include:

- `CPlayerPed`: 1448 to 1520 bytes (re3), 1692 to 1752 bytes (reVC)
- `CPhone`: 48 to 52 bytes
- `CZone`: 52 to 56 bytes (re3)
- re3 map-zone `CZone`: 52 to 56 bytes (the 25-record map array is separate
  from the 50-record normal zone array)
- serialized reVC zone: 50 to 56 bytes
- `CParticleObject`: 128 to 136 bytes (re3), 124 to 132 bytes (reVC)

The converter rebuilds those records field by field, regenerates padding, and
recalculates the additive checksum. For re3, persisted mission-marked cars are
also supported: the fixed 1448-byte PC-compatible `CAutomobile` record is
trimmed to the 1436-byte raw ARM object (and padded in the reverse direction).
The target loader consumes the meaningful fields before that ABI tail; the
tail is reserved by the compatible save path and is not guessed. The garage
block is rebuilt too: each
`CStoredCar` is converted from 40 bytes (PC) to 36 bytes (ARM), including the
stored car embedded in every `CGarage`. This keeps safe-house/garage hidden
vehicles and shifts every following garage record back to the correct offset.
Unknown layouts and persisted mission boats are rejected instead of producing a
guessed save. reVC mission vehicles still need their own mapping; re3 mission
cars are supported by the converter.

The re3 zone block has two different `CZone` arrays. Both arrays must be
converted; treating the normal array as 75 records shifts the zone counters,
so a later `IS_PLAYER_IN_ZONE` command receives `-1` and the 3DS build aborts
when it dereferences the missing zone. The converter now handles all 50 normal
zones and all 25 map zones explicitly.

`CPlayerInfo` is intentionally kept at its original 32-bit block size. Only a
small pointer-free prefix is serialized, both original-PC and 64-bit re builds
accept that size, and preserving it avoids changing bytes that do not need
conversion.

## Usage

```sh
# Main path: PC → 3DS (the default)
python3 convert_save.py /path/to/GTA3sf1.b
python3 convert_save.py /path/to/GTAVCsf8.b -o /path/to/GTAVCsf8.3ds.b

# Optional reverse path: 3DS → PC
python3 convert_save.py --direction 3ds-to-pc /path/to/GTA3sf1.b

python3 convert_save.py /path/to/GTA3sf1.b --inspect
```

The default output names are `GTA3sf1.3ds.b` / `GTAVCsf8.3ds.b` for PC → 3DS,
and `GTA3sf1.pc.b` / `GTAVCsf1.pc.b` for 3DS → PC. Rename a copy to the slot
name expected by the target game only after preserving the original.

## Current safety boundary

re3 persisted mission cars are supported in both directions. A re3 save with
persisted mission boats is rejected because the boat ABI still needs a separate
mapping. reVC persisted mission vehicles remain outside the current boundary.
Normal traffic is not stored in this block and does not trigger the
restriction. Garage hidden cars are a separate structure and are supported by
both directions.
