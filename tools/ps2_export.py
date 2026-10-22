"""Export bounded PS2 world records to SA writer intermediates and COL2."""
import math
import struct
from ps2_world import transform

SURFACES = (0,1,9,6,25,4,63,45,66,50,64,51,52,53,57,56,55,62,29,39,42,70,43,67,68,41,18,58,59,60,61,69,54,33,34)



# Exact retail LCS CGarages::IsModelIndexADoor allowlist. Keep this tied to
# ModelIndices.h/CGarages::IsModelIndexADoor in res-relcs; name heuristics also
# match static storefronts and garage shells that must never move as doors.
PHYSICAL_DOOR_MODEL_NAMES = frozenset(name.lower() for name in (
    'oddjgaragdoor', 'bombdoor', 'door_bombshop', 'vheistlocdoor',
    'door2_garage', 'ind_slidedoor', 'bankjobdoor', 'door_jmsgrage',
    'ind_safeh_gdoor', 'door_sfehousegrge', 'shedgaragedoor', 'door4_garage',
    'door_col_compnd_01', 'door_col_compnd_02', 'door_col_compnd_03',
    'door_col_compnd_04', 'door_col_compnd_05', 'impex_door', 'SalvGarage',
    'door3_garage', 'leveldoor2', 'double_garage_dr', 'amcogaragedoor',
    'towergaragedoor1', 'towergaragedoor2', 'towergaragedoor3',
    'plysve_gragedoor', 'impexpsubgrgdoor', 'Sub_sprayshopdoor',
    'ind_plyrwoor', '8ballsuburbandoor', 'door_nthgrage', 'hangardoor1',
    'hangardoor2', 'neds_door', 'fs_wrhsedoor', 'jm_ContraGarage',
    'jm_imp_SalvGarage', 'Door2_sjl'))

def name32(name):
    value = name.encode('ascii')
    if len(value) > 31 or not value:
        raise ValueError('Name exceeds SA texture field')
    return value+b'\0'*(32-len(value))


def inverse_entity(matrix, world):
    # Original entity bases are orthonormal; scale belongs to the optimized
    # resource's instance matrix. Reject unsupported entity scales/shear.
    axes = [matrix[i:i+3] for i in (0,4,8)]
    for i, a in enumerate(axes):
        for j, b in enumerate(axes):
            if abs(sum(x*y for x,y in zip(a,b))-(1 if i==j else 0)) > .001:
                raise ValueError('Non-orthonormal original entity transform')
    delta = [world[i]-matrix[12+i] for i in range(3)]
    return tuple(sum(x*y for x,y in zip(delta,a)) for a in axes)


def mesh_file(level, rows, entity, output, texture_names=None):
    vertices = []
    triangles = []
    materials = []
    vertex_index = {}
    textures = set()
    seen = set()
    for row in sorted(rows, key=lambda r: r['renderpass']):
        if row['resource'] in seen:
            continue
        seen.add(row['resource'])
        for mesh in level.building(row['resource']):
            texture = (texture_names or {}).get(mesh['texture'],f'tex_{level.name.lower()}_{mesh["texture"]:04x}')
            textures.add(mesh['texture'])
            if texture not in materials:
                materials.append(texture)
            material = materials.index(texture)
            remap = []
            for pos, uv, rgba in mesh['vertices']:
                local = inverse_entity(entity['matrix'], transform(row['matrix'], pos))
                value = (*local, *uv, *rgba)
                key = struct.pack('<5f4B', *value)
                index = vertex_index.get(key)
                if index is None:
                    index = len(vertices); vertices.append(key); vertex_index[key] = index
                remap.append(index)
            for a,b,c in mesh['triangles']:
                ids = (remap[a], remap[b], remap[c])
                if len(set(ids)) == 3:
                    triangles.append((*ids, material))
    if len(vertices) > 65535:
        raise ValueError(f'{output.name}: model exceeds SA vertex capacity')
    with output.open('wb') as stream:
        stream.write(struct.pack('<4s3I', b'PSM1', len(vertices), len(triangles), len(materials)))
        for name in materials: stream.write(name32(name))
        for vertex in vertices: stream.write(vertex)
        for face in triangles: stream.write(struct.pack('<4H', *face))
    points=[struct.unpack_from('<3f',v) for v in vertices]
    bounds=([min(v[j] for v in points) for j in range(3)]+
            [max(v[j] for v in points) for j in range(3)]) if points else None
    return dict(vertices=len(vertices), triangles=len(triangles), textures=sorted(textures),bounds=bounds,
                resources=sorted(seen), materialRGB=[255,255,255], vertexPrelight='original PS2 RGB5551')


