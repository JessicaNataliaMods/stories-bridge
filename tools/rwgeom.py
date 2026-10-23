"""Decode / re-encode non-native RenderWare 3.6 geometries (stdlib only).

Only the subset used by the converted LCS ferry is supported (non-native,
single morph target, no skin).  Extension chunks other than BinMesh are kept
verbatim.
"""
import math
import struct
from rwtree import parse

FLAG_TRISTRIP, FLAG_POSITIONS, FLAG_TEXTURED, FLAG_PRELIT, FLAG_NORMALS, FLAG_LIGHT, FLAG_MODULATE, FLAG_TEXTURED2 = 1, 2, 4, 8, 16, 32, 64, 128
BREAKABLE_PLUGIN = 0x253F2FD


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


def _walk(nodes):
    for node in nodes:
        yield node
        yield from _walk(node.get('kids', ()))


def _rw_roots(raw):
    from rwtree import parse

    if len(raw) < 12:
        raise ValueError('Truncated RenderWare DFF')
    root_end = struct.unpack_from('<I', raw, 4)[0] + 12
    if root_end > len(raw) or any(raw[root_end:]):
        raise ValueError('Invalid RenderWare DFF bounds or nonzero padding')
    roots = parse(raw, 0, root_end)
    if len(roots) != 1 or roots[0]['id'] != 16:
        raise ValueError('Expected one bounded RenderWare Clump')
    return roots, root_end


def _render_chunk_tree(raw, node, additions):
    """Rebuild container lengths while leaving every leaf payload untouched."""
    if 'kids' not in node:
        a, b = node['body']
        payload = raw[a:b]
    else:
        a, b = node['body']
        cursor = a
        parts = []
        for child in node['kids']:
            if child['off'] != cursor:
                raise ValueError('Container has unparsed bytes between chunks')
            parts.append(_render_chunk_tree(raw, child, additions))
            cursor = child['body'][1]
        if cursor != b:
            raise ValueError('Container has trailing unparsed bytes')
        payload = b''.join(parts)
        payload += b''.join(additions.get(node['off'], ()))
    ident, _, ver = struct.unpack_from('<3I', raw, node['off'])
    return struct.pack('<3I', ident, len(payload), ver) + payload


def _patch_geometry_extensions(raw, additions):
    """Append plugin chunks to geometry Extension chunks without RW re-encoding.

    `additions` maps geometry indexes to lists of complete plugin chunks. Only
    ancestor chunk sizes and the target Extension payload change.
    """
    roots, root_end = _rw_roots(raw)
    geometries = [n for n in _walk(roots) if n['id'] == 15]
    if any(index < 0 or index >= len(geometries) for index in additions):
        raise ValueError('Geometry index outside DFF')
    ext_additions = {}
    for index, chunks in additions.items():
        geometry = geometries[index]
        extensions = [n for n in geometry.get('kids', ()) if n['id'] == 3]
        if len(extensions) != 1:
            raise ValueError('Geometry must have exactly one Extension chunk')
        extension = extensions[0]
        present = [n for n in extension.get('kids', ())]
        existing_ids = [n['id'] for n in present]
        for plugin in chunks:
            if len(plugin) < 12:
                raise ValueError('Truncated plugin chunk')
            plugin_id, plugin_size, _ = struct.unpack_from('<3I', plugin)
            if plugin_size != len(plugin) - 12:
                raise ValueError('Plugin chunk length mismatch')
            if plugin_id in existing_ids:
                existing = next(n for n in present if n['id'] == plugin_id)
                a, b = existing['off'], existing['body'][1]
                if raw[a:b] != plugin:
                    raise ValueError('Refusing to replace a different existing geometry plugin')
                continue
            existing_ids.append(plugin_id)
            ext_additions.setdefault(extension['off'], []).append(plugin)
    if not ext_additions:
        return raw
    output = []
    cursor = 0
    for root in roots:
        if root['off'] != cursor:
            raise ValueError('DFF has unparsed bytes between top-level chunks')
        output.append(_render_chunk_tree(raw, root, ext_additions))
        cursor = root['body'][1]
    if cursor != root_end:
        raise ValueError('DFF has trailing unparsed bytes')
    return b''.join(output) + raw[root_end:]


