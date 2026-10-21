"""Compare compiled console placements with the source entity pools."""
import collections,json,pathlib,sys
root=pathlib.Path(__file__).resolve().parents[1]
data=json.loads((root/'reports/ps2-world-source.json').read_text())
models={r['id']:r for r in data['models']}
gas=[(p['name'],r) for p in data['pools'] for r in p['entries'] if r['model'] in (691,905)]
result=dict(gasEntities=gas,levels=[])
for level in data['levels']:
    instances=level['instances'];byid=collections.defaultdict(list)
    for r in instances:byid[r['id']].append(r)
    near=[]
    for pool,e in gas:
        pos=e['position']
        matches=[r for r in instances if sum((pos[i]-r['bound'][i])**2 for i in range(3))<10000]
        near.append(dict(pool=pool,entity=e,worldInstances=[r for r in matches[:40]],totalNear=len(matches)))
    result['levels'].append(dict(name=level['name'],sectors=len(level['sectors']),instances=len(instances),uniqueIds=len(byid),maxId=max(byid,default=-1),uniqueResources=len(level['resources']),nearGas=near))
print(json.dumps(result,indent=2))