def bound_collision(name,bounds):
    """Render-layer bounds only. Do not duplicate original physical surfaces."""
    if not bounds or len(bounds)!=6 or any(not math.isfinite(v) for v in bounds):raise ValueError('Invalid layer bounds')
    mn=bounds[:3];mx=bounds[3:]
    if any(a>b for a,b in zip(mn,mx)):raise ValueError('Inverted layer bounds')
    center=[(a+b)/2 for a,b in zip(mn,mx)]
    radius=math.dist(center,mx)+.0001
    encoded=name.encode('ascii')
    if len(encoded)>22:raise ValueError('Layer name exceeds COL capacity')
    body=encoded.ljust(22,b'\0')+struct.pack('<H',0)
    body+=struct.pack('<10f',*(v-.0001 for v in mn),*(v+.0001 for v in mx),*center,radius)
    body+=struct.pack('<3H2BI6I',0,0,0,0,0,0,0,0,0,0,0,0)
    return struct.pack('<4sI',b'COL2',len(body))+body


def texture_file(level, ids, output):
    with output.open('wb') as stream:
        stream.write(struct.pack('<4sI', b'PST1', len(ids)))
        for id in sorted(ids):
            w,h,rgba = level.texture(id)
            stream.write(name32(f'tex_{level.name.lower()}_{id:04x}'))
            stream.write(struct.pack('<2I', w,h));stream.write(rgba)


