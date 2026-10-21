"""Extract only the three resident retail marker meshes, without game execution.

Uses the existing PS2 RSL and SA RW converters. Does not stage/install a package
or grant opcode support. The original PS2 mesh types are not SA type aliases.
"""
import argparse
import hashlib
import json
from pathlib import Path
import struct
import subprocess

from ps2_world import Chunk, Game
from ps2_geometry_audit import geometries

ROOT = Path(__file__).resolve().parents[1]


def run(executable, *args, cwd=None):
    result = subprocess.run([str(executable), *map(str, args)], cwd=cwd,
                            capture_output=True, text=True)
    if result.returncode:
        raise RuntimeError(result.stdout + '\n' + result.stderr)
    return result.stdout


def prepare(rom, output):
    if output.exists():
        raise ValueError('Marker resource destination already exists')
    chunk = Chunk.compressed(rom/'CHK/PS2/GAME.DTZ')
    # Original Init caller10483C reads ResourceImage+0xA0, after chunk header32.
    table = chunk.u32(32+0xA0)
    resident = {i: chunk.u32(table+4*i) for i in range(32)
                if chunk.u32(table+4*i)}
    if resident != {1: 0x470364, 2: 0x419924, 5: 0x3F5880}:
        raise ValueError('Unreviewed original resident marker table')
    for pointer in resident.values():
        chunk.span(pointer, 32)
    output.mkdir(parents=True)
    intermediate = output/'intermediate'
    intermediate.mkdir()
    assets = output/'rw-assets'
    assets.mkdir()
    game = intermediate/'game.dat'
    game.write_bytes(chunk.data)
    rsl = ROOT/'build/Release/lcs-ps2-rsl.exe'
    rw = ROOT/'build/Release/lcs-convert-rw.exe'
    log = run(rsl, '-k', game, cwd=intermediate)
    rows = []
    for index, pointer in resident.items():
        original = intermediate/f'marker{index}.dff'
        converted = assets/f'lcs_marker{index}.dff'
        before = geometries(original)
        log += run(rw, original, converted)
        after = geometries(converted)
        if len(before) != 1 or len(after) != 1:
            raise ValueError('Retail marker must have one geometry')
        for field in ('vertices', 'triangles', 'points', 'colors'):
            if before[0][field] != after[0][field]:
                raise ValueError(f'Marker{index} changed original {field}')
        if not after[0]['binmesh']:
            raise ValueError('Converted marker has no validated native triangle mesh')
        info = run(rw, original, '--info')
        rows.append(dict(type=index, originalPointer=pointer,
                         vertices=after[0]['vertices'], triangles=after[0]['triangles'],
                         file=converted.name, sha256=hashlib.sha256(converted.read_bytes()).hexdigest(),
                         sourceInfo=info.splitlines()))
    # Only type5 has a texture. Keep original zonewhite64 pixels; never replace
    # it with a guessed native texture or hydrate an entire resource archive.
    game_image = Game(rom, ROOT/'vendor/librwgta/librwgta-master/tools/storiesconv/hashes.inc')
    selected = next((t for t in game_image.named_textures() if t['name']=='zonewhite64'), None)
    if selected is None:
        raise ValueError('Original marker zonewhite64 texture missing')
    width, height, pixels = selected['image']
    name = b'zonewhite64'.ljust(32, b'\0')
    (intermediate/'lcs_markers.pst').write_bytes(
        b'PST1'+struct.pack('<I', 1)+name+struct.pack('<II', width, height)+pixels)
    # The writer sees the PST and ignores original DFF/game.dat files.
    textures = output/'texture-assets'
    log += run(ROOT/'build/Release/lcs-ps2-write.exe', intermediate, textures)
    texture = textures/'lcs_markers.txd'
    log += run(rw, textures, '--validate')
    pixel_hash = hashlib.sha256(struct.pack('<II', width, height)+pixels).hexdigest()
    fingerprints = run(ROOT/'build/Release/lcs-ps2-write.exe', '--texture-fingerprints', textures)
    if fingerprints.strip().split('\t') != ['lcs_markers', 'zonewhite64', str(width), str(height), pixel_hash]:
        raise ValueError('Converted marker texture pixels differ from original')
    log += fingerprints
    manifest = dict(valid=True, source='CHK/PS2/GAME.DTZ',
                    sourceSha256=hashlib.sha256((rom/'CHK/PS2/GAME.DTZ').read_bytes()).hexdigest(),
                    tableOffset=table, meshes=rows,
                    texture=dict(file=str(texture.relative_to(output)),
                                 width=width, height=height, dictionary=selected['dictionary'],
                                 originalPixelSha256=pixel_hash, originalPixelsPreserved=True,
                                 sha256=hashlib.sha256(texture.read_bytes()).hexdigest()),
                    gameExecuted=False, renderingExecuted=False,
                    commandsCompleted=0, installed=False)
    (output/'conversion.log').write_text(log, encoding='utf-8')
    (output/'manifest.json').write_text(json.dumps(manifest, indent=2)+'\n', encoding='utf-8')
    return manifest


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--rom', type=Path, default=ROOT / 'local/originals/lcs-ps2')
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    result = prepare(args.rom.resolve(), args.output.resolve())
    print(json.dumps(dict(meshes=len(result['meshes']), texture=result['texture'],
                          gameExecuted=False, renderingExecuted=False, installed=False)))
