"""Convert LCS PS2's resident path graph into native SA NODES*.DAT/IMG.

Source layouts: res-relcs/src/control/PathFind.h and librwgta's leedsgta.h.
Destination layout: SA CPathFind::LoadPathFindData (0x4529F0). All data is
read from the user's GAME.DTZ; no graph, geometry or ROM bytes are bundled.
"""
import argparse
import collections
import hashlib
import json
import pathlib
import struct

from ps2_world import Chunk


def area(x, y):
    # Inputs are original signed fixed-point coordinates (world * 8).
    if not -24000 <= x < 24000 or not -24000 <= y < 24000:
        raise ValueError('Path coordinate outside native SA map bounds')
    return (x + 24000) // 6000 + 8 * ((y + 24000) // 6000)


def read_graph(chunk):
    root = chunk.u32(32)  # ResourceImage::paths, NOT streamed model data
    fields = chunk.read('<5I3i4h2i2B', root)
    np, navp, cp, dp, vp = fields[:5]
    count, vehicles, peds = fields[5:8]
    connection_count, vehicle_connections, nav_count = fields[9], fields[10], fields[12]
    if not (0 < count < 16384 and vehicles + peds == count and
            0 <= vehicle_connections <= connection_count <= 32767 and 0 <= nav_count <= 16384):
        raise ValueError('Invalid PS2 path graph counts')
    nodes = list(chunk.read_array('<7hBB4B', np, count))
    navs = list(chunk.read_array('<3h2b4B', navp, nav_count))
    connections = [v for v, in chunk.read_array('<H', cp, connection_count)]
    lengths = list(chunk.span(dp, connection_count))
    # This array ONLY covers vehicle connections. Reading the ped tail would
    # consume unrelated resident memory, which can resemble valid nav IDs.
    vehicle_links = [v for v, in chunk.read_array('<H', vp, vehicle_connections)]
    ranges = []
    edges = set()
    for ident, node in enumerate(nodes):
        start, degree = node[6], node[9] & 15
        area(node[2], node[3])
        if start < 0 or start + degree > connection_count:
            raise ValueError('PS2 node connections outside graph')
        if (ident < vehicles and start + degree > vehicle_connections) or (ident >= vehicles and start < vehicle_connections):
            raise ValueError('PS2 vehicle/ped connection partition mismatch')
        ranges.extend(range(start, start + degree))
        for link in range(start, start + degree):
            target = connections[link] & 0x3fff
            if target >= count or (target < vehicles) != (ident < vehicles):
                raise ValueError('PS2 connection has invalid target/type')
            edges.add((ident, target))
            if nodes[target][8] != node[8]:
                raise ValueError('Connected PS2 nodes have different flood-fill groups')
            if ident < vehicles:
                nav = vehicle_links[link]
                if nav >= nav_count or navs[nav][2] not in (ident, target):
                    raise ValueError('PS2 vehicle link has invalid navigation attachment')
    if sorted(ranges) != list(range(connection_count)):
        raise ValueError('PS2 graph has missing/overlapping connection ranges')
    if any((b, a) not in edges for a, b in edges):
        raise ValueError('PS2 graph has an asymmetric connection')
    for nav in navs:
        if not 0 <= nav[2] < vehicles or not (-100 <= nav[3] <= 100 and -100 <= nav[4] <= 100):
            raise ValueError('PS2 navigation attachment/direction invalid')
        if (nav[5] & 7) == 7 or ((nav[5] >> 3) & 7) == 7:
            raise ValueError('PS2 navigation lanes were not resolved')
        if nav[7] * 2 > 127:
            raise ValueError('PS2 divider width exceeds native signed navigation byte')
    return dict(nodes=nodes, navs=navs, connections=connections, lengths=lengths,
                vehicleLinks=vehicle_links, vehicles=vehicles, peds=peds,
                sourceGroupCounts=list(fields[14:16]))


def node_flags(node, vehicle):
    a, b, c = node[9:12]
    # Only semantic bits are transferred. PS2 padding/unused bits contain 0xAA.
    first = (a & 0x3f) | ((a & 0x80) >> 1) | ((b & 1) << 7)
    speed = (b >> 3) & 3
    second = ((a & 0x20) >> 5) | (0x20 if vehicle and speed else 0x10)
    return first | (second << 8) | ((c & 15) << 16)


