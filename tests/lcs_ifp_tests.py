"""Unit tests for tools/lcs_ifp.py on synthetic animations (no game data, no GTA)."""
import math
import random
import struct
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'tools'))
import lcs_ifp as li
import weapon_animations as wa

random.seed(7)


def quaternion():
    q = tuple(random.uniform(-1, 1) for _ in range(4))
    return li.qnorm(q)


def matrix_product(a, b):
    return [[sum(a[i][k] * b[k][j] for k in range(3)) for j in range(3)] for i in range(3)]


def close(a, b, tolerance=1e-9):
    return all(abs(x - y) <= tolerance for row_a, row_b in zip(a, b) for x, y in zip(row_a, row_b))


# 1. The quaternion product follows the row-vector matrix product.
for _ in range(50):
    a, b = quaternion(), quaternion()
    assert close(li.qmat(li.rmul(a, b)), matrix_product(li.qmat(a), li.qmat(b)))

# 2. Thighs hang from Pelvis: key = spine * thigh, resampled over both key sets.
spine = [(0.0, quaternion(), None), (0.5, quaternion(), None), (1.0, quaternion(), None)]
thigh = [(0.0, quaternion(), (1, 2, 3)), (0.25, quaternion(), (1, 2, 3)), (1.0, quaternion(), (1, 2, 3))]
arm = [(0.0, quaternion(), (4, 5, 6))]
root = [(0.0, quaternion(), (0.0, 1.0, 2.0)), (1.0, quaternion(), (0.0, 2.0, 3.0))]
converted = li.original_to_sa({0: root, 2: spine, 41: thigh, 32: arm})
assert [k[0] for k in converted[41]] == [0.0, 0.25, 0.5, 1.0]
for t, q, p in converted[41]:
    expected = matrix_product(li.qmat(li.sample(thigh, t)), li.qmat(li.sample(spine, t)))
    assert close(li.qmat(q), expected, 1e-7) and p is None
assert converted[32][0][2] is None and converted[2] == [(t, q, None) for t, q, _ in spine]
assert converted[0][1][2] == (0.0, 2.0, 3.0)  # only the root keeps its translation
assert 51 not in converted  # a thigh the animation does not key stays out of it
only_thigh = li.original_to_sa({41: thigh})
assert all(abs(abs(sum(a * b for a, b in zip(x[1], y[1]))) - 1) < 1e-9 for x, y in zip(only_thigh[41], thigh))  # no spine: unchanged
assert set(li.original_to_sa({2: spine, 41: thigh, 5: arm}, allowed={2, 5})) == {2, 5}

# 3. ANP3 encoding keeps rotations, times (1/60 s) and the root translation.
data = li.encode_animation('test', converted)
tracks, others = li.read_sa_animation(data)
assert not others and set(tracks) == set(converted)
for bone, track in converted.items():
    assert [round(k[0] * 60) for k in track] == [round(k[0] * 60) for k in tracks[bone]]
    for (_, q, p), (_, r, s) in zip(track, tracks[bone]):
        assert abs(abs(sum(x * y for x, y in zip(q, li.qnorm(r)))) - 1) < 1e-6
        assert (p is None) == (s is None) or bone == 0
assert tracks[0][1][2] == (0.0, 2.0, 3.0)
# Two keys inside one tick keep only the later one; ticks stay increasing.
packed = li.encode_object(2, [(0.0, li.IDENTITY, None), (0.001, quaternion(), None), (0.5, li.IDENTITY, None)])
frames = struct.unpack_from('<I', packed, 28)[0]
assert frames == 2 and li.sa_duration(li.encode_animation('x', {2: [(0.0, li.IDENTITY, None), (0.5, li.IDENTITY, None)]})) == 0.5


# 4. A block: equal names are replaced (case-insensitive), the rest stays byte-exact.
def pack(tag, payload):
    return tag + struct.pack('<I', len(payload)) + payload + b'\0' * (-len(payload) % 4)


def anpk(block, animations):
    body = pack(b'INFO', struct.pack('<I', len(animations)) + block.encode() + b'\0')
    for name, sequences in animations.items():
        children = pack(b'INFO', struct.pack('<II', len(sequences), 0))
        for bone, keys in sequences.items():
            header = bone.encode().ljust(28, b'\0') + struct.pack('<iiii', len(keys), 0, len(keys) - 1, -1)
            payload = b''.join(struct.pack('<5f', -q[0], -q[1], -q[2], q[3], t) for t, q in keys)  # ANPK stores the conjugate
            children += pack(b'CPAN', pack(b'ANIM', header) + pack(b'KR00', payload))
        body += pack(b'NAME', name.encode() + b'\0') + pack(b'DGAN', children)
    return pack(b'ANPK', body)


