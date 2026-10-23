"""Original LCS ped animations converted for the converted SA ped rig.

The converted peds (convert_ped.cpp, --sa-ped) keep each original skin in its
own bone-local coordinates and drive it with the SA frame tree. Original and SA
animations use the same absolute orientation convention (an original and an SA
`sprint_civi` differ by about one degree), so an original animation is exact
when every bone ends up with the world orientation it has on the original rig.
The two skeletons share bone ids, parents and local axes except for one thing:
the original thighs are children of Spine, SA's are children of Pelvis. A key
copied unchanged leaves the feet 6 to 16 cm off the original position (sliding,
bent feet) whenever the spine moves; folding the spine rotation into the thigh
key removes the error (measured against a simulation of both rigs: 0.3-2 cm).

Pure data transforms, standard library only; never executes GTA.
Quaternions are the runtime form stored in ANP3 (x, y, z, w); the original ANPK
stores the conjugate. Frame matrices are row-vector (child world = local *
parent world); rmul(a, b) is the quaternion of the product of their matrices.
"""
import math
import struct

import weapon_animations as wa

# Original skeleton: Root > Pelvis > Spine > {Spine1 > Neck > {Head, clavicles},
# thighs}; SA moves the thighs under Pelvis. Ids are shared by both rigs.
LCS_PARENT = {0: None, 1: 0, 2: 1, 3: 2, 4: 3, 5: 4, 21: 4, 22: 21, 23: 22, 24: 23, 25: 24,
              31: 4, 32: 31, 33: 32, 34: 33, 35: 34, 41: 2, 42: 41, 43: 42, 44: 43,
              51: 2, 52: 51, 53: 52, 54: 53}
THIGHS = (41, 51)
IDENTITY = (0.0, 0.0, 0.0, 1.0)

# Original frame names. The ANPK also carries SA-style names with a leading
# space, 'normal' for the root and 'Spine 1'/'Spine 2' for Spine/Spine1. Nodes
# that are not here (Belly, breasts, Jaw, brows, Finger01, props such as the
# chainsaw) do not exist on the 24-bone skin and drive nothing.
_NAMES = {'root': 0, 'normal': 0, 'pelvis': 1, 'spine': 2, 'spine 1': 2, 'spine1': 3, 'spine 2': 3,
          'neck': 4, 'head': 5, 'bip01 r clavicle': 21, 'r upperarm': 22, 'r forearm': 23, 'r hand': 24,
          'r finger': 25, 'r fingers': 25, 'bip01 l clavicle': 31, 'l upperarm': 32, 'l forearm': 33,
          'l hand': 34, 'l finger': 35, 'l fingers': 35, 'l thigh': 41, 'l calf': 42, 'l foot': 43,
          'l toe0': 44, 'r thigh': 51, 'r calf': 52, 'r foot': 53, 'r toe0': 54}


def bone_id(name):
    return _NAMES.get(name.strip().lower())


# ---------------------------------------------------------------- quaternions
def qmul(a, b):
    ax, ay, az, aw = a
    bx, by, bz, bw = b
    return (aw * bx + ax * bw + ay * bz - az * by, aw * by - ax * bz + ay * bw + az * bx,
            aw * bz + ax * by - ay * bx + az * bw, aw * bw - ax * bx - ay * by - az * bz)


def rmul(a, b):
    """Quaternion of qmat(a) * qmat(b) (RenderWare row-vector order)."""
    return qmul(b, a)


def qnorm(q):
    n = math.sqrt(sum(v * v for v in q))
    return tuple(v / n for v in q) if n > 1e-12 else IDENTITY


def qslerp(a, b, t):
    dot = sum(x * y for x, y in zip(a, b))
    if dot < 0:
        b, dot = tuple(-v for v in b), -dot
    if dot > 0.9995:
        return qnorm(tuple(x + (y - x) * t for x, y in zip(a, b)))
    theta = math.acos(min(1.0, dot))
    s = math.sin(theta)
    wa_, wb = math.sin((1 - t) * theta) / s, math.sin(t * theta) / s
    return qnorm(tuple(wa_ * x + wb * y for x, y in zip(a, b)))


