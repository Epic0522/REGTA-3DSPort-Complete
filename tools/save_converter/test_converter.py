#!/usr/bin/env python3

import tempfile
import unittest
import struct
from pathlib import Path

import convert_save


RAW_SAMPLES = {
    "re3": Path("/Users/epicreds/Desktop/save/GTA3sf1.b"),
    "revc": Path("/Users/epicreds/Desktop/save/GTAVCsf1.b"),
}
PC_SAMPLES = {
    "re3": Path("/Users/epicreds/Games/re3/userfiles/GTA3sf2.b"),
    "revc": Path("/Users/epicreds/Games/reVC/userfiles/GTAVCsf1.b"),
}


class ConverterTests(unittest.TestCase):
    def _roundtrip(self, path: Path, direction: str) -> tuple[convert_save.SaveFile, bytes, convert_save.SaveFile]:
        source = convert_save.parse_save(path)
        original_hash = convert_save.sha256(source.raw)
        result = convert_save.convert(source, direction)
        convert_save.verify_converted_bytes(path, result, source.variant, direction)
        self.assertEqual(convert_save.sha256(path.read_bytes()), original_hash)
        with tempfile.NamedTemporaryFile(suffix=".b") as output:
            output.write(result)
            output.flush()
            converted = convert_save.parse_save(Path(output.name))
        return source, result, converted

    def test_supplied_3ds_samples_convert_to_pc(self):
        for variant, path in RAW_SAMPLES.items():
            with self.subTest(variant=variant):
                source, result, converted = self._roundtrip(path, "3ds-to-pc")
                self.assertEqual(source.variant, variant)
                self.assertEqual(convert_save.classify_format(source), "3ds-arm")
                self.assertEqual(convert_save.classify_format(converted), "pc-compatible")
                idx = convert_save.conversion_indices(variant)
                self.assertEqual(
                    converted.blocks[idx["player_info"]].inner(),
                    source.blocks[idx["player_info"]].inner(),
                )
                self.assertEqual(
                    convert_save.garage_car_records(variant, source.blocks[idx["garage"]], "3ds-to-pc"),
                    convert_save.garage_car_records(variant, converted.blocks[idx["garage"]], "pc-to-3ds"),
                )
                self.assertGreater(
                    sum(int.from_bytes(car[:4], "little") != 0 for car in convert_save.garage_car_records(variant, source.blocks[idx["garage"]], "3ds-to-pc")),
                    0,
                )

    def test_pc_samples_convert_to_3ds_and_keep_garage_cars(self):
        for variant, path in PC_SAMPLES.items():
            with self.subTest(variant=variant):
                source, result, converted = self._roundtrip(path, "pc-to-3ds")
                self.assertEqual(source.variant, variant)
                self.assertEqual(convert_save.classify_format(source), "pc-compatible")
                self.assertEqual(convert_save.classify_format(converted), "3ds-arm")
                idx = convert_save.conversion_indices(variant)
                self.assertEqual(
                    convert_save.garage_car_records(variant, source.blocks[idx["garage"]], "pc-to-3ds"),
                    convert_save.garage_car_records(variant, converted.blocks[idx["garage"]], "3ds-to-pc"),
                )
                if variant == "re3":
                    # Both the normal 50-zone array and the 25 map-zone array
                    # must be narrowed; leaving the latter at the PC stride
                    # shifts the counters and makes every zone lookup return
                    # -1 on the 3DS (the crash-dump symptom).
                    zones = converted.blocks[idx["zones"]].inner()
                    self.assertEqual(zones.find(b"THEMAP"), 16 + 50 * 52 + 100 * 58 + 4)
                    self.assertNotEqual(
                        struct.unpack_from("<H", zones, 16 + 50 * 52 + 100 * 58)[0],
                        0,
                    )

    def test_default_direction_is_pc_to_3ds(self):
        source = convert_save.parse_save(PC_SAMPLES["re3"])
        result = convert_save.convert(source)
        with tempfile.NamedTemporaryFile(suffix=".b") as output:
            output.write(result)
            output.flush()
            converted = convert_save.parse_save(Path(output.name))
        self.assertEqual(convert_save.classify_format(converted), "3ds-arm")

    def test_cli_write_keeps_source_untouched(self):
        source_path = PC_SAMPLES["re3"]
        before = source_path.read_bytes()
        with tempfile.TemporaryDirectory() as directory:
            output = Path(directory) / "GTA3sf2.3ds.b"
            code = convert_save.main([str(source_path), "-o", str(output)])
            self.assertEqual(code, 0)
            self.assertTrue(output.exists())
            self.assertEqual(source_path.read_bytes(), before)

    def test_wrong_direction_and_vehicle_pool_conversion(self):
        raw = convert_save.parse_save(RAW_SAMPLES["revc"])
        with self.assertRaises(convert_save.ConversionError):
            convert_save.convert(raw, "pc-to-3ds")

        pc_with_vehicle = convert_save.parse_save(Path("/Users/epicreds/Games/re3/userfiles/GTA3sf1.b"))
        result = convert_save.convert(pc_with_vehicle, "pc-to-3ds")
        convert_save.verify_converted_bytes(
            pc_with_vehicle.path, result, pc_with_vehicle.variant, "pc-to-3ds"
        )
        with tempfile.NamedTemporaryFile(suffix=".b") as output:
            output.write(result)
            output.flush()
            converted = convert_save.parse_save(Path(output.name))
        idx = convert_save.conversion_indices("re3")
        self.assertEqual(convert_save.vehicle_counts("re3", converted.blocks[idx["vehicle"]]), (1, 0))
        vehicle_inner = converted.blocks[idx["vehicle"]].inner()
        source_vehicle_inner = pc_with_vehicle.blocks[idx["vehicle"]].inner()
        self.assertEqual(len(vehicle_inner), 8 + 10 + convert_save.RE3_CAR_3DS_SIZE)
        self.assertEqual(vehicle_inner[8:18], source_vehicle_inner[8:18])
        self.assertEqual(vehicle_inner[8 + 10 + 0x1F4], 2)

        roundtrip = convert_save.convert(converted, "3ds-to-pc")
        roundtrip_blocks = []
        offset = 0
        while offset < len(roundtrip) - 4:
            size = int.from_bytes(roundtrip[offset : offset + 4], "little")
            roundtrip_blocks.append(roundtrip[offset + 4 : offset + 4 + size])
            offset += 4 + ((size + 3) & ~3)
        self.assertEqual(roundtrip_blocks[idx["vehicle"]], pc_with_vehicle.blocks[idx["vehicle"]].data)


if __name__ == "__main__":
    unittest.main()