def anp3(block, animations):
    return wa.write_anp3(block, [(name, li.encode_animation(name, tracks)) for name, tracks in animations])


def keys(*times):
    return [(t, quaternion()) for t in times]


original = anpk('ped', {'WALK_civi': {'Root': keys(0, 1), 'Spine': keys(0, 1), 'L Thigh': keys(0, 0.5, 1)},
                        'extra_only': {' Spine': keys(0, 2), 'Pelvis': keys(0, 2)},
                        'Drive_L': {'Pelvis': keys(0, 1)}})
sa_walk = li.read_original_block(anpk('ped', {'x': {'Pelvis': keys(0, 1)}}))['x']
sa_block = anp3('ped', [('walk_civi', {1: [(0.0, li.IDENTITY, None), (2.0, li.IDENTITY, None)]}),
                        ('climb', {1: [(0.0, li.IDENTITY, None), (1.0, li.IDENTITY, None)]}),
                        ('DRIVE_L', {1: [(0.0, li.IDENTITY, None), (2.0, li.IDENTITY, None)]})])
original_tracks = li.read_original_block(original)
assert set(original_tracks['extra_only']) == {2, 1}  # ' Spine' (leading space) is the spine
sa_animations = dict(wa.read_anp3(sa_block)[1])
result, report = li.convert_block(sa_block, {'ped': original}, add_original_only=True,
                                  keep_sa_length=lambda name: name.lower().startswith('drive'))
assert report == {'walk_civi': 'original', 'climb': 'kept', 'DRIVE_L': 'original', 'extra_only': 'added'}
animations = dict(wa.read_anp3(result)[1])
assert animations['climb'] == sa_animations['climb']  # no original counterpart: byte-exact
assert list(animations) == ['walk_civi', 'climb', 'DRIVE_L', 'extra_only']
assert li.sa_duration(animations['walk_civi']) == 1.0  # original timing
assert li.sa_duration(animations['DRIVE_L']) == 2.0  # SA length kept for engine-timed animations
walk = li.read_sa_animation(animations['walk_civi'])[0]
assert set(walk) == {0, 2, 41} and [round(k[0], 2) for k in walk[41]] == [0.0, 0.5, 1.0]
# A partial SA animation limits the replacement to the bones it keys.
partial = anp3('ped', [('walk_civi', {2: [(0.0, li.IDENTITY, None), (1.0, li.IDENTITY, None)]})])
assert set(li.read_sa_animation(dict(wa.read_anp3(li.convert_block(partial, {'ped': original}, same_bones=True)[0])[1])['walk_civi'])[0]) == {2}

# 5. ANPK blocks (gestures): only thigh sequences change, to spine * thigh.
retargeted, count = li.retarget_anpk(original)
assert count == 1
before, after = wa.read_anpk(original), wa.read_anpk(retargeted)
assert before['Drive_L'] == after['Drive_L'] and before['extra_only'] == after['extra_only']
assert before['WALK_civi'][0] == after['WALK_civi'][0] and before['WALK_civi'][1] == after['WALK_civi'][1]
folded = li.read_original_block(retargeted)['WALK_civi'][41]
assert [round(k[0], 3) for k in folded] == [0.0, 0.5, 1.0] and after['WALK_civi'][2][1] == 'KR00'
t, s = original_tracks['WALK_civi'][41], original_tracks['WALK_civi'][2]
for time, q, _ in folded:
    assert close(li.qmat(q), matrix_product(li.qmat(li.sample(t, time)), li.qmat(li.sample(s, time))), 1e-5)

# 6. Seat height: a rotation-only Root sequence gains a constant (0, 0, -drop) translation; the rest is untouched.
_tag, _begin, _end, _at = li._section(original, 0)
_at = li._section(original, _begin)[3]
_name, _nb, _ne, _after_name = li._section(original, _at)
_dgan, _db, _de, _after = li._section(original, _after_name)
walk_record = original[_at:_after]
assert original[_nb:_ne].split(bytes(1))[0] == b'WALK_civi'
lowered = li.lower_root(walk_record, 0.35)
tracks_after = li.read_original_block(pack(b'ANPK', pack(b'INFO', struct.pack('<I', 1) + b'ped' + bytes(1)) + lowered))['WALK_civi']
assert len(tracks_after[0]) == 2 and all(k[2][:2] == (0.0, 0.0) and abs(k[2][2] + 0.35) < 1e-6 for k in tracks_after[0])
assert tracks_after[2] == original_tracks['WALK_civi'][2] and tracks_after[41] == original_tracks['WALK_civi'][41]
try:
    li.lower_root(lowered, 0.35)
    raise SystemExit('A Root that already keys translation must be refused')
except ValueError:
    pass
print('PASS: lcs_ifp quaternion order, thigh fold, ANP3 encoding, block replacement and ANPK retarget')
