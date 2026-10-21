"""Original PS2 visibility predicates and separate compiled render layers."""
import collections
import hashlib
import json


def key(rule):
    base=(tuple(rule['instance']) if rule['instance'] is not None else None,
            tuple(tuple(x) for x in rule['resources']),
            tuple(tuple(x) for x in rule['interiors']),
            tuple((tuple(tuple(c) for c in term['on']),tuple(tuple(c) for c in term['off']))
                  for term in rule.get('sectorAlternatives',[])))
    return (*base,rule['atomicLod']) if rule.get('atomicLod',0) else base


def predicate(condition,hour,states):
    off,on=condition
    if off==255:return False
    if off&128:
        off&=127
        if not 0<=off<24 or not 0<=on<24:raise ValueError('Invalid original time predicate')
        return on<=hour<off if on<off else hour>=on or hour<off
    return states.get(off,0)==on


def visible(rule,hour,states):
    if not 0<=hour<24:raise ValueError('Invalid clock hour')
    terms=rule.get('sectorAlternatives')
    selected=(not terms or any(all(states.get(slot,0)==state for slot,state in term['on'])
                              and all(states.get(slot,0)!=state for slot,state in term['off'])
                              for term in terms)) if terms is not None else (
        not rule['interiors'] or any(states.get(slot,0)==state for slot,state in rule['interiors']))
    return ((rule['instance'] is None or predicate(rule['instance'],hour,states))
            and (not rule['resources'] or any(predicate(x,hour,states) for x in rule['resources']))
            and selected)


def time_compatible(model,level,rows):
    """A source time model cannot own a different time model's resources."""
    time=model.get('time')
    if time is None:return True
    for row in rows:
        rule=level.conditions(row)
        for alternatives in ([rule['instance']] if rule['instance'] is not None else [],rule['resources']):
            timed=[(on,off&127) for off,on in alternatives if off!=255 and off&128]
            if timed and tuple(time) not in timed:return False
    return True


def layer_name(model,rule,primary=False):
    if primary:return model['name']
    if rule.get('atomicLod',0) and key({**rule,'atomicLod':0})==(None,(),(),()):
        name=model['name']+'_l'+str(rule['atomicLod'])
        if len(name)<=22:return name
    token=hashlib.sha256((str(model['id'])+json.dumps(rule,sort_keys=True)).encode()).hexdigest()[:6]
    # COL names allow 22 bytes; SA's text IPL loader has a 24-byte field.
    return model['name'][:15]+'_'+token


def assemblies(game,level,links):
    entities={(e['pool'],e['index']):e for e in game.entities}
    groups={}
    for link in links:
        if link['level']!=level.name:continue
        for row in level.instances[link['worldId']]:
            rule=level.conditions(row)
            lod=link.get('resourceLods',{}).get(str(row['resource']),0)
            if lod:rule['atomicLod']=lod
            identity=(link['model'],link['entityPool'],link['entityIndex'],key(rule),bool(link.get('frameOnly')))
            if identity not in groups:
                groups[identity]=dict(model=link['model'],entity=entities[link['entityPool'],link['entityIndex']],
                                      conditions=rule,rows=[],worldIds=set(),frameOnly=bool(link.get('frameOnly')))
            group=groups[identity];group['worldIds'].add(link['worldId']);group['rows'].append(row)
    bymodel=collections.defaultdict(list)
    for group in groups.values():
        # One source resource is repeated in residency copies/interior sectors.
        # Keep separate layers if predicates differ, never merge story states.
        group['rows']=list({r['resource']:r for r in group['rows']}.values())
        group['worldIds']=sorted(group['worldIds'])
        bymodel[group['model']].append(group)
    return bymodel


def select_layers(model,groups):
    byrule={}
    for group in groups:
        if not time_compatible(model,group['level'],group['rows']):
            raise ValueError('Verified entity has incompatible original time resources: '+model['name'])
        rule_key=key(group['conditions']);current=byrule.get(rule_key)
        # Use the most complete source instance for each shared render layer.
        # Every layer remains separate regardless of how many fragments it has.
        if current is None or len(group['rows'])>len(current['rows']):byrule[rule_key]=group
    ordered=sorted(byrule.values(),key=lambda g:(key(g['conditions'])!=(None,(),(),()),
        not visible(g['conditions'],12,{}),json.dumps(g['conditions'],sort_keys=True)))
    for i,group in enumerate(ordered):
        yield dict(**group,name=layer_name(model,group['conditions'],i==0),primary=i==0)
