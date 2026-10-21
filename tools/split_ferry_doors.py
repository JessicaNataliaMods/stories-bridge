"""Split the ORIGINAL door-leaf / ramp-plate triangles out of ferry chassis_hi into real atomics.

Evidence base (see REPORT.md): the raw PS2 clump has exactly 2 elements (chassis_vlo, chassis_hi); the door and ramp
dummy frames carry no element.  The door leaves and ramp plates exist only as triangles inside chassis_hi, positioned
on the dummy hinge points to <0.01 m.  This tool selects exactly those triangles (rules below), moves them into 4 new
geometries expressed in the local space of their dummy frame, attaches 4 new atomics DIRECTLY to the dummy frames
(the retail COLLAPSE design: atomics of a component live on the component frame), and removes them from chassis_hi.

Selection (hull = chassis_hi local space, H = dummy origin in hull space):
  door_X : all 3 vertices on the plane |y-Hy|<=0.02, ramp_hinge_z-0.005 <= z <= Hz+0.005 and |x| <= door-leaf half width
           (half width = max |x| of the Ferry_Door_SJL triangles on that plane, +0.01).
  ramp_X : all 3 vertices beyond the door plane (front: y>=door Hy-0.02, back: y<=door Hy+0.02) and inside the
           deck plate slab ramp_Hz-0.31 <= z <= ramp_Hz+0.005 (plate is 0.30 thick: top Floor, bottom Hull, rim Side2).
Every other triangle stays in chassis_hi.  The split is audited (multiset of world-space triangles identical).

Usage: split_ferry_doors.py SRC.dff OUT.dff [--back-door-orientation identity|stored] [--audit audit.json]
"""
import argparse, json, struct, sys, collections
import ferrylib
from rwgeom import decode_geometry, encode_geometry, material_info, Geometry

NAMES = dict(door_front='door_front_dummy', door_back='door_back_dummy',
             ramp_front='ramp_front_dummy', ramp_back='ramp_back_dummy')


def fidx(c, name):
    r = [i for i, f in enumerate(c.frames) if f.name == name]
    assert len(r) == 1, (name, r)
    return r[0]


def select(c, hi_frame, g, tex):
    hull = lambda i: ferrylib.to_ancestor(c, fidx(c, i), hi_frame, [0.0, 0.0, 0.0])
    H = {k: hull(v) for k, v in NAMES.items()}
    sel = {k: [] for k in NAMES}
    pos = g.pos

    def tri_pts(t):
        return [pos[t[0]], pos[t[1]], pos[t[2]]]

    for side, sgn in (('front', 1), ('back', -1)):
        d, r = H['door_' + side], H['ramp_' + side]
        # half width of the Door leaf on this plane (derived from the Ferry_Door_SJL triangles)
        xs = [abs(p[0]) for ti, t in enumerate(g.tris) if tex[t[3]] == 'Ferry_Door_SJL'
              for p in tri_pts(t) if abs(p[1] - d[1]) <= 0.02]
        assert xs, 'no door-textured triangles on the ' + side + ' plane'
        half = max(xs) + 0.01
        for ti, t in enumerate(g.tris):
            P = tri_pts(t)
            if all(abs(p[1] - d[1]) <= 0.02 and r[2] - 0.005 <= p[2] <= d[2] + 0.005 and abs(p[0]) <= half for p in P):
                sel['door_' + side].append(ti)
            elif all((p[1] - (d[1] - sgn * 0.02)) * sgn >= 0 and r[2] - 0.31 <= p[2] <= r[2] + 0.005 for p in P):
                sel['ramp_' + side].append(ti)
    return H, sel


def compact(g, tri_ids, mat_keep=None):
    """Return (positions, normals, uvs, tris, materials-used) for the triangle subset."""
    vmap, pos, nrm, uv = {}, [], [], []
    mats = sorted({g.tris[t][3] for t in tri_ids})
    mmap = {m: i for i, m in enumerate(mats)}
    tris = []
    for t in tri_ids:
        v1, v2, v3, m = g.tris[t]
        out = []
        for v in (v1, v2, v3):
            if v not in vmap:
                vmap[v] = len(pos)
                pos.append(g.pos[v])
                nrm.append(g.nrm[v] if g.nrm else None)
                uv.append([g.uvs[s][v] for s in range(len(g.uvs))])
            out.append(vmap[v])
        tris.append((out[0], out[1], out[2], mmap[m]))
    return pos, nrm, uv, tris, mats


def make_geometry(src, tri_ids, xform_pos, xform_dir):
    pos, nrm, uv, tris, mats = compact(src, tri_ids)
    g = Geometry()
    g.__dict__.update({k: v for k, v in src.__dict__.items() if k in ('ver', 'lib', 'flags', 'ambient', 'ntex', 'mat_ver', 'ext_ver', 'ext_other')})
    g.pos = [xform_pos(p) for p in pos]
    g.nrm = [xform_dir(n) for n in nrm] if src.nrm else None
    g.uvs = [[u[s] for u in uv] for s in range(len(src.uvs))]
    g.colors = None
    g.tris = tris
    g.materials = [src.materials[m] for m in mats]
    g.nv, g.nt = len(g.pos), len(tris)
    return g, mats