def qmat(q):
    """Row-vector rotation matrix of a runtime quaternion (checked against the
    rest rotations of both rigs)."""
    x, y, z, w = qnorm(q)
    return [[1 - 2 * (y * y + z * z), 2 * (x * y + z * w), 2 * (x * z - y * w)],
            [2 * (x * y - z * w), 1 - 2 * (x * x + z * z), 2 * (y * z + x * w)],
            [2 * (x * z + y * w), 2 * (y * z - x * w), 1 - 2 * (x * x + y * y)]]


# --------------------------------------------------------------------- tracks
# A track is a time-ordered list of (time seconds, quaternion, translation|None).
def sample(track, t):
    if t <= track[0][0]:
        return track[0][1]
    for (t0, q0, _), (t1, q1, _) in zip(track, track[1:]):
        if t <= t1:
            return q0 if t1 <= t0 else qslerp(q0, q1, (t - t0) / (t1 - t0))
    return track[-1][1]


def sample_translation(track, t):
    keys = [k for k in track if k[2] is not None]
    if not keys:
        return None
    if t <= keys[0][0]:
        return keys[0][2]
    for (t0, _, p0), (t1, _, p1) in zip(keys, keys[1:]):
        if t <= t1:
            f = 0.0 if t1 <= t0 else (t - t0) / (t1 - t0)
            return tuple(a + (b - a) * f for a, b in zip(p0, p1))
    return keys[-1][2]


def _continuous(track):
    """Same rotations, with successive quaternions in one hemisphere."""
    out, previous = [], None
    for t, q, p in track:
        if previous is not None and sum(x * y for x, y in zip(q, previous)) < 0:
            q = tuple(-v for v in q)
        out.append((t, q, p))
        previous = q
    return out


def read_original_block(data):
    """{animation: {bone id: track}} from an original ANPK block (runtime
    quaternions). A bone named twice keeps its first sequence."""
    animations = {}
    for name, sequences in wa.read_anpk(data).items():
        tracks = {}
        for bone, kind, frames in sequences:
            bid = bone_id(bone)
            if bid is None or bid in tracks:
                continue
            keys = []
            for frame in frames:
                x, y, z, w = frame[:4]
                keys.append((frame[-1], (-x, -y, -z, w), tuple(frame[4:7]) if kind != 'KR00' else None))
            keys.sort(key=lambda k: k[0])  # a few original sequences list times out of order
            tracks[bid] = _continuous(keys)
        animations[name] = tracks
    return animations


def read_sa_animation(raw):
    """(tracks of the original rig's bones, raw object bytes of every other bone)
    of one ANP3 animation record."""
    objects = struct.unpack_from('<I', raw, 24)[0]
    cursor, tracks, others = 36, {}, []
    for _ in range(objects):
        kind, count, bid = struct.unpack_from('<IIi', raw, cursor + 24)
        size = 36 + count * (10 if kind == 3 else 16)
        record = raw[cursor:cursor + size]
        cursor += size
        if bid not in LCS_PARENT:
            others.append(record)
            continue
        keys = []
        for i in range(count):
            if kind == 3:
                v = struct.unpack_from('<5h', record, 36 + 10 * i)
                keys.append((v[4] / 60, tuple(c / 4096 for c in v[:4]), None))
            else:
                v = struct.unpack_from('<8h', record, 36 + 16 * i)
                keys.append((v[4] / 60, tuple(c / 4096 for c in v[:4]), tuple(c / 1024 for c in v[5:8])))
        tracks[bid] = _continuous(keys)
    return tracks, others


# ------------------------------------------------------------------- convert
def original_to_sa(tracks, allowed=None):
    """Original animation -> tracks for the SA frame tree.

    Every bone keeps its local key; the thighs are expressed relative to Pelvis
    (spine * thigh, resampled where their key times differ). Translation is kept
    for the root only: other original translations are offsets inside the
    original tree and the converted rig already carries the skin's own lengths.
    allowed: ids a partial animation may key (None: all)."""
    result = {}
    for bid, track in tracks.items():
        if allowed is not None and bid not in allowed:
            continue
        if bid in THIGHS:
            spine = tracks.get(2)
            times = sorted({k[0] for k in track} | ({k[0] for k in spine} if spine else set()))
            result[bid] = _continuous([(t, rmul(sample(track, t), sample(spine, t) if spine else IDENTITY), None)
                                       for t in times])
        elif bid == 0:
            result[bid] = list(track)
        else:
            result[bid] = [(t, q, None) for t, q, _ in track]
    return result


def _int16(value, scale):
    return max(-32767, min(32767, int(round(value * scale))))


