# re3/reVC PC ↔ 3DS save converter

Converts supported GTA III and Vice City saves between PC and 3DS.
Inputs are preserved by default. Back up your saves before converting.

Open `save_converter.html` for the offline browser version. Nothing is uploaded.

## Compatibility

PC and 3DS saves use different record layouts. This tool converts the supported
records and recalculates the checksum; renaming an unconverted save will not work.

- Supports garage cars, including hidden vehicles, in both directions.
- Supports GTA III persisted mission cars.
- Rejects GTA III persisted mission boats and unsupported layouts.
- Vice City persisted mission vehicles are not supported.
- Normal traffic does not trigger the mission-vehicle restriction.

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
