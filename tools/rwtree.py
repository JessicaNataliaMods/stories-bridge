"""Minimal RenderWare chunk-tree reader/dumper (stdlib only)."""
import struct, sys

NAMES = {1: 'Struct', 2: 'String', 3: 'Extension', 6: 'Texture', 7: 'Material', 8: 'MaterialList',
         14: 'FrameList', 15: 'Geometry', 16: 'Clump', 20: 'AtomicStruct?', 26: 'GeometryList',
         0x0253F2F3: 'Pipeline', 0x253F2FE: 'NodeName', 0x11E: 'HAnim', 0x50E: 'BinMesh',
         0x116: 'Skin', 0x120: 'MaterialEffects', 0x253F2FC: 'Collision/EnvMat?', 0x253F2F6: 'RightToRender',
         0x253F2F8: '2dfx', 0x253F2F9: 'Breakable?', 0x253F2FD: 'Collision'}
CONTAINERS = {2: False, 3: True, 7: True, 8: True, 14: True, 15: True, 16: True, 26: True, 6: True, 20: False}
# Struct(1) and String(2) are leaves.  Atomic(20) is a container with Struct+Extension.
CONTAINER_IDS = {3, 6, 7, 8, 14, 15, 16, 20, 26}


def parse(raw, start=0, end=None):
    end = len(raw) if end is None else end
    out = []
    while start + 12 <= end:
        ident, size, ver = struct.unpack_from('<3I', raw, start)
        stop = start + 12 + size
        if stop > end:
            raise ValueError('chunk exceeds parent at %d' % start)
        node = dict(id=ident, ver=ver, off=start, body=(start + 12, stop))
        if ident in CONTAINER_IDS:
            node['kids'] = parse(raw, start + 12, stop)
        out.append(node)
        start = stop
    return out


def dump(raw, nodes, depth=0, lines=None):
    lines = [] if lines is None else lines
    for n in nodes:
        a, b = n['body']
        lines.append('%s%s(0x%X) off=%d size=%d ver=0x%X' % ('  ' * depth, NAMES.get(n['id'], '?'), n['id'], n['off'], b - a, n['ver']))
        if 'kids' in n:
            dump(raw, n['kids'], depth + 1, lines)
    return lines


if __name__ == '__main__':
    raw = open(sys.argv[1], 'rb').read()
    print('\n'.join(dump(raw, parse(raw))))