def collision(game, model, version=2, *, map_surfaces=True):
    chunk, offset = game.collisions[model]
    ns, nb, nt = chunk.read('<3h', offset+48)
    nl = chunk.span(offset+54, 1)[0]
    if min(ns,nb,nt) < 0 or max(ns,nb,nt,nl) > 32767:
        raise ValueError('Invalid original collision count')
    ps, pl, pb, sections, pv, pt = chunk.read('<6I', offset+60)
    points = []
    spheres = []
    sphere_shapes = []
    boxes = []
    lines = []
    faces = []
    def surface(value):
        if value >= len(SURFACES):
            if value != 255:
                raise ValueError(f'Unknown original collision surface {value}')
            return 0 if map_surfaces else value
        return SURFACES[value] if map_surfaces else value
    for i in range(ns):
        x,y,z,r = chunk.read('<4f', ps+32*i)
        mat, piece = chunk.read('<2B', ps+32*i+16)
        if r < 0 or not all(math.isfinite(v) for v in (x,y,z,r)):
            raise ValueError('Invalid collision sphere')
        points.extend(((x-r,y-r,z-r),(x+r,y+r,z+r)))
        sphere_shapes.append(((x,y,z),r))
        spheres.append(struct.pack('<4f4B', r,x,y,z,surface(mat),piece,0,255))
    for i in range(nb):
        mn = chunk.read('<3f', pb+48*i);mx = chunk.read('<3f', pb+48*i+16)
        mat,piece = chunk.read('<2B', pb+48*i+32)
        if any(mn[j] > mx[j] for j in range(3)):
            raise ValueError('Inverted original collision box')
        points.extend((mn,mx))
        boxes.append(struct.pack('<6f4B', *mn,*mx,surface(mat),piece,0,255))
    for i in range(nl):
        a = chunk.read('<3f', pl+32*i);b = chunk.read('<3f', pl+32*i+16)
        points.extend((a,b));lines.append(struct.pack('<6f', *a,*b))
    nv = 0
    for a,b,c,mat,_padding in chunk.read_array('<3H2B', pt, nt):
        if any(v % 6 for v in (a,b,c)):
            raise ValueError('PS2 triangle byte offset is not aligned to a vertex')
        a,b,c = a//6,b//6,c//6
        nv = max(nv, a+1,b+1,c+1)
        # PS2 CColTriangle has one surface byte and alignment padding.
        # SA COLL V1 surface is material, flag, brightness, packed light.
        # Ignore the source padding; day/night lighting both use 15/15.
        faces.append(struct.pack('<3I4B', a,b,c,surface(mat),0,0,255))
    verts = [tuple(v/128. for v in xyz) for xyz in chunk.read_array('<3h', pv, nv)]
    points.extend(verts)
    # Door broad-phase bounds are also gameplay data: both LCS and SA derive
    # garage travel from max.z-min.z. A door's primitive can cover only a small
    # part of its mesh, so tightening these bounds changes its actual movement.
    if (game.models[model]['name'] or '').lower() in PHYSICAL_DOOR_MODEL_NAMES:
        x,y,z,radius = chunk.read('<4f', offset)
        center=(x,y,z);mn=chunk.read('<3f',offset+16);mx=chunk.read('<3f',offset+32)
        if any(a>b for a,b in zip(mn,mx)):raise ValueError('Inverted source garage door bounds')
    elif points:
        mn = tuple(min(p[j] for p in points) for j in range(3))
        mx = tuple(max(p[j] for p in points) for j in range(3))
        center = tuple((a+b)/2 for a,b in zip(mn,mx))
        radius = max([math.dist(center,p) for p in points]+
                     [math.dist(center,p)+r for p,r in sphere_shapes])+1e-4
        mn = tuple(v-1e-4 for v in mn);mx = tuple(v+1e-4 for v in mx)
    else:
        x,y,z,radius = chunk.read('<4f', offset)
        center=(x,y,z);mn=chunk.read('<3f',offset+16);mx=chunk.read('<3f',offset+32)
    if not all(math.isfinite(v) for v in (*center,radius,*mn,*mx)) or radius < 0:
        raise ValueError('Nonfinite collision bounds')
    body = struct.pack('<10f', radius,*center,*mn,*mx)
    for array in (spheres,lines,boxes,[struct.pack('<3f',*p) for p in verts],faces):
        body += struct.pack('<I', len(array))+b''.join(array)
    name=game.models[model]['name'].encode('ascii')
    if len(name)>22:
        raise ValueError('Original collision name exceeds SA capacity')
    body=name+b'\0'*(22-len(name))+struct.pack('<H',0)+body
    legacy=struct.pack('<4sI',b'COLL',len(body))+body
    if version==1:return legacy
    if version!=2:raise ValueError('Unsupported collision output version')
    if nl:raise ValueError('COL2 map export does not support suspension lines')
    # SA offsets are relative to byte 4, not the chunk origin. Preserve
    # source compressed coordinates exactly; they already use SA's 1/128.
    arrays=[b''.join(struct.pack('<4f4B',*struct.unpack('<4f',s[:16])[1:],
                                struct.unpack('<f',s[:4])[0],*s[16:]) for s in spheres),
            b''.join(boxes),b'',b''.join(struct.pack('<3h',*(round(v*128) for v in p)) for p in verts),
            b''.join(struct.pack('<3H2B',*struct.unpack('<3I',f[:12]),f[12],f[15]) for f in faces)]
    payload=bytearray();offsets=[]
    for array in arrays:
        if array:
            payload.extend(b'\0'*((-len(payload))%4))
            offsets.append(108+len(payload)-4);payload.extend(array)
        else:offsets.append(0)
    payload.extend(b'\0'*((-len(payload))%4))
    header=(name.ljust(22,b'\0')+struct.pack('<H',0)+
            struct.pack('<10f',*mn,*mx,*center,radius)+
            struct.pack('<3H2BI6I',ns,nb,nt,0,0,2 if ns or nb or nt else 0,*offsets,0))
    body=header+payload
    return struct.pack('<4sI',b'COL2',len(body))+body
