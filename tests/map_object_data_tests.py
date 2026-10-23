"""Authored ObjectData contracts; no game fixtures or model tables."""
import importlib.util
from pathlib import Path
import unittest
import sys

sys.path.insert(0, str(Path(__file__).resolve().parents[1]/'tools'))

spec = importlib.util.spec_from_file_location('map_objects', Path(__file__).resolve().parents[1]/'tools/mount-campaign-garage-doors.py')
objects = importlib.util.module_from_spec(spec)
spec.loader.exec_module(objects)


class ObjectDataContracts(unittest.TestCase):
    def source(self, damage='0', response='0'):
        return ('authored_prop', '25.0', '40.0', '0.99', '0.2', '75', '12.0', '0.8', damage, response, '1')

    def test_native_parser_and_alias_keep_source_physics(self):
        row = objects.translate_object_data(self.source(), 'private_alias')
        self.assertEqual(row[0], 'private_alias')
        self.assertEqual(tuple(row[1:8]), self.source()[1:8])
        self.assertGreaterEqual(len(row), 13)
        self.assertEqual(row[10], '1')

    def test_damage_enums_are_translated(self):
        self.assertEqual(objects.translate_object_data(self.source('1'), 'authored_alias')[8], '0')
        donor = (*self.source('1'), '0', '0')
        self.assertEqual(objects.translate_object_data(self.source('1'), 'authored_alias', donor)[8], '0')
        row = objects.translate_object_data(self.source('3'), 'authored_alias')
        self.assertEqual(row[8], '0')
        self.assertEqual(objects.translate_object_data(self.source('3'), 'authored_alias', damaged_atomic_available=True)[8], '1')
        for mode in ('50', '60', '70', '80', '100'):
            self.assertEqual(objects.translate_object_data(self.source(mode), 'authored_alias')[8], '0')
            self.assertEqual(objects.translate_object_data(self.source(mode), 'authored_alias', breakable_effect=200)[8], '200')

    def test_unknown_damage_is_not_silently_static(self):
        with self.assertRaises(ValueError):
            objects.translate_object_data(self.source('255'), 'authored_alias')


if __name__ == '__main__':
    unittest.main()
