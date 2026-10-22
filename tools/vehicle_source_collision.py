"""Extract an original vehicle collision record from the user's PS2 GAME.DTZ.

The returned bytes are an in-memory legacy COLL record for the existing SA
vehicle converter. No source collision data is written by this helper.
"""
from __future__ import annotations

import math
import struct

from ps2_export import collision as export_collision
from ps2_world import Game


def derive_body_collision(path, name: str) -> bytes:
    """Explicit SA adaptation for a source vehicle with no physical COL.

    Use the original rigid body triangles in root space, excluding rotating
    rotors and duplicate LODs. This is derived collision, not recovered retail
    physics. LCS SURFACE_CAR (6) is mapped once by the existing COL converter.
    """
    import ferrylib
    from rwgeom import decode_geometry

    clump = ferrylib.load(path)
    roots = [i for i, frame in enumerate(clump.frames) if frame.parent == -1]
    if len(roots) != 1 or clump.frames[roots[0]].name.casefold() != name.casefold():
        raise ValueError('Derived vehicle collision requires its original named root')
    vertices, faces = [], []
    for frame_index, geometry_index, _flags, _unused in clump.atomics:
        frame = clump.frames[frame_index]
        if frame.name not in {'chassis_hi', 'tail', 'skid_left', 'skid_right', 'topknot'}:
            continue
        geometry = decode_geometry(clump.raw, clump.geom_nodes[geometry_index])
        base = len(vertices)
        points = [ferrylib.to_ancestor(clump, frame_index, roots[0], point) for point in geometry.pos]
        if not all(math.isfinite(value) for point in points for value in point):
            raise ValueError('Nonfinite derived vehicle collision vertex')
        vertices.extend(points)
        for a, b, c, _material in geometry.tris:
            if max(a, b, c) >= len(points):
                raise ValueError('Derived vehicle collision triangle index out of range')
            if len({a, b, c}) == 3:
                faces.append(struct.pack('<3I4B', base+a, base+b, base+c, 6, 0, 0, 255))
    if not vertices or not faces or len(vertices) > 32767 or len(faces) > 32767:
        raise ValueError('Derived vehicle body has no usable bounded collision mesh')
    minimum = [min(point[i] for point in vertices) for i in range(3)]
    maximum = [max(point[i] for point in vertices) for i in range(3)]
    center = [(a+b)*0.5 for a, b in zip(minimum, maximum)]
    body = struct.pack('<10f', math.dist(center, maximum), *center, *minimum, *maximum)
    for array in ([], [], [], [struct.pack('<3f', *point) for point in vertices], faces):
        body += struct.pack('<I', len(array)) + b''.join(array)
    raw_name = name.encode('ascii')
    if len(raw_name) > 23:
        raise ValueError('Derived vehicle name is too long')
    body = raw_name.ljust(24, b'\0') + body
    record = struct.pack('<4sI', b'COLL', len(body)) + body
    _validate_legacy_record(record, name)
    return record


def _validate_legacy_record(record: bytes, expected_name: str) -> None:
    if len(record) < 32 + 40 + 20 or record[:4] != b'COLL':
        raise ValueError('Original vehicle collision did not serialize as a legacy COLL record')
    declared_size, = struct.unpack_from('<I', record, 4)
    if declared_size != len(record) - 8:
        raise ValueError('Original vehicle COLL record has an invalid size')

    raw_name = record[8:32].split(b'\0', 1)[0]
    try:
        record_name = raw_name.decode('ascii')
    except UnicodeDecodeError as exc:
        raise ValueError('Original vehicle COLL model name is not ASCII') from exc
    if record_name.casefold() != expected_name.casefold():
        raise ValueError('Original vehicle COLL model name does not match the requested source model')

    # The existing C++ COLL reader consumes these five legacy arrays. Check
    # their bounds here and refuse a pointer that resolves to a shape-less
    # temporary model instead of turning it into a fake empty vehicle COL3.
    cursor = 32 + 40
    counts = []
    for stride in (20, 24, 28, 12, 16):
        if cursor + 4 > len(record):
            raise ValueError('Truncated original vehicle COLL array count')
        count, = struct.unpack_from('<I', record, cursor)
        cursor += 4
        if count > 32767 or count > (len(record) - cursor) // stride:
            raise ValueError('Invalid original vehicle COLL array bounds')
        counts.append(count)
        cursor += count * stride
    if cursor != len(record):
        raise ValueError('Original vehicle COLL record has trailing or unparsed data')
    spheres, _lines, boxes, _vertices, triangles = counts
    if not (spheres or boxes or triangles):
        raise ValueError('Original vehicle collision model has no collision volumes')


