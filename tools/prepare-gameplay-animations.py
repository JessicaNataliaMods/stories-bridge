"""Prepare the original mission gesture blocks together; never execute GTA.

Every block is retargeted to the SA frame tree (tools/lcs_ifp.py: the original
thighs hang from Spine, SA's from Pelvis) before it is tagged."""
import hashlib, json, re, struct, subprocess
from pathlib import Path
import lcs_ifp

root = Path(__file__).resolve().parents[1]
relcs = root / 'local/references/res-relcs' / 'src'
out = root / 'out' / 'gameplay-animations-v4'
manager = (relcs / 'animation' / 'AnimManager.cpp').read_text()
ped = (relcs / 'peds' / 'Ped.cpp').read_text()
enum = re.search(r'enum eWaitState\s*\{(.*?)\};', (relcs / 'peds' / 'Ped.h').read_text(), re.S)[1]
states = {name: i for i, name in enumerate(re.findall(r'\bWAITSTATE_\w+', enum))}
names = {key: re.findall(r'"([^"]+)"', body) for key, body in re.findall(r'char const \*([a-zA-Z0-9_]+)\[\] = \{(.*?)\};', manager, re.S)}
descs = {key: re.findall(r'\{\s*(ANIM_\w+)\s*,\s*([^}]+)\}', body) for key, body in re.findall(r'AnimAssocDesc (\w+)\[\] = \{(.*?)\};', manager, re.S)}
groups = {}
for group, block, array, desc in re.findall(r'\{\s*"([^"]+)",\s*"([^"]+)",\s*MI_COP,\s*awc\((\w+)\),\s*(\w+)\s*\}', manager):
    if array not in names or desc not in descs: continue
    groups[group.upper()] = (block.lower(), {anim: (name, flags) for (anim, flags), name in zip(descs[desc], names[array])})
groups['STD'] = groups['MAN']
load_body = ped.split('CPed::LoadNonStandardPedAnim')[1].split('CPed::UnloadNonStandardPedAnim')[0]
needed = set(re.findall(r'case (WAITSTATE_\w+):', load_body))
set_body = ped.split('CPed::SetWaitState(')[1].split('CPed::Wait(')[0]
bindings = {}
pending = []
for token in re.split(r'(case WAITSTATE_\w+:)', set_body):
    if token.startswith('case WAITSTATE_'): pending.append(token[5:-1]); continue
    match = re.search(r'CAnimManager::(?:AddAnimation|BlendAnimation)\(GetClump\(\),\s*ASSOCGRP_(\w+),\s*(ANIM_\w+)', token)
    if match:
        group, anim = match.groups()
        if group in groups:
            block, entries = groups[group]
            if anim in entries:
                name, flags = entries[anim]
                native_flags = 1 | (2 if 'ASSOC_REPEAT' in flags else 0) | (16 if 'ASSOC_PARTIAL' in flags else 0) | (8 if 'ASSOC_FADEOUTWHENDONE' in flags else 4)
                for state in pending:
                    if state in needed or group == 'STD': bindings[state] = (block, name, native_flags)
    pending = []
for state in needed - bindings.keys():
    if state.startswith('WAITSTATE_CS_MISC_'):
        block, entries = groups['CS_MISC']
        anim = state.replace('WAITSTATE_', 'ANIM_')
        if anim in entries:
            name, flags = entries[anim]
            bindings[state] = (block, name, 1 | (2 if 'ASSOC_REPEAT' in flags else 0) | (16 if 'ASSOC_PARTIAL' in flags else 0) | (8 if 'ASSOC_FADEOUTWHENDONE' in flags else 4))
missing = needed - bindings.keys()
if missing: raise RuntimeError('Unresolved original gesture states: ' + ', '.join(sorted(missing)))
# These source cases fall through to a shared call with a variable animation ID.
for state, anim in {
    'PLAYANIM_COWER': 'HANDSCOWER', 'PLAYANIM_HANDSUP': 'HANDSUP',
    'PLAYANIM_HANDSCOWER': 'HANDSCOWER', 'PLAYANIM_DUCK': 'DUCK_DOWN',
    'PLAYANIM_TAXI': 'HAILTAXI', 'PLAYANIM_CHAT': 'CHAT',
}.items():
    name, flags = groups['STD'][1]['ANIM_STD_' + anim]
    bindings['WAITSTATE_' + state] = ('ped', name, 1 | (2 if 'ASSOC_REPEAT' in flags else 0) | (16 if 'ASSOC_PARTIAL' in flags else 0) | 4)
out.mkdir(exist_ok=True)
if (out / 'manifest.json').exists(): raise RuntimeError('Prepared gesture package exists')
(out / 'validation').mkdir(exist_ok=True)
blocks = sorted({b[0] for b in bindings.values()})
aliases = {block: f'LG{i:02}' for i, block in enumerate(blocks)}
def run(name, *args):
    result = subprocess.run([str(root / 'build' / 'Release' / (name + '.exe')), *map(str, args)], capture_output=True, text=True)
    if result.returncode: raise RuntimeError(result.stdout + result.stderr)
    return result.stdout
