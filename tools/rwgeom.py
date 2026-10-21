"""Decode / re-encode non-native RenderWare 3.6 geometries (stdlib only).

Only the subset used by the converted LCS ferry is supported (non-native,
single morph target, no skin).  Extension chunks other than BinMesh are kept
verbatim.
"""
import struct
from rwtree import parse

FLAG_TRISTRIP, FLAG_POSITIONS, FLAG_TEXTURED, FLAG_PRELIT, FLAG_NORMALS, FLAG_LIGHT, FLAG_MODULATE, FLAG_TEXTURED2 = 1, 2, 4, 8, 16, 32, 64, 128


def chunk(ident, ver, payload):
    return struct.pack('<3I', ident, len(payload), ver) + payload


class Geometry:
    pass


def decode_geometry(raw, node):
    """node: parsed Geometry(0xF) node.  Returns Geometry object."""
    g = Geometry()
    g.ver = node['ver']
    kids = node['kids']
    a, b = kids[0]['body']
    flags, nt, nv, nm = struct.unpack_from('<4I', raw, a)
    assert nm == 1 and not flags & 0x1000000, 'unsupported native/multi-morph'
    p = a + 16
    lib = (((g.ver >> 14) & 0x3ff00) + 0x30000) | ((g.ver >> 16) & 0x3f)
    g.lib = lib
    g.flags = flags
    g.ambient = None
    if lib < 0x34000:
        g.ambient = raw[p:p + 12]
        p += 12
    g.colors = None
    if flags & FLAG_PRELIT:
        g.colors = [struct.unpack_from('<4B', raw, p + 4 * i) for i in range(nv)]
        p += 4 * nv
    ntex = (flags >> 16) & 255
    if not ntex:
        ntex = 2 if flags & FLAG_TEXTURED2 else int(bool(flags & FLAG_TEXTURED))
    g.uvs = []
    for s in range(ntex):
        g.uvs.append([struct.unpack_from('<2f', raw, p + 8 * i) for i in range(nv)])
        p += 8 * nv
    # triangles: v2 v1 matId v3  (RW order: vertex2, vertex1, matId, vertex3)
    g.tris = []
    for i in range(nt):
        v2, v1, mat, v3 = struct.unpack_from('<4H', raw, p + 8 * i)
        g.tris.append((v1, v2, v3, mat))
    p += 8 * nt
    g.sphere = struct.unpack_from('<4f', raw, p)
    has_pos, has_nrm = struct.unpack_from('<2I', raw, p + 16)
    p += 24
    g.pos = [struct.unpack_from('<3f', raw, p + 12 * i) for i in range(nv)]
    p += 12 * nv
    g.nrm = None
    if has_nrm:
        g.nrm = [struct.unpack_from('<3f', raw, p + 12 * i) for i in range(nv)]
        p += 12 * nv
    assert p == b, 'geometry struct tail mismatch'
    g.nv, g.nt = nv, nt
    g.ntex = ntex
    # material list (raw bytes of each material chunk kept verbatim)
    ml = [k for k in kids if k['id'] == 8][0]
    ms = ml['kids'][0]
    a, b = ms['body']
    cnt = struct.unpack_from('<I', raw, a)[0]
    g.mat_index_src = list(struct.unpack_from('<%di' % cnt, raw, a + 4))
    g.materials = [raw[k['off']:k['body'][1]] for k in ml['kids'][1:]]
    g.mat_nodes = ml['kids'][1:]
    g.mat_ver = ml['ver']
    ext = [k for k in kids if k['id'] == 3][0]
    g.ext_other = []
    g.ext_ver = ext['ver']
    for k in ext['kids']:
        if k['id'] == 0x50E:
            g.binmesh_raw = raw[k['body'][0]:k['body'][1]]
            continue
        g.ext_other.append(raw[k['off']:k['body'][1]])
    return g


def material_info(raw, node):
    """Return (rgba, texture name, mask name) of a Material node."""
    a, b = node['kids'][0]['body']
    flags, color, unk, textured, amb, spec, diff = struct.unpack_from('<IIIIfff', raw, a)
    name = mask = None
    for k in node['kids']:
        if k['id'] == 6:
            strs = [kk for kk in k['kids'] if kk['id'] == 2]
            def s(n):
                x, y = n['body']
                return raw[x:y].split(b'\0')[0].decode('ascii')
            name, mask = s(strs[0]), s(strs[1])
    return dict(color=color.to_bytes(4, 'little'), textured=textured, name=name, mask=mask, amb=amb, spec=spec, diff=diff)


def build_binmesh(g):
    """BinMesh plugin payload as written by librw: triangle list per material."""
    # group indices per material id in the order of first use?  librw builds by
    # material index order; we use ascending material index.
    per = {}
    for v1, v2, v3, m in g.tris:
        per.setdefault(m, []).extend((v1, v2, v3))
    mats = sorted(per)
    total = sum(len(per[m]) for m in mats)
    out = struct.pack('<3I', 0, len(mats), total)  # flags(0=trilist), numMeshes, totalIndices
    for m in mats:
        out += struct.pack('<2I', len(per[m]), m) + struct.pack('<%dI' % len(per[m]), *per[m])
    return out


def encode_geometry(g, binmesh=None, drop_unused_materials=False):
    nv, nt = len(g.pos), len(g.tris)
    flags = g.flags
    body = struct.pack('<4I', flags, nt, nv, 1)
    if g.ambient is not None:
        body += g.ambient
    if flags & FLAG_PRELIT:
        for c in g.colors:
            body += struct.pack('<4B', *c)
    for uv in g.uvs:
        for u, v in uv:
            body += struct.pack('<2f', u, v)
    for v1, v2, v3, m in g.tris:
        body += struct.pack('<4H', v2, v1, m, v3)
    if getattr(g, 'keep_sphere', False):
        cx, cy, cz, r = g.sphere
    else:
        # bounding sphere: centre of the AABB, radius to the farthest vertex
        xs = g.pos
        cx = (min(p[0] for p in xs) + max(p[0] for p in xs)) / 2
        cy = (min(p[1] for p in xs) + max(p[1] for p in xs)) / 2
        cz = (min(p[2] for p in xs) + max(p[2] for p in xs)) / 2
        r = max(((p[0] - cx) ** 2 + (p[1] - cy) ** 2 + (p[2] - cz) ** 2) ** .5 for p in xs)
    body += struct.pack('<4f', cx, cy, cz, r) + struct.pack('<2I', 1, 1 if g.nrm else 0)
    for p in g.pos:
        body += struct.pack('<3f', *p)
    if g.nrm:
        for p in g.nrm:
            body += struct.pack('<3f', *p)
    ver = g.ver
    struct_chunk = chunk(1, ver, body)
    n = len(g.materials)
    ms = chunk(1, g.mat_ver, struct.pack('<I', n) + struct.pack('<%di' % n, *([-1] * n)))
    ml = chunk(8, g.mat_ver, ms + b''.join(g.materials))
    ext_payload = chunk(0x50E, g.ext_ver, binmesh if binmesh is not None else build_binmesh(g)) + b''.join(g.ext_other)
    return chunk(0xF, ver, struct_chunk + ml + chunk(3, g.ext_ver, ext_payload))
