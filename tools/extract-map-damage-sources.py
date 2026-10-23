"""Add source DFFs of proven damaged-atomic models to a map source directory.

The current PS2 map conversion only wrote streamed MDL/DFF sources for the
models that existed when it ran. This reads the user's own PS2 streaming image
and converts the missing models whose source row proves atomic 1 is damaged
(map_damage._source_damage). Existing files are shared by hardlink and are
never rewritten. Output stays in a private directory.
"""
import argparse
import json
import os
from pathlib import Path
import shutil
import struct
import subprocess

import map_damage
from map_effects import Game
from ps2_world import Chunk


def extract(rom, names, base, output, build):
    if output.exists():
        raise ValueError('Choose a new separate private map-source directory')
    rsl = build/'Release/lcs-ps2-rsl.exe'
    game = Game(rom, names)
    streamed = output/'streamed-source'
    streamed.mkdir(parents=True)
    for source in (base/'streamed-source').iterdir():
        os.link(source, streamed/source.name)
    (output/'intermediate').mkdir()
    shutil.copy2(base/'intermediate/map-lighting.psl', output/'intermediate/map-lighting.psl')
    added = []
    with (rom/'MODELS/GTA3PS2.IMG').open('rb') as stream:
        for model, row in game.models.items():
            name = row.get('name')
            if row.get('type') not in (1, 3) or not name or not row.get('streamSize'):
                continue
            if map_damage._source_damage(row) is not None or (streamed/(name+'.dff')).exists():
                continue
            position, size = game.stream(model)
            stream.seek(position)
            raw = stream.read(size)
            length = struct.unpack_from('<I', raw, 8)[0]
            if len(raw) != size or len(raw) < 32 or length > size:
                raise ValueError('Truncated original streamed asset: '+name)
            mdl = streamed/(name+'.mdl')
            mdl.write_bytes(Chunk(raw[:length], f'original stream {model}').data)
            completed = subprocess.run([str(rsl), str(mdl), str(streamed/(name+'.dff'))],
                                       capture_output=True, text=True)
            if completed.returncode:
                raise RuntimeError('Source model conversion failed: '+name)
            added.append(name)
    return dict(output=str(output), added=sorted(added), gameExecuted=False)


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    for key in ('rom', 'names', 'base', 'output', 'build'):
        parser.add_argument('--'+key, type=Path, required=True)
    print(json.dumps(extract(**vars(parser.parse_args())), indent=2))
