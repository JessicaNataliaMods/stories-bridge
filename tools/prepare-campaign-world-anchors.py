"""Convert source collision-only story anchors without changing approved map media."""
import hashlib,json,pathlib,struct,subprocess
from ps2_world import Game
from ps2_layout import quaternion,sa_flags

ROOT=pathlib.Path(__file__).resolve().parents[1]

def prepare(rom, package, source_map):
    revision=json.loads((ROOT/'local/metadata/ps2-source-revision.json').read_text())
    for name,value in revision['files'].items():
        with (rom/name).open('rb') as stream:
            if hashlib.file_digest(stream,'sha256').hexdigest()!=value:raise ValueError('Unsupported source revision: '+name)
    game=Game(rom,ROOT/'vendor/librwgta/librwgta-master/tools/storiesconv/hashes.inc')
    # GAME model infos and sector rendering are independent on PS2. Export
    # collision carriers for missing SCM swap definitions, NOT substitute
    # visible objects. Runtime rejects CREATE_OBJECT on these carriers.
    script=(rom/'DATA/MAIN.SCM').read_bytes()[8:]
    p=struct.unpack_from('<I',script,3)[0]+8
    saved=struct.unpack_from('<I',script,p)[0];p+=4+2*saved
    count=struct.unpack_from('<H',script,p)[0];p+=4
    names={script[p+24*i:p+24*(i+1)].split(b'\0')[0].decode('ascii').lower() for i in range(count)}
    mounted={row.split('\t')[2].lower() for row in (package/'data/lcs/campaign-model-bindings.tsv').read_text().splitlines()}
    original_names=tuple(row['name'] for row in game.models.values() if row['type'] in (1,3) and row['name'] and row['name'].lower() in names-mounted)
    source={row['name']:row for row in game.models.values() if row['name'] in original_names}
    missing={row['id'] for row in json.loads((source_map/'manifest.json').read_text())['placedModelsNotExported']}
    if set(source)!=set(original_names):raise ValueError('Story anchor source identities missing')
    rows=(package/'data/lcs/model-bindings.tsv').read_text().splitlines()
    next_id=max(int(row.split('\t')[1]) for row in rows)+1
    payload=package/'metadata/campaign-anchor-intermediate';payload.mkdir()
    assets=package/'models/lcs/campaign-anchors'
    ide=['objs'];ipl=['inst'];bindings=[];report=[]
    for index,name in enumerate(original_names):
        model=source[name];ident=model['id'];target=next_id+index
        if model['streamSize'] or model['type'] not in (1,3) or ident not in game.collisions:raise ValueError('Carrier is not an original unstreamed world definition')
        c,p=game.collisions[ident];counts=c.read('<3h',p+48)
        # Empty rendering carrier with no triangles/material texture. Physical
        # shape and bounds come from the original COL already in world.col.
        center=game.sphere(ident)[:3]
        data=struct.pack('<4s3I',b'PSM1',1,0,1)+bytes(32)+struct.pack('<5f4B',*center,0,0,255,255,255,255)
        (payload/(name+'.psm')).write_bytes(data)
        # No triangle uses a texture; use the already mounted generic TXD,
        # including its exact alias, rather than inventing an empty source TXD.
        txd=json.loads((package/'metadata/map-preview.json').read_text())['textureDictionaryAliases'].get('generic','generic')
        ide.append(f'{target}, {name}, {txd}, {model["draw"][0]}, {sa_flags(model["flags"])}')
        placements=[e for e in game.entities if e['model']==ident]
        for entity in placements:
            matrix=entity['matrix'];basis=[matrix[start:start+3] for start in (0,4,8)]
            ipl.append(', '.join(map(str,[target,name,0,*matrix[12:15],*quaternion(basis),-1])))
        bindings.append(f'{ident}\t{target}\t{name}\t{name}')
        report.append(dict(original=ident,native=target,name=name,streamBytes=0,renderTriangles=0,
                           sourceCollisionCounts=counts,placements=len(placements),sourceFrames=placements,
                           role='physical SCM swap carrier; compiled sector rendering is separate'))
    ide.append('end');ipl.append('end')
    (package/'data/lcs/campaign-anchors.ide').write_text('\n'.join(ide)+'\n')
    (package/'data/lcs/campaign-anchors.ipl').write_text('\n'.join(ipl)+'\n')
    (package/'data/lcs/campaign-anchor-bindings.tsv').write_text('\n'.join(bindings)+'\n')
    result=subprocess.run([str(ROOT/'build/Release/lcs-ps2-write.exe'),str(payload),str(assets)],capture_output=True,text=True)
    if result.returncode:raise RuntimeError(result.stdout+result.stderr)
    profile=package/'data/gta.dat';text=profile.read_text()
    text=text.replace('COLFILE 0 MODELS\\LCS\\WORLD.COL','IDE DATA\\LCS\\CAMPAIGN-ANCHORS.IDE\nCOLFILE 0 MODELS\\LCS\\WORLD.COL')
    text+='IPL DATA\\LCS\\CAMPAIGN-ANCHORS.IPL\n';profile.write_text(text)
    (package/'metadata/campaign-anchors.json').write_text(json.dumps(dict(models=report,gameExecuted=False,approvedMapMediaChanged=False),indent=2))
    return bindings

if __name__=='__main__':
    import argparse
    parser=argparse.ArgumentParser(description=__doc__)
    for key in ('rom','package','source-map'):parser.add_argument('--'+key,type=pathlib.Path,required=True)
    args=parser.parse_args();print(json.dumps(prepare(args.rom,args.package,args.source_map)))