def tri_key(pos3, uv3, tex, nrm3=None):
    r = lambda p: tuple(round(x, 4) for x in p)
    return (tuple(sorted((r(p), tuple(round(x, 4) for x in u)) for p, u in zip(pos3, uv3))), tex)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('src'); ap.add_argument('out')
    ap.add_argument('--back-door-orientation', choices=('identity', 'stored'), default='identity')
    ap.add_argument('--audit')
    a = ap.parse_args()
    c = ferrylib.load(a.src)
    raw = c.raw
    hi = fidx(c, 'chassis_hi')
    gi_hi = [x[1] for x in c.atomics if x[0] == hi]
    assert len(gi_hi) == 1
    gi_hi = gi_hi[0]
    src = decode_geometry(raw, c.geom_nodes[gi_hi])
    tex = [material_info(raw, m)['name'] for m in src.mat_nodes]
    H, sel = select(c, hi, src, tex)

    # optionally reset the back door rest orientation (see REPORT: retail SetRotateXOnly overwrites it)
    frames = [ferrylib.Frame(f.rot, f.pos, f.parent, f.flags, f.ext_raw) for f in c.frames]
    for f, o in zip(frames, c.frames):
        f.name = o.name
    notes = []
    if a.back_door_orientation == 'identity':
        f = frames[fidx(c, 'door_back_dummy')]
        notes.append('door_back_dummy stored rotation %s replaced by identity (position unchanged)' % [round(x, 6) for x in f.rot])
        f.rot = [1, 0, 0, 0, 1, 0, 0, 0, 1]

    new_geoms, new_atomics, report = [], [], {}
    selected_all = set()
    for k in ('door_front', 'door_back', 'ramp_front', 'ramp_back'):
        di = fidx(c, NAMES[k])
        ids = sel[k]
        assert ids, 'empty selection for ' + k
        assert not (set(ids) & selected_all)
        selected_all |= set(ids)
        # hull -> dummy local, using the (possibly edited) frame matrices
        cc = ferrylib.Clump(); cc.frames = frames
        xp = lambda p, cc=cc, di=di: ferrylib.from_ancestor(cc, di, hi, list(p))
        xd = lambda n, cc=cc, di=di: ferrylib.rot_inverse_dir(cc, di, hi, list(n))
        g, mats = make_geometry(src, ids, xp, xd)
        new_geoms.append(encode_geometry(g))
        new_atomics.append([di, len(c.geom_nodes) + len(new_geoms) - 1, 5, 0])
        lo = [min(p[i] for p in g.pos) for i in range(3)]
        hiB = [max(p[i] for p in g.pos) for i in range(3)]
        report[k] = dict(frame=NAMES[k], frame_index=di, tris=len(ids), tri_ids=ids, vertices=len(g.pos),
                         materials=[tex[m] for m in mats], hinge_in_hull=[round(x, 4) for x in H[k]],
                         local_bounds_min=[round(x, 4) for x in lo], local_bounds_max=[round(x, 4) for x in hiB])

    keep = [t for t in range(len(src.tris)) if t not in selected_all]
    g_hi, mats_hi = make_geometry(src, keep, lambda p: tuple(p), lambda n: tuple(n))
    new_hi = encode_geometry(g_hi)
    geoms = list(c.geom_raw)
    geoms[gi_hi] = new_hi
    geoms += new_geoms
    atomics = [list(x) for x in c.atomics] + new_atomics
    out = ferrylib.build(c, frames=frames, geoms=geoms, atomics=atomics)
    open(a.out, 'wb').write(out)

    # ---- audit: re-read the WRITTEN file; world-space triangle multiset must equal the original chassis_hi ----
    def world_keys(clump, geom, frame_idx):
        keys = collections.Counter()
        for v1, v2, v3, m in geom.tris:
            P = [ferrylib.to_ancestor(clump, frame_idx, 0, list(geom.pos[v])) for v in (v1, v2, v3)]
            U = [tuple(geom.uvs[0][v]) for v in (v1, v2, v3)]
            keys[tri_key(P, U, geom.texnames[m])] += 1
        return keys

    src.texnames = tex
    # original frames: dummies at their stored rest pose; the new file's geometry was re-expressed in the
    # (possibly orientation-reset) dummy frames, so world positions at rest must be identical.
    before = world_keys(c, src, hi)
    c2 = ferrylib.load(a.out)
    names2 = {i: f.name for i, f in enumerate(c2.frames)}
    after = collections.Counter()
    ok_atomics = []
    for n, at in enumerate(c2.atomics):
        g2 = decode_geometry(c2.raw, c2.geom_nodes[at[1]])
        g2.texnames = [material_info(c2.raw, m)['name'] for m in g2.mat_nodes]
        ok_atomics.append((names2[at[0]], at[1], g2.nv, g2.nt))
        if n:                      # atomic 0 is chassis_vlo, untouched
            after.update(world_keys(c2, g2, at[0]))
    identical = (before == after)
    audit = dict(source=a.src, output=a.out, back_door_orientation=a.back_door_orientation, notes=notes,
                 original_hi_triangles=len(src.tris), remaining_hi_triangles=len(keep),
                 moved={k: report[k]['tris'] for k in report},
                 world_space_triangle_multiset_identical=identical,
                 atomics=ok_atomics, pieces=report)
    print(json.dumps({k: v for k, v in audit.items() if k != 'pieces'}, indent=1))
    if a.audit:
        open(a.audit, 'w').write(json.dumps(audit, indent=1))
    if not identical:
        sys.exit('AUDIT FAILED')


if __name__ == '__main__':
    main()
