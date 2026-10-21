"""Review source identities without equating PS2 pass and model LOD flag.

The optimized sector pass and CSimpleModelInfo's big-building bit are separate
source fields. Require a unique entity with exact transformed reference bounds
and compatible original time predicates. Reference bounds identify only names;
all generated geometry and pixels still come from the user's PS2 files.
"""
import argparse,collections,csv,json,math,pathlib
import numpy as np
from ps2_world import Game,Level,transform
from ps2_visibility import time_compatible


def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('rom',type=pathlib.Path);p.add_argument('links',type=pathlib.Path)
    p.add_argument('output',type=pathlib.Path);a=p.parse_args()
    root=pathlib.Path(__file__).resolve().parents[1]
    g=Game(a.rom,root/'vendor/librwgta/librwgta-master/tools/storiesconv/hashes.inc')
    bounds={r['name'].lower():np.asarray([float(r[k]) for k in ('minx','miny','minz','maxx','maxy','maxz')])
            for r in csv.DictReader((root/'reports/reference-model-bounds.csv').open(encoding='utf-8-sig'))}
    centers=collections.defaultdict(list)
    for e in g.entities:
        m=g.models[e['model']];b=bounds.get((m['name'] or '').lower())
        if m['type'] not in (1,3) or b is None:continue
        center=transform(e['matrix'],(b[:3]+b[3:])/2)
        centers[tuple(round(x/10) for x in center)].append((e,center,b))
    record=json.loads(a.links.read_text());extra=[];remaining=[];diagnostics=[]
    for lname in ('INDUST','COMMER','SUBURB','UNDERG'):
        level=Level(a.rom,lname)
        for item in record['unresolved']:
            if item['level']!=lname:continue
            if item['reason']=='source contains empty geometry':remaining.append(item);continue
            rows=list({r['resource']:r for r in level.instances[item['worldId']]}.values());points=[]
            for r in rows:
                axes=np.asarray([r['matrix'][j:j+3] for j in (0,4,8)])
                for mesh in level.building(r['resource']):
                    if mesh['vertices']:points.append(np.asarray([v[0] for v in mesh['vertices']])@axes+np.asarray(r['matrix'][12:15]))
            if not points:raise ValueError('Nonempty input has no geometry')
            points=np.concatenate(points);seeds=(rows[0]['matrix'][12:15],(points.min(axis=0)+points.max(axis=0))/2)
            candidates={}
            for seed in seeds:
                cell=tuple(round(x/10) for x in seed)
                for dx in (-1,0,1):
                    for dy in (-1,0,1):
                        for dz in (-1,0,1):
                            for e,center,b in centers.get((cell[0]+dx,cell[1]+dy,cell[2]+dz),[]):
                                if math.dist(center,seed)<15:candidates[e['pool'],e['index']]=(e,b)
            matches=[]
            for e,b in candidates.values():
                model=g.models[e['model']]
                if not time_compatible(model,level,rows):continue
                axes=np.asarray([e['matrix'][j:j+3] for j in (0,4,8)])
                local=(points-np.asarray(e['matrix'][12:15]))@axes.T
                error=float(np.max(np.abs(np.concatenate((local.min(axis=0),local.max(axis=0)))-b)))
                if error<=.03:matches.append((e,error))
            if len(matches)!=1:
                remaining.append(item)
                if matches:diagnostics.append(dict(level=lname,worldId=item['worldId'],
                    candidates=[dict(model=e['model'],name=g.models[e['model']]['name'],pool=e['pool'],index=e['index'],error=error) for e,error in matches]))
                continue
            e,error=matches[0]
            extra.append(dict(level=lname,worldId=item['worldId'],model=e['model'],entityPool=e['pool'],entityIndex=e['index'],
                resources=item['resources'],boundsError=error,method='unique exact source bounds and time predicates; optimized pass independent of model LOD bit'))
        print(lname,'new full-bound identities',sum(x['level']==lname for x in extra),flush=True)
    record['links'].extend(extra);record['unresolved']=remaining;record['passReviewAmbiguities']=diagnostics
    record['geometryVerified']=False
    record['verificationStatus']='proposed full-bound associations; independent vertex verification required before conversion'
    a.output.write_text(json.dumps(record,indent=2),encoding='utf-8')
    print('Additional source identities:',len(extra),'unresolved:',len(remaining),flush=True)


if __name__=='__main__':main()
