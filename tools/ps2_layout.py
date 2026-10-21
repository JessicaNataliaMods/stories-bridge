"""Source entity transforms, LOD graph and an explicitly incomplete SA review."""
import collections,json,math,pathlib
from ps2_world import transform
from ps2_visibility import visible

# Exact models and level choices in LCS CStreaming::RequestBigBuildings /
# RemoveIslandsNotUsed. These are separate authored island views, not one
# generated mega-mesh. Their source keys are checked by Game's name database.
ISLAND_LEVELS = {'islandlodind':(2,3), 'islandlodcomind':(1,),
                 'islandlodcomsub':(3,), 'islandlodsubind':(1,), 'islandlodsubcom':(2,)}


def level_at(game, position):
    return next((z['level'] for z in game.map_zones[1:]
                 if all(z['bounds'][j]<=position[j]<=z['bounds'][j+3] for j in range(3))),
                game.map_zones[0]['level'])


def quaternion(basis):
    # Source basis vectors are matrix columns; SA's text IPL loader conjugates
    # the quaternion before SetRotate, or derives heading with the same sign.
    if any(abs(sum(a*b for a,b in zip(basis[i],basis[j]))-(i==j))>.001
           for i in range(3) for j in range(3)):raise ValueError('Unsupported scaled/sheared source entity')
    r=[[basis[j][i] for j in range(3)] for i in range(3)]
    det=sum(r[0][i]*(r[1][(i+1)%3]*r[2][(i+2)%3]-r[1][(i+2)%3]*r[2][(i+1)%3]) for i in range(3))
    if abs(det-1)>.001:raise ValueError('Unsupported reflected source entity')
    tr=sum(r[i][i] for i in range(3))
    if tr>0:
        s=math.sqrt(tr+1)*2
        q=[(r[2][1]-r[1][2])/s,(r[0][2]-r[2][0])/s,(r[1][0]-r[0][1])/s,s/4]
    else:
        i=max(range(3),key=lambda i:r[i][i]);j=(i+1)%3;k=(i+2)%3
        s=math.sqrt(max(0,1+r[i][i]-r[j][j]-r[k][k]))*2
        q=[0.,0.,0.,(r[k][j]-r[j][k])/s];q[i]=s/4;q[j]=(r[i][j]+r[j][i])/s;q[k]=(r[i][k]+r[k][i])/s
    length=math.sqrt(sum(x*x for x in q));q=[x/length for x in q]
    if q[3]<0:q=[-x for x in q]
    return [-q[0],-q[1],-q[2],q[3]]


def sa_flags(flags):
    # GAME.DTZ stores CSimpleModelInfo bitfields, not source IDE flags.
    pairs=((2,1),(6,4),(7,8),(10,64),(11,128),(13,512),(14,1024))
    return sum(sa for bit,sa in pairs if flags&(1<<bit))


