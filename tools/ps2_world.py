"""Bounded readers for the original LCS PS2 relocatable world files.

Offsets are kept as offsets, never executed or cast to native pointers. Layouts
and sector transforms follow librwgta/tools/storiesconv and storiesview.
"""
import collections
import math
import pathlib
import re
import struct
import zlib


class Chunk:
    def __init__(self, data, label='chunk'):
        self.data = data
        self.label = label
        self.header = self.read('<8I', 0)
        if self.header[2] != len(data) or len(data) > 128*1024*1024:
            raise ValueError(f'{label}: invalid chunk size')
        self.span(self.header[4], self.header[5]*4)
        for offset, in self.read_array('<I', self.header[4], self.header[5]):
            value = self.read('<I', offset)[0]
            if value:
                self.span(value, 1)

    @classmethod
    def compressed(cls, path):
        raw = path.read_bytes()
        obj = zlib.decompressobj()
        data = obj.decompress(raw, 128*1024*1024+1)
        if not obj.eof or obj.unconsumed_tail:
            raise ValueError(f'{path}: invalid/oversized compressed chunk')
        return cls(data, str(path))

    def span(self, offset, size):
        if offset < 0 or size < 0 or offset+size > len(self.data):
            raise ValueError(f'{self.label}: field outside chunk: {offset}+{size}')
        return self.data[offset:offset+size]

    def read(self, fmt, offset):
        return struct.unpack(fmt, self.span(offset, struct.calcsize(fmt)))

    def read_array(self, fmt, offset, count):
        if count < 0 or count > 1000000:
            raise ValueError('Invalid record count')
        return struct.iter_unpack(fmt, self.span(offset, struct.calcsize(fmt)*count))

    def u32(self, offset):
        return self.read('<I', offset)[0]

    def text(self, offset, size):
        return self.span(offset, size).split(b'\0')[0].decode('ascii')


def transform(matrix, point):
    return tuple(sum(matrix[4*k+j]*point[k] for k in range(3))+matrix[12+j]
                 for j in range(3))


