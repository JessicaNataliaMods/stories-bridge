"""Development-only vertex correspondence for split PS2 world fragments."""
import argparse, collections, json, pathlib, struct
import numpy as np
from ps2_world import Game, Level
from ps2_visibility import time_compatible


def key(points):
    q=np.rint(points*10).astype(np.int64)+32768
    if np.any(q<0) or np.any(q>65535):raise ValueError('Reference vertex outside bounded grid')
    return (q[:,0]<<32)|(q[:,1]<<16)|q[:,2]


def main():
    p=argparse.ArgumentParser();p.add_argument('rom',type=pathlib.Path)
    p.add_argument('links',type=pathlib.Path);p.add_argument('vertices',type=pathlib.Path)
    p.add_argument('output',type=pathlib.Path)
    p.add_argument('--independent-pass',action='store_true',help='Validate vertices without equating optimized render pass and model LOD bit')
    a=p.parse_args()
    root=pathlib.Path(__file__).resolve().parents[1]
    g=Game(a.rom,root/'vendor/librwgta/librwgta-master/tools/storiesconv/hashes.inc')
    raw=a.vertices.read_bytes();pos=8
    if raw[:4]!=b'RGV1':raise ValueError('Invalid reference vertex file')
    reference={}
    for _ in range(struct.unpack_from('<I',raw,4)[0]):
        name=raw[pos:pos+32].split(b'\0')[0].decode('ascii').lower();n=struct.unpack_from('<I',raw,pos+32)[0];pos+=36
        reference[name]=np.frombuffer(raw,dtype='<f4',count=n*3,offset=pos).reshape(n,3).astype(np.float64)
        pos+=n*12
    if pos!=len(raw):raise ValueError('Unparsed reference bytes')
    record=json.loads(a.links.read_text());known={(x['level'],x['worldId']) for x in record['links']}
    worlds=[[],[]];owners=[[],[]]
    for i,e in enumerate(g.entities):
        m=g.models[e['model']];verts=reference.get((m['name'] or '').lower())
        if verts is None or m['type'] not in (1,3):continue
        matrix=np.asarray([e['matrix'][j:j+3] for j in (0,4,8)])
        world=verts @ matrix+np.asarray(e['matrix'][12:15]);kind=0 if a.independent_pass else int(bool(m['flags']&16))
        worlds[kind].append(world);owners[kind].append(np.full(len(world),i,dtype=np.int32))
    indexes=[]
    for w,o in zip(worlds,owners):
        if not w:indexes.append(None);continue
        w=np.concatenate(w);o=np.concatenate(o);k=key(w);sort=np.argsort(k)
        indexes.append((k[sort],w[sort],o[sort]));print('reference vertices',len(w),flush=True)
    unresolved=[];extra=[]
    for lname in ('INDUST','COMMER','SUBURB','UNDERG'):
        level=Level(a.rom,lname);new=0
        for item in record['unresolved']:
            if item['level']!=lname:continue
            if item['reason']=='source contains empty geometry':
                unresolved.append(item);continue
            rows=list({r['resource']:r for r in level.instances[item['worldId']]}.values())
            points=[]
            for r in rows:
                mat=np.asarray([r['matrix'][j:j+3] for j in (0,4,8)])
                for mesh in level.building(r['resource']):
                    verts=np.asarray([v[0] for v in mesh['vertices']]);points.append(verts @ mat+np.asarray(r['matrix'][12:15]))
            points=np.unique(np.concatenate(points),axis=0)
            if len(points)>300:points=points[np.linspace(0,len(points)-1,300,dtype=np.int32)]
            kind=0 if a.independent_pass else int(rows[0]['renderpass'] in (0,1));keys,world,owner=indexes[kind]
            cells=np.rint(points*10).astype(np.int64)+32768
            votes=collections.Counter();distances=collections.defaultdict(list)
            for pi,point in enumerate(points):
                hits=set()
                for dx in (-1,0,1):
                    for dy in (-1,0,1):
                        for dz in (-1,0,1):
                            x,y,z=cells[pi]+(dx,dy,dz);k=(x<<32)|(y<<16)|z
                            start,end=np.searchsorted(keys,k,side='left'),np.searchsorted(keys,k,side='right')
                            if end==start:continue
                            d=np.linalg.norm(world[start:end]-point,axis=1)
                            for oi,delta in zip(owner[start:end][d<=.03],d[d<=.03]):
                                m=g.models[g.entities[oi]['model']]
                                if not time_compatible(m,level,rows):continue
                                hits.add(int(oi));distances[int(oi)].append(float(delta))
                votes.update(hits)
            best=votes.most_common(5)
            fraction=best[0][1]/len(points) if best else 0
            second=best[1][1]/len(points) if len(best)>1 else 0
            if best and fraction>=.95 and fraction-second>=.2:
                e=g.entities[best[0][0]]
                extra.append(dict(level=lname,worldId=item['worldId'],model=e['model'],
                    entityPool=e['pool'],entityIndex=e['index'],resources=item['resources'],
                    matchedVertexFraction=fraction,runnerUpFraction=second,
                    maxVertexDistance=max(distances[best[0][0]]),swap=level.swaps.get(item['worldId']),
                    method='source vertex correspondence; optimized pass independent of model LOD bit' if a.independent_pass else 'source vertex correspondence'))
                new+=1
            else:
                item['vertexCandidates']=[dict(model=g.entities[i]['model'],name=g.models[g.entities[i]['model']]['name'],
                    entityPool=g.entities[i]['pool'],entityIndex=g.entities[i]['index'],fraction=n/len(points)) for i,n in best]
                unresolved.append(item)
        print(lname,'vertex links',new,flush=True)
    record['links'].extend(extra);record['unresolved']=unresolved
    a.output.write_text(json.dumps(record,indent=2),encoding='utf-8')
    print('linked',len(record['links']),'unresolved',len(unresolved),flush=True)


if __name__=='__main__':main()
