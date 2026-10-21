"""Read PS2 relocatable metadata as data. Never executes game code."""
import argparse, json, pathlib, re, struct, zlib

class Chunk:
    def __init__(self, path):
        self.data=zlib.decompress(path.read_bytes())
        self.header=struct.unpack_from('<8I',self.data)
        if self.header[2]!=len(self.data):
            raise ValueError('Chunk fileSize differs from decompressed size')
    def read(self, fmt, offset):
        size=struct.calcsize(fmt)
        if offset<0 or offset+size>len(self.data): raise ValueError('Field outside chunk')
        return struct.unpack_from(fmt,self.data,offset)
    def u32(self,p): return self.read('<I',p)[0]
    def text(self,p,n):
        b=self.data[p:p+n]
        if len(b)!=n: raise ValueError('String outside chunk')
        return b.split(b'\0')[0].decode('ascii')

def main():
    p=argparse.ArgumentParser();p.add_argument('rom',type=pathlib.Path);p.add_argument('--output',type=pathlib.Path)
    a=p.parse_args();g=Chunk(a.rom/'CHK/PS2/GAME.DTZ');root=32
    names={int(h,16):n for h,n in re.findall(r'\{\s*(0x[0-9A-Fa-f]+),\s*"([^"]+)"', (pathlib.Path(__file__).parent.parent/'vendor/librwgta/librwgta-master/tools/storiesconv/hashes.inc').read_text())}
    nmodels=g.u32(root+24);models=g.u32(root+28);txpool=g.u32(root+64)
    txitems,txflags,txsize=g.read('<3I',txpool)
    txds={i:g.text(txitems+28*i+8,20) for i in range(txsize) if not g.data[txflags+i]&128}
    streaming=g.u32(root+92);rows=[]
    for i in range(nmodels):
        m=g.u32(models+4*i)
        if not m:continue
        hashkey=g.u32(m+8);kind=g.data[m+16];txslot=g.read('<h',m+30)[0]
        offset,size=g.read('<2I',streaming+4+20*i+12)
        row=dict(id=i,name=names.get(hashkey,f'hash_{hashkey:x}'),hash=f'{hashkey:08X}',type=kind,txd=txds.get(txslot),streamOffset=offset*2048,streamSize=size*2048)
        if kind in (1,3):
            row.update(draw=g.read('<3f',m+40),atomics=g.data[m+52],flags=g.read('<H',m+54)[0],relatedPointer=g.u32(m+56))
        rows.append(row)
    byptr={g.u32(models+4*r['id']):r['id'] for r in rows}
    for row in rows:
        if 'relatedPointer' in row:row['related']=byptr.get(row.pop('relatedPointer'))
    pools=[]
    for label,offset in [('building',4),('treadable',8),('dummy',12)]:
        pool=g.u32(root+offset);items,flags,size=g.read('<3I',pool)
        entries=[]
        for i in range(size):
            if g.data[flags+i]&128:continue
            e=items+96*i
            entries.append(dict(index=i,model=g.read('<h',e+88)[0],position=g.read('<3f',e+48),level=g.data[e+90],area=g.data[e+91]))
        pools.append(dict(name=label,size=size,entries=entries))
    levels=[]
    for label in ('INDUST','COMMER','SUBURB','UNDERG'):
        l=Chunk(a.rom/'MODELS'/f'{label}.LVZ');first=l.u32(36);last=l.u32(404)
        count=(last-first)//32
        if count<0 or count>10000 or (last-first)%32:raise ValueError('Invalid world sector directory')
        sectors=[];resources={};instances=[]
        with (a.rom/'MODELS'/f'{label}.IMG').open('rb') as stream:
            for i in range(count):
                h=l.read('<8I',first+32*i);stream.seek(h[6]);data=struct.pack('<8I',*h)+stream.read(h[2]-32)
                if len(data)!=h[2]:raise ValueError('Truncated world sector')
                resource_pointer,nres=struct.unpack_from('<IH',data,32)
                for j in range(nres):
                    id,pointer=struct.unpack_from('<II',data,resource_pointer+8*j)
                    resources.setdefault(id,[]).append(dict(sector=i,pointer=pointer))
                passes=struct.unpack_from('<8I',data,40)
                n=(passes[-1]-passes[0])//80
                if n<0 or n>10000 or (passes[-1]-passes[0])%80:raise ValueError('Invalid sector instance layout')
                for j in range(n):
                    p=passes[0]+80*j;id,res=struct.unpack_from('<HH',data,p)
                    instances.append(dict(sector=i,index=j,id=id&32767,flag=bool(id&32768),resource=res,bound=struct.unpack_from('<4e',data,p+4),matrix=struct.unpack_from('<16f',data,p+16)))
                sectors.append(dict(index=i,resources=nres,instances=n))
        levels.append(dict(name=label,sectors=sectors,resources=resources,instances=instances))
    result=dict(header=g.header,models=rows,txds=txds,pools=pools,levels=levels,gameExecuted=False)
    if a.output:a.output.write_text(json.dumps(result,indent=2))
    print(json.dumps(dict(modelCount=len(rows),mapModels=sum(r['type'] in (1,3) for r in rows),streamedMapModels=sum(r['type'] in (1,3) and bool(r['streamSize']) for r in rows),txdCount=len(txds),pools=[dict(name=r['name'],size=r['size'],used=len(r['entries'])) for r in pools],levels=[dict(name=l['name'],sectors=len(l['sectors']),resources=len(l['resources']),instances=len(l['instances'])) for l in levels],petrol=[r for r in rows if 'petrol' in r['name'].lower() or 'rolstation' in r['name'].lower()]),indent=2))
if __name__=='__main__':main()