class Game:
    def __init__(self, rom, name_database):
        self.rom = pathlib.Path(rom)
        self.chunk = g = Chunk.compressed(self.rom/'CHK/PS2/GAME.DTZ')
        self.names = {int(h, 16): n for h, n in re.findall(
            r'\{\s*(0x[0-9A-Fa-f]+),\s*"([^"]+)"', name_database.read_text())}
        # Original LCS door names absent from librwgta's public hash list.
        # Validate each against the original uppercase CRC key in GAME.DTZ.
        for name in ('iz_DonOfcDr_L','iz_tA_fd_L','iz_tA_bd_L'):
            self.names[zlib.crc32(name.upper().encode('ascii')) ^ 0xffffffff] = name
        self.streaming = g.u32(32+92)
        txpool = g.u32(32+64)
        items, flags, size = g.read('<3I', txpool)
        self.txds = {i: g.text(items+28*i+8, 20) for i in range(size)
                     if not g.span(flags+i, 1)[0] & 128}
        self.resident_txds = {i: g.u32(items+28*i) for i in self.txds
                              if g.u32(items+28*i)}
        model_ptrs = g.u32(32+28)
        self.models = {}
        bypointer = {}
        for i in range(g.u32(32+24)):
            pointer = g.u32(model_ptrs+4*i)
            if not pointer:
                continue
            key = g.u32(pointer+8)
            txslot = g.read('<h', pointer+30)[0]
            row = dict(id=i, name=self.names.get(key), hash=f'{key:08X}',
                       type=g.span(pointer+16, 1)[0], txd=self.txds.get(txslot),
                       txdSlot=txslot, colPointer=g.u32(pointer+20))
            row['streamOffset'], row['streamSize'] = self.stream(i)
            if row['type'] in (1, 3):
                row.update(draw=g.read('<3f', pointer+40),
                           atomics=g.span(pointer+52, 1)[0],
                           flags=g.read('<H', pointer+54)[0],
                           related=g.u32(pointer+56))
                if row['type'] == 3:
                    row['time'] = g.read('<2I', pointer+60)
            bypointer[pointer] = i
            self.models[i] = row
        for row in self.models.values():
            if 'related' in row:
                row['related'] = bypointer.get(row['related'])
        zones = g.u32(32+40)
        count = g.read('<h', zones+104)[0]
        if not 1 <= count <= 1024:
            raise ValueError('Invalid original map-zone count')
        array = g.u32(zones+28)
        self.map_zones = []
        for i in range(count):
            bounds = g.read('<6f', array+56*i+8)
            level = g.read('<i', array+56*i+36)[0]
            if not 0 <= level <= 4 or any(bounds[j]>bounds[j+3] for j in range(3)):
                raise ValueError('Invalid original island zone')
            self.map_zones.append(dict(name=g.text(array+56*i,8),bounds=bounds,level=level))
        self.entities = []
        for label, offset in [('building', 4), ('treadable', 8), ('dummy', 12)]:
            items, flags, size = g.read('<3I', g.u32(32+offset))
            for i in range(size):
                if g.span(flags+i, 1)[0] & 128:
                    continue
                e = items+96*i
                self.entities.append(dict(pool=label, index=i,
                    model=g.read('<h', e+88)[0], matrix=g.read('<16f', e),
                    level=g.span(e+90, 1)[0], area=g.span(e+91, 1)[0]))
        self.collisions = {}
        self.render_spheres = {}
        for i, row in self.models.items():
            if row['colPointer']:
                self.collisions[i] = (g, row['colPointer'])
                self.render_spheres[i] = g.read('<4f', row['colPointer'])
        self.load_collision_store()

    def stream(self, index):
        pos, size = self.chunk.read('<2I', self.streaming+4+20*index+12)
        return pos*2048, size*2048

    def load_collision_store(self):
        g = self.chunk
        _, flags, size = g.read('<3I', g.u32(32+72))
        with (self.rom/'MODELS/GTA3PS2.IMG').open('rb') as stream:
            for i in range(size):
                if g.span(flags+i, 1)[0] & 128:
                    continue
                pos, count = self.stream(4900+1200+i)
                if not count:
                    continue
                stream.seek(pos)
                header = stream.read(32)
                if len(header) != 32 or struct.unpack_from('<I', header)[0] != 0x636F6C32:
                    raise ValueError('Invalid original collision stream')
                length = struct.unpack_from('<I', header, 8)[0]
                if length > count or length < 40:
                    raise ValueError('Collision chunk exceeds stream entry')
                c = Chunk(header+stream.read(length-32), f'collision {i}')
                p = 32
                while True:
                    model, pointer = c.read('<iI', p)
                    p += 8
                    if model < 0:
                        break
                    if model not in self.models:
                        raise ValueError('Collision refers to unknown model')
                    c.span(pointer, 96)
                    self.collisions[model] = (c, pointer)

    def sphere(self, model):
        c, p = self.collisions[model]
        return c.read('<4f', p)

    def named_textures(self):
        """Original RslTexture names and pixels, including resident generic.

        Static sector resources only store an RslRaster. Named dictionaries
        store the larger RslTexture and can identify identical sector pixels.
        """
        with (self.rom/'MODELS/GTA3PS2.IMG').open('rb') as stream:
            for slot, dictionary in self.txds.items():
                if slot in self.resident_txds:
                    chunk, root = self.chunk, self.resident_txds[slot]
                else:
                    position, size = self.stream(4900+slot)
                    if not size:continue
                    stream.seek(position);raw = stream.read(size)
                    if len(raw)!=size:raise ValueError('Truncated named texture stream')
                    length=struct.unpack_from('<I',raw,8)[0]
                    if length>size:raise ValueError('Named texture chunk exceeds stream')
                    chunk=Chunk(raw[:length],f'named dictionary {dictionary}')
                    if chunk.u32(0)!=0x00746578:raise ValueError('Invalid named texture ident')
                    root=32
                terminator=root+8;link=chunk.u32(terminator);seen=set()
                while link!=terminator:
                    if link in seen or len(seen)>4096:raise ValueError('Corrupt texture list')
                    seen.add(link);texture=link-8;chunk.span(texture,80)
                    raster=chunk.u32(texture)
                    name=chunk.text(texture+16,32);mask=chunk.text(texture+48,32)
                    if not name:raise ValueError('Original named texture is empty')
                    yield dict(dictionary=dictionary,slot=slot,name=name,mask=mask,
                               resident=slot in self.resident_txds,
                               rasterFlags=chunk.u32(raster+4),image=decode_raster(chunk,raster))
                    link=chunk.u32(link)