files = []
def select_animations(data, selected):
    """Keep complete NAME/DGAN records; do not duplicate the whole locomotion block."""
    def section(at, expected):
        tag, size = struct.unpack_from('<4sI', data, at)
        if tag != expected: raise RuntimeError('Unexpected original IFP record')
        return at + 8, at + 8 + size, at + 8 + ((size + 3) & ~3)
    begin, end, _ = section(0, b'ANPK')
    ib, ie, at = section(begin, b'INFO')
    info = bytearray(data[ib:ie]); records = {}; aliases = {n.lower(): n for n in selected}; actual = {}
    for _ in range(struct.unpack_from('<I', info)[0]):
        start = at; nb, ne, at = section(at, b'NAME')
        name = data[nb:ne].split(b'\0')[0].decode('ascii')
        _, _, at = section(at, b'DGAN')
        if name.lower() in aliases:
            requested = aliases[name.lower()]; records[requested] = data[start:at]; actual[requested] = name
    if at != end or set(records) != selected: raise RuntimeError('Original animation selection differs: missing=' + repr(selected - set(records)) + ', end=' + str((at, end)))
    for state, (block, name, flags) in list(bindings.items()):
        if name in actual: bindings[state] = (block, actual[name], flags)
    struct.pack_into('<I', info, 0, len(records))
    # INFO has space for the four-character owned namespace, including NUL.
    if len(info) < 9: info.extend(b'\0' * (9-len(info)))
    payload = b'INFO' + struct.pack('<I', len(info)) + info + b'\0' * (-len(info) % 4) + b''.join(records.values())
    return b'ANPK' + struct.pack('<I', len(payload)) + payload
def native_sequence_layout(data):
    # Original PED uses ANIM[40], while SA can consume verified bone tags in
    # ANIM[44]. Values follow SA eBoneTag; every keyframe record stays intact.
    bones = {'Root':0, 'Pelvis':1, 'Spine':2, 'Spine1':3, 'Neck':4, 'Head':5,
             'Bip01 R Clavicle':21, 'R UpperArm':22, 'R Forearm':23, 'R Hand':24, 'R Finger':25, 'R Fingers':25,
             'Bip01 L Clavicle':31, 'L UpperArm':32, 'L Forearm':33, 'L Hand':34, 'L Finger':35, 'L Fingers':35,
             'L Thigh':41, 'L Calf':42, 'L Foot':43, 'L Toe0':44, 'R Thigh':51, 'R Calf':52, 'R Foot':53, 'R Toe0':54, 'Belly':201}
    def pack(tag, body): return tag + struct.pack('<I', len(body)) + body + b'\0' * (-len(body) % 4)
    def children(body):
        result=b''; at=0
        while at<len(body):
            tag, size=struct.unpack_from('<4sI', body, at); payload=body[at+8:at+8+size]
            if len(payload)!=size: raise RuntimeError('Truncated original animation record')
            if tag in (b'ANPK', b'DGAN', b'CPAN'): payload=children(payload)
            elif tag==b'ANIM' and size in (40,48):
                name=payload[:28].split(b'\0')[0].decode('ascii'); tag_id=bones.get(name,-1)
                if tag_id<0 and struct.unpack_from('<I', payload,28)[0]>1: raise RuntimeError('Unmapped moving standard bone: '+name)
                payload=payload[:40]+struct.pack('<i',tag_id)
            elif tag==b'ANIM' and size!=44: raise RuntimeError('Unsupported original sequence header: '+str(size))
            result+=pack(tag,payload); at+=8+((size+3)&~3)
        return result
    return children(data)
for block in blocks:
    source = root / 'out' / ('lcs-converted/anim/ped.ifp' if block == 'ped' else 'img-v1/' + block + '.ifp')
    selected = out / 'validation' / (block + '-selected.ifp')
    selected.write_bytes(select_animations(source.read_bytes(), {b[1] for b in bindings.values() if b[0] == block}))
    if block == 'ped':
        selected.write_bytes(native_sequence_layout(selected.read_bytes()))
    selected.write_bytes(lcs_ifp.retarget_anpk(selected.read_bytes())[0])
    report = run('lcs-ifp-inspect', selected, '--bindings')
    mapping = []
    for anim, name, tag, frames in re.findall(r'binding animation=(\S+) name=(.+) bone_tag=(-?\d+) keyframes=(\d+)', report):
        if int(tag) < 0 and int(frames):
            if name not in ('L Toe0', 'R Toe0'):
                if int(frames) <= 1: continue # static model/dummy export tracks, outside the ped skeleton
                raise RuntimeError(f'Unmapped moving skin sequence: {block}:{name}')
            mapping.append(f'{anim}\t{name}\t{44 if name == "L Toe0" else 54}')
    tags = out / 'validation' / (block + '-tags.tsv')
    tags.write_text('\n'.join(mapping) + ('\n' if mapping else ''))
    tagged = out / 'validation' / (block + '-tagged.ifp')
    if tagged.exists(): tagged.unlink() # resume only this tool's incomplete package
    run('lcs-ifp-adapt', selected, tagged, '--bone-tags', tags)
    destination = out / (aliases[block] + '.ifp')
    if destination.exists(): destination.unlink()
    run('lcs-ifp-adapt', tagged, destination, '--block-name', aliases[block])
    validation = run('lcs-ifp-inspect', destination, '--bindings')
    (out / 'validation' / (block + '.txt')).write_text(validation)
    for original, (requested_block, anim, flags) in bindings.items():
        if requested_block == block and f'animation={anim} ' not in validation: raise RuntimeError('Original gesture missing: ' + original)
    files.append(dict(original=block, alias=aliases[block], source=str(source), sourceSha256=hashlib.sha256(source.read_bytes()).hexdigest().upper(), path=destination.name, sha256=hashlib.sha256(destination.read_bytes()).hexdigest().upper()))
index = '\n'.join(f'{states[state]}\t{aliases[block]}\t{anim}\t{flags}' for state, (block, anim, flags) in sorted(bindings.items(), key=lambda row: states[row[0]])) + '\n'
(out / 'index.tsv').write_text(index)
(out / 'manifest.json').write_text(json.dumps(dict(prepared=True, gameExecuted=False, campaignPlayable=False, states=len(bindings), blocks=len(blocks), files=files), indent=2))
print(f'Prepared {len(bindings)} original gesture states across {len(blocks)} original animation blocks; gameExecuted=false')
