"""Original LCS gun animations inside SA's weapon animation blocks.

SA blocks (anim/anim.img, ANP3) are rebuilt with each animation that has an
original counterpart replaced by the converted LCS animation (ANPK, from the
user's PS2 IMG); SA-only animations stay byte-identical. Pure data transforms;
never executes GTA. docs/weapons.md documents the rules.
"""
import struct

# Original ANPK bone names -> SA bone ids (same skeleton numbering).
BONES = {'root': 0, 'pelvis': 1, 'spine': 2, 'spine1': 3, 'neck': 4, 'head': 5,
         'bip01 r clavicle': 21, 'r upperarm': 22, 'r forearm': 23, 'r hand': 24, 'r finger': 25,
         'bip01 l clavicle': 31, 'l upperarm': 32, 'l forearm': 33, 'l hand': 34, 'l finger': 35,
         'l thigh': 41, 'l calf': 42, 'l foot': 43, 'l toe0': 44, 'r thigh': 51, 'r calf': 52, 'r foot': 53, 'r toe0': 54}
SA_BONE_NAMES = {0: 'normal', 1: ' Pelvis', 2: ' Spine 1', 3: ' Spine1', 4: ' Neck', 5: ' Head',
                 21: 'Bip01 R Clavicle', 22: ' R UpperArm', 23: ' R ForeArm', 24: ' R Hand', 25: ' R Finger',
                 31: 'Bip01 L Clavicle', 32: ' L UpperArm', 33: ' L ForeArm', 34: ' L Hand', 35: ' L Finger',
                 41: ' L Thigh', 42: ' L Calf', 43: ' L Foot', 44: ' L Toe0', 51: ' R Thigh', 52: ' R Calf', 53: ' R Foot', 54: ' R Toe0'}
# SA block -> {SA animation: (original block, original animation)}. Low-skill
# variants get the same original animation: LCS has no weapon skill.
BLOCKS = {
    'colt45': {'colt45_fire': ('colt45', 'colt45_fire'), 'colt45_reload': ('colt45', 'colt45_reload'),
               'colt45_crouchfire': ('colt45', 'colt45_crouchfire'), 'colt45_crouchreload': ('colt45', 'colt45_crouchreload')},
    'python': {'python_fire': ('python', 'python_fire'), 'python_fire_poor': ('python', 'python_fire'),
               'python_reload': ('python', 'python_reload'), 'python_crouchfire': ('python', 'python_crouchfire'),
               'python_crouchreload': ('python', 'python_crouchreload')},
    'shotgun': {'shotgun_fire': ('shotgun', 'shotgun_fire'), 'shotgun_fire_poor': ('shotgun', 'shotgun_fire'),
                'shotgun_crouchfire': ('shotgun', 'shotgun_crouchfire')},
    'buddy': {'buddy_fire': ('buddy', 'buddy_fire'), 'buddy_fire_poor': ('buddy', 'buddy_fire'),
              'buddy_reload': ('buddy', 'buddy_reload'), 'buddy_crouchfire': ('buddy', 'buddy_crouchfire')},
    'uzi': {'UZI_fire': ('uzi', 'UZI_fire'), 'UZI_fire_poor': ('uzi', 'UZI_fire'), 'UZI_reload': ('uzi', 'UZI_reload'),
            'UZI_crouchfire': ('uzi', 'UZI_crouchfire'), 'UZI_crouchreload': ('uzi', 'UZI_crouchreload')},
    'rifle': {'RIFLE_fire': ('rifle', 'RIFLE_fire'), 'RIFLE_fire_poor': ('rifle', 'RIFLE_fire'), 'RIFLE_load': ('rifle', 'RIFLE_load'),
              'RIFLE_crouchfire': ('rifle', 'RIFLE_crouchfire'), 'RIFLE_crouchload': ('rifle', 'RIFLE_crouchload')},
    'flame': {'FLAME_fire': ('flame', 'FLAME_fire')},
    'grenade': {'WEAPON_start_throw': ('grenade', 'WEAPON_start_throw'), 'WEAPON_throw': ('grenade', 'WEAPON_throw'),
                'WEAPON_throwu': ('grenade', 'WEAPON_throwu')},
}