def encode_object(bid, track):
    """One compressed ANP3 object (rotation frames; the root also carries the
    translation, x1024). Times are ticks of 1/60 s, strictly increasing."""
    translated = bid == 0 and any(k[2] is not None for k in track)
    rows = []
    for t, q, p in track:
        tick = _int16(t, 60)
        if rows and tick <= rows[-1][0]:
            rows.pop()  # two keys in one tick: the later one wins
        rows.append((tick, q, p))
    data = b''
    for tick, q, p in rows:
        values = [_int16(q[0], 4096), _int16(q[1], 4096), _int16(q[2], 4096), _int16(q[3], 4096), tick]
        if translated:
            values += [_int16(c, 1024) for c in (p if p is not None else (0, 0, 0))]
        data += struct.pack('<%dh' % len(values), *values)
    return (wa.SA_BONE_NAMES[bid].encode('latin1').ljust(24, b'\0') +
            struct.pack('<IIi', 4 if translated else 3, len(rows), bid) + data)


def encode_animation(name, tracks, extra=()):
    objects = [encode_object(b, tracks[b]) for b in sorted(tracks) if tracks[b]] + list(extra)
    return (name.encode('latin1').ljust(24, b'\0') +
            struct.pack('<III', len(objects), sum(len(o) - 36 for o in objects), 1) + b''.join(objects))


def duration(tracks):
    return max((k[0] for t in tracks.values() for k in t), default=0.0)


def sa_duration(raw):
    """Length of an ANP3 animation over all of its objects (the engine's total time)."""
    objects = struct.unpack_from('<I', raw, 24)[0]
    cursor, end = 36, 0
    for _ in range(objects):
        kind, count, _ = struct.unpack_from('<IIi', raw, cursor + 24)
        stride = 10 if kind == 3 else 16
        if count:
            end = max(end, struct.unpack_from('<h', raw, cursor + 36 + stride * (count - 1) + 8)[0] / 60)
        cursor += 36 + count * stride
    return end


def stretch(tracks, factor):
    return {b: [(t * factor, q, p) for t, q, p in track] for b, track in tracks.items()}


def convert_block(sa_block, original_blocks, name_map=None, same_bones=False, add_original_only=False,
                  keep_sa_length=None):
    """Rebuild an SA ANP3 block with original animations.

    original_blocks: {original block name: ANPK bytes}.
    name_map: {SA animation: (original block, original animation)}; None maps
              equal names (case-insensitive) of the single original block.
    same_bones: a replacement keys only the bones the SA animation keys (SA fires
              and reloads on the upper body so the walk keeps driving the legs).
    add_original_only: append original animations the SA block has no name for.
    keep_sa_length: predicate on the SA animation name. The SA task code that
              drives car doors, seats, jumps and falls waits for fixed times of the
              animation, so those keep the SA length (times scaled; skipped when the
              lengths differ by more than a factor of two, e.g. long idles).
    Returns (ANP3 bytes, report): {animation: 'original' | 'kept' | 'added'}."""
    block, animations = wa.read_anp3(sa_block)
    parsed = {name: read_original_block(data) for name, data in original_blocks.items()}
    if name_map is None:
        if len(parsed) != 1:
            raise ValueError('Equal-name mapping needs exactly one original block')
        owner, only = next(iter(parsed.items()))
        lookup = {name.lower(): (owner, name) for name in only}
        name_map = {n: lookup[n.lower()] for n, _ in animations if n.lower() in lookup}
    report, rebuilt, used = {}, [], set()
    for name, raw in animations:
        source = name_map.get(name)
        if source is None:
            rebuilt.append((name, raw))
            report[name] = 'kept'
            continue
        used.add(source)
        allowed = set(read_sa_animation(raw)[0]) if same_bones else None
        converted = original_to_sa(parsed[source[0]][source[1]], allowed)
        if not converted:
            raise ValueError('Original animation has no usable bone: ' + name)
        if keep_sa_length and keep_sa_length(name):
            wanted, got = sa_duration(raw), duration(converted)
            if got > 0 and 0.5 <= wanted / got <= 2.0 and abs(wanted - got) > 1 / 60:
                converted = stretch(converted, wanted / got)
        rebuilt.append((name, encode_animation(name, converted)))
        report[name] = 'original'
    if add_original_only:
        known = {n.lower() for n, _ in animations}
        for owner, anims in parsed.items():
            for name, tracks in anims.items():
                if name.lower() in known or (owner, name) in used or not tracks:
                    continue
                known.add(name.lower())
                rebuilt.append((name, encode_animation(name, original_to_sa(tracks))))
                report[name] = 'added'
    return wa.write_anp3(block, rebuilt), report


