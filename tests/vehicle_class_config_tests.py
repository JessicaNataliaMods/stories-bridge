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
    def test_ferry_boat_physics_requires_dedicated_binding_and_reorders_camera(self):
        text = '% FERRY ' + ' '.join(str(i) for i in range(1, 15)) + '\n'
        with self.assertRaises(ValueError):
            convert_special_handling(text, 'FERRY', 'TROPIC', 'boat')
        with self.assertRaises(ValueError):
            convert_special_handling(text, 'FERRY', 'OTHER', 'boat', dedicated_ferry_adapter=True)
        row = convert_special_handling(text, 'FERRY', 'TROPIC', 'boat', dedicated_ferry_adapter=True)[0].split()
        self.assertEqual(row, ['%', 'TROPIC'] + [str(i) for i in range(1,8)] + [str(i) for i in range(9,15)] + ['8'])

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
        source = (
            "DODO 1\n$ MAVERICK " + " ".join(maverick)
            + "\n$ DODO " + " ".join(dodo)
        )

        line = convert_special_handling(
            source, "DODO", "NATIVE_PLANE", "plane", native_handling_id=186
        )[0]
        fields = line.split()
        self.assertEqual(fields[:2], ["$", "NATIVE_PLANE"])
        self.assertEqual(fields[2:13], dodo[:11])
        self.assertEqual(fields[13:16], ["0.0", "0.0", "0.0"])
        self.assertEqual(fields[16:], dodo[11:])

    def test_dodo_source_car_misc_remains_a_wheel_id_for_flagged_plane_target(self):
        row = ide_row("car", "DODO", "256", "1.0")
        converted = convert_ide(
            row,
            model_id=900,
            model_name="NATIVE_DODO",
            texture_dictionary="NATIVE_DODO",
            handling_name="NATIVE_PLANE",
            wheel_model_asset="embedded-original",
            physical_class="plane",
            lcs_handling_flags=0x00040000,
        )
        self.assertEqual(converted["fields"][3], "plane")
        self.assertEqual(converted["fields"][11], "-1")
        self.assertEqual(converted["fields"][12:14], ["1.0", "1.0"])
        self.assertEqual(converted["provenance"][11]["target_field"], "wheel_model_id")

    def test_source_car_heli_requires_matching_handling_class_flag(self):
        row = ide_row("car", "MAVERICK", "256", "1.0")
        with self.assertRaisesRegex(ValueError, "requires original handling flags"):
            convert_ide(
                row,
                model_id=902,
                model_name="NATIVE_MAVERICK",
                texture_dictionary="NATIVE_MAVERICK",
                handling_name="NATIVE_HELI",
                wheel_model_asset="embedded-original",
                physical_class="heli",
            )
        with self.assertRaisesRegex(ValueError, "does not match its handling flags"):
            convert_ide(
                row,
                model_id=902,
                model_name="NATIVE_MAVERICK",
                texture_dictionary="NATIVE_MAVERICK",
                handling_name="NATIVE_HELI",
                wheel_model_asset="embedded-original",
                physical_class="heli",
                lcs_handling_flags=0x00040000,
            )

        converted = convert_ide(
            row,
            model_id=902,
            model_name="NATIVE_MAVERICK",
            texture_dictionary="NATIVE_MAVERICK",
            handling_name="NATIVE_HELI",
            wheel_model_asset="embedded-original",
            physical_class="heli",
            lcs_handling_flags=0x00020000,
        )
        self.assertEqual(converted["fields"][3], "heli")
        self.assertEqual(converted["fields"][11], "-1")
        self.assertEqual(converted["provenance"][11]["target_field"], "wheel_model_id")

    def test_unknown_aircraft_render_flag_uses_sa_native_camera_path(self):
        handling_values = ["1.0"] * 30
        handling_values[14] = "F"
        handling_values[15] = "P"
        row = ["MAVERICK"] + handling_values + ["8020000", "1", "1"]
        converted = convert_handling(
            row, "NATIVE_HELI", ide_row("car", "MAVERICK"), physical_class="heli"
        )
        self.assertEqual(converted["unmapped_flags"], 0)
        self.assertEqual(converted["fields"][31], "2004000")
        adaptation = next(
            item for item in converted["flag_adapters"]
            if item.get("source_bit") == "0x08000000"
        )
        self.assertEqual(adaptation["target_word"], "model_flags")
        self.assertEqual(adaptation["target_bit"], "0x00004000")
        self.assertEqual(adaptation["adaptation"], "native-system adaptation")

    def test_unknown_aircraft_render_flag_without_aircraft_class_stays_unmapped(self):
        handling_values = ["1.0"] * 30
        handling_values[14] = "F"
        handling_values[15] = "P"
        row = ["MAVERICK"] + handling_values + ["8000000", "1", "1"]
        converted = convert_handling(
            row, "NATIVE_CAR", ide_row("car", "MAVERICK"), physical_class="car"
        )
        self.assertEqual(converted["unmapped_flags"], 0x08000000)

    def test_flying_special_row_requires_a_native_flying_pointer_slot(self):
        values = " ".join(str(index) for index in range(1, 19))
        source = "DODO 1\n$ MAVERICK " + values
        with self.assertRaisesRegex(ValueError, "target common handling ID"):
            convert_special_handling(source, "DODO", "NATIVE_PLANE", "plane")
        with self.assertRaisesRegex(ValueError, "outside GetFlyingPointer"):
            convert_special_handling(
                source, "DODO", "NATIVE_PLANE", "plane", native_handling_id=185
            )

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

    def test_plane_lod_adapter_preserves_airtrain_lod_and_sa_wheel_sizes(self):
        row = ide_row("plane", "AIRTRAIN", "257", "0.0")
        converted = convert_ide(
            row,
            model_id=905,
            model_name="NATIVE_AIRTRAIN",
            texture_dictionary="NATIVE_AIRTRAIN",
            handling_name="NATIVE_AIRTRAIN",
            plane_lod_adapter=True,
        )
        self.assertEqual(converted["fields"][11], "-1")
        self.assertEqual(converted["fields"][12:14], ["1.0", "1.0"])
        self.assertEqual(converted["provenance"][11]["source_field"], "plane_lod_model_id")
        self.assertEqual(converted["provenance"][11]["source_value"], "257")
        self.assertEqual(converted["plane_lod_adaptation"]["source_lod_model_id"], 257)
        self.assertEqual(converted["plane_lod_adaptation"]["target_value"], "-1")

    def test_plane_lod_adapter_records_absent_deaddodo_lod(self):
        row = ide_row("plane", "DEADDODO", "-1", "0.0")
        converted = convert_ide(
            row,
            model_id=906,
            model_name="NATIVE_DEADDODO",
            texture_dictionary="NATIVE_DEADDODO",
            handling_name="NATIVE_DEADDODO",
            plane_lod_adapter=True,
        )
        self.assertEqual(converted["fields"][11], "-1")
        self.assertEqual(converted["fields"][12:14], ["1.0", "1.0"])
        self.assertEqual(converted["provenance"][11]["source_value"], "-1")
        self.assertIsNone(converted["plane_lod_adaptation"]["source_lod_model_id"])

    def test_plane_lod_adapter_rejects_non_plane_source_rows(self):
        row = ide_row("car", "DODO", "256", "1.0")
        with self.assertRaisesRegex(ValueError, "only to source and target plane rows"):
            convert_ide(
                row,
                model_id=907,
                model_name="NATIVE_DODO",
                texture_dictionary="NATIVE_DODO",
                handling_name="NATIVE_DODO",
                wheel_model_asset="embedded-original",
                physical_class="plane",
                lcs_handling_flags=0x00040000,
                plane_lod_adapter=True,
            )

    def test_ferry_uses_sa_boat_type_only_with_original_boat_flag(self):
        row = ide_row("ferry", "FERRY", "0", "0.0")
        converted = convert_ide(
            row,
            model_id=908,
            model_name="TROPIC",
            texture_dictionary="TROPIC",
            handling_name="TROPIC",
            physical_class="boat",
            lcs_handling_flags=0x00080004,
        )
        self.assertEqual(converted["fields"][3], "boat")
        self.assertEqual(converted["fields"][11], "-1")
        self.assertEqual(
            converted["ferry_boat_adaptation"]["source_handling_flags"], "0x00080004"
        )
        self.assertEqual(converted["ferry_boat_adaptation"]["required_source_class_flag"], "0x00080000")

        with self.assertRaisesRegex(ValueError, "requires original handling flags"):
            convert_ide(
                row,
                model_id=908,
                model_name="TROPIC",
                texture_dictionary="TROPIC",
                handling_name="TROPIC",
                physical_class="boat",
            )
        with self.assertRaisesRegex(ValueError, "does not match its handling flags"):
            convert_ide(
                row,
                model_id=908,
                model_name="TROPIC",
                texture_dictionary="TROPIC",
                handling_name="TROPIC",
                physical_class="boat",
                lcs_handling_flags=0x00020000,
            )

    def test_train_keeps_native_type_and_explicit_common_group_zero(self):
        ide = ide_row("train", "TRAIN", "17", "3.0")
        ide[7] = "ignore"
        converted_ide = convert_ide(
            ide,
            model_id=904,
            model_name="NATIVE_TRAIN",
            texture_dictionary="NATIVE_TRAIN",
            handling_name="NATIVE_TRAIN",
            physical_class="train",
        )
        self.assertEqual(converted_ide["fields"][3], "train")
        self.assertEqual(len(converted_ide["fields"]), 15)
        self.assertEqual(converted_ide["provenance"][3]["value"], "train")
        self.assertEqual(converted_ide["provenance"][3]["source_index"], 3)
        self.assertEqual(converted_ide["provenance"][11]["target_field"], "unused_optional_misc")

        handling_values = ["1.0"] * 30
        handling_values[14] = "F"
        handling_values[15] = "P"
        handling = ["TRAIN"] + handling_values + ["0", "1", "1"]
        converted_handling = convert_handling(
            handling, "NATIVE_TRAIN", ide, physical_class="train"
        )
        self.assertEqual(converted_handling["fields"][35], "0")
        self.assertEqual(
            converted_handling["animation_group_adapter"]["adaptation"],
            "native-system adaptation",
        )
        self.assertEqual(
            converted_handling["provenance"][35]["source_value"], "train"
        )


if __name__ == "__main__":
    unittest.main()