def _source_bounds(chunk, offset: int) -> bytes:
    # PS2 CColModel stores center and radius first, followed by padded min/max
    # vectors. Legacy COLL expects radius, center, min and max.
    center_radius = chunk.read('<4f', offset)
    minimum = chunk.read('<3f', offset + 16)
    maximum = chunk.read('<3f', offset + 32)
    values = (center_radius[3], *center_radius[:3], *minimum, *maximum)
    if not all(math.isfinite(value) for value in values):
        raise ValueError('Original vehicle collision bounds contain a nonfinite value')
    radius, _cx, _cy, _cz, min_x, min_y, min_z, max_x, max_y, max_z = values
    if radius < 0 or min_x > max_x or min_y > max_y or min_z > max_z:
        raise ValueError('Original vehicle collision bounds are invalid')
    return struct.pack('<10f', *values)


def read_vehicle_collision(game: Game, model_id: int, name: str) -> bytes | None:
    """Return the source model's legacy COLL bytes, or ``None`` if no COL exists.

    ``game`` is a ``ps2_world.Game`` loaded from the user's original LCS source.
    ``name`` must match that GAME.DTZ model's name and prevents accidentally
    binding another model's collision by ID. A zero ``m_colModel`` pointer is
    reported as ``None``; an invalid nonzero pointer or a shape-less model is
    an error. The existing ``ps2_export.collision(..., version=1)`` serializer
    carries the original bounds, spheres, boxes, lines, surfaces, and pieces
    into a legacy COLL record that ``convert_vehicle.cpp`` can convert to SA
    COL3.
    """
    if type(model_id) is not int or model_id < 0:
        raise ValueError('Vehicle model ID must be a nonnegative integer')
    if not isinstance(name, str) or not name or not name.isascii():
        raise ValueError('Expected source vehicle model name must be nonempty ASCII')

    row = getattr(game, 'models', {}).get(model_id)
    if row is None:
        raise ValueError(f'Original GAME.DTZ has no model ID {model_id}')
    source_name = row.get('name')
    if not isinstance(source_name, str) or source_name.casefold() != name.casefold():
        raise ValueError('Original GAME.DTZ model ID and expected vehicle name do not match')

    col_pointer = row.get('colPointer')
    if not isinstance(col_pointer, int) or col_pointer < 0:
        raise ValueError('Original GAME.DTZ vehicle has an invalid collision pointer')
    if col_pointer == 0:
        return None

    collisions = getattr(game, 'collisions', None)
    if not isinstance(collisions, dict) or model_id not in collisions:
        raise ValueError('Original GAME.DTZ collision pointer was not registered by its reader')
    source_chunk, source_offset = collisions[model_id]
    if source_chunk is not getattr(game, 'chunk', None) or source_offset != col_pointer:
        raise ValueError('Original GAME.DTZ collision pointer is inconsistent with the parsed model')

    original_bounds = _source_bounds(source_chunk, source_offset)
    record = bytearray(export_collision(game, model_id, version=1, map_surfaces=False))
    # COLL: 8-byte record header, 24-byte model name, then the 40-byte bounds.
    record[32:72] = original_bounds
    _validate_legacy_record(record, source_name)
    return bytes(record)