def lod_graph(game):
    bymodel=collections.defaultdict(list)
    for e in game.entities:bymodel[e['model']].append(e)
    def sphere(entity):
        s=game.render_spheres.get(entity['model'])
        return (transform(entity['matrix'],s[:3]),s[3]) if s else (entity['matrix'][12:15],0.)
    pairs=[];pending=[];claimed={}
    for lod in game.entities:
        model=game.models[lod['model']]
        if not model.get('flags',0)&16:continue
        related=model.get('related')
        if related is None:
            pending.append(dict(sourcePool=lod['pool'],sourceIndex=lod['index'],model=lod['model'],reason='source LOD has no related model'))
            continue
        center,radius=sphere(lod);scored=[]
        for near in bymodel[related]:
            if near['area']!=lod['area']:continue
            nc,nr=sphere(near);distance=math.dist(center,nc)
            if distance>radius+nr+1:continue
            scored.append((distance,near))
        scored.sort(key=lambda r:r[0])
        # Identity comes from the original related-model pointer. Spatial
        # checks select the instance, with no model-name substring guessing.
        good=(len(scored)==1 or (scored and scored[0][0]<1 and
                               (len(scored)==1 or scored[1][0]>scored[0][0]+1)))
        if not good:
            pending.append(dict(sourcePool=lod['pool'],sourceIndex=lod['index'],model=lod['model'],related=related,
                                reason='no unique related instance with matching render bounds',candidates=len(scored)))
            continue
        distance,near=scored[0];identity=near['pool'],near['index']
        if identity in claimed:
            pending.append(dict(sourcePool=lod['pool'],sourceIndex=lod['index'],model=lod['model'],related=related,
                                reason='related instance already has a different LOD parent'))
            continue
        claimed[identity]=(lod['pool'],lod['index'])
        pairs.append(dict(nearPool=near['pool'],nearIndex=near['index'],nearModel=near['model'],
                          lodPool=lod['pool'],lodIndex=lod['index'],lodModel=lod['model'],renderCenterDistance=distance))
    return dict(sourceLodInstances=sum(bool(game.models[e['model']].get('flags',0)&16) for e in game.entities),
                pairs=pairs,pending=pending,gameExecuted=False)


def partitions(rows,maximum=4000):
    parents=list(range(len(rows)))
    def find(i):
        while parents[i]!=i:parents[i]=parents[parents[i]];i=parents[i]
        return i
    for i,row in enumerate(rows):
        target=row['lod']
        if target==-1:continue
        if target<0 or target>=len(rows) or target==i:raise ValueError('Invalid LOD index')
        a,b=find(i),find(target);parents[b]=a
    groups=collections.defaultdict(list)
    for i in range(len(rows)):groups[find(i)].append(i)
    result=[];current=[]
    for group in sorted(groups.values(),key=lambda g:g[0]):
        if len(group)>maximum:raise ValueError('LOD component exceeds native IPL capacity')
        if len(group)+len(current)>maximum:result.append(current);current=[]
        current.extend(group)
    if current:result.append(current)
    return result


