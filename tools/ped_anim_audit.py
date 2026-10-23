"""Offline proof that a converted ped block poses the converted rig like the
original animation poses the original rig (tools/lcs_ifp.py).

Both rigs are read from converted skins (an original ped DFF and the same ped
after convert_ped), each animation is sampled over its length and the joints
of the keyed bones are compared. Bones an animation does not key follow the
animation underneath it in the game, so they are not compared. No GTA.
"""
import argparse
import json
import math
import struct
from pathlib import Path

import lcs_ifp
import weapon_animations as wa

COMPARED = (2, 5, 24, 34, 43, 53)  # spine, head, hands, feet
ORIGINAL_TOES = {44: 54, 54: 55}   # the original skin numbers its toes 54/55


def _chunks(raw, begin, end):
    while begin + 12 <= end:
        kind, size, _ = struct.unpack_from('<III', raw, begin)
        yield kind, begin + 12, begin + 12 + size
        begin += 12 + size


def rig(path):
    """{bone id: (parent bone id | None, rest rotation rows, rest position)} of a
    clump's HAnim frames."""
    raw = Path(path).read_bytes()
    root = next(_chunks(raw, 0, len(raw)))
    frames = next(c for c in _chunks(raw, root[1], root[2]) if c[0] == 14)
    data = next(c for c in _chunks(raw, frames[1], frames[2]) if c[0] == 1)
    count = struct.unpack_from('<I', raw, data[1])[0]
    extensions = [c for c in _chunks(raw, frames[1], frames[2]) if c[0] == 3]
    rows = []
    for i in range(count):
        f = struct.unpack_from('<12f2i', raw, data[1] + 4 + 56 * i)
        bone = None
        for c in _chunks(raw, extensions[i][1], extensions[i][2]):
            if c[0] == 286:
                bone = struct.unpack_from('<i', raw, c[1] + 4)[0]
        rows.append((bone if bone is not None and bone >= 0 else None, f[12], f[:9], f[9:12]))
    bones = {}
    for bone, parent, rot, pos in rows:
        if bone is None:
            continue
        parent_bone = rows[parent][0] if parent >= 0 else None
        bones[bone] = (parent_bone, [list(rot[0:3]), list(rot[3:6]), list(rot[6:9])], list(pos))
    return bones


def _mul(a, b):
    return [[sum(a[i][k] * b[k][j] for k in range(3)) for j in range(3)] for i in range(3)]


def joints(bones, rotations, root_translation):
    """World joint positions: rest offsets, local rotations replaced by the animation."""
    world, position = {}, {}

    def build(bone):
        if bone in world:
            return
        parent, rest, offset = bones[bone]
        local = qmat(rotations[bone]) if bone in rotations else rest
        if parent is None or parent not in bones:
            world[bone], position[bone] = local, list(root_translation if bone == 0 else offset)
            return
        build(parent)
        pw = world[parent]
        world[bone] = _mul(local, pw)
        position[bone] = [sum(offset[k] * pw[k][j] for k in range(3)) + position[parent][j] for j in range(3)]

    for bone in bones:
        build(bone)
    return position


def qmat(q):
    return lcs_ifp.qmat(q)


def audit(original_rig, final_rig, original_ifp, converted_ifp, threshold=0.06):
    original_bones, final_bones = rig(original_rig), rig(final_rig)
    original_tracks = lcs_ifp.read_original_block(Path(original_ifp).read_bytes())
    block, animations = wa.read_anp3(Path(converted_ifp).read_bytes())
    results = {}
    for name, raw in animations:
        converted, _ = lcs_ifp.read_sa_animation(raw)
        source = original_tracks.get(name)
        if not source or not converted:
            continue
        def chain(bone):
            while bone is not None:
                yield bone
                bone = lcs_ifp.LCS_PARENT[bone]
        # A bone only follows this animation when its whole chain is keyed.
        keyed = [b for b in COMPARED if all(a in converted for a in chain(b))]
        if not keyed:
            continue
        worst = 0.0
        for fraction in (0, 0.2, 0.4, 0.6, 0.8, 1.0):
            # An animation may be stretched to the SA length: compare equal progress.
            def pose(tracks):
                t = lcs_ifp.duration(tracks) * fraction
                return ({b: lcs_ifp.sample(tr, t) for b, tr in tracks.items()},
                        lcs_ifp.sample_translation(tracks[0], t) if 0 in tracks else None)
            original_rotation, original_root = pose(source)
            final_rotation, final_root = pose(converted)
            original_rotation = {ORIGINAL_TOES.get(b, b): q for b, q in original_rotation.items()}
            a = joints(original_bones, original_rotation, original_root or original_bones[0][2])
            b = joints(final_bones, final_rotation, final_root or final_bones[0][2])
            for bone in keyed:
                worst = max(worst, math.dist(a[bone], b[bone]))
        results[name] = round(worst, 4)
    flagged = {n: e for n, e in results.items() if e > threshold}
    return dict(compared=len(results), maxJointErrorMeters=max(results.values()),
                medianJointErrorMeters=sorted(results.values())[len(results) // 2],
                aboveThreshold=flagged, thresholdMeters=threshold)


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    for key in ('original-rig', 'final-rig', 'original-ifp', 'converted-ifp'):
        parser.add_argument('--' + key, type=Path, required=True)
    parser.add_argument('--threshold', type=float, default=0.06)
    arguments = parser.parse_args()
    print(json.dumps(audit(arguments.original_rig, arguments.final_rig, arguments.original_ifp,
                           arguments.converted_ifp, arguments.threshold), indent=2))
