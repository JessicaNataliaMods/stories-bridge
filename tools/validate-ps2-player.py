"""Audit Toni mesh topology, UVs, weights and texture pixels against PS2 input.

Runs offline; uses a separate bounded reader for the final RenderWare files.
The native SA rig is checked against the user's reference, never bundled media.
"""
import argparse
import hashlib
import importlib.util
import json
import math
import pathlib
import struct
from types import SimpleNamespace
from ps2_player import LABELS, run
from ps2_world import Game
import ped_rig_audit

_path = pathlib.Path(__file__).with_name('validate-ps2-texture-pixels.py')
_spec = importlib.util.spec_from_file_location('texture_audit', _path)
_textures = importlib.util.module_from_spec(_spec)
_spec.loader.exec_module(_textures)
chunks = _textures.chunks


def child(raw, parent, kind):
    found = [x for x in chunks(raw, parent[1], parent[2]) if x[0] == kind]
    if len(found) != 1:
        raise ValueError('Missing/duplicate RenderWare chunk: ' + hex(kind))
    return found[0]


def mesh(path):
    raw = path.read_bytes()
    root = next(chunks(raw, 0, len(raw)))
    if root[0] != 0x10 or any(raw[root[2]:]):
        raise ValueError('Expected one ped clump')
    bones = None
    frames = child(raw, root, 0x0e)
    for extension in chunks(raw, frames[1], frames[2]):
        if extension[0] != 3:
            continue
        for plugin in chunks(raw, extension[1], extension[2]):
            if plugin[0] != 0x11e:
                continue
            data = raw[plugin[1]:plugin[2]]
            version, _tag, count = struct.unpack_from('<3I', data)
            if count:
                if bones is not None or version != 256 or len(data) != 20 + count * 12:
                    raise ValueError('Invalid HAnim hierarchy')
                rows = list(struct.iter_unpack('<3i', data[20:]))
                if [x[1] for x in rows] != list(range(count)):
                    raise ValueError('Unexpected bone index order')
                bones = [x[0] for x in rows]
    if not bones or len(set(bones)) != len(bones):
        raise ValueError('Missing/duplicate bone IDs')
    geometries = child(raw, root, 0x1a)
    geometry = child(raw, geometries, 0x0f)
    data_chunk = child(raw, geometry, 1)
    data = raw[data_chunk[1]:data_chunk[2]]
    flags, nt, nv, nm = struct.unpack_from('<4I', data)
    if flags & 0x1000000 or not 0 < nv < 65536 or nm != 1:
        raise ValueError('Expected one editable ped geometry')
    cursor = 16
    colors = data[cursor:cursor + nv * 4] if flags & 8 else b''
    cursor += len(colors)
    sets = flags >> 16 & 255
    if not sets:
        sets = 2 if flags & 0x80 else int(bool(flags & 4))
    uv = data[cursor:cursor + nv * 8 * sets]
    cursor += len(uv)
    triangles = data[cursor:cursor + nt * 8]
    cursor += len(triangles)
    _x, _y, _z, _radius, has_vertices, has_normals = struct.unpack_from('<4f2I', data, cursor)
    cursor += 24
    if has_vertices != 1:
        raise ValueError('Missing ped vertices')
    vertices = list(struct.iter_unpack('<3f', data[cursor:cursor + nv * 12]))
    cursor += nv * 12
    if has_normals:
        normals = list(struct.iter_unpack('<3f', data[cursor:cursor + nv * 12]))
        if any(not all(math.isfinite(v) for v in n) for n in normals):
            raise ValueError('Invalid ped normals')
        cursor += nv * 12
    if cursor != len(data) or len(vertices) != nv:
        raise ValueError('Unparsed/truncated geometry')
    if any(not all(math.isfinite(v) and abs(v) < 3 for v in p) for p in vertices):
        raise ValueError('Invalid/exploded ped vertices')
    ext = child(raw, geometry, 3)
    skin_chunk = child(raw, ext, 0x116)
    skin = raw[skin_chunk[1]:skin_chunk[2]]
    nb, used, nw, _pad = struct.unpack_from('<4B', skin)
    if nb != len(bones) or not 1 <= nw <= 4:
        raise ValueError('Skin palette disagrees with HAnim')
    pos = 4 + used
    indices = skin[pos:pos + nv * 4]
    pos += nv * 4
    weights = skin[pos:pos + nv * 16]
    pos += nv * 16
    matrices = skin[pos:pos + nb * 64]
    if len(indices) != nv * 4 or len(weights) != nv * 16 or len(matrices) != nb * 64:
        raise ValueError('Truncated skin')
    return dict(bones=bones, nv=nv, nt=nt, colors=colors, uv=uv, triangles=triangles, vertices=vertices,
                indices=indices, weights=weights, matrices=matrices,
                bounds=[[min(p[i] for p in vertices), max(p[i] for p in vertices)] for i in range(3)])


