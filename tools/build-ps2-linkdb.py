"""Development-only linker: emits metadata, never copies reference assets.

Reference DFF bounds are only candidate seeds. The source catalog, LOD pass,
world position and transformed PS2 geometry bounds are verified together.
Ambiguous/unmatched links remain explicit and block a complete installation.
"""
import argparse, collections, csv, json, math, pathlib
import numpy as np
from ps2_world import Game, Level, transform
from ps2_export import inverse_entity


def main():
    p=argparse.ArgumentParser()
    p.add_argument('rom',type=pathlib.Path)
    p.add_argument('reference_bounds',type=pathlib.Path)
    p.add_argument('output',type=pathlib.Path)
    a=p.parse_args()
    root=pathlib.Path(__file__).resolve().parents[1]
    g=Game(a.rom,root/'vendor/librwgta/librwgta-master/tools/storiesconv/hashes.inc')
    bounds={r['name'].lower():[float(r[k]) for k in ('minx','miny','minz','maxx','maxy','maxz')]
            for r in csv.DictReader(a.reference_bounds.open(encoding='utf-8-sig'))}
    center_index=collections.defaultdict(list)
    for e in g.entities:
        m=g.models[e['model']];b=bounds.get((m['name'] or '').lower())
        if m['type'] not in (1,3) or not b:continue
        e['renderCenter']=transform(e['matrix'],[(b[j]+b[j+3])/2 for j in range(3)])
        center_index[tuple(round(x/10) for x in e['renderCenter'])].append(e)
    output=dict(format=1,source='LCS PS2 sector links; geometry verified against development reference bounds',
                gameExecuted=False,links=[],unresolved=[])
    for levelname in ('INDUST','COMMER','SUBURB','UNDERG'):
        level=Level(a.rom,levelname)
        hits=0
        for id,rows in sorted(level.instances.items()):
            # The same object can occur in a swapped interior and ordinary
            # sectors. Geometry resources with distinct IDs are retained.
            byresource={r['resource']:r for r in rows}
            rows=list(byresource.values())
            first=rows[0];pos=first['matrix'][12:15]
            candidates=[]
            is_lod=first['renderpass'] in (0,1)
            # Decode once and compare each candidate in its original local
            # coordinate system; this also verifies rotation, height and scale.
            allworld=[]
            for row in rows:
                for mesh in level.building(row['resource']):
                    allworld.extend(transform(row['matrix'],v[0]) for v in mesh['vertices'])
            if not allworld:
                output['unresolved'].append(dict(level=levelname,worldId=id,
                    resources=sorted(byresource),reason='source contains empty geometry'))
                continue
            world_min=[min(v[j] for v in allworld) for j in range(3)]
            world_max=[max(v[j] for v in allworld) for j in range(3)]
            seeds=(pos,[(a+b)/2 for a,b in zip(world_min,world_max)])
            seen=set()
            for seed in seeds:
                key=tuple(round(x/10) for x in seed)
                for dx in (-1,0,1):
                    for dy in (-1,0,1):
                        for dz in (-1,0,1):
                            for e in center_index.get((key[0]+dx,key[1]+dy,key[2]+dz),[]):
                                identity=e['pool'],e['index']
                                if identity in seen:continue
                                m=g.models[e['model']]
                                if bool(m['flags'] & 16) != is_lod:continue
                                dist=min(math.dist(s,e['renderCenter']) for s in seeds)
                                if dist<15:
                                    candidates.append((dist,e));seen.add(identity)
            world_array=np.asarray(allworld,dtype=np.float64)
            scored=[]
            for dist,e in candidates:
                # Exact extrema require original vertices for rotated models.
                axes=np.asarray([e['matrix'][i:i+3] for i in (0,4,8)])
                local=(world_array-np.asarray(e['matrix'][12:15])) @ axes.T
                actual=np.concatenate((local.min(axis=0),local.max(axis=0))).tolist()
                ref=bounds[g.models[e['model']]['name'].lower()]
                error=max(abs(x-y) for x,y in zip(actual,ref))
                scored.append((error,dist,e,actual))
            scored.sort(key=lambda t:(t[0],t[1]))
            best=scored[0] if scored else None
            # Sub-centimeter quantization allowance, plus modest DFF text
            # precision. Never silently choose between different model IDs.
            good=[r for r in scored if r[0] <= .03]
            models={r[2]['model'] for r in good}
            if len(models)==1:
                error,dist,e,actual=good[0]
                output['links'].append(dict(level=levelname,worldId=id,model=e['model'],
                    entityPool=e['pool'],entityIndex=e['index'],resources=sorted(byresource),
                    boundsError=error,centerDistance=dist,swap=level.swaps.get(id)))
                hits+=1
            else:
                output['unresolved'].append(dict(level=levelname,worldId=id,lod=is_lod,
                    resources=sorted(byresource),worldBounds=world_min+world_max,
                    best=[dict(model=r[2]['model'],name=g.models[r[2]['model']]['name'],
                               entityPool=r[2]['pool'],entityIndex=r[2]['index'],
                               boundsError=r[0],centerDistance=r[1]) for r in scored[:4]],
                    reason='ambiguous' if len(models)>1 else 'no verified bounds match'))
        print(f'{levelname}: verified={hits}, total={len(level.instances)}',flush=True)
    a.output.write_text(json.dumps(output,indent=2),encoding='utf-8')
    print(f'links={len(output["links"])} unresolved={len(output["unresolved"])}',flush=True)


if __name__=='__main__':main()
