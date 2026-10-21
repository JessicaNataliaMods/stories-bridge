"""Record map-layer evidence and compare physical COLs with the prior export."""
import argparse,collections,hashlib,json,pathlib,struct


def collisions(path):
    raw=path.read_bytes();position=0;entries={}
    while position<len(raw):
        if raw[position:position+4]!=b'COL2':raise ValueError('Expected COL2')
        size=struct.unpack_from('<I',raw,position+4)[0]+8
        if size<108 or position+size>len(raw):raise ValueError('Invalid COL2 extent')
        name=raw[position+8:position+30].split(b'\0')[0].decode('ascii')
        if name in entries:raise ValueError('Duplicate COL2 name')
        entries[name]=hashlib.sha256(raw[position:position+size]).hexdigest();position+=size
    return entries


def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('package',type=pathlib.Path);p.add_argument('prior',type=pathlib.Path)
    p.add_argument('report',type=pathlib.Path);a=p.parse_args()
    m=json.loads((a.package/'manifest.json').read_text());layers=list(m['models'].values())+m['renderLayers']
    luigi=[x for x in layers if x.get('sourceModel')==3754]
    state1=next(x for x in luigi if 4597 in x['resources']);state2=next(x for x in luigi if 4600 in x['resources'])
    if state1['name']==state2['name'] or 4600 in state1['resources'] or 4597 in state2['resources']:
        raise ValueError('Original Luigi club states were merged')
    first=collisions(a.prior/'world.col');second=collisions(a.package/'world.col')
    if any(second.get(name)!=value for name,value in first.items()):raise ValueError('Original physical collision data changed')
    texture_names=json.loads((a.package/'texture-names.json').read_text())
    if any(not r['outputName'] or r['outputName'].startswith(('tex_indust_','tex_commer_','tex_suburb_','tex_underg_'))
           for r in texture_names['records']):raise ValueError('Missing/provisional texture name')
    layout=json.loads((a.package/'layout-review/manifest.json').read_text())
    result=dict(format=1,package=str(a.package.resolve()),gameExecuted=False,installed=False,
        originalModelDefinitions=len(m['models']),additionalRenderComponents=len(m['renderLayers']),
        componentNamesDerivedFromOriginalModel=True,
        luigiState1=dict(asset=state1['name'],conditions=state1['conditions'],resources=state1['resources']),
        luigiState2=dict(asset=state2['name'],conditions=state2['conditions'],resources=state2['resources']),
        mutuallyExclusiveLuigiStatesSeparated=True,originalPhysicalCollisionsIdentical=len(first),
        boundOnlyRenderComponents=len(second)-len(first),confirmedTextureReferences=texture_names['resolved'],
        equivalentOriginalAliases=texture_names['equivalentAliasReferences'],provisionalTextureNames=0,
        originalEmptyDictionaryNames=sorted(p.stem for p in (a.package/'intermediate').glob('*.pst')
            if struct.unpack_from('<I',p.read_bytes(),4)[0]==0),
        pendingSourceLinks=len(m['unresolvedSourceLinks']),
        pendingSourceLinkReasons=dict(collections.Counter(x['reason'] for x in m['unresolvedSourceLinks'])),
        placedModelIdsNotExported=len(m['placedModelsNotExported']),
        prototypeDefinitionsAwaitingEntityBinding=len({r['model'] for r in m.get('prototypeFramesWithoutEntityBinding',[])}),
        originalEntitiesWithoutPlacement=len(layout['sourceEntitiesWithoutPlacement']),
        originalLodPairs=len(layout['lodGraph']['pairs']),pendingLodInstances=len(layout['lodGraph']['pending']),
        nativeReviewLodLinks=layout['reviewSnapshot']['nativeLodLinks'],mapComplete=False,
        remaining='Missing source geometry/empty-object classification, LOD range groups and runtime story-layer swaps')
    a.report.write_text(json.dumps(result,indent=2),encoding='utf-8');print(json.dumps(result,indent=2))


if __name__=='__main__':main()