def validate(rom, player, root):
    game = Game(rom, root / 'vendor/librwgta/librwgta-master/tools/storiesconv/hashes.inc')
    originals = {label: {} for label in LABELS}
    for texture in game.named_textures():
        label = texture['dictionary'].lower()
        if label in originals:
            w, h, pixels = texture['image']
            originals[label][texture['name']] = (w, h, pixels, any(a != 255 for a in pixels[3::4]))
    reference = mesh(player / 'intermediate/native-male01.dff')
    audit = player / 'intermediate/topology-audit'
    audit.mkdir(exist_ok=True)
    rows = []
    pixels = 0
    textures = 0
    for label in LABELS:
        source_path = audit / (label + '.dff')
        if not source_path.exists():
            run(root / 'build/Release/lcs-convert-rw.exe', player / 'intermediate' / (label + '.dff'), source_path)
        original = mesh(source_path)
        native = mesh(player / 'rw-assets' / ('lcs_' + label + '.dff'))
        for key in ('nv', 'nt', 'colors', 'uv', 'triangles', 'weights'):
            if original[key] != native[key]:
                raise ValueError(label + ': original ' + key + ' changed')
        rig=ped_rig_audit.verify(source_path, player / 'rw-assets' / ('lcs_' + label + '.dff'), player / 'intermediate/native-male01.dff', SimpleNamespace(mesh=mesh,chunks=chunks,child=child))
        for index, (weight,) in enumerate(struct.iter_unpack('<f', original['weights'])):
            if weight:
                source_tag = original['bones'][original['indices'][index]]
                target_tag = native['bones'][native['indices'][index]]
                if target_tag != {54: 44, 55: 54}.get(source_tag, source_tag):
                    raise ValueError(label + ': weighted bone identity changed')
        actual = _textures.native_textures(player / 'rw-assets' / ('lcs_' + label + '.txd'))
        if actual != originals[label]:
            raise ValueError(label + ': original texture names/pixels/alpha changed')
        textures += len(actual)
        pixels += sum(len(t[2]) for t in actual.values())
        rows.append(dict(label=label, vertices=native['nv'], triangles=native['nt'],
                         bounds=native['bounds'], nativeBones=len(native['bones']),rigAudit=rig))
    report = dict(valid=True, outfits=rows, textures=textures, pixelBytes=pixels,
                  sourceTopologyUVPrelightWeightsPreserved=True,
                  sourceTextureNamesPixelsAlphaPreserved=True,
                  nativeSkeletonBindMatricesVerified=True,
                  sourceBodyProportionsVerified=True, nativeAnimationAxesVerified=True, gameExecuted=False)
    (player / 'player-validation.json').write_text(json.dumps(report, indent=2), encoding='utf-8')
    return report


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--rom', required=True, type=pathlib.Path)
    parser.add_argument('--player', required=True, type=pathlib.Path)
    args = parser.parse_args()
    print(json.dumps(validate(args.rom, args.player, pathlib.Path(__file__).resolve().parents[1]), indent=2))