def _breakable_plugin(raw, geometry_node, position_to_object=True):
    g = decode_geometry(raw, geometry_node)
    if not 0 < g.nv <= 0xFFFF or not 0 < g.nt <= 0xFFFF:
        raise ValueError('Breakable geometry needs 1..65535 vertices and faces')
    if not g.materials or len(g.materials) > 0xFFFF:
        raise ValueError('Breakable geometry has no materials or too many materials')
    if not g.uvs and any(material_info(raw, node)['textured'] for node in g.mat_nodes):
        raise ValueError('Textured breakable geometry has no UV coordinates')

    materials = [material_info(raw, node) for node in g.mat_nodes]
    if len(materials) != len(g.materials):
        raise ValueError('Breakable material list mismatch')
    for triangle in g.tris:
        if len(triangle) != 4:
            raise ValueError('Malformed breakable triangle')
        v1, v2, v3, material = triangle
        if max(v1, v2, v3) >= g.nv or material >= len(materials):
            raise ValueError('Breakable triangle index outside geometry')

    header = [0] * 13
    header[0] = int(bool(position_to_object))
    header[1] = g.nv
    header[5] = g.nt
    header[8] = len(materials)
    payload = struct.pack('<I13I', 1, *header)
    colors = g.colors if g.colors is not None else [(255, 255, 255, 255)] * g.nv
    uvs = g.uvs[0] if g.uvs else [(0.0, 0.0)] * g.nv
    for index, vertex in enumerate(g.pos):
        if any(not math.isfinite(value) for value in (*vertex, *uvs[index])):
            raise ValueError('Breakable geometry has a nonfinite vertex or UV')
        payload += struct.pack('<3f', *vertex)
    for uv in uvs:
        payload += struct.pack('<2f', *uv)
    for color in colors:
        payload += struct.pack('<4B', *color)
    for v1, v2, v3, _material in g.tris:
        payload += struct.pack('<3H', v1, v2, v3)
    for _v1, _v2, _v3, material in g.tris:
        payload += struct.pack('<H', material)

    for key in ('name', 'mask'):
        for material in materials:
            name = material[key] or ''
            try:
                encoded = name.encode('ascii')
            except UnicodeEncodeError as exc:
                raise ValueError('Breakable texture names must be ASCII') from exc
            if len(encoded) > 31:
                raise ValueError('Breakable texture name exceeds 31 bytes')
            payload += encoded + bytes(32 - len(encoded))
    for material in materials:
        props = (material['amb'], material['spec'], material['diff'])
        if any(not math.isfinite(value) for value in props):
            raise ValueError('Breakable material has nonfinite surface properties')
        payload += struct.pack('<3f', *props)

    extension = next((n for n in geometry_node.get('kids', ()) if n['id'] == 3), None)
    if extension is None:
        raise ValueError('Geometry has no Extension chunk')
    return chunk(BREAKABLE_PLUGIN, extension['ver'], payload)


def inject_breakable(raw, geometry_indexes=None, position_to_object=True):
    """Attach Rockstar's registered Breakable geometry plugin to source meshes.

    The plugin stream layout follows librwgta's registered ID_BREAKABLE
    (0x253F2FD): positions, first UV set, vertex RGBA, triangle indices,
    per-face material indices, texture/mask names and source surface props.
    No geometry is generated or re-encoded. Existing plugins are preserved;
    an existing nonidentical Breakable plugin is an error.
    """
    roots, _root_end = _rw_roots(raw)
    geometries = [n for n in _walk(roots) if n['id'] == 15]
    selected = list(range(len(geometries))) if geometry_indexes is None else list(geometry_indexes)
    if len(set(selected)) != len(selected):
        raise ValueError('Duplicate geometry index')
    additions = {}
    for index in selected:
        if not 0 <= index < len(geometries):
            raise ValueError('Geometry index outside DFF')
        plugin = _breakable_plugin(raw, geometries[index], position_to_object)
        ext = next((n for n in geometries[index].get('kids', ()) if n['id'] == 3), None)
        if ext is None:
            raise ValueError('Geometry has no Extension chunk')
        existing = next((n for n in ext.get('kids', ()) if n['id'] == BREAKABLE_PLUGIN), None)
        if existing is not None:
            a, b = existing['off'], existing['body'][1]
            if raw[a:b] != plugin:
                raise ValueError('Refusing to replace a different existing Breakable plugin')
            continue
        additions[index] = [plugin]
    if not additions:
        return raw
    return _patch_geometry_extensions(raw, additions)


def geometry_plugins(raw, geometry_index, plugin_ids):
    """Return exact plugin chunk bytes for one Geometry, keyed by plugin ID."""
    roots, _root_end = _rw_roots(raw)
    geometries = [n for n in _walk(roots) if n['id'] == 15]
    if not 0 <= geometry_index < len(geometries):
        raise ValueError('Geometry index outside DFF')
    extensions = [n for n in geometries[geometry_index].get('kids', ()) if n['id'] == 3]
    if len(extensions) != 1:
        raise ValueError('Geometry must have exactly one Extension chunk')
    result = {}
    for node in extensions[0].get('kids', ()):
        if node['id'] in plugin_ids:
            a, b = node['off'], node['body'][1]
            if node['id'] in result:
                raise ValueError('Duplicate geometry plugin ID')
            result[node['id']] = raw[a:b]
    return result


def copy_geometry_plugins(source, target, plugin_ids, source_geometry=0, target_geometry=0):
    """Copy selected plugin chunks verbatim between geometry indexes."""
    plugins = geometry_plugins(source, source_geometry, set(plugin_ids))
    return _patch_geometry_extensions(target, {target_geometry: list(plugins.values())}) if plugins else target