# ----------------------------------------------------- ANPK blocks (gestures)
def _padded(size):
    return (size + 3) & ~3


def _section(data, at):
    tag, size = struct.unpack_from('<4sI', data, at)
    return tag, at + 8, at + 8 + size, at + 8 + _padded(size)


def _pack(tag, payload):
    return tag + struct.pack('<I', len(payload)) + payload + b'\0' * (-len(payload) % 4)


def _anpk_sequences(dgan):
    """[(INFO bytes of the DGAN, [(ANIM bytes, keyframe tag, keyframe bytes, bone name)])]"""
    tag, begin, end, _ = _section(dgan, 0)
    info = dgan[:_section(dgan, 0)[3]]
    at, sequences = len(info), []
    while at < len(dgan):
        tag, begin, end, padded = _section(dgan, at)
        if tag != b'CPAN':
            raise ValueError('ANPK sequence without CPAN')
        _, anim_begin, anim_end, after_anim = _section(dgan, begin)
        anim = dgan[anim_begin:anim_end]
        if len(anim) not in (40, 44, 48):
            raise ValueError('Unsupported original sequence header')
        keys_tag, keys_begin, keys_end, _ = _section(dgan, after_anim) if after_anim < end else (b'', 0, 0, 0)
        sequences.append((anim, keys_tag, dgan[keys_begin:keys_end], anim[:28].split(b'\0')[0].decode('latin1')))
        at = padded
    return info, sequences


def _keys_of(tag, payload):
    per = {b'KR00': 5, b'KRT0': 8, b'KRTS': 11}[tag]
    count = len(payload) // (4 * per)
    keys = []
    for i in range(count):
        v = struct.unpack_from('<%df' % per, payload, 4 * per * i)
        x, y, z, w = v[:4]
        keys.append((v[-1], (-x, -y, -z, w), None))
    keys.sort(key=lambda k: k[0])
    return _continuous(keys)


def retarget_anpk(data):
    """An original ANPK block for the SA frame tree: in every animation that keys
    a thigh, the thigh keys become spine * thigh (see module docstring). All
    other records stay byte-exact. Returns (bytes, number of thighs rewritten)."""
    tag, begin, end, _ = _section(data, 0)
    if tag != b'ANPK':
        raise ValueError('Not an ANPK block')
    info_tag, ib, ie, at = _section(data, begin)
    out, rewritten = [data[:at]], 0
    while at < end:
        name_tag, nb, ne, after_name = _section(data, at)
        dgan_tag, db, de, after_dgan = _section(data, after_name)
        if name_tag != b'NAME' or dgan_tag != b'DGAN':
            raise ValueError('Unexpected ANPK record')
        out.append(data[at:after_name])
        info, sequences = _anpk_sequences(data[db:de])
        spine = next((_keys_of(k, p) for a, k, p, n in sequences if bone_id(n) == 2 and k), None)
        body = info
        for anim, keys_tag, payload, bone in sequences:
            bid = bone_id(bone)
            if bid in THIGHS and spine is not None and keys_tag:
                thigh = _keys_of(keys_tag, payload)
                times = sorted({k[0] for k in thigh} | {k[0] for k in spine})
                rows = b''
                for t in times:
                    x, y, z, w = rmul(sample(thigh, t), sample(spine, t))
                    rows += struct.pack('<5f', -x, -y, -z, w, t)
                header = bytearray(anim)
                struct.pack_into('<i', header, 28, len(times))
                struct.pack_into('<i', header, 36, len(times) - 1)
                body += _pack(b'CPAN', _pack(b'ANIM', bytes(header)) + _pack(b'KR00', rows))
                rewritten += 1
            else:
                body += _pack(b'CPAN', _pack(b'ANIM', anim) + (_pack(keys_tag, payload) if keys_tag else b''))
        out.append(_pack(b'DGAN', body))
        at = after_dgan
    payload = b''.join(out)[8:]
    return _pack(b'ANPK', payload) + data[_section(data, 0)[3]:], rewritten