def export_review(game,package,models,layers,placements):
    directory=package/'layout-review';directory.mkdir()
    graph=lod_graph(game);assets=list(models.values())+layers
    definitions={r['name']:dict(**r,saId=10000+i) for i,r in enumerate(assets)}
    if 10000+len(assets)>20000:raise ValueError('Native model capacity exceeded')
    rows=[];deferred=[]
    for placement in placements:
        source=game.models[placement['sourceModel']]
        # Review snapshot only. For original time models leave the clock to
        # native SA TOBJ, while still checking the original story predicates.
        hour=source['time'][0] if 'time' in source else 12
        if not visible(placement['conditions'],hour,{}):deferred.append(placement);continue
        definition=definitions[placement['asset']]
        rows.append(dict(**placement,saId=definition['saId'],quaternion=quaternion(placement['basis']),lod=-1))
    byentity=collections.defaultdict(list)
    for i,row in enumerate(rows):byentity[row['sourcePool'],row['sourceIndex']].append(i)
    native_pairs=[]
    # Artist L0/L1/L2 atomics compiled into different world IDs are distinct
    # native entities. Link them only within the same original placement.
    for indices in byentity.values():
        by_lod=collections.defaultdict(list)
        for i in indices:by_lod[rows[i]['conditions'].get('atomicLod',0)].append(i)
        for lod in sorted(by_lod):
            parents=by_lod.get(lod+1,[])
            if len(parents)!=1:continue
            for i in by_lod[lod]:rows[i]['lod']=parents[0];native_pairs.append((i,parents[0]))
    for pair in graph['pairs']:
        near=byentity.get((pair['nearPool'],pair['nearIndex']),[])
        lod=byentity.get((pair['lodPool'],pair['lodIndex']),[])
        # More than one LOD layer needs a render-layer dependency graph.
        # Keep it explicit instead of selecting an arbitrary native parent.
        if not near or len(lod)!=1:continue
        for i in near:
            if rows[i]['lod']!=-1:continue
            rows[i]['lod']=lod[0];native_pairs.append((i,lod[0]))
    ide=[]
    for section in ('objs','tobj'):
        ide.append(section)
        for name,definition in definitions.items():
            source=game.models[definition.get('sourceModel',next((i for i,r in models.items() if r['name']==name),-1))]
            timed='time' in source
            if timed!=(section=='tobj'):continue
            atomic_lod=definition.get('conditions',{}).get('atomicLod',0)
            draw=abs(source['draw'][atomic_lod])
            values=[definition['saId'],name,definition['txd'],draw,sa_flags(source['flags'])]
            if timed:values.extend(source['time'])
            ide.append(', '.join(str(x) for x in values))
        ide.append('end')
    ide.append('txdp')
    for txd in sorted({r['txd'] for r in assets}):
        if txd!='generic':ide.append(txd+', generic')
    ide.append('end');(directory/'world.ide').write_text('\n'.join(ide)+'\n',encoding='ascii')
    ranges=[]
    for name,definition in definitions.items():
        source=game.models[definition.get('sourceModel',next((i for i,r in models.items() if r['name']==name),-1))]
        atomic_lod=definition.get('conditions',{}).get('atomicLod',0)
        is_lod=bool(source['flags']&16) or atomic_lod>0
        related=models.get(source.get('related'),{})
        near_id=definitions[related['name']]['saId'] if related else -1
        if atomic_lod:near_id=definitions[models[source['id']]['name']]['saId']
        levels=ISLAND_LEVELS.get(source['name'].lower())
        mask=sum(1<<i for i in levels) if levels else 31
        minimum=source['draw'][atomic_lod-1] if atomic_lod else source['draw'][2] if is_lod else 0
        has_shapes=False
        if definition.get('primary',True) and not is_lod and source['id'] in game.collisions:
            c,p=game.collisions[source['id']];has_shapes=any(c.read('<3h',p+48))
        ranges.append([definition['saId'],int(is_lod),minimum,near_id,mask,int(has_shapes)])
    (directory/'render-ranges.tsv').write_text(''.join('\t'.join(map(str,r))+'\n' for r in ranges),encoding='ascii')
    (directory/'island-zones.tsv').write_text(''.join('\t'.join(map(str,[z['level'],*z['bounds']]))+'\n' for z in game.map_zones),encoding='ascii')
    files=[]
    for part,indices in enumerate(partitions(rows)):
        local={source:i for i,source in enumerate(indices)};lines=['inst']
        for i in indices:
            row=rows[i];lod=local[row['lod']] if row['lod']!=-1 else -1
            values=[row['saId'],row['asset'],row['area'],*row['position'],*row['quaternion'],lod]
            lines.append(', '.join(str(x) for x in values))
        lines.append('end');name=f'world{part}.ipl';(directory/name).write_text('\n'.join(lines)+'\n',encoding='ascii')
        files.append(dict(file=name,instances=len(indices)))
    placed={(p['sourcePool'],p['sourceIndex']) for p in placements}
    missing=[dict(pool=e['pool'],index=e['index'],model=e['model'],name=game.models[e['model']]['name'])
             for e in game.entities if (e['pool'],e['index']) not in placed]
    report=dict(complete=False,installed=False,gameExecuted=False,sourceEntities=len(game.entities),
                sourcePlacements=len(placements),sourceEntitiesWithoutPlacement=missing,
                reviewSnapshot=dict(hour=12,storyStates='all zero; not verified as MAIN startup',instances=len(rows),
                                    deferredLayers=len(deferred),files=files,nativeLodLinks=len(native_pairs)),
                lodGraph=graph,modelDefinitions=len(definitions),
                remaining='Resolve missing source geometry and LOD range groups; attach story-state layers to the campaign bridge before installation')
    (directory/'manifest.json').write_text(json.dumps(report,indent=2),encoding='utf-8')
    return report
