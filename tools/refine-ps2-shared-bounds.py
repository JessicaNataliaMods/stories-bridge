"""Resolve console render fragments using identical original bound/state keys.

Names come from previously verified source entity links. No geometry or pixels
from a reference game are copied. Ambiguous owner keys remain unresolved.
"""
import argparse,collections,json,pathlib
from ps2_world import Level


def keys(level,world_id):
    swap=level.swaps.get(world_id)
    return {(tuple(row['bound']),row['renderpass'] in (0,1),row['interior'],swap)
            for row in level.instances[world_id]}


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('rom',type=pathlib.Path);parser.add_argument('links',type=pathlib.Path)
    parser.add_argument('output',type=pathlib.Path);args=parser.parse_args()
    record=json.loads(args.links.read_text());levels={n:Level(args.rom,n) for n in ('INDUST','COMMER','SUBURB','UNDERG')}
    owners=collections.defaultdict(dict)
    for link in record['links']:
        identity=(link['model'],link['entityPool'],link['entityIndex'])
        for key in keys(levels[link['level']],link['worldId']):owners[key][identity]=link
    remaining=[];extra=[]
    for item in record['unresolved']:
        if item['reason']=='source contains empty geometry':remaining.append(item);continue
        candidates={}
        for key in keys(levels[item['level']],item['worldId']):candidates.update(owners.get(key,{}))
        if len(candidates)!=1:remaining.append(item);continue
        (model,pool,index),anchor=next(iter(candidates.items()))
        extra.append(dict(level=item['level'],worldId=item['worldId'],model=model,entityPool=pool,
                          entityIndex=index,resources=item['resources'],swap=levels[item['level']].swaps.get(item['worldId']),
                          method='identical source bound/lod/interior/swap key',
                          anchorLevel=anchor['level'],anchorWorldId=anchor['worldId']))
    record['links'].extend(extra);record['unresolved']=remaining
    args.output.write_text(json.dumps(record,indent=2),encoding='utf-8')
    print('Additional source fragments:',len(extra),'remaining:',len(remaining),flush=True)


if __name__=='__main__':main()
