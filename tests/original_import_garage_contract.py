"""Retail PS2 contract of COMMAND_HAS_IMPORT_GARAGE_SLOT_BEEN_FILLED (985) and its import list, as data only.

Usage: original_import_garage_contract.py [DEFAULT.IDE]

Pins the handler (two operands, slot-1 into CGarages::HasImportExportGarageCollectedThisCar), the
garage-type to list mapping (types 8, 9, 10 and 22) and the sixteen-model list that the LCS import garage
accepts. With DEFAULT.IDE it also proves the model names used by ExclusiveGarages resolve to exactly those
retail model ids. Grants no runtime support.
"""
import importlib.util
import os
import pathlib
import re
import struct
import sys

ROOT = pathlib.Path(__file__).resolve().parents[1]


def module(name, path):
    spec = importlib.util.spec_from_file_location(name, path)
    loaded = importlib.util.module_from_spec(spec)
    sys.modules[name] = loaded
    spec.loader.exec_module(loaded)
    return loaded


original = module('import_garage_original_inspect', ROOT/'tools/inspect-original-command.py')
elf_path = pathlib.Path(os.environ.get('LCS_PS2_ELF', ROOT / 'local/originals/lcs-ps2/SLUS_214.23'))
image = original.OriginalImage(elf_path)
image.require_profile()

handler = image.handler(985)
assert handler['handler'] == 0x22A670 and handler['default'] is False
assert image.word(0x22A680) == 0x24060002                    # addiu a2,zero,2: CollectParameters(2)
assert image.word(0x22A68C) == 0x8E050004                    # lw a1,4(s0): second operand (slot)
assert image.word(0x22A694) == 0x0C000000 | (0x19A330 >> 2)  # jal HasImportExportGarageCollectedThisCar
assert image.word(0x22A698) == 0x24A5FFFF                    # addiu a1,a1,-1: car = slot-1
# Garage type -> list index: 8, 9, 10 and 22 are the collector types.
getter = 0x19A330
assert image.word(getter+0x18) == 0x24020009                 # addiu v0,zero,9
assert image.word(getter+0x28) == 0x24020008                 # addiu v0,zero,8
assert image.word(getter+0x3C) == 0x2402000A                 # addiu v0,zero,10
assert image.word(getter+0x44) == 0x24020016                 # addiu v0,zero,22

# Retail model list of the import garage (CGarages gaCarsToCollectInCraigsGarages[0]).
names = ('hearse', 'faggio', 'freeway', 'spider', 'manana', 'shelby', 'pontiac', 'esprit',
         'hotrod', 'pcj600', 'sentinel', 'infernus', 'banshee', 'patriot', 'bfinject', 'landstal')
expected = [156, 206, 207, 130, 141, 185, 186, 187, 189, 205, 136, 142, 160, 137, 155, 131]
needle = b''.join(struct.pack('<i', value) for value in expected)
assert image.data.count(needle * 4) == 1, 'the retail import list must occur as the four identical garage rows'
source = (ROOT/'include/lcs/exclusive_garages.hpp').read_text()
listed = re.search(r'names\[import_slots\]=\{(.*?)\};', source, re.S)
assert tuple(re.findall(r'"(\w+)"', listed[1])) == names, 'ExclusiveGarages model names drifted from the verified list'
if len(sys.argv) > 1:
    ids, cars = {}, False
    for line in pathlib.Path(sys.argv[1]).read_text(encoding='latin1').splitlines():
        text = line.strip()
        if not text or text[0] == '#':
            continue
        if not cars:
            cars = text == 'cars'
            continue
        if text == 'end':
            cars = False
            continue
        fields = [field.strip() for field in text.split(',')]
        if len(fields) >= 5:
            ids[fields[1].lower()] = int(fields[0])
    assert [ids[name] for name in names] == expected, 'converted DEFAULT.IDE ids differ from the retail import list'
print('PASS: retail 985 handler, type mapping and sixteen-model import list')
