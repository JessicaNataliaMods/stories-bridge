"""Development-only triangle/winding comparison against a named reference DFF.

The reference supplies validation evidence only. No geometry is exported or
copied into a generated model or the public converter distribution.
"""
import argparse, collections, json, pathlib, struct
import numpy as np


def children(raw, start, end):
    while start < end:
        if start + 12 > end:
            raise ValueError('Truncated RenderWare chunk header')
        ident, size, version = struct.unpack_from('<3I', raw, start)
        stop = start + 12 + size
        if stop > end:
            raise ValueError('RenderWare chunk exceeds its parent')
        yield ident, start + 12, stop, version
        start = stop


def geometries(path):
    raw = path.read_bytes()
    ident, size, _version = struct.unpack_from('<3I', raw)
    if ident != 0x10 or size + 12 > len(raw):
        raise ValueError('Expected a bounded DFF clump')
    result = []
    for ident, start, end, _ in children(raw, 12, size + 12):
        if ident != 0x1a:
            continue
        for ident, start, end, _ in children(raw, start, end):
            if ident != 0xf:
                continue
            fields = next(children(raw, start, end))
            if fields[0] != 1:
                raise ValueError('Geometry lacks its structure')
            _, start, end, version = fields
            flags, nt, nv, nm = struct.unpack_from('<4I', raw, start)
            if flags & 0x1000000 or not 0 < nv <= 65535 or nm != 1:
                raise ValueError('Expected a single decoded morph target')
            position = start + 16
            # Pre-3.4 structures carry the legacy surface properties.
            library = ((version >> 14) & 0x3ff00) + 0x30000 | ((version >> 16) & 0x3f)
            if library < 0x34000:
                position += 12
            if flags & 8:
                position += 4 * nv
            texsets = (flags >> 16) & 0xff
            if not texsets:
                texsets = 2 if flags & 0x80 else int(bool(flags & 4))
            position += 8 * nv * texsets
            faces = []
            if position + 8 * nt + 24 > end:
                raise ValueError('Truncated geometry triangles/morph target')
            for b, a, _material, c in struct.iter_unpack('<4H', raw[position:position + 8 * nt]):
                if max(a, b, c) >= nv:
                    raise ValueError('Triangle references an invalid vertex')
                faces.append((a, b, c))
            position += 8 * nt
            has_positions, has_normals = struct.unpack_from('<2I', raw, position + 16)
            position += 24
            required = 12 * nv * (has_positions + has_normals)
            if has_positions != 1 or has_normals not in (0, 1) or position + required != end:
                raise ValueError('Unexpected morph-target data extent')
            points = np.frombuffer(raw, dtype='<f4', count=nv * 3, offset=position).reshape(nv, 3).astype(np.float64)
            if not np.isfinite(points).all():
                raise ValueError('Nonfinite geometry')
            result.append((points, faces))
    if not result:
        raise ValueError('No decoded geometry')
    return result


def oriented(face):
    return min(face, (face[1], face[2], face[0]), (face[2], face[0], face[1]))


def audit(converted, reference, intermediate=None):
    current = geometries(converted)
    original = geometries(reference)
    # A named reference may contain artist LOD atomics. Its first atomic is
    # the near model used by the existing bounds/vertex validation metadata.
    points, faces = current[0]
    refpoints, reffaces = original[0]
    canonical, inverse = np.unique(refpoints, axis=0, return_inverse=True)
    remap = []
    distances = []
    for start in range(0, len(points), 128):
        delta = points[start:start + 128, None, :] - canonical[None, :, :]
        squared = np.sum(delta * delta, axis=2)
        nearest = np.argmin(squared, axis=1)
        remap.extend(nearest.tolist())
        distances.extend(np.sqrt(squared[np.arange(len(nearest)), nearest]).tolist())
    index = collections.defaultdict(set)
    reference_degenerate = 0
    for face in reffaces:
        values = tuple(int(inverse[i]) for i in face)
        if len(set(values)) == 3:
            index[tuple(sorted(values))].add(oriented(values))
        else:
            reference_degenerate += 1
    counts = collections.Counter()
    matched = set()
    for face in faces:
        values = tuple(remap[i] for i in face)
        key = tuple(sorted(values))
        if max(distances[i] for i in face) > .03:
            counts['unmatchedVertices'] += 1
        elif len(set(values)) < 3:
            counts['collapsedAtReferencePrecision'] += 1
        elif key not in index:
            counts['notInReferenceTopology'] += 1
        else:
            counts['sameWinding' if oriented(values) in index[key] else 'oppositeWinding'] += 1
            matched.add(key)
    result = dict(converted=str(converted.resolve()), reference=str(reference.resolve()),
                  convertedAtomics=len(current), referenceAtomics=len(original),
                  convertedVertices=len(points), convertedTriangles=len(faces),
                  referenceVertices=len(refpoints), referenceTriangles=len(reffaces),
                  referenceDistinctTrianglePositions=len(index),
                  referenceDegenerateTrianglePositions=reference_degenerate,
                  maximumVertexDistance=max(distances), triangleComparison=dict(counts),
                  referenceTrianglePositionsNotMatched=len(set(index) - matched),
                  tolerance=.03, referenceGeometryCopied=False, gameExecuted=False)
    if intermediate is not None:
        raw = intermediate.read_bytes()
        magic, nv, nt, nm = struct.unpack_from('<4s3I', raw)
        start = 16 + 32 * nm
        if magic != b'PSM1' or len(raw) != start + 24 * nv + 8 * nt:
            raise ValueError('Invalid PSM extent')
        source_points = np.asarray([struct.unpack_from('<3f', raw, start + 24 * i) for i in range(nv)])
        source_faces = [tuple(r[:3]) for r in struct.iter_unpack('<4H', raw[start + 24 * nv:])]
        if not np.array_equal(source_points, points) or source_faces != faces:
            raise ValueError('Serialized DFF positions/winding differ from decoded PSM')
        result['serializedPositionsAndTriangleWindingExact'] = True
    return result


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('converted', type=pathlib.Path)
    parser.add_argument('reference', type=pathlib.Path)
    parser.add_argument('report', type=pathlib.Path)
    parser.add_argument('--intermediate', type=pathlib.Path)
    args = parser.parse_args()
    report = audit(args.converted, args.reference, args.intermediate)
    args.report.write_text(json.dumps(report, indent=2), encoding='utf-8')
    print(json.dumps(report, indent=2))
