"""The ASI's original->SA weapon table matches the packaged slot plan."""
import pathlib, re, sys
root = pathlib.Path(__file__).resolve().parents[1]
sys.path.insert(0, str(root / 'tools'))
import weapon_slots

source = (root / 'src/native.cpp').read_text(encoding='utf-8')
table = re.search(r'equivalent_weapon_counterparts\[\]=\{([^}]*)\}', source)[1]
runtime = [int(value) for value in table.split(',')]
assert runtime == weapon_slots.WEAPON_TYPES, 'native.cpp weapon types differ from tools/weapon_slots.py'
targets = {}
for name, target in weapon_slots.WEAPON_TARGETS.items():
    targets.setdefault(target, []).append(name)
shared = {target: names for target, names in targets.items() if len(names) > 1}
allowed = {'brassknuckle', 'gun_dildo2', 'golfclub', 'nitestick', 'bat', 'sniper', 'minigun'}
assert set(shared) <= allowed, shared
print('PASS: original weapon types and models map to their own SA slots; gameExecuted=false')

# The gun-sound table (include/lcs/weapon_audio.hpp) routes SA weapon types to the
# original weapon converted onto them: its copy of the table must match too.
header = (root / 'include/lcs/weapon_audio.hpp').read_text(encoding='utf-8')
copy = re.search(r'lcs_to_sa_weapon_type\{([^}]*)\}', header)[1]
assert [int(value) for value in copy.split(',')] == weapon_slots.WEAPON_TYPES, 'weapon_audio.hpp weapon types differ from tools/weapon_slots.py'
print('PASS: weapon sound table agrees with the slot plan; gameExecuted=false')
