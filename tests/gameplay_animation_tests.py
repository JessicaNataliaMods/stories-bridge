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
            records[bone] = (bone_tag, Counter(keyframes))
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
        for bone, (tag, frames) in seqs.items():
            old_tag, old_frames = source_seqs[bone]
            assert frames == old_frames, (entry['original'], name, bone, 'keyframes changed')
            assert tag is not None
            if old_tag is not None and old_tag >= 0: assert tag == old_tag
            if tag < 0: assert sum(frames.values()) <= 1
            all_frames += sum(frames.values())
print(f'PASS: 71 original gesture states, 25 native blocks, {all_frames} unchanged keyframe records; gameExecuted=false')