def encode_graph(graph):
    nodes, navs = graph['nodes'], graph['navs']
    regions = [[] for _ in range(64)]
    for ident, node in enumerate(nodes):
        regions[area(node[2], node[3])].append(ident)
    # Original cars precede peds globally and within each region.
    addresses = {ident: (a, i) for a, ids in enumerate(regions) for i, ident in enumerate(ids)}
    nav_regions = [[] for _ in range(64)]
    for ident, nav in enumerate(navs):
        # SA stores a car path link in the area containing the link's own
        # position (31462 of 31466 retail SA links), not its attached node's
        # area; the attachment address may point into a neighbouring area.
        nav_regions[area(nav[0], nav[1])].append(ident)
    if any(len(ids) > 1024 for ids in nav_regions):
        raise ValueError('Native SA navigation address exceeds 10-bit local index')
    nav_addresses = {ident: (a << 10) | i for a, ids in enumerate(nav_regions) for i, ident in enumerate(ids)}
    payloads = {}
    for a, ids in enumerate(regions):
        vehicle_count = sum(i < graph['vehicles'] for i in ids)
        link_count = sum(nodes[i][9] & 15 for i in ids)
        if len(ids) > 65535 or link_count > 32767:
            raise ValueError('Native SA region node/link capacity exceeded')
        data = bytearray(struct.pack('<5I', len(ids), vehicle_count, len(ids) - vehicle_count, len(nav_regions[a]), link_count))
        links, nav_links, lengths, intersections = bytearray(), bytearray(), bytearray(), bytearray()
        base = 0
        for local, ident in enumerate(ids):
            n = nodes[ident]
            width = n[7] * 2 if ident < graph['vehicles'] else n[7]
            if width > 255:
                raise ValueError('Native SA path width capacity exceeded')
            data.extend(struct.pack('<II3hhHHHBBI', 0, 0, *n[2:5], 32766, base,
                                    a, local, width, n[8], node_flags(n, ident < graph['vehicles'])))
            for j in range(n[6], n[6] + (n[9] & 15)):
                connection = graph['connections'][j]
                links.extend(struct.pack('<HH', *addresses[connection & 0x3fff]))
                nav_links.extend(struct.pack('<H', nav_addresses[graph['vehicleLinks'][j]] if ident < graph['vehicles'] else 0xffff))
                lengths.append(graph['lengths'][j])
                intersections.append(((connection >> 15) & 1) | (((connection >> 14) & 1) << 1))
            base += n[9] & 15
        for ident in nav_regions[a]:
            n = navs[ident]
            data.extend(struct.pack('<2h2H2bbBH', *n[:2], *addresses[n[2]], *n[3:5],
                                    n[7] * 2, n[5] & 127, n[6] & 7))
        if link_count:
            # SA reads 16 * 12 reserved dynamic addresses, then nav addresses,
            # then padded lengths/intersections. Do NOT interleave these arrays.
            data.extend(links + struct.pack('<HH', 0xffff, 0) * 192)
            data.extend(nav_links)
            data.extend(lengths + bytes(192))
            data.extend(intersections + bytes(192))
        payloads[a] = bytes(data)
    return payloads, addresses, nav_addresses


def pack_img(payloads):
    if set(payloads) != set(range(64)):
        raise ValueError('Native navigation must cover all 64 map areas')
    directory = bytearray(b'VER2' + struct.pack('<I', 64))
    body = bytearray()
    sector = 2
    for a in range(64):
        payload = payloads[a]
        size = (len(payload) + 2047) // 2048
        if not 0 < size <= 65535:
            raise ValueError('Native path entry size outside IMG limits')
        directory.extend(struct.pack('<IHH24s', sector, size, 0, ('nodes%d.dat' % a).encode('ascii')))
        body.extend(payload + bytes(size * 2048 - len(payload)))
        sector += size
    return bytes(directory + bytes(4096 - len(directory)) + body)


def export_paths(chunk, output):
    output = pathlib.Path(output)
    if output.exists():
        raise ValueError('Navigation output exists; choose a new directory')
    graph = read_graph(chunk)
    payloads, addresses, nav_addresses = encode_graph(graph)
    output.mkdir(parents=True)
    for a, data in payloads.items():
        (output / ('nodes%d.dat' % a)).write_bytes(data)
    archive = pack_img(payloads)
    (output / 'paths.img').write_bytes(archive)
    (output / 'source-bindings.tsv').write_text('source\tarea\tnode\n' + ''.join(
        f'{i}\t{a}\t{n}\n' for i, (a, n) in sorted(addresses.items())), encoding='ascii')
    (output / 'nav-bindings.tsv').write_text('source\tarea\tnavi\n' + ''.join(
        f'{i}\t{a >> 10}\t{a & 1023}\n' for i, a in sorted(nav_addresses.items())), encoding='ascii')
    report = dict(format=1, source='PS2 GAME.DTZ resident CPathFind', sourceChunkSha256=hashlib.sha256(chunk.data).hexdigest(),
                  vehicleNodes=graph['vehicles'], pedestrianNodes=graph['peds'], navigationNodes=len(graph['navs']),
                  directedConnections=len(graph['connections']), vehicleConnections=len(graph['vehicleLinks']),
                  crossings=sum(bool(c & 0x8000) for c in graph['connections']),
                  pedestrianTrafficLights=sum(bool(c & 0x4000) for c in graph['connections']),
                  vehicleTrafficLights=sum(bool(n[6] & 3) for n in graph['navs']),
                  crossAreaConnections=sum(addresses[i][0] != addresses[graph['connections'][j] & 0x3fff][0]
                      for i, n in enumerate(graph['nodes']) for j in range(n[6], n[6] + (n[9] & 15))),
                  areas=64, populatedAreas=sum(bool(ids) for ids in collections.Counter(a for a, _ in addresses.values()).values()),
                  coordinateScale=8, coordinatesPreserved=True, linksPreserved=True, originalLaneCountsPreserved=True,
                  pedestrianWanderWidthsPreserved=True, sourceGroupCounts=graph['sourceGroupCounts'],
                  reservedDynamicLinksPerPopulatedArea=192, nativeCarLaneWidth=5.4, sourceCarLaneWidth=5.0,
                  speedPolicy='LCS speed class 0: ordinary road; classes 1/2: native SA highway',
                  unsupportedSourceFlags='betweenLevels/onlySmallBoats and unused flags are not assigned unrelated SA bits',
                  pathsImgSha256=hashlib.sha256(archive).hexdigest(), gameExecuted=False, runtimeTrafficVerified=False)
    (output / 'conversion.json').write_text(json.dumps(report, indent=2), encoding='utf-8')
    return report


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('rom', type=pathlib.Path)
    parser.add_argument('output', type=pathlib.Path)
    args = parser.parse_args()
    print(json.dumps(export_paths(Chunk.compressed(args.rom / 'CHK/PS2/GAME.DTZ'), args.output), indent=2))
