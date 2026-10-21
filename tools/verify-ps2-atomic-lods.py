"""Development-only LOD identity audit. References provide no exported geometry."""
import collections,importlib.util,json,pathlib,re,struct,sys
import numpy as np
from ps2_world import Game,Level,transform
from ps2_export import inverse_entity

root=pathlib.Path(__file__).resolve().parents[1]
spec=importlib.util.spec_from_file_location('topology',root/'tools/audit-ps2-model-topology.py')
top=importlib.util.module_from_spec(spec);spec.loader.exec_module(top)


def references(path):
    raw=path.read_bytes();end=12+struct.unpack_from('<I',raw,4)[0]
    frames=[];atomics=[]
    for ident,start,stop,_ in top.children(raw,12,end):
        if ident==14:
            parts=list(top.children(raw,start,stop))
            for ident,s,e,_ in parts[1:]:
                name=''
                if ident!=3:raise ValueError('Unexpected frame extension')
                for plugin,p,q,_ in top.children(raw,s,e):
                    if plugin==0x253f2fe:name=raw[p:q].split(b'\0')[0].decode('ascii')
                frames.append(name)
        if ident==20:
            _,p,q,_=next(top.children(raw,start,stop));frame,geometry=struct.unpack_from('<2I',raw,p)
            atomics.append((frame,geometry))
    geos=top.geometries(path);result={}
    for frame,geometry in atomics:
        match=re.search(r'_l([0-2])$',frames[frame],re.I)
        if not match:raise ValueError('Reference atomic lacks an explicit LOD name: '+frames[frame])
        lod=int(match[1]);points,faces=geos[geometry]
        canonical,inverse=np.unique(points,axis=0,return_inverse=True)
        triangles={top.oriented(tuple(int(inverse[i]) for i in face)) for face in faces if len(set(inverse[list(face)]))==3}
        result[lod]=(canonical,triangles)
    return result


def main():
    before,after,report=map(pathlib.Path,sys.argv[1:4])
    data=json.loads(before.read_text());g=Game(root / 'local/originals/lcs-ps2',root/'vendor/librwgta/librwgta-master/tools/storiesconv/hashes.inc')
    levels={};refs={};cache={};records=[];unmatched=[]
    entities={(e['pool'],e['index']):e for e in g.entities}
    for link in data['links']+data.get('prototypeLinks',[]):
        model=g.models[link['model']]
        if model.get('atomics',0)<2 or model['streamSize']:continue
        name=model['name'];ref=refs.setdefault(name,None)
        if ref is None:ref=refs[name]=references(root/'out/img-v1'/(name+'.dff'))
        lname=link['level'];level=levels.setdefault(lname,None)
        if level is None:level=levels[lname]=Level(root / 'local/originals/lcs-ps2',lname)
        entity=entities[link['entityPool'],link['entityIndex']];mapping={}
        for row in {r['resource']:r for r in level.instances[link['worldId']]}.values():
            key=lname,model['id'],row['resource']
            if key not in cache:
                points=[];faces=[]
                for mesh in level.building(row['resource']):
                    start=len(points)
                    points.extend(inverse_entity(entity['matrix'],transform(row['matrix'],v[0])) for v in mesh['vertices'])
                    faces.extend(tuple(start+i for i in f) for f in mesh['triangles'])
                if not faces:continue
                points=np.asarray(points);candidates=[];maximum={}
                for lod,(original,triangles) in ref.items():
                    remap=[];distances=[]
                    for start in range(0,len(points),64):
                        squared=np.sum((points[start:start+64,None,:]-original[None,:,:])**2,axis=2)
                        nearest=np.argmin(squared,axis=1);remap.extend(nearest.tolist())
                        distances.extend(np.sqrt(squared[np.arange(len(nearest)),nearest]).tolist())
                    maximum[lod]=max(distances)
                    if max(distances)>.03:continue
                    if all(top.oriented(tuple(remap[i] for i in f)) in triangles or len({remap[i] for i in f})<3 for f in faces):candidates.append(lod)
                if not candidates:
                    unmatched.append(dict(level=lname,model=model['id'],name=name,worldId=link['worldId'],resource=row['resource'],distances=maximum))
                    continue
                chosen=min(candidates);cache[key]=chosen
                records.append(dict(level=lname,model=model['id'],name=name,worldId=link['worldId'],resource=row['resource'],atomicLod=chosen,
                                    triangles=len(faces),matchingAtomicLods=candidates,maximumDistance=maximum[chosen]))
            if key in cache:mapping[str(row['resource'])]=cache[key]
        if mapping:link['resourceLods']=mapping
    result=dict(referenceGeometryCopied=False,gameExecuted=False,resources=len(records),links=sum('resourceLods' in r for r in data['links']),
                sourceTriangleWindingVerified=True,unmatched=unmatched,records=records)
    report.write_text(json.dumps(result,indent=2),encoding='utf-8')
    if unmatched:raise ValueError(f'{len(unmatched)} resources do not match a reference atomic; see {report}')
    data['atomicLodsVerified']=True
    after.write_text(json.dumps(data,indent=2),encoding='utf-8')
    print(json.dumps({k:v for k,v in result.items() if k!='records'},indent=2))


if __name__=='__main__':main()
