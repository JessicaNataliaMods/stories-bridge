"""Clump-level read/modify/write helpers for the ferry DFFs (stdlib only)."""
import struct
from rwtree import parse
from rwgeom import decode_geometry, encode_geometry, chunk, material_info


class Frame:
    def __init__(self, rot, pos, parent, flags, ext_raw):
        self.rot = list(rot)      # right(3), up(3), at(3)
        self.pos = list(pos)
        self.parent = parent
        self.flags = flags
        self.ext_raw = ext_raw    # raw Extension chunk bytes (contains NodeName)
        self.name = ''

    def mat_mul_point(self, p):
        r, u, a = self.rot[0:3], self.rot[3:6], self.rot[6:9]
        return [p[0] * r[i] + p[1] * u[i] + p[2] * a[i] + self.pos[i] for i in range(3)]


def node_name(raw, ext_node):
    for k in ext_node.get('kids', []):
        if k['id'] == 0x253F2FE:
            a, b = k['body']
            return raw[a:b].split(b'\0')[0].decode('latin1')
    return ''


class Clump:
    pass


def load(path):
    raw = open(path, 'rb').read()
    top = parse(raw)
    assert len(top) == 1 and top[0]['id'] == 0x10
    node = top[0]
    c = Clump()
    c.raw = raw
    c.ver = node['ver']
    kids = node['kids']
    a, b = kids[0]['body']
    c.n_atomics, c.n_lights, c.n_cameras = struct.unpack_from('<3I', raw, a)
    fl = [k for k in kids if k['id'] == 14][0]
    fa, fb = fl['kids'][0]['body']
    nf = struct.unpack_from('<I', raw, fa)[0]
    c.frames = []
    exts = fl['kids'][1:]
    assert len(exts) == nf
    for i in range(nf):
        rec = struct.unpack_from('<12f', raw, fa + 4 + 56 * i)
        parent, flags = struct.unpack_from('<iI', raw, fa + 4 + 56 * i + 48)
        f = Frame(rec[0:9], rec[9:12], parent, flags, raw[exts[i]['off']:exts[i]['body'][1]])
        f.name = node_name(raw, exts[i])
        c.frames.append(f)
    c.frame_ver = fl['ver']
    gl = [k for k in kids if k['id'] == 26][0]
    c.geom_ver = gl['ver']
    c.geom_nodes = [k for k in gl['kids'] if k['id'] == 15]
    c.geom_raw = [raw[k['off']:k['body'][1]] for k in c.geom_nodes]
    c.atomics = []
    c.atomic_ext_raw = []
    for k in kids:
        if k['id'] == 20:
            sa, sb = k['kids'][0]['body']
            c.atomics.append(list(struct.unpack_from('<4I', raw, sa)))
            c.atomic_ext_raw.append(raw[k['kids'][1]['off']:k['kids'][1]['body'][1]])
    c.atomic_ver = [k for k in kids if k['id'] == 20][0]['ver']
    ext = [k for k in kids if k['id'] == 3 and k is kids[-1]][0]
    c.clump_ext_raw = raw[ext['off']:ext['body'][1]]
    c.clump_ext_node = ext
    return c


def world_in(c, idx, stop_idx):
    """Transform of frame idx expressed in the local space of ancestor stop_idx (inclusive of idx up to but excluding stop)."""
    chain = []
    i = idx
    while i != stop_idx:
        chain.append(i)
        i = c.frames[i].parent
        assert i >= 0, 'stop frame is not an ancestor'
    return chain  # leaf..first-child-of-stop


def to_ancestor(c, idx, stop_idx, p):
    for i in world_in(c, idx, stop_idx):
        p = c.frames[i].mat_mul_point(p)
    return p


def from_ancestor(c, idx, stop_idx, p):
    """Inverse of to_ancestor (rotations assumed orthonormal)."""
    for i in reversed(world_in(c, idx, stop_idx)):
        f = c.frames[i]
        q = [p[k] - f.pos[k] for k in range(3)]
        r, u, a = f.rot[0:3], f.rot[3:6], f.rot[6:9]
        p = [q[0] * r[0] + q[1] * r[1] + q[2] * r[2],
             q[0] * u[0] + q[1] * u[1] + q[2] * u[2],
             q[0] * a[0] + q[1] * a[1] + q[2] * a[2]]
    return p


def rot_inverse_dir(c, idx, stop_idx, n):
    """Apply inverse rotation chain to a direction."""
    for i in reversed(world_in(c, idx, stop_idx)):
        f = c.frames[i]
        r, u, a = f.rot[0:3], f.rot[3:6], f.rot[6:9]
        n = [n[0] * r[0] + n[1] * r[1] + n[2] * r[2],
             n[0] * u[0] + n[1] * u[1] + n[2] * u[2],
             n[0] * a[0] + n[1] * a[1] + n[2] * a[2]]
    return n


def build(c, frames=None, geoms=None, atomics=None, clump_ext_raw=None):
    """Serialise a clump.  geoms: list of raw geometry chunk bytes."""
    frames = c.frames if frames is None else frames
    geoms = c.geom_raw if geoms is None else geoms
    atomics = c.atomics if atomics is None else atomics
    ver = c.ver
    cs = chunk(1, ver, struct.pack('<3I', len(atomics), 0, 0))
    fs = struct.pack('<I', len(frames))
    for f in frames:
        fs += struct.pack('<12f', *(f.rot + f.pos)) + struct.pack('<iI', f.parent, f.flags)
    fl = chunk(14, c.frame_ver, chunk(1, c.frame_ver, fs) + b''.join(f.ext_raw for f in frames))
    gl = chunk(26, c.geom_ver, chunk(1, c.geom_ver, struct.pack('<I', len(geoms))) + b''.join(geoms))
    at = b''
    for a, ext in zip(atomics, c.atomic_ext_raw + [c.atomic_ext_raw[0]] * (len(atomics) - len(c.atomic_ext_raw))):
        at += chunk(0x14, c.atomic_ver, chunk(1, c.atomic_ver, struct.pack('<4I', *a)) + ext)
    ce = c.clump_ext_raw if clump_ext_raw is None else clump_ext_raw
    return chunk(0x10, ver, cs + fl + gl + at + ce)
