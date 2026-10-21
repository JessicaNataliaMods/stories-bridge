"""Identify source empty resources by exact original half-float render bounds.

This development tool never substitutes visible models. Source resources must
decode to zero meshes and have one unique original entity owner. Ambiguous
helpers and unmatched geometry remain in the explicit coverage ledger.
"""
import argparse,collections,json,pathlib,struct
from ps2_world import Game,Level,transform


def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('rom',type=pathlib.Path);p.add_argument('links',type=pathlib.Path)
    p.add_argument('output',type=pathlib.Path);a=p.parse_args()
    root=pathlib.Path(__file__).resolve().parents[1]
    game=Game(a.rom,root/'vendor/librwgta/librwgta-master/tools/storiesconv/hashes.inc')
    levels={name:Level(a.rom,name) for name in ('INDUST','COMMER','SUBURB','UNDERG')}
    record=json.loads(a.links.read_text());owners=collections.defaultdict(list)
    for entity in game.entities:
        model=game.models[entity['model']];sphere=game.render_spheres.get(model['id'])
        if model['type'] not in (1,3) or sphere is None:continue
        encoded=struct.pack('<4e',*transform(entity['matrix'],sphere[:3]),sphere[3])
        owners[encoded,bool(model['flags']&16)].append(entity)
    extra=[];remaining=[]
    for item in record['unresolved']:
        if item['reason']!='source contains empty geometry':remaining.append(item);continue
        level=levels[item['level']];rows=level.instances[item['worldId']]
        if any(level.building(row['resource']) for row in rows):raise ValueError('Empty-link input contains geometry')
        candidates=None
        for row in rows:
            matches={(e['pool'],e['index']):e for e in owners.get(
                (struct.pack('<4e',*row['bound']),row['renderpass'] in (0,1)),[])}
            candidates=matches if candidates is None else {k:v for k,v in candidates.items() if k in matches}
        if not candidates or len(candidates)!=1:
            remaining.append(dict(**item,emptyBoundCandidates=len(candidates or {})));continue
        entity=next(iter(candidates.values()))
        extra.append(dict(level=level.name,worldId=item['worldId'],model=entity['model'],
                          entityPool=entity['pool'],entityIndex=entity['index'],resources=item['resources'],
                          emptySource=True,method='zero source meshes and unique exact original half-float render sphere'))
    record['links'].extend(extra);record['unresolved']=remaining
    a.output.write_text(json.dumps(record,indent=2),encoding='utf-8')
    print('Verified original empty resources:',len(extra),'unresolved:',len(remaining),flush=True)


if __name__=='__main__':main()
