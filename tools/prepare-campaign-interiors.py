"""Restore original PS2 interior render prototypes used by SCM building swaps.

Only the user's ROM supplies vertices and pixels. The link table contains
identities and verification measurements, never reference model assets.
"""
import argparse, hashlib, json, pathlib, struct, subprocess, shutil, tempfile
from ps2_world import Game, Level
from ps2_export import mesh_file, name32
from ps2_names import TextureNames, image_key

ROOT=pathlib.Path(__file__).resolve().parents[1]

def prepare(rom,package,source_map):
    revision=json.loads((ROOT/'local/metadata/ps2-source-revision.json').read_text())
    for name,expected in revision['files'].items():
        with (rom/name).open('rb') as stream:
            if hashlib.file_digest(stream,'sha256').hexdigest()!=expected:
                raise ValueError('Unsupported PS2 interior source: '+name)
    game=Game(rom,ROOT/'vendor/librwgta/librwgta-master/tools/storiesconv/hashes.inc')
    table=json.loads((ROOT/'local/metadata/ps2-interior-links.json').read_text())
    if table['format']!=1 or table['containsAssets']:raise ValueError('Invalid interior link metadata')
    names=TextureNames(game);levels={};converted={};reports=[]
    entities={(e['pool'],e['index']):e for e in game.entities}
    intermediate=package/'metadata/campaign-interior-intermediate';intermediate.mkdir(exist_ok=True)
    assets=package/'models/lcs/campaign-anchors'
    (intermediate/'map-lighting.psl').write_bytes((source_map/'intermediate/map-lighting.psl').read_bytes())
    bindings={int(row[0]):(int(row[1]),row[2]) for line in (package/'data/lcs/campaign-anchor-bindings.tsv').read_text().splitlines() if (row:=line.split())}
    dictionaries={};digests={}
    for link in table['links']:
        model=game.models[link['model']];name=model['name']
        if name!=link['name'] or model['id'] not in bindings:raise ValueError('Unregistered interior prototype')
        entity=entities[link['entityPool'],link['entityIndex']]
        if entity['model']!=link['sourceEntityModel']:raise ValueError('Interior source frame changed')
        if link['level'] not in levels:levels[link['level']]=Level(rom,link['level'])
        level=levels[link['level']]
        rows=[r for r in level.instances[link['worldId']] if r['resource'] in link['resources']]
        if {r['resource'] for r in rows}!=set(link['resources']):raise ValueError('Interior geometry resource missing')
        if any(level.conditions(r)!=link['conditions'] for r in rows):raise ValueError('Interior source selection changed')
        textures={};resolved={}
        for row in rows:
            for mesh in level.building(row['resource']):
                ident=mesh['texture']
                if ident in resolved:continue
                image=level.texture(ident);record=names.identify(image,model['txd'],name)
                if not record['name']:raise ValueError('Unidentified interior texture: '+name)
                resolved[ident]=record['name']
                for alias in {record['name'],*record.get('aliases',[])}:
                    if alias in textures and image_key(textures[alias])!=image_key(image):raise ValueError('Interior texture collision')
                    textures[alias]=image
        psm=intermediate/(name+'.psm')
        info=mesh_file(level,rows,entity,psm,resolved)
        if not info['triangles'] or info['vertices']>65535:raise ValueError('Invalid interior topology')
        digest=hashlib.sha256(psm.read_bytes()).hexdigest()
        if name in digests:
            if digest!=digests[name]:raise ValueError('Interior residency copies differ: '+name)
            continue
        digests[name]=digest
        alias=f'lcsint{model["id"]}'
        dictionaries[name]=alias;converted[name]=dict(model=model['id'],native=bindings[model['id']][0],alias=alias)
        with (intermediate/(alias+'.pst')).open('wb') as stream:
            stream.write(struct.pack('<4sI',b'PST1',len(textures)))
            for texture,(width,height,pixels) in sorted(textures.items()):
                stream.write(name32(texture));stream.write(struct.pack('<2I',width,height));stream.write(pixels)
        reports.append(dict(name=name,original=model['id'],native=bindings[model['id']][0],
                            level=level.name,worldId=link['worldId'],sourceFrame=[entity['pool'],entity['index']],
                            sourceResources=link['resources'],conditions=link['conditions'],
                            intermediateSHA256=digest,textureNames=sorted(textures),**info))
    if len(converted)!=15:raise ValueError('Incomplete original interior prototype inventory')
    with tempfile.TemporaryDirectory(prefix='interior-writer-',dir=package/'metadata') as scratch:
        native=pathlib.Path(scratch)/'native'
        result=subprocess.run([str(ROOT/'build/Release/lcs-ps2-write.exe'),str(intermediate),str(native)],capture_output=True,text=True)
        if result.returncode:raise RuntimeError(result.stdout+result.stderr)
        for file in native.iterdir():shutil.copy2(file,assets/file.name)
    ide=package/'data/lcs/campaign-anchors.ide';lines=[];skip_parents=False
    for line in ide.read_text().splitlines():
        if line.strip()=='txdp':skip_parents=True;continue
        if skip_parents:
            if line.strip()=='end':skip_parents=False
            continue
        fields=[x.strip() for x in line.split(',')]
        if len(fields)==5 and fields[1] in dictionaries:fields[2]=dictionaries[fields[1]];line=', '.join(fields)
        lines.append(line)
    lines+=['txdp',*(alias+', generic' for alias in sorted(dictionaries.values())),'end']
    ide.write_text('\n'.join(lines)+'\n')
    (package/'data/lcs/campaign-render-ranges.tsv').write_text(''.join(f'{ident}\t0\t0\t-1\t31\t1\n' for ident,_ in bindings.values()))
    inventory=subprocess.run([str(ROOT/'build/Release/lcs-convert-rw.exe'),str(assets),'--map-inventory'],capture_output=True,text=True)
    if inventory.returncode:raise RuntimeError(inventory.stdout+inventory.stderr)
    geometries={};materials={};native_textures={}
    for line in inventory.stdout.splitlines():
        fields=line.split('\t')
        if fields[0]=='G':geometries[fields[1]]=(int(fields[2]),int(fields[3]))
        elif fields[0]=='D':materials.setdefault(fields[1],set()).add(fields[2])
        elif fields[0]=='T':native_textures.setdefault(fields[1],set()).add(fields[2])
    for row in reports:
        name=row['name'];alias=dictionaries[name]
        if geometries.get(name)!=(row['vertices'],row['triangles']):raise ValueError('Native interior mesh counts differ: '+name)
        if materials.get(name,set())-native_textures.get(alias,set()):raise ValueError('Native interior material has no texture: '+name)
        row['nativeMeshReread']=True
    generated=[ide,package/'data/lcs/campaign-render-ranges.tsv']
    generated += [assets/(name+'.dff') for name in converted]
    generated += [assets/(alias+'.txd') for alias in dictionaries.values()]
    report=dict(format=1,models=reports,originalPrototypeNamesPreserved=True,
                originalTextureNamesPreserved=True,gameExecuted=False,
                files=[dict(relative=str(file.relative_to(package)).replace('\\','/'),sha256=hashlib.sha256(file.read_bytes()).hexdigest()) for file in generated],
                activation='Original SCM SWAP_NEAREST_BUILDING_MODEL; no permanent overlapping IPL')
    (package/'metadata/campaign-interiors.json').write_text(json.dumps(report,indent=2))
    anchor_report=package/'metadata/campaign-anchors.json'
    anchor_data=json.loads(anchor_report.read_text());rendered={row['original']:row for row in reports}
    for row in anchor_data['models']:
        if row['original'] in rendered:
            row['renderTriangles']=rendered[row['original']]['triangles']
            row['role']='original interior render prototype and physical SCM swap model'
    anchor_report.write_text(json.dumps(anchor_data,indent=2))
    return report

if __name__=='__main__':
    parser=argparse.ArgumentParser(description=__doc__)
    for key in ('rom','package','source-map'):parser.add_argument('--'+key,type=pathlib.Path,required=True)
    args=parser.parse_args();print(json.dumps(prepare(args.rom,args.package,args.source_map),indent=2))
