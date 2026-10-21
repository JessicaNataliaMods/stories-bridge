"""Independent bounds/graph/PS2 preservation audit of native SA path archives."""
import argparse
import hashlib
import json
import pathlib
import re
import struct


def parse_archive(data):
    if len(data) < 8 or data[:4] != b'VER2':
        raise ValueError('Navigation archive must be native IMG v2')
    if struct.unpack_from('<I', data, 4)[0] != 64:
        raise ValueError('Native navigation archive must cover all 64 map areas')
    if len(data) < 4096:
        raise ValueError('Truncated native navigation directory')
    entries, ranges = {}, []
    for i in range(64):
        sector, size, compressed, rawname = struct.unpack_from('<IHH24s', data, 8 + i * 32)
        name = rawname.split(b'\0')[0].decode('ascii')
        match = re.fullmatch(r'nodes([0-9]+)\.dat', name)
        if not match:
            raise ValueError('Unexpected native path entry name')
        area = int(match[1])
        if area >= 64 or area in entries:
            raise ValueError('Duplicate or invalid native path area')
        start, length = sector * 2048, size * 2048
        if not size or compressed or start < 4096 or start + length > len(data):
            raise ValueError('Native path entry has invalid archive bounds')
        entries[area] = data[start:start + length]
        ranges.append((start, start + length))
    ranges.sort()
    if any(a[1] > b[0] for a, b in zip(ranges, ranges[1:])):
        raise ValueError('Overlapping navigation archive entries')
    regions = {}
    for area, raw in entries.items():
        total, cars, peds, navs, links = struct.unpack_from('<5I', raw)
        if total != cars + peds or total > 65535 or navs > 1024 or links > 32767:
            raise ValueError('Inconsistent native node counts/capacity')
        padded = links + 192 if links else 0
        size = 20 + 28 * total + 14 * navs + 4 * padded + 2 * links + 2 * padded
        if size > len(raw) or len(raw) != ((size + 2047) // 2048) * 2048:
            raise ValueError('Native path payload length disagrees with counts/dynamic padding')
        if any(raw[size:]):
            raise ValueError('Nonzero native path sector padding')
        nodes = [struct.unpack_from('<II4hhHHBBI', raw, 20 + 28 * i) for i in range(total)]
        off = 20 + total * 28
        navigation = [struct.unpack_from('<2h2H2bbBH', raw, off + 14 * i) for i in range(navs)]
        off += navs * 14
        targets = [struct.unpack_from('<2H', raw, off + 4 * i) for i in range(links)]
        if links and raw[off + 4 * links:off + 4 * padded] != struct.pack('<2H', 0xffff, 0) * 192:
            raise ValueError('Invalid reserved dynamic path addresses')
        off += 4 * padded
        nav_links = [struct.unpack_from('<H', raw, off + 2 * i)[0] for i in range(links)]
        off += 2 * links
        lengths = raw[off:off + links]
        if any(raw[off + links:off + padded]):
            raise ValueError('Invalid reserved dynamic link lengths')
        off += padded
        intersections = raw[off:off + links]
        if any(raw[off + links:off + padded]):
            raise ValueError('Invalid reserved dynamic intersections')
        regions[area] = dict(cars=cars, peds=peds, nodes=nodes, navs=navigation, targets=targets,
                             navLinks=nav_links, lengths=lengths, intersections=intersections, logicalBytes=size)
    return regions


def check_graph(regions):
    edges, cross_area = {}, 0
    for area, r in regions.items():
        used = []
        for i, n in enumerate(r['nodes']):
            next_ptr, prev_ptr, x, y, z, dist, base, stored_area, stored_id, width, group, flags = n
            if next_ptr or prev_ptr or dist != 32766 or (stored_area, stored_id) != (area, i):
                raise ValueError('Invalid native node runtime pointers/identity')
            if not -24000 <= x < 24000 or not -24000 <= y < 24000 or (x + 24000) // 6000 + 8 * ((y + 24000) // 6000) != area:
                raise ValueError('Native node is assigned to wrong map region')
            degree = flags & 15
            if base < 0 or base + degree > len(r['targets']):
                raise ValueError('Native node base/degree outside link array')
            used.extend(range(base, base + degree))
            for j in range(base, base + degree):
                a, b = r['targets'][j]
                if a not in regions or b >= len(regions[a]['nodes']):
                    raise ValueError('Native connection points to nonexistent node')
                if (i < r['cars']) != (b < regions[a]['cars']):
                    raise ValueError('Native connection crosses vehicle/ped partitions')
                if regions[a]['nodes'][b][10] != group:
                    raise ValueError('Native connection crosses flood-fill groups')
                if r['intersections'][j] & ~3:
                    raise ValueError('Invalid native intersection flags')
                packed = r['navLinks'][j]
                if i < r['cars']:
                    na, ni = packed >> 10, packed & 1023
                    if packed == 65535 or na not in regions or ni >= len(regions[na]['navs']):
                        raise ValueError('Native vehicle connection has invalid navigation address')
                    nav = regions[na]['navs'][ni]
                    if tuple(nav[2:4]) not in ((area, i), (a, b)):
                        raise ValueError('Native navigation is not attached to either link endpoint')
                elif packed != 65535:
                    raise ValueError('Pedestrian connection must have invalid navigation sentinel')
                edges[(area, i, a, b)] = j
                cross_area += area != a
        if sorted(used) != list(range(len(r['targets']))):
            raise ValueError('Native graph has missing/overlapping link ranges')
        for nav in r['navs']:
            if nav[2] not in regions or nav[3] >= regions[nav[2]]['cars']:
                raise ValueError('Native navigation points to nonexistent vehicle node')
            # Native SA files store each link in the area of its own position.
            stored = (nav[0] + 24000) // 6000 + 8 * ((nav[1] + 24000) // 6000)
            if stored != area or not -100 <= nav[4] <= 100 or not -100 <= nav[5] <= 100 or nav[6] < 0 or nav[7] & 128 or nav[8] & ~7:
                raise ValueError('Invalid native navigation direction/width/flags/area')
    if any((b_area, b, a_area, a) not in edges for a_area, a, b_area, b in edges):
        raise ValueError('Native graph has an asymmetric connection')
    return dict(NativeNavigationValidated=True, Areas=64,
                VehicleNodes=sum(r['cars'] for r in regions.values()), PedNodes=sum(r['peds'] for r in regions.values()),
                NavigationNodes=sum(len(r['navs']) for r in regions.values()), DirectedConnections=len(edges),
                CrossAreaConnections=cross_area, PopulatedAreas=sum(bool(r['nodes']) for r in regions.values()),
                Mode='Original PS2 graph' if edges else 'Empty preview graph', GameExecuted=False)


def bindings(path, columns):
    rows = path.read_text(encoding='ascii').splitlines()
    if rows[0].split('\t') != columns:
        raise ValueError('Invalid original path binding header')
    result = {}
    for row in rows[1:]:
        source, a, b = map(int, row.split('\t'))
        if source in result:
            raise ValueError('Duplicate original path binding')
        result[source] = (a, b)
    return result


def compare_ps2(regions, rom, directory):
    # Read source structs independently of the exporter; do not call its flag,
    # address, regionalization or encoding functions here.
    from ps2_world import Chunk
    chunk = Chunk.compressed(rom / 'CHK/PS2/GAME.DTZ')
    root = chunk.u32(32)
    nodes_ptr, nav_ptr, targets_ptr, lengths_ptr, navlinks_ptr = chunk.read('<5I', root)
    total, vehicles, peds = chunk.read('<3i', root + 20)
    _, link_count, carlink_count, _ = chunk.read('<4h', root + 32)
    nav_count = chunk.u32(root + 40)
    node_map = bindings(directory / 'source-bindings.tsv', ['source', 'area', 'node'])
    nav_map = bindings(directory / 'nav-bindings.tsv', ['source', 'area', 'navi'])
    if set(node_map) != set(range(total)) or set(nav_map) != set(range(nav_count)) or len(set(node_map.values())) != total or len(set(nav_map.values())) != nav_count:
        raise ValueError('Incomplete/duplicate original node/navigation bindings')
    if sum(len(r['nodes']) for r in regions.values()) != total or sum(len(r['navs']) for r in regions.values()) != nav_count:
        raise ValueError('Original graph coverage changed')
    checked_links = 0
    for ident, (area, local) in node_map.items():
        if area not in regions or local >= len(regions[area]['nodes']):
            raise ValueError('Binding points outside native graph')
        source = chunk.span(nodes_ptr + ident * 20, 20)
        xyz = struct.unpack_from('<3h', source, 4)
        start = struct.unpack_from('<h', source, 12)[0]
        width, group, a, b, c = source[14:19]
        r = regions[area]
        n = r['nodes'][local]
        is_car = ident < vehicles
        expected_flags = (a & 63) | ((a & 128) >> 1) | ((b & 1) << 7)
        expected_flags |= (((a >> 5) & 1) | (32 if is_car and (b & 24) else 16)) << 8
        expected_flags |= (c & 15) << 16
        if tuple(n[2:5]) != xyz or n[9] != (width * 2 if is_car else width) or n[10] != group or n[11] != expected_flags or (local < r['cars']) != is_car:
            raise ValueError('Original position/width/group/node flags/type changed')
        for k in range(a & 15):
            j = start + k
            target = chunk.read('<H', targets_ptr + j * 2)[0]
            slot = n[6] + k
            expected_intersection = ((target >> 15) & 1) | (((target >> 14) & 1) << 1)
            if r['targets'][slot] != node_map[target & 16383] or r['lengths'][slot] != chunk.span(lengths_ptr + j, 1)[0] or r['intersections'][slot] != expected_intersection:
                raise ValueError('Original connection/distance/crossing/traffic light changed')
            if is_car:
                if j >= carlink_count:
                    raise ValueError('Original vehicle link outside navigation array')
                nav = chunk.read('<H', navlinks_ptr + j * 2)[0]
                na, ni = nav_map[nav]
                if r['navLinks'][slot] != (na << 10) | ni:
                    raise ValueError('Original vehicle/navigation binding changed')
            checked_links += 1
    for ident, (area, local) in nav_map.items():
        source = chunk.span(nav_ptr + ident * 12, 12)
        x, y, owner, dx, dy = struct.unpack_from('<3h2b', source)
        expected = (x, y, *node_map[owner], dx, dy, source[10] * 2, source[8] & 127, source[9] & 7)
        if regions[area]['navs'][local] != expected:
            raise ValueError('Original navigation position/owner/direction/lanes/lights changed')
    if checked_links != link_count:
        raise ValueError('Original graph connection coverage changed')
    return dict(OriginalPs2NodesCompared=total, OriginalPs2NavigationCompared=nav_count,
                OriginalPs2ConnectionsCompared=checked_links, OriginalCoordinatesAndLanesPreserved=True)


def validate(path, rom=None, bindings_directory=None):
    raw = path.read_bytes()
    regions = parse_archive(raw)
    report = check_graph(regions)
    if rom is not None:
        report.update(compare_ps2(regions, rom, bindings_directory))
    report.update(ArchiveBytes=len(raw), PathsImgSha256=hashlib.sha256(raw).hexdigest())
    return report


if __name__ == '__main__':
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('archive', type=pathlib.Path)
    p.add_argument('--rom', type=pathlib.Path)
    p.add_argument('--bindings', type=pathlib.Path)
    p.add_argument('--report', type=pathlib.Path)
    args = p.parse_args()
    report = validate(args.archive, args.rom, args.bindings)
    if args.report:
        args.report.write_text(json.dumps(report, indent=2), encoding='utf-8')
    print(json.dumps(report, indent=2))
