"""Recover related story prototypes at verified original entity frames.

Near and LOD artist pivots need not be identical. Require an existing source
frame, a source related-model pointer, unique bounds and EVERY optimized vertex
matching the proposed original prototype. Never translate meshes by a COL box
or silently merge the near variant into its LOD.
"""
import argparse,csv,json,pathlib,struct
import numpy as np
from ps2_world import Game,Level
from ps2_visibility import time_compatible


def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('rom',type=pathlib.Path);p.add_argument('verified',type=pathlib.Path)
    p.add_argument('proposals',type=pathlib.Path);p.add_argument('output',type=pathlib.Path)
    a=p.parse_args();root=pathlib.Path(__file__).resolve().parents[1]
    g=Game(a.rom,root/'vendor/librwgta/librwgta-master/tools/storiesconv/hashes.inc')
    record=json.loads(a.verified.read_text());proposed=json.loads(a.proposals.read_text())
    known={(r['level'],r['worldId']) for r in record['links']}
    proposals=[r for r in proposed['links'] if (r['level'],r['worldId']) not in known]
    bounds={r['name'].lower():np.asarray([float(r[k]) for k in ('minx','miny','minz','maxx','maxy','maxz')])
            for r in csv.DictReader((root/'reports/reference-model-bounds.csv').open(encoding='utf-8-sig'))}
    raw=(root/'reports/reference-near-vertices.bin').read_bytes();position=8;reference={}
    if raw[:4]!=b'RGV1':raise ValueError('Invalid reference geometry')
    for _ in range(struct.unpack_from('<I',raw,4)[0]):
        name=raw[position:position+32].split(b'\0')[0].decode('ascii').lower()
        count=struct.unpack_from('<I',raw,position+32)[0];position+=36
        reference[name]=np.frombuffer(raw,dtype='<f4',count=count*3,offset=position).reshape(count,3).astype(np.float64);position+=count*12
    if position!=len(raw):raise ValueError('Unparsed reference geometry')
    extra=[];prototypes=[];levels={};diagnostics=[]
    for candidate in proposals:
        anchor=g.models[candidate['model']];related=anchor.get('related')
        if related is None:continue
        model=g.models[related];name=model['name'].lower()
        if name not in bounds or name not in reference:continue
        if candidate['level'] not in levels:levels[candidate['level']]=Level(a.rom,candidate['level'])
        level=levels[candidate['level']];rows=list({r['resource']:r for r in level.instances[candidate['worldId']]}.values())
        if not time_compatible(model,level,rows):continue
        points=[]
        for row in rows:
            axes=np.asarray([row['matrix'][j:j+3] for j in (0,4,8)])
            for mesh in level.building(row['resource']):
                if mesh['vertices']:points.append(np.asarray([v[0] for v in mesh['vertices']])@axes+np.asarray(row['matrix'][12:15]))
        points=np.unique(np.concatenate(points),axis=0);center=(points.min(axis=0)+points.max(axis=0))/2
        matches=[]
        for entity in g.entities:
            current=g.models[entity['model']]
            if current['type'] not in (1,3) or bool(current['flags']&16)!=bool(model['flags']&16):continue
            if np.linalg.norm(np.asarray(entity['matrix'][12:15])-center)>300:continue
            axes=np.asarray([entity['matrix'][j:j+3] for j in (0,4,8)])
            local=(points-np.asarray(entity['matrix'][12:15]))@axes.T
            error=float(np.max(np.abs(np.concatenate((local.min(axis=0),local.max(axis=0)))-bounds[name])))
            if error>.03:continue
            distances=[];ref=reference[name]
            for start in range(0,len(local),128):
                delta=local[start:start+128,None,:]-ref[None,:,:]
                distances.extend(np.sqrt(np.min(np.sum(delta*delta,axis=2),axis=1)).tolist())
            if all(d<=.03 for d in distances):matches.append((entity,error,max(distances)))
        diagnostics.append(dict(level=level.name,worldId=candidate['worldId'],prototype=model['id'],name=model['name'],
                                frameMatches=len(matches),referenceVerticesChecked=len(points),
                                matchingEntities=[dict(pool=e['pool'],index=e['index'],model=e['model']) for e,_b,_v in matches]))
        if not matches:continue
        # Equal source frames identify a definition, but not which of the
        # colocated physical entities should own a runtime story swap.
        frame_only=len(matches)!=1
        if frame_only:
            original=matches[0][0]['matrix']
            if any(max(abs(e['matrix'][j]-original[j]) for j in (0,1,2,4,5,6,8,9,10))>1e-6
                   or max(abs(e['matrix'][j]-original[j]) for j in (12,13,14))>.001
                   for e,_b,_v in matches):continue
            matches.sort(key=lambda r:(r[2],r[1],r[0]['pool'],r[0]['index']))
        entity,error,distance=matches[0]
        target=prototypes if frame_only else extra
        target.append(dict(level=level.name,worldId=candidate['worldId'],model=model['id'],
            entityPool=entity['pool'],entityIndex=entity['index'],sourceEntityModel=entity['model'],
            frameOnly=frame_only,frameAliases=[dict(pool=e['pool'],index=e['index'],model=e['model']) for e,_b,_v in matches],
            resources=candidate['resources'],variantFromRelatedLodModel=anchor['id'],
            boundsError=error,maxVertexDistance=distance,referenceVerticesChecked=len(points),referenceVerticesAllMatched=True,
            method='original related story prototype at a unique original source frame; every vertex verified'))
    linked={(r['level'],r['worldId']) for r in extra}
    record['links'].extend(extra);record['unresolved']=[r for r in record['unresolved'] if (r['level'],r['worldId']) not in linked]
    record['prototypeLinks']=prototypes
    record['storyPrototypeReview']=diagnostics
    a.output.write_text(json.dumps(record,indent=2),encoding='utf-8')
    print(json.dumps(dict(verifiedRelatedPrototypeLinks=extra,prototypeOnlyLinks=prototypes,review=diagnostics,unresolved=len(record['unresolved'])),indent=2))


if __name__=='__main__':main()