# Animations whose conversion is verified in game. Empty on purpose: copying the
# LCS local rotations onto the SA skeleton leaves forearms/hands 80-130 degrees
# off the SA aim pose (python_fire, UZI_fire), so guns aimed sideways/down and
# the fire tracks locked the legs. The SA animations stay until a retargeting
# step (per-bone rest-pose correction) exists and is checked against the game.
VERIFIED = frozenset()


def _padded(size):
    return (size + 3) & ~3


def read_anpk(data):
    """{animation: [(bone name, kind, frames)]} with float frames (quat, [trans,] time)."""
    if data[:4] != b'ANPK' or data[8:12] != b'INFO':
        raise ValueError('Not an ANPK animation block')
    cursor = 8
    size, count = struct.unpack_from('<Ii', data, cursor + 4)
    cursor += 8 + _padded(size)
    animations = {}
    for _ in range(count):
        if data[cursor:cursor + 4] != b'NAME':
            raise ValueError('ANPK animation without NAME')
        size = struct.unpack_from('<I', data, cursor + 4)[0]
        name = data[cursor + 8:cursor + 8 + size].split(b'\0')[0].decode('latin1')
        cursor += 8 + _padded(size)
        if data[cursor:cursor + 4] != b'DGAN':
            raise ValueError('ANPK animation without DGAN')
        end = cursor + 8 + struct.unpack_from('<I', data, cursor + 4)[0]
        cursor += 8
        size, objects = struct.unpack_from('<Ii', data, cursor + 4)
        cursor += 8 + _padded(size)
        sequences = []
        for _ in range(objects):
            if data[cursor:cursor + 4] != b'CPAN' or data[cursor + 8:cursor + 12] != b'ANIM':
                raise ValueError('ANPK sequence without CPAN/ANIM')
            cursor += 8
            size = struct.unpack_from('<I', data, cursor + 4)[0]
            body = data[cursor + 8:cursor + 8 + size]
            bone = body[:28].split(b'\0')[0].decode('latin1')
            frames = struct.unpack_from('<i', body, 28)[0]
            cursor += 8 + _padded(size)
            kind = data[cursor:cursor + 4]
            size = struct.unpack_from('<I', data, cursor + 4)[0]
            per = {b'KR00': 5, b'KRT0': 8, b'KRTS': 11}[kind]
            values = [struct.unpack_from('<%df' % per, data, cursor + 8 + 4 * per * i) for i in range(frames)]
            cursor += 8 + _padded(size)
            sequences.append((bone, kind.decode(), values))
        animations[name] = sequences
        cursor = end
    return animations


def read_anp3(data):
    """(block name, [(animation name, raw bytes)]) preserving each animation's bytes."""
    if data[:4] != b'ANP3':
        raise ValueError('Not an ANP3 animation block')
    block = data[8:32].split(b'\0')[0].decode('latin1')
    count = struct.unpack_from('<I', data, 32)[0]
    cursor, animations = 36, []
    for _ in range(count):
        start = cursor
        name = data[cursor:cursor + 24].split(b'\0')[0].decode('latin1')
        objects, frame_bytes, compressed = struct.unpack_from('<III', data, cursor + 24)
        if compressed != 1:
            raise ValueError('Uncompressed ANP3 animation: ' + name)
        cursor += 36
        total = 0
        for _ in range(objects):
            kind, frames = struct.unpack_from('<II', data, cursor + 24)
            if kind not in (3, 4):
                raise ValueError('Unknown ANP3 frame type in ' + name)
            cursor += 36 + frames * (10 if kind == 3 else 16)
            total += frames * (10 if kind == 3 else 16)
        if total != frame_bytes:
            raise ValueError('ANP3 frame size mismatch in ' + name)
        animations.append((name, data[start:cursor]))
    return block, animations


def write_anp3(block, animations):
    body = block.encode('latin1').ljust(24, b'\0') + struct.pack('<I', len(animations)) + b''.join(raw for _, raw in animations)
    return b'ANP3' + struct.pack('<I', len(body)) + body


