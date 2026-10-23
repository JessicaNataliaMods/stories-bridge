"""Check original animation records independently of the converter; no GTA execution."""
from collections import Counter
from pathlib import Path
import hashlib, json, struct, sys

package = Path(sys.argv[1])
manifest = json.loads((package / 'manifest.json').read_text())
assert manifest['states'] == 71 and manifest['blocks'] == 25
assert manifest['gameExecuted'] is False and manifest['campaignPlayable'] is False

def sections(data):
    at = 0
    while at < len(data):
        assert len(data) - at >= 8
        tag, size = struct.unpack_from('<4sI', data, at)
        end = at + 8 + size
        assert end <= len(data)
        yield tag, data[at+8:end]
        at += 8 + ((size+3) & ~3)
    assert at == len(data)

# Original thighs hang from Spine, SA's from Pelvis: a native thigh key is the
# original spine rotation times the original thigh rotation (row-vector order).
# Independent of tools/lcs_ifp.py: plain matrices, stored ANPK quaternions.
import math
def matrix(q):
    x, y, z, w = q
    n = math.sqrt(x*x + y*y + z*z + w*w); x, y, z, w = x/n, y/n, z/n, w/n
    return [[1-2*(y*y+z*z), 2*(x*y-z*w), 2*(x*z+y*w)], [2*(x*y+z*w), 1-2*(x*x+z*z), 2*(y*z-x*w)],
            [2*(x*z-y*w), 2*(y*z+x*w), 1-2*(x*x+y*y)]]
def product(a, b):
    return [[sum(a[i][k]*b[k][j] for k in range(3)) for j in range(3)] for i in range(3)]
def decode(kind, raw):
    stride = {b'KR00': 5, b'KRT0': 8, b'KRTS': 11}[kind]
    return sorted((struct.unpack('<%df' % stride, frame) for frame in raw), key=lambda f: f[-1])
def rotation_at(keys, t):
    if t <= keys[0][-1]: return matrix(keys[0][:4])
    for a, b in zip(keys, keys[1:]):
        if t <= b[-1]:
            f = 0.0 if b[-1] <= a[-1] else (t - a[-1]) / (b[-1] - a[-1])
            qa, qb = a[:4], b[:4]
            if sum(u*v for u, v in zip(qa, qb)) < 0: qb = tuple(-v for v in qb)
            return matrix(tuple(u + (v-u)*f for u, v in zip(qa, qb)))
    return matrix(keys[-1][:4])
def thigh_follows_spine(old_thigh, old_spine, new):
    old_t, old_s, new_k = decode(*old_thigh), decode(*old_spine), decode(*new)
    times = sorted({k[-1] for k in old_t} | {k[-1] for k in old_s})
    if len(new_k) != len(times) or any(abs(k[-1]-t) > 1e-5 for k, t in zip(new_k, times)): return False
    for k in new_k:
        expected = product(rotation_at(old_t, k[-1]), rotation_at(old_s, k[-1])); actual = matrix(k[:4])
        if max(abs(expected[i][j]-actual[i][j]) for i in range(3) for j in range(3)) > 2e-3: return False
    return True
def role(bone):
    return {'l thigh': 'thigh', 'r thigh': 'thigh', 'spine': 'spine', 'spine 1': 'spine'}.get(bone.strip().lower())

def archive(path, selected=None):
    data = path.read_bytes()
    size = struct.unpack_from('<I', data, 4)[0]
    assert data[:4] == b'ANPK' and all(b == 0 for b in data[8+size:])
    children = list(sections(data[8:8+size]))
    tag, info = children.pop(0); assert tag == b'INFO'
    count = struct.unpack_from('<I', info)[0]
    block = info[4:].split(b'\0')[0].decode('ascii')
    assert len(children) == 2 * count
    animations = {}
    for i in range(count):
        tag, name = children[2*i]; assert tag == b'NAME'
        name = name.split(b'\0')[0].decode('ascii')
        tag, body = children[2*i+1]; assert tag == b'DGAN'
        if selected is not None and name.lower() not in selected: continue
        seqs = list(sections(body)); tag, count_data = seqs.pop(0); assert tag == b'INFO'
        assert len(seqs) == struct.unpack_from('<I', count_data)[0]
        records = {}
        for tag, sequence in seqs:
            assert tag == b'CPAN'
            fields = list(sections(sequence)); tag, header = fields.pop(0)
            assert tag == b'ANIM' and len(header) in (40, 44, 48)
            bone = header[:28].split(b'\0')[0].decode('ascii')
            frames = struct.unpack_from('<I', header, 28)[0]
            bone_tag = struct.unpack_from('<i', header, 40)[0] if len(header) == 44 else None
            keyframes = []
            if frames:
                assert len(fields) == 1
                kind, payload = fields[0]
                stride = {b'KR00':20, b'KRT0':32, b'KRTS':44}[kind]
                assert len(payload) == frames * stride
                keyframes = [payload[j:j+stride] for j in range(0, len(payload), stride)]
            else: assert not fields
            assert bone not in records
            records[bone] = (bone_tag, Counter(keyframes), (fields[0][0], keyframes) if frames else None)
        assert name not in animations
        animations[name] = records
    return block, animations

rows = [line.split('\t') for line in (package / 'index.tsv').read_text().splitlines()]
states = {int(row[0]) for row in rows}
assert len(rows) == len(states) == 71
assert states == set(range(36,86)) | {2,3,8,9,10,11,12,13,14,15,16,17,18,19,20,21,23,24,25,31,35}
index = {}
for state, block, animation, flags in rows:
    assert block.startswith('LG') and len(block) == 4
    assert int(flags) & 1 and not (int(flags) & ~31)
    index.setdefault(block, set()).add(animation)
assert len(index) == 25

all_frames = 0
for entry in manifest['files']:
    source = Path(entry['source']); destination = package / entry['path']
    assert destination.parent == package and destination.name == entry['alias'] + '.ifp'
    for path, expected in ((source, entry['sourceSha256']), (destination, entry['sha256'])):
        assert hashlib.sha256(path.read_bytes()).hexdigest().upper() == expected
    native_block, native = archive(destination)
    original_block, original = archive(source, {name.lower() for name in native})
    assert original_block.lower() == entry['original'] and native_block == entry['alias']
    assert set(native) == index[native_block]
    original = {name.lower(): seqs for name, seqs in original.items()}
    for name, seqs in native.items():
        source_seqs = original[name.lower()]
        assert set(source_seqs) == set(seqs)
        spine = next((b for b in source_seqs if role(b) == 'spine'), None)
        for bone, (tag, frames, raw) in seqs.items():
            old_tag, old_frames, old_raw = source_seqs[bone]
            if role(bone) == 'thigh' and spine is not None and old_raw and source_seqs[spine][2]:
                assert thigh_follows_spine(old_raw, source_seqs[spine][2], raw), (entry['original'], name, bone, 'thigh is not spine * thigh')
            else:
                assert frames == old_frames, (entry['original'], name, bone, 'keyframes changed')
            assert tag is not None
            if old_tag is not None and old_tag >= 0: assert tag == old_tag
            if tag < 0: assert sum(frames.values()) <= 1
            all_frames += sum(frames.values())
print(f'PASS: 71 original gesture states, 25 native blocks, {all_frames} keyframe records (thighs follow the spine, all others unchanged); gameExecuted=false')