class Level:
    def __init__(self, rom, name):
        self.name = name
        self.chunk = c = Chunk.compressed(pathlib.Path(rom)/'MODELS'/f'{name}.LVZ')
        self.img = pathlib.Path(rom)/'MODELS'/f'{name}.IMG'
        self.resources = {}
        self.instances = collections.defaultdict(list)
        self.sectors = []
        self.resource_conditions = collections.defaultdict(set)
        self.interior_conditions = collections.defaultdict(set)
        self.overlay_conditions = collections.defaultdict(set)
        self.coordinates = {}
        self.interior_choices = collections.defaultdict(list)
        self.swaps = {id: (off, on) for off, on, id in
                      c.read_array('<BBh', c.u32(804), c.u32(800))}
        first, last = c.u32(36), c.u32(404)
        self.origins = {}
        for y in range(46):
            start, off = c.read('<2I', 36+8*y)
            end = c.u32(36+8*(y+1))
            if (end-start) % 32:
                raise ValueError('Invalid sector row')
            for j in range((end-start)//32):
                sector = (start-first)//32+j
                x = off+j
                self.origins[sector] = (-1950.+100*x-(y & 1)*50,
                                      -2000.+43.3+86.6*y, 0.)
                self.coordinates[sector] = (x,y)
        self.exterior_count = (last-first)//32
        for x, y, slot, state, sector in c.read_array('<4Bh', c.u32(820), c.u32(816)):
            self.origins[sector] = (-1950.+100*x-(y & 1)*50,
                                  -2000.+43.3+86.6*y, 0.)
            self.interior_conditions[sector].add((slot,state))
            self.coordinates[sector] = (x,y)
            self.interior_choices[x,y].append((slot,state,sector))
        # The following directory entries have the same WRLD ident but are
        # dynamic/radar resources rather than sectors. Only actual sector IDs
        # referenced by the rows and interior swaps may be parsed as Sector.
        self.headers = [c.read('<8I', first+32*i)
                        for i in range(max(self.origins, default=-1)+1)]
        self.resource_chunks = []
        table = c.u32(32)
        for i in range(c.u32(412)):
            pointer = c.u32(table+8*i)
            if pointer:
                self.resources[i] = (None, pointer)
        with self.img.open('rb') as stream:
            for sector, h in enumerate(self.headers):
                stream.seek(h[6])
                raw = Chunk(struct.pack('<8I', *h)+stream.read(h[2]-32), f'{name} sector {sector}')
                self.sectors.append(raw)
                # Swapped resource chunks carry their own visibility rules.
                # They can supply night/day or story-state geometry to an
                # ordinary instance without an entry in the levelSwaps table.
                for off,on,target in raw.read_array('<BBh',raw.u32(76),raw.read('<h',72)[0]):
                    if target<0:raise ValueError('Invalid overlay sector identity')
                    self.overlay_conditions[target].add((off,on))
                res, nres = raw.read('<IH', 32)
                for id, pointer in raw.read_array('<2I', res, nres):
                    previous = self.resources.get(id)
                    if previous is None:
                        self.resources[id] = (sector, pointer)
                    # Resource IDs are canonical within a level, overlays hold
                    # residency copies. Geometry equivalence checked at export.
                passes = raw.read('<8I', 40)
                if any(passes[i] > passes[i+1] for i in range(7)):
                    raise ValueError(f'{name} sector {sector}: unsorted sector passes {passes}')
                origin = self.origins.get(sector)
                if origin is None and passes[-1] != passes[0]:
                    raise ValueError(f'Missing origin of sector {sector}')
                for renderpass in range(7):
                    size = passes[renderpass+1]-passes[renderpass]
                    if size % 80:
                        raise ValueError('Invalid instance array')
                    for p in range(passes[renderpass], passes[renderpass+1], 80):
                        id, resource = raw.read('<2H', p)
                        matrix = list(raw.read('<16f', p+16))
                        for j in range(3):
                            matrix[12+j] += origin[j]
                        row = dict(id=id & 32767, flag=bool(id & 32768), resource=resource,
                                   bound=raw.read('<4e', p+4), matrix=matrix,
                                   renderpass=renderpass, sector=sector,
                                   interior=sector >= (last-first)//32)
                        rows = self.instances[row['id']]
                        signature = (resource, renderpass)
                        existing = next((r for r in rows if r['signature'] == signature), None)
                        if existing:
                            if max(abs(existing['matrix'][j]-matrix[j]) for j in
                                   (0,1,2,4,5,6,8,9,10,12,13,14)) > .02:
                                raise ValueError('Sector copies have different world transforms')
                            existing['copies'] += 1
                            existing['sectors'].add(sector)
                        else:
                            row.update(signature=signature, copies=1,sectors={sector})
                            rows.append(row)
            pointer = first+32*len(self.headers)
            while c.u32(pointer) == 0x57524c44:
                h = c.read('<8I', pointer)
                stream.seek(h[6])
                raw = Chunk(struct.pack('<8I',*h)+stream.read(h[2]-32), f'{name} resource chunk')
                res, nres = raw.read('<2I', 32)
                index = len(self.resource_chunks)
                self.resource_chunks.append(raw)
                for id, offset in raw.read_array('<2I', res, nres):
                    if id not in self.resources:
                        self.resources[id] = (-index-1, offset)
                pointer += 32
        for target,conditions in self.overlay_conditions.items():
            if target<len(self.sectors):
                source=self.sectors[target];res,nres=source.read('<IH',32)
            else:
                index=target-len(self.sectors)
                if index>=len(self.resource_chunks):raise ValueError('Overlay target outside resource directory')
                source=self.resource_chunks[index];res,nres=source.read('<2I',32)
            for resource,_pointer in source.read_array('<2I',res,nres):
                self.resource_conditions[resource].update(conditions)

    def conditions(self,row):
        """Original predicates: instance AND resource AND interior selection.

        Conditions within resource/interior lists are alternatives; the three
        categories must all pass. Never infer story visibility from file names.
        """
        swap=self.swaps.get(row['id'])
        return dict(instance=list(swap) if swap is not None else None,
                    resources=[list(c) for c in sorted(self.resource_conditions.get(row['resource'],()))],
                    interiors=[],sectorAlternatives=self.sector_conditions(row))

    def sector_conditions(self,row):
        """Exactly one exterior/interior sector is selected per grid cell.

        An unchanged resource present in all counterparts has no condition.
        Otherwise retain the OR of its copies, including exclusions for the
        original exterior and earlier interior choices in source table order.
        """
        copies=row.get('sectors',{row['sector']});terms=set()
        bycell=collections.defaultdict(set)
        for sector in copies:bycell[self.coordinates[sector]].add(sector)
        for cell,sectors in bycell.items():
            choices=self.interior_choices.get(cell,[])
            exterior=any(s<self.exterior_count for s in sectors)
            if exterior and all(s in sectors for _slot,_state,s in choices):return []
            conditions=tuple((slot,state) for slot,state,_s in choices)
            if exterior:terms.add(((),conditions))
            earlier=[]
            for slot,state,sector in choices:
                if sector in sectors and (slot,state) not in earlier:
                    terms.add((((slot,state),),tuple(earlier)))
                earlier.append((slot,state))
        if not terms:raise ValueError('Source instance has no selectable sector')
        return [dict(on=[list(c) for c in on],off=[list(c) for c in off])
                for on,off in sorted(terms)]

    def resource(self, id):
        sector, pointer = self.resources[id]
        chunk = self.chunk if sector is None else (self.sectors[sector] if sector >= 0
                else self.resource_chunks[-sector-1])
        return chunk, pointer

    def dynamics(self):
        c=self.chunk
        result=[]
        for i in range(c.u32(808)):
            scale_x,scale_y,scale_z,model,res,x,y,z,flags,res2,*bounds=c.read(
                '<3f2h3fHh4f',c.u32(812)+48*i)
            matrix=[scale_x,0,0,0, 0,scale_y,0,0, 0,0,scale_z,0, x,y,z,1]
            result.append(dict(model=model,resource=res,resource2=res2,matrix=matrix,
                               lod=flags & 255,flags=flags,bounds=bounds))
        return result

    def building(self, id):
        c, pointer = self.resource(id)
        count, header_size = c.read('<2H', pointer)
        if count == 0:
            return []
        if count > 4096 or header_size < count*22:
            raise ValueError(f'Invalid building geometry {self.name}:{id}')
        packet = pointer+4+header_size
        meshes = []
        for i in range(count):
            tex, size, u, v, flags, *bounds = c.read('<2H2eH6e', pointer+4+22*i)
            size &= 0x7fff
            end = packet+size
            c.span(packet, size)
            vertices = []
            first = True

            def expect(value, mask=0xffffffff):
                nonlocal packet
                actual = c.u32(packet)
                if actual & mask != value:
                    raise ValueError(f'{self.name}:{id}: unexpected VIF {actual:08X}')
                packet += 4

            def unpack(value, mask, stride):
                nonlocal packet
                cmd = c.u32(packet)
                expect(value, mask)
                n = (cmd >> 16) & 255
                if not n:
                    n = 256
                start = packet
                packet += (n*stride+3)//4*4
                if packet > end:
                    raise ValueError('VIF array exceeds mesh packet')
                return start, n

            while packet < end:
                expect(0x6c018000)
                c.span(packet, 16)
                n = c.u32(packet+12) & 32767
                packet += 16
                expect(0x20000000); packet += 4
                expect(0x30000000); packet += 16
                pos, np = unpack(0x79000000, 0xff004000, 6)
                expect(0x20000000); packet += 4
                expect(0x30000000); packet += 16
                uv, nuv = unpack(0x76004000, 0xff004000, 2)
                col, nc = unpack(0x6f000000, 0xff004000, 2)
                expect(0x14000006)
                while packet < end and c.u32(packet) == 0:
                    packet += 4
                if not 3 <= n <= min(np, nuv, nc):
                    raise ValueError('VIF vertex count differs from GIF tag')
                for j in range(0 if first else 2, n):
                    xyz = tuple(x/32767. for x in c.read('<3h', pos+6*j))
                    st = tuple(x/128.*s for x,s in zip(c.read('<2B', uv+2*j), (u,v)))
                    color = c.read('<H', col+2*j)[0]
                    rgba = tuple(((color >> shift) & 31)*255//31 for shift in (0,5,10)) + (255 if color & 32768 else 0,)
                    vertices.append((xyz, st, rgba))
                first = False
            if packet != end:
                raise ValueError('Unparsed VIF mesh data')
            triangles = []
            for j in range(2, len(vertices)):
                a, b, d = (j-2, j-1, j) if j % 2 == 0 else (j-1, j-2, j)
                xyz = [vertices[k][0] for k in (a,b,d)]
                if len(set(xyz)) == 3:
                    triangles.append((a,b,d))
            meshes.append(dict(texture=tex, flags=flags, bounds=bounds,
                               vertices=vertices, triangles=triangles))
        return meshes

    def texture(self, id):
        return decode_raster(*self.resource(id))


def decode_raster(c, pointer):
    data, flags = c.read('<2I', pointer)
    logw, logh, depth = flags & 63, flags >> 6 & 63, flags >> 12 & 63
    mip = flags >> 20 & 15
    if logw > 11 or logh > 11 or depth not in (4,8,16,32) or mip > 12:
        raise ValueError(f'Invalid PS2 raster {self.name}:{id}')
    w, h = 1 << logw, 1 << logh
    raw = c.span(data, w*h*depth//8)
    if depth <= 8:
        if depth == 4:
            raw = bytes(x for b in raw for x in (b & 15, b >> 4))
        if flags >> 24 & 1:
            def swizzle(x,y):
                x ^= (((y >> 1) ^ (y >> 2)) & 1) << 2
                nx = (x & 7) | ((x >> 1) & ~7)
                ny = (y & 1) | ((y >> 1) & ~1)
                return ((y >> 1) & 1) | (((x >> 3) & 1) << 1) | (nx << 2) | (ny << (logw+1))
            indexes = [swizzle(x,y) for y in range(h) for x in range(w)]
            if max(indexes) >= len(raw):
                raise ValueError('Swizzle address outside raster')
            raw = bytes(raw[i] for i in indexes)
        if depth == 8:
            remap = (0,16,8,24)
            raw = bytes((x & ~24) | remap[(x & 24) >> 3] for x in raw)
        pw, ph, palette_offset = w, h, data
        for _ in range(mip):
            palette_offset += pw*ph*depth//8
            pw //= 2; ph //= 2
        palette = c.span(palette_offset, (1 << depth)*4)
        colors = [bytes((r,g,b,min(255,a*255//128))) for r,g,b,a in
                  struct.iter_unpack('<4B', palette)]
        rgba = b''.join(colors[i] for i in raw)
    elif depth == 32:
        if flags >> 24 & 1:
            stride = w*4
            output = bytearray(len(raw))
            cursor = 0
            for by in range((h+7)//8):
                for bx in range(stride//16):
                    for y in range(8):
                        offset = (by*8+y)*stride+bx*16
                        if offset+16 > len(output):
                            raise ValueError('Invalid 32-bit swizzle shape')
                        output[offset:offset+16] = raw[cursor:cursor+16]
                        cursor += 16
            raw = output
        rgba = bytearray(raw)
        for i in range(3, len(rgba), 4):
            rgba[i] = min(255, rgba[i]*255//128)
        rgba = bytes(rgba)
    else:
        # PS2 1555 packing, with the source high alpha bit retained.
        rgba = b''.join(bytes(((v & 31)*255//31, (v >> 5 & 31)*255//31,
                              (v >> 10 & 31)*255//31, 255 if v & 32768 else 0))
                        for v, in struct.iter_unpack('<H', raw))
    return w, h, rgba