def _int16(value, scale):
    return max(-32768, min(32767, int(round(value * scale))))


def anp3_bone_ids(raw):
    """Bone ids animated by one raw ANP3 animation record."""
    objects = struct.unpack_from('<I', raw, 24)[0]
    cursor, ids = 36, set()
    for _ in range(objects):
        kind, frames, bone_id = struct.unpack_from('<IIi', raw, cursor + 24)
        ids.add(bone_id)
        cursor += 36 + frames * (10 if kind == 3 else 16)
    return ids


def convert_animation(name, sequences, allowed=None):
    """One original ANPK animation as SA ANP3 bytes: quaternion conjugate
    (ANPK stores it negated), x4096; time in 1/60 s; translation x1024 only on
    the root, as other original translations carry LCS bone lengths.
    allowed: bone ids of the SA animation being replaced. SA fires and reloads
    standing animations on the upper body only (partial blend), so the walk
    cycle keeps driving the legs while aiming; the LCS ones also key the
    root, pelvis and legs, which froze the player in place."""
    objects, frame_bytes = [], 0
    for bone, kind, frames in sequences:
        if bone.lower() not in BONES:
            continue  # prop nodes (FLAME_fire animates 'chnsaw'); SA peds have no such node
        bone_id = BONES[bone.lower()]
        if allowed is not None and bone_id not in allowed:
            continue
        translated = bone_id == 0 and kind in ('KRT0', 'KRTS')
        packed = b''
        for frame in frames:
            x, y, z, w = frame[:4]
            time = frame[-1]
            values = [_int16(-x, 4096), _int16(-y, 4096), _int16(-z, 4096), _int16(w, 4096), _int16(time, 60)]
            if translated:
                values += [_int16(v, 1024) for v in frame[4:7]]
            packed += struct.pack('<%dh' % len(values), *values)
        objects.append(SA_BONE_NAMES[bone_id].encode('latin1').ljust(24, b'\0') +
                       struct.pack('<IIi', 4 if translated else 3, len(frames), bone_id) + packed)
        frame_bytes += len(packed)
    return (name.encode('latin1').ljust(24, b'\0') + struct.pack('<III', len(objects), frame_bytes, 1) + b''.join(objects))


def rebuild_block(sa_block, originals):
    """SA block bytes with the mapped animations replaced.
    originals: {original block name: ANPK bytes}."""
    block, animations = read_anp3(sa_block)
    if block.lower() not in BLOCKS:
        raise ValueError('No original counterpart for SA block ' + block)
    mapping = BLOCKS[block.lower()]
    parsed = {name: read_anpk(data) for name, data in originals.items()}
    names = {name for name, _ in animations}
    missing = set(mapping) - names
    if missing:
        raise ValueError('SA block lacks mapped animations: ' + ', '.join(sorted(missing)))
    rebuilt, replaced = [], []
    for name, raw in animations:
        if name in mapping and name in VERIFIED:
            source_block, source_name = mapping[name]
            raw = convert_animation(name, parsed[source_block][source_name], anp3_bone_ids(raw))
            replaced.append(name)
        rebuilt.append((name, raw))
    return write_anp3(block, rebuilt), replaced


def img_entries(path):
    """{lower-case name: bytes} of a SA VER2 IMG archive (anim/anim.img)."""
    data = path.read_bytes()
    if data[:4] != b'VER2':
        raise ValueError('Not a VER2 IMG archive: ' + str(path))
    count = struct.unpack_from('<I', data, 4)[0]
    entries = {}
    for index in range(count):
        offset, size, _, name = struct.unpack_from('<IHH24s', data, 8 + 32 * index)
        entries[name.split(b'\0')[0].decode('latin1').lower()] = data[offset * 2048:(offset + size) * 2048]
    return entries


def roundtrip(sa_block):
    """True when the parser/writer reproduce the SA block exactly (ignoring IMG padding)."""
    block, animations = read_anp3(sa_block)
    rebuilt = write_anp3(block, animations)
    return sa_block[:len(rebuilt)] == rebuilt and not sa_block[len(rebuilt):].strip(b'\0')
