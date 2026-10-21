"""Audit texture-name recovery directly from original PS2 source bytes."""
import collections,json,pathlib,sys
from ps2_world import Game,Level
from ps2_names import TextureNames

root=pathlib.Path(__file__).resolve().parents[1]
rom=root / 'local/originals/lcs-ps2'
game=Game(rom,root/'vendor/librwgta/librwgta-master/tools/storiesconv/hashes.inc')
names=TextureNames(game)
manifest=json.loads((root/'out/ps2-map-reconverted-v1/manifest.json').read_text())
records=[];summary={}
for lname in ('INDUST','COMMER','SUBURB','UNDERG'):
    level=Level(rom,lname);usage=collections.defaultdict(set)
    for model in manifest['models'].values():
        if model.get('level')!=lname:continue
        for resource in model.get('textures',[]):usage[resource].add(model['txd'])
    for resource,dictionaries in sorted(usage.items()):
        image=level.texture(resource)
        for dictionary in sorted(dictionaries):
            record=names.identify(image,dictionary)
            records.append(dict(level=lname,resource=resource,dictionary=dictionary,**record))
    counts=collections.Counter(x['method'] for x in records if x['level']==lname)
    summary[lname]=dict(counts);print(lname,dict(counts),flush=True)
report=dict(originalNamedTextureRecords=len(names.records),records=records,summary=summary,
            source='User PS2 GAME.DTZ resident dictionaries and GTA3PS2.IMG named TEX chunks',
            pixelMatchIncludesDimensions=True)
(root/'reports/ps2-original-texture-name-audit.json').write_text(json.dumps(report,indent=2),encoding='utf-8')
print('records',len(records),'confirmed',sum(bool(x['name']) for x in records),flush=True)
