"""Independently verify every vertex of newly proposed source associations."""
import argparse,json,pathlib,struct
import numpy as np
from ps2_world import Game,Level


def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('rom',type=pathlib.Path);p.add_argument('before',type=pathlib.Path)
    p.add_argument('after',type=pathlib.Path);p.add_argument('report',type=pathlib.Path)
    p.add_argument('--related',action='store_true',help='Audit the original related near prototype, without assigning its runtime placement')
    a=p.parse_args();root=pathlib.Path(__file__).resolve().parents[1]
    g=Game(a.rom,root/'vendor/librwgta/librwgta-master/tools/storiesconv/hashes.inc')
    prior=json.loads(a.before.read_text());after=json.loads(a.after.read_text())
    known={(r['level'],r['worldId']) for r in prior['links']}
    extra=[r for r in after['links'] if (r['level'],r['worldId']) not in known]
    raw=(root/'reports/reference-near-vertices.bin').read_bytes();position=8;reference={}
    if raw[:4]!=b'RGV1':raise ValueError('Invalid development vertex metadata')
    for _ in range(struct.unpack_from('<I',raw,4)[0]):
        name=raw[position:position+32].split(b'\0')[0].decode('ascii').lower()
        count=struct.unpack_from('<I',raw,position+32)[0];position+=36
        reference[name]=np.frombuffer(raw,dtype='<f4',count=count*3,offset=position).reshape(count,3).astype(np.float64)
        position+=count*12
    if position!=len(raw):raise ValueError('Unparsed reference vertex bytes')
    entities={(e['pool'],e['index']):e for e in g.entities};levels={};results=[]
    for link in extra:
        level=levels.setdefault(link['level'],None)
        if level is None:level=levels[link['level']]=Level(a.rom,link['level'])
        entity=entities[link['entityPool'],link['entityIndex']];model=g.models[link['model']]
        anchor_model=model['id']
        if a.related:
            if model.get('related') is None:raise ValueError('Source anchor lacks an original related prototype')
            model=g.models[model['related']]
        points=[]
        for row in {r['resource']:r for r in level.instances[link['worldId']]}.values():
            axes=np.asarray([row['matrix'][i:i+3] for i in (0,4,8)])
            for mesh in level.building(row['resource']):
                if mesh['vertices']:points.append(np.asarray([v[0] for v in mesh['vertices']])@axes+np.asarray(row['matrix'][12:15]))
        axes=np.asarray([entity['matrix'][i:i+3] for i in (0,4,8)])
        local=np.unique((np.concatenate(points)-np.asarray(entity['matrix'][12:15]))@axes.T,axis=0)
        shift=np.zeros(3)
        if a.related:
            # The original near and LOD prototypes can have different artist
            # pivots. Recover the near pivot from its resident PS2 render box,
            # never from reference vertices or by assuming equal origins.
            cp=model['colPointer']
            mn=np.asarray(g.chunk.read('<3f',cp+16));mx=np.asarray(g.chunk.read('<3f',cp+32))
            shift=(local.min(axis=0)+local.max(axis=0)-mn-mx)/2
            local=local-shift
        ref=reference[model['name'].lower()];distances=[]
        for start in range(0,len(local),128):
            delta=local[start:start+128,None,:]-ref[None,:,:]
            distances.extend(np.sqrt(np.min(np.sum(delta*delta,axis=2),axis=1)).tolist())
        fraction=sum(d<=.03 for d in distances)/len(distances)
        results.append(dict(level=level.name,worldId=link['worldId'],model=model['id'],name=model['name'],anchorModel=anchor_model,
                            sourceVertices=len(distances),matchedFraction=fraction,maximumDistance=max(distances),
                            pivotShiftFromSourceBounds=shift.tolist(),
                            allVerticesMatch=fraction==1))
    result=dict(gameExecuted=False,newAssociations=len(extra),allNewVerticesMatch=all(r['allVerticesMatch'] for r in results),
                relatedPrototypeAudit=a.related,runtimePlacementVerified=False,records=results)
    a.report.write_text(json.dumps(result,indent=2),encoding='utf-8');print(json.dumps(result,indent=2))
    if not result['allNewVerticesMatch']:raise ValueError('New association does not match every original reference vertex')


if __name__=='__main__':main()
