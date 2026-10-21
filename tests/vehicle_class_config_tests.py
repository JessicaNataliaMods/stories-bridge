"""Directed LCS-to-SA vehicle-class config checks using authored rows only."""

import sys
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tools"))

from vehicle_config_fields import (  # noqa: E402
    convert_handling,
    convert_ide,
    convert_special_handling,
)


def ide_row(vehicle_type="bike", handling="MOPED", misc="10", wheel_scale="1.0"):
    animation_file = "bikev" if vehicle_type == "bike" else "null"
    return [
        "100", "testmodel", "testtxd", vehicle_type, handling, "TEST",
        animation_file, "moped", "20", "0", "0", misc, wheel_scale, "0.0",
    ]


class VehicleClassConfigTests(unittest.TestCase):
    def test_bike_class_flag_and_special_row_keep_original_values(self):
        handling_values = ["1.0"] * 30
        handling_values[14] = "F"
        handling_values[15] = "P"
        row = ["MOPED"] + handling_values + ["10000", "1", "1"]

        converted = convert_handling(row, "NATIVE_BIKE", ide_row())
        self.assertEqual(converted["fields"][31], "1000000")
        self.assertEqual(converted["unmapped_flags"], 0)
        self.assertEqual(converted["fields"][35], "5")

        values = [str(index) for index in range(1, 16)]
        line = convert_special_handling(
            "! MOPED " + " ".join(values), "MOPED", "NATIVE_BIKE", "bike"
        )[0]
        self.assertEqual(line, "! NATIVE_BIKE " + " ".join(values))

    def test_boat_special_fields_use_sa_loader_order(self):
        values = [str(index) for index in range(1, 15)]
        line = convert_special_handling(
            "% PREDATOR " + " ".join(values), "PREDATOR", "NATIVE_BOAT", "boat"
        )[0]
        expected = values[:7] + values[8:14] + values[7:8]
        self.assertEqual(line, "% NATIVE_BOAT " + " ".join(expected))

    def test_nonflight_key_uses_effective_lcs_flying_slot_zero(self):
        maverick = [str(index) for index in range(1, 19)]
        dodo = [str(index) for index in range(21, 39)]
        source = "$ MAVERICK " + " ".join(maverick) + "\n$ DODO " + " ".join(dodo)

        line = convert_special_handling(source, "DODO", "NATIVE_PLANE", "plane")[0]
        fields = line.split()
        self.assertEqual(fields[:2], ["$", "NATIVE_PLANE"])
        self.assertEqual(fields[2:13], dodo[:11])
        self.assertEqual(fields[13:16], ["0.0", "0.0", "0.0"])
        self.assertEqual(fields[16:], dodo[11:])

    def test_dodo_source_car_misc_remains_a_wheel_id_for_plane_target(self):
        row = ide_row("car", "DODO", "256", "1.0")
        converted = convert_ide(
            row,
            model_id=900,
            model_name="NATIVE_DODO",
            texture_dictionary="NATIVE_DODO",
            handling_name="NATIVE_PLANE",
            wheel_model_asset="embedded-original",
            physical_class="plane",
        )
        self.assertEqual(converted["fields"][3], "plane")
        self.assertEqual(converted["fields"][11], "-1")
        self.assertEqual(converted["fields"][12:14], ["1.0", "1.0"])
        self.assertEqual(converted["provenance"][11]["target_field"], "wheel_model_id")

    def test_plane_lod_cannot_be_reused_as_sa_wheel_model(self):
        row = ide_row("plane", "DODO", "256", "1.0")
        with self.assertRaisesRegex(ValueError, "plane LOD model ID"):
            convert_ide(
                row,
                model_id=901,
                model_name="NATIVE_PLANE",
                texture_dictionary="NATIVE_PLANE",
                handling_name="NATIVE_PLANE",
            )


if __name__ == "__main__":
    unittest.main()
