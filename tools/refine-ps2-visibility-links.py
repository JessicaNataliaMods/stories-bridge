"""Development-only owner validation using original visibility and fragments."""
import argparse,collections,csv,json,pathlib
import numpy as np
from ps2_world import Game,Level
from ps2_visibility import key,time_compatible


def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('rom',type=pathlib.Path);p.add_argument('links',type=pathlib.Path)
    p.add_argument('output',type=pathlib.Path);a=p.parse_args()
    root=pathlib.Path(__file__).resolve().parents[1]
    game=Game(a.rom,root/'vendor/librwgta/librwgta-master/tools/storiesconv/hashes.inc')
    levels={name:Level(a.rom,name) for name in ('INDUST','COMMER','SUBURB','UNDERG')}
    record=json.loads(a.links.read_text());entities={(e['pool'],e['index']):e for e in game.entities}
    reference={r['name'].lower():[float(r[k]) for k in ('minx','miny','minz','maxx','maxy','maxz')]
               for r in csv.DictReader((root/'reports/reference-model-bounds.csv').open(encoding='utf-8-sig'))}
    def verify_time_owner(level,world_id):
        rows=list({r['resource']:r for r in level.instances[world_id]}.values())
        points=[]
        for r in rows:
            matrix=np.asarray([r['matrix'][j:j+3] for j in (0,4,8)])
            for mesh in level.building(r['resource']):
                vertices=np.asarray([v[0] for v in mesh['vertices']])
                if len(vertices):points.append(vertices@matrix+np.asarray(r['matrix'][12:15]))
        if not points:return []
        points=np.concatenate(points);owners=[]
        for e in game.entities:
            m=game.models[e['model']];bounds=reference.get((m['name'] or '').lower())
            if 'time' not in m or not bounds or not time_compatible(m,level,rows):continue
            axes=np.asarray([e['matrix'][j:j+3] for j in (0,4,8)])
            local=(points-np.asarray(e['matrix'][12:15]))@axes.T
            error=float(np.max(np.abs(np.concatenate((local.min(axis=0),local.max(axis=0)))-bounds)))
            if error<=.03:owners.append((e,error))
        return owners
    repairs=[]
    for link in record['links']:
        level=levels[link['level']]
        if not time_compatible(game.models[link['model']],level,level.instances[link['worldId']]):
            owners=verify_time_owner(level,link['worldId'])
            if len(owners)!=1:raise ValueError('Cannot verify the proper time owner of an existing link')
            e,error=owners[0];repairs.append(dict(level=level.name,worldId=link['worldId'],oldModel=link['model'],model=e['model']))
            link.update(model=e['model'],entityPool=e['pool'],entityIndex=e['index'],boundsError=error,
                        method='original resource time predicate and verified geometry bounds')
    owners=collections.defaultdict(list)
    for link in record['links']:
        level=levels[link['level']]
        for row in level.instances[link['worldId']]:
            owners[level.name,row['bound'][0],row['bound'][1],row['renderpass'] in (0,1),key(level.conditions(row))].append((link,row))
    remaining=[];extra=[]
    for item in record['unresolved']:
        if item['reason']=='source contains empty geometry':remaining.append(item);continue
        level=levels[item['level']];rows=level.instances[item['worldId']]
        timed=any(off!=255 and off&128 for r in rows for off,on in level.conditions(r)['resources'])
        identity={}
        if timed:
            for e,error in verify_time_owner(level,item['worldId']):
                identity[e['model'],e['pool'],e['index']]=(e,error,'original resource time predicate and verified geometry bounds')
        if not identity:
            # A split part retains the source center and state even if its
            # radius is recomputed. Require one unique verified entity owner,
            # matching half-float X/Y exactly, Z within .03 and radius within 1.
            for row in rows:
                k=(level.name,row['bound'][0],row['bound'][1],row['renderpass'] in (0,1),key(level.conditions(row)))
                for anchor,other in owners.get(k,[]):
                    if abs(row['bound'][2]-other['bound'][2])>.03 or abs(row['bound'][3]-other['bound'][3])>1:continue
                    e=entities[anchor['entityPool'],anchor['entityIndex']]
                    identity[e['model'],e['pool'],e['index']]=(e,None,'split source center and visibility key')
        if len(identity)!=1:remaining.append(item);continue
        e,error,method=next(iter(identity.values()))
        extra.append(dict(level=level.name,worldId=item['worldId'],model=e['model'],entityPool=e['pool'],
                          entityIndex=e['index'],resources=item['resources'],method=method,boundsError=error))
    record['links'].extend(extra);record['unresolved']=remaining
    record['visibilityOwnerRepairs']=repairs
    a.output.write_text(json.dumps(record,indent=2),encoding='utf-8')
    print('Time-owner repairs:',len(repairs),'additional fragments:',len(extra),'unresolved:',len(remaining),flush=True)


if __name__=='__main__':main()
