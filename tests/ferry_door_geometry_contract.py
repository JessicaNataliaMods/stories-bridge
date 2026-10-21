"""Ferry door/ramp geometry contract on the prepared outputs (frame -> geometry).

Usage: ferry_door_geometry_contract.py <output dir of tools/prepare-ferry-door-model.py>

Checks: the audited split moved exactly the door/ramp triangles (8+8+16+16) without changing the world-space triangle set, the
clump has 6 atomics (chassis_vlo, chassis_hi and one on each of the four dummies), the visible triangle total is the
original 1193 (167 + 978 + 48), every piece has local bounds larger than 1 m, and the SA boat keeps the four node names that
sa_ferry resolves through GetFrameFromName plus the pinned output hashes.
"""
import hashlib
import json
import sys
from pathlib import Path

NODES = ('door_front_dummy', 'door_back_dummy', 'ramp_front_dummy', 'ramp_back_dummy')
SHA = {'ferry.dff': '579651cdd317bddb1524d7de15dd3decba95a261f8425036e951f4f907c4bb9a',
       'tropic.dff': '2f31d859e723e6b7a9e28d78f6eb4974916139fc18ca05e50acb21ee734ce10b'}


def main(directory):
    root = Path(directory)
    audit = json.loads((root/'door-split-audit.json').read_text(encoding='utf-8'))
    assert audit['world_space_triangle_multiset_identical'] is True
    assert audit['moved'] == dict(door_front=8, door_back=8, ramp_front=16, ramp_back=16), audit['moved']
    names = [row[0] for row in audit['atomics']]
    assert names == ['chassis_vlo', 'chassis_hi', *NODES], names
    assert sum(row[3] for row in audit['atomics']) == 1193
    assert audit['original_hi_triangles'] == audit['atomics'][1][3] + 48
    for key, piece in audit['pieces'].items():
        assert piece['frame'] == key+'_dummy' and piece['tris'] > 0, key
        low, high = piece['local_bounds_min'], piece['local_bounds_max']
        assert all(h >= l for l, h in zip(low, high)) and max(h-l for l, h in zip(low, high)) > 1.0, key   # plates may be flat in one axis
    for name, expected in SHA.items():
        assert hashlib.sha256((root/name).read_bytes()).hexdigest() == expected, name
    boat = (root/'tropic.dff').read_bytes()
    for node in NODES:
        assert node.encode() in boat, node
    assert b'COL3' in boat, 'embedded docked/undocked collision'
    print('PASS: ferry door/ramp geometry: 6 atomics, 1193 triangles, 48 moved from chassis_hi, set unchanged, SA boat hashes pinned')


if __name__ == '__main__':
    main(*sys.argv[1:])
