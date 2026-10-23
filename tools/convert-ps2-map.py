"""Local map reconversion from the user's PS2 files. No bundled game assets.

This development build exports verified assets and an explicit coverage report.
It refuses to mark a package complete or install it while source links remain.
"""
import argparse, collections, hashlib, json, math, pathlib, struct, subprocess
import importlib.util
from ps2_input import extract_iso
from ps2_world import Game, Level, Chunk
from ps2_export import mesh_file, texture_file, collision, bound_collision, name32
from ps2_water import export_water
from ps2_names import TextureNames,TextureDictionaries
from ps2_visibility import assemblies,select_layers,key as visibility_key
from ps2_layout import export_review
from ps2_geometry_audit import validate as validate_geometry
from ps2_lighting import export_lighting
from ps2_effects import export_assets as export_effects
from map_damage import export_damage
from ps2_paths import export_paths
from ps2_player import export_player
from ps2_peds import export_peds
from ps2_weapons import export_weapons


def run(exe,*args):
    result=subprocess.run([str(exe),*(str(x) for x in args)],capture_output=True,text=True)
    if result.returncode:raise RuntimeError(result.stdout+'\n'+result.stderr)
    return result.stdout.strip()


def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('rom',type=pathlib.Path);p.add_argument('output',type=pathlib.Path)
    p.add_argument('--paths-only',action='store_true',help='Export/audit the original PS2 graph without reconverting map assets')
    p.add_argument('--player-only',action='store_true',help='Convert the 16 original PS2 gameplay Toni outfits')
    p.add_argument('--weapons-only',action='store_true',help='Convert all original PS2 weapon models and textures; preserve separate weapon identities')
    p.add_argument('--peds-only',action='store_true',help='Convert/audit all streamed gameplay peds and extra gameplay characters')
    p.add_argument('--sa-game',type=pathlib.Path,help='User SA installation supplying its native pedestrian skeleton')
    default_links=pathlib.Path(__file__).with_name('..').resolve()/'local/metadata/ps2-source-links.json'
    p.add_argument('--links',type=pathlib.Path,default=default_links,
                   help='Verified source-link metadata (included in the standalone package)')
    a=p.parse_args();root=pathlib.Path(__file__).resolve().parents[1]
    if sum((a.player_only,a.paths_only,a.peds_only,a.weapons_only)) > 1:p.error('Choose only one export-only mode')
    if (a.player_only or a.peds_only) and not a.sa_game:p.error('Player/ped conversion requires --sa-game')
    links={} if a.player_only or a.paths_only or a.peds_only or a.weapons_only else json.loads(a.links.read_text())
    if links.get('geometryVerified') is False:raise ValueError('Proposed source associations lack independent geometry verification')
    if a.output.exists():raise ValueError('Output directory already exists; source/previous packages are never overwritten')
    a.output.mkdir(parents=True)
    rom=extract_iso(a.rom,a.output/'rom-input') if a.rom.is_file() else a.rom
    # Link metadata belongs to one precise console world revision. Verify all
    # original input bytes before sending RSL chunks to the native decoder.
    revision=json.loads((root/'local/metadata/ps2-source-revision.json').read_text())
    source_hashes={}
    for name,expected in revision['files'].items():
        with (rom/name).open('rb') as stream:value=hashlib.file_digest(stream,'sha256').hexdigest()
        if value!=expected:raise ValueError(f'Unsupported source revision: {name}')
        source_hashes[name]=value
    if a.weapons_only:
        report=export_weapons(rom,a.output/'weapons',root)
        (a.output/'manifest.json').write_text(json.dumps(dict(sourceHashes=source_hashes,weapons=report,mode='WeaponsOnly',gameExecuted=False),indent=2),encoding='utf-8')
        print('Converted/audited',len(report['converted']),'weapon models; gameExecuted=false',flush=True)
        return
    chunk=Chunk.compressed(rom/'CHK/PS2/GAME.DTZ')
    if a.peds_only:
        report=export_peds(rom,a.sa_game,a.output/'peds',root)
        (a.output/'manifest.json').write_text(json.dumps(dict(sourceHashes=source_hashes,peds=report,mode='PedsOnly',gameExecuted=False),indent=2),encoding='utf-8')
        print('Converted/audited',len(report['converted']),'gameplay skins; gameExecuted=false',flush=True)
        return
    if a.player_only:
        report=export_player(rom,a.sa_game,a.output/'player',root)
        (a.output/'manifest.json').write_text(json.dumps(dict(sourceHashes=source_hashes,player=report,mode='PlayerOnly',gameExecuted=False),indent=2),encoding='utf-8')
        print('Converted 16 PS2 Toni outfits; player.img unchanged; gameExecuted=false',flush=True)
        return
    navigation=export_paths(chunk,a.output/'paths')
    spec=importlib.util.spec_from_file_location('sa_path_validation',root/'tools/validate-sa-paths.py')
    path_validator=importlib.util.module_from_spec(spec);spec.loader.exec_module(path_validator)
    path_audit=path_validator.validate(a.output/'paths/paths.img',rom,a.output/'paths')
    (a.output/'paths/validation.json').write_text(json.dumps(path_audit,indent=2),encoding='utf-8')
    if a.paths_only:
        (a.output/'manifest.json').write_text(json.dumps(dict(format=1,sourceHashes=source_hashes,navigation=navigation,
            nativeNavigationValidation=path_audit,gameExecuted=False,installed=False,mode='PathsOnly'),indent=2),encoding='utf-8')
        print(json.dumps(path_audit,indent=2),flush=True)
        return
    g=Game(rom,root/'vendor/librwgta/librwgta-master/tools/storiesconv/hashes.inc')
    intermediate=a.output/'intermediate';intermediate.mkdir()
    lighting=export_lighting(g,a.output)
    assets=a.output/'assets';assets.mkdir()
    writer=root/'build/Release/lcs-ps2-write.exe';rsl=root/'build/Release/lcs-ps2-rsl.exe';rw=root/'build/Release/lcs-convert-rw.exe'
    models={};layers=[];all_groups=collections.defaultdict(list);source_layers={}
    original_names=TextureNames(g)
    dictionaries=TextureDictionaries(original_names.by_dictionary)
    textures=dictionaries.textures
    name_records=[]
    levels={name:Level(rom,name) for name in ('INDUST','COMMER','SUBURB','UNDERG')}
    for level in levels.values():
        for model,groups in assemblies(g,level,links['links']+links.get('prototypeLinks',[])).items():
            for group in groups:group['level']=level
            all_groups[model].extend(groups)
    raster_cache={};resolution_cache={}
    for lname in ('INDUST','COMMER','SUBURB','UNDERG'):
        level=levels[lname]
        def resolve(level,rows,dictionary,model_name):
            resources={mesh['texture'] for row in rows for mesh in level.building(row['resource'])}
            resolved={};requested={};records=[]
            for id in sorted(resources):
                key=(level.name,id,dictionary,model_name)
                raster_key=(level.name,id)
                if raster_key not in raster_cache:raster_cache[raster_key]=level.texture(id)
                image=raster_cache[raster_key]
                if key not in resolution_cache:
                    record=original_names.identify(image,dictionary,model_name)
                    if not record['name']:raise ValueError(f'Unidentified original texture: {level.name}:{id} ({model_name})')
                    resolution_cache[key]=record
                record=resolution_cache[key];name=record['name']
                for alias in {name,*record.get('aliases',[])}:
                    if not dictionaries.compatible(requested,{alias:image}):raise ValueError('Conflicting texture names within one compiled model')
                    requested[alias]=image
                records.append(dict(level=level.name,resource=id,dictionary=dictionary,model=model_name,
                                    outputName=name,**record))
                resolved[id]=name
            target=dictionaries.assign(dictionary,requested)
            if target in dictionaries.variants:
                names=dictionaries.variants[target]['models']
                if model_name not in names:names.append(model_name)
            name_records.extend(dict(**record,outputDictionary=target) for record in records)
            return target,resolved
        for model,groups in all_groups.items():
            if model in models or not any(group['level'].name==lname for group in groups):continue
            m=g.models[model]
            for layer in select_layers(m,groups):
                source_level=layer['level'];rows=layer['rows']
                dictionary,names=resolve(source_level,rows,m['txd'],m['name'])
                if layer['name'].lower() in {r['name'].lower() for r in (*models.values(),*layers)}:
                    raise ValueError('Conflicting render-layer asset name')
                record=mesh_file(source_level,rows,layer['entity'],intermediate/(layer['name']+'.psm'),names)
                result=dict(name=layer['name'],txd=dictionary,sourceTxd=m['txd'],sourceModel=model,
                            originalModelName=m['name'],kind='sector render layer',level=source_level.name,
                            sourcePlacementRole='definition only; physical entity binding pending' if layer['frameOnly'] else 'verified original entity frame',
                            worldIds=layer['worldIds'],conditions=layer['conditions'],primary=layer['primary'],**record)
                source_layers[model,visibility_key(layer['conditions'])]=result
                if layer['primary']:models[model]=result
                else:layers.append(result)
        identity=dict(matrix=[1,0,0,0,0,1,0,0,0,0,1,0,0,0,0,1])
        for d in level.dynamics():
            model=d['model']
            if model in models or d['lod']!=0:continue
            m=g.models[model];rows=[dict(resource=d['resource'],matrix=d['matrix'],renderpass=3)]
            if d['resource2']:rows.append(dict(resource=d['resource2'],matrix=d['matrix'],renderpass=6))
            dictionary,names=resolve(level,rows,m['txd'],m['name'])
            record=mesh_file(level,rows,identity,intermediate/(m['name']+'.psm'),names)
            models[model]=dict(name=m['name'],txd=dictionary,sourceTxd=m['txd'],kind='dynamic model definition',level=lname,**record)
        print(f'{lname}: decoded model definitions={len(models)}',flush=True)
    # Preserve every named texture in dictionaries used by streamed MDLs,
    # including original resident generic (not present as an IMG stream).
    used={g.models[e['model']]['txd'] for e in g.entities}
    used.update(g.models[i]['txd'] for i in g.models if g.models[i]['type'] in (1,3) and g.models[i]['streamSize'])
    used.add('generic')
    for dictionary in sorted(used):
        if dictionary not in g.txds.values():raise ValueError('Model refers to an unknown source dictionary')
        # Source TXDs can legitimately be empty (e.g. subglass). The native
        # IDE still needs their real dictionary, including its generic parent.
        dictionaries.add_resident(dictionary,original_names.by_dictionary.get(dictionary,{}))
    for txd,records in textures.items():
        if not txd:raise ValueError('Original model lacks dictionary name')
        with (intermediate/(txd+'.pst')).open('wb') as stream:
            stream.write(struct.pack('<4sI',b'PST1',len(records)))
            for name,(w,h,pixels) in sorted(records.items()):
                stream.write(name32(name));stream.write(struct.pack('<2I',w,h));stream.write(pixels)
    print(run(writer,intermediate,a.output/'rw-assets'),flush=True)
    assets=a.output/'rw-assets'
    streamed=a.output/'streamed-source';streamed.mkdir()
    with (rom/'MODELS/GTA3PS2.IMG').open('rb') as stream:
        def read_chunk(index):
            pos,size=g.stream(index);stream.seek(pos);raw=stream.read(size)
            if len(raw)!=size or len(raw)<32:raise ValueError('Truncated original streamed asset')
            length=struct.unpack_from('<I',raw,8)[0]
            if length>size:raise ValueError('Original asset exceeds stream entry')
            return Chunk(raw[:length],f'original stream {index}')
        for model,m in g.models.items():
            if m['type'] not in (1,3) or not m['name'] or not m['streamSize']:continue
            is_map_model = model in models
            if is_map_model:
                atomics=m.get('atomics',0);distances=m.get('draw',())
                if m.get('flags',0)&0x10 or atomics not in (2,3) or len(distances)<atomics:continue
                if any(not math.isfinite(float(value)) for value in distances[:atomics]):continue
                marks_damage=(float(distances[0])>=float(distances[1]) if atomics==2 else
                    not (float(distances[0])<float(distances[1]) and
                         float(distances[1])<float(distances[2])))
                if not marks_damage:continue
            source=streamed/(m['name']+'.mdl');source.write_bytes(read_chunk(model).data)
            native=streamed/(m['name']+'.dff');run(rsl,source,native)
            if is_map_model:continue
            run(rw,native,assets/(m['name']+'.dff'),'--atomic-white',intermediate/'map-lighting.psl')
            models[model]=dict(name=m['name'],txd=m['txd'],kind='original MDL')
        radar_source=a.output/'radar-source';radar_source.mkdir()
        for txslot,name in g.txds.items():
            if not name.upper().startswith('RADAR'):continue
            source=streamed/(name+'.tex');source.write_bytes(read_chunk(4900+txslot).data)
            run(rsl,source,radar_source/(name+'.txd'))
    # The verified PS2 source input set has GAME.DTZ and GTA3PS2.IMG, but no
    # standalone DATA/OBJECT.DAT. Do not guess breakable candidates from model
    # names or the hash catalogue; export only source-proven damaged atomics.
    damage = export_damage(
        g, models, assets, streamed, rw, intermediate/'map-lighting.psl',
        a.output/'damage-validation.json')
    effects = export_effects(g, models, assets, a.output/'effects-validation.json')
    print(run(writer,'--radar',radar_source,a.output/'radar'),flush=True)
    water=export_water(g,a.output/'water.dat')
    collision_models=[m for m in g.models.values() if m['type'] in (1,3) and m['name'] and m['id'] in g.collisions]
    with (a.output/'world.col').open('wb') as stream:
        for m in collision_models:stream.write(collision(g,m['id']))
        for layer in layers:stream.write(bound_collision(layer['name'],layer['bounds']))
    print(run(rw,assets,'--validate'),flush=True)
    print(run(rw,a.output/'world.col','--validate-col'),flush=True)
    geometry_validation=validate_geometry(a.output)
    spec=importlib.util.spec_from_file_location('ps2_material_validation',root/'tools/validate-ps2-map.py')
    validator=importlib.util.module_from_spec(spec);spec.loader.exec_module(validator)
    material_validation=validator.validate(a.output,{**models,**{r['name']:r for r in layers}},rw)
    if not material_validation['complete']:raise ValueError('Native map has unresolved material textures; inspect material-validation.json')
    spec=importlib.util.spec_from_file_location('ps2_pixel_validation',root/'tools/validate-ps2-texture-pixels.py')
    pixel_validator=importlib.util.module_from_spec(spec);spec.loader.exec_module(pixel_validator)
    pixel_validation=pixel_validator.validate(a.output)
    (a.output/'texture-names.json').write_text(json.dumps(dict(
        namedSourceRecords=len(original_names.records),resolved=sum(bool(x['name']) for x in name_records),
        referenceNameFingerprints=original_names.metadata_records,
        equivalentAliasReferences=sum(x.get('originalAliasAmbiguous',False) for x in name_records),
        sectorReferences=len(name_records),dictionaryVariants=dictionaries.variants,
        records=name_records),indent=2),encoding='utf-8')
    needed={e['model'] for e in g.entities}
    missing=[dict(id=i,name=g.models[i]['name']) for i in sorted(needed-set(models))]
    manifest=dict(format=1,sourceHashes=source_hashes,originalModelNamesPreserved=True,
        originalTextureNamesPreserved=all(x['name'] for x in name_records),
        dictionaryVariants=dictionaries.variants,
        gameExecuted=False,installed=False,complete=False,models=models,renderLayers=layers,water=water,navigation=navigation,
        collisionModels=len(collision_models),renderLayerBounds=len(layers),radarTiles=144,
        collisionFormat='COL2',
        textureNames=dict(confirmed=sum(bool(x['name']) for x in name_records),references=len(name_records),
                          fallbackNames=0),
        nativeMaterialValidation=material_validation,
        nativePixelValidation=pixel_validation,
        nativeGeometryValidation=geometry_validation,nativeLighting=lighting,nativeDamage=damage,
        nativeEffects=effects,
        placedModelsNotExported=missing,unresolvedSourceLinks=links['unresolved'],
        prototypeFramesWithoutEntityBinding=links.get('prototypeLinks',[]),
        remaining='Source link review, null/collision-only objects, LOD/IPL/profile packaging and engine verification')
    (a.output/'manifest.json').write_text(json.dumps(manifest,indent=2),encoding='utf-8')
    placements={}
    for model,groups in all_groups.items():
        for group in groups:
            if group['frameOnly']:continue
            entity=group['entity'];rule=group['conditions']
            ident=(entity['pool'],entity['index'],visibility_key(rule))
            if ident in placements:continue
            layer=source_layers[model,visibility_key(rule)]
            placements[ident]=dict(sourcePool=entity['pool'],sourceIndex=entity['index'],sourceModel=model,
                asset=layer['name'],area=entity['area'],conditions=rule,
                basis=[entity['matrix'][i:i+3] for i in (0,4,8)],position=entity['matrix'][12:15])
    for entity in g.entities:
        if entity['model'] not in models or entity['model'] in all_groups:continue
        ident=(entity['pool'],entity['index'],None)
        placements[ident]=dict(sourcePool=entity['pool'],sourceIndex=entity['index'],sourceModel=entity['model'],
            asset=models[entity['model']]['name'],area=entity['area'],conditions=dict(instance=None,resources=[],interiors=[]),
            basis=[entity['matrix'][i:i+3] for i in (0,4,8)],position=entity['matrix'][12:15])
    (a.output/'source-placements.json').write_text(json.dumps(dict(format=1,gameExecuted=False,
        placements=list(placements.values()),complete=False),indent=2),encoding='utf-8')
    layout=export_review(g,a.output,models,layers,list(placements.values()))
    manifest['layoutReview']=dict(sourcePlacements=layout['sourcePlacements'],
        sourceEntitiesWithoutPlacement=len(layout['sourceEntitiesWithoutPlacement']),
        sourceLodLinks=len(layout['lodGraph']['pairs']),pendingLodInstances=len(layout['lodGraph']['pending']),
        nativeSnapshotLodLinks=layout['reviewSnapshot']['nativeLodLinks'],complete=False)
    (a.output/'manifest.json').write_text(json.dumps(manifest,indent=2),encoding='utf-8')
    spec=importlib.util.spec_from_file_location('ps2_layout_validation',root/'tools/validate-ps2-layout.py')
    layout_validator=importlib.util.module_from_spec(spec);spec.loader.exec_module(layout_validator)
    layout_validator.validate(a.output)
    print(f'Native SA assets exported={len(models)}; package remains incomplete and was not installed.',flush=True)


if __name__=='__main__':main()
