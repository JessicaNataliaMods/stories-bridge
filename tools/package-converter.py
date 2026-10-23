"""Build a standalone offline tool bundle using an explicit asset-free allowlist."""
import argparse,hashlib,json,pathlib,shutil,zipfile

root=pathlib.Path(__file__).resolve().parents[1]
parser=argparse.ArgumentParser(description=__doc__)
parser.add_argument('output',type=pathlib.Path)
parser.add_argument('--python',type=pathlib.Path,required=True)
parser.add_argument('--crt',type=pathlib.Path,required=True)
parser.add_argument('--links',type=pathlib.Path,default=root/'reports/ps2-verified-links-v13.json')
args=parser.parse_args();out=args.output.resolve()
links=json.loads(args.links.read_text())
if links.get('geometryVerified') is False:raise ValueError('Proposed source associations cannot be included in the public tool')
if out.exists():raise ValueError('Bundle output already exists')
out.mkdir(parents=True)

def copy(source,target):
    target.parent.mkdir(parents=True,exist_ok=True);shutil.copyfile(source,target)

for name in ('StoriesConverter.exe',):copy(root/'build/Release'/name,out/name)
resources=out/'resources'
for name in ('lcs-ps2-write.exe','lcs-ps2-rsl.exe','lcs-convert-rw.exe'):
    copy(root/'build/Release'/name,resources/'build/Release'/name)
for name in ('python.exe','python312.dll','python3.dll','vcruntime140.dll','vcruntime140_1.dll','LICENSE.txt'):
    copy(args.python/name,resources/'python'/name)
for name in ('select.pyd','unicodedata.pyd'):
    copy(args.python/'DLLs'/name,resources/'python/DLLs'/name)
with zipfile.ZipFile(resources/'python/python312.zip','w',compression=zipfile.ZIP_DEFLATED) as archive:
    for path in (args.python/'Lib').rglob('*.py'):
        relative=path.relative_to(args.python/'Lib')
        if any(part in ('site-packages','__pycache__','test','tests','idlelib','ensurepip','tkinter','turtledemo') for part in relative.parts):continue
        archive.write(path,relative.as_posix())
# Native helpers and launcher use the x86 CRT. CPython has its own x64 CRT.
for name in ('msvcp140.dll','msvcp140_1.dll','msvcp140_2.dll','vcruntime140.dll','msvcp140_atomic_wait.dll'):
    copy(args.crt/name,out/name);copy(args.crt/name,resources/'build/Release'/name)
scripts=('convert-ps2-map.py','ps2_world.py','ps2_export.py','ps2_input.py','ps2_water.py','ps2_names.py','ps2_visibility.py','ps2_layout.py','ps2_lighting.py','ps2_geometry_audit.py','ps2_paths.py','ps2_player.py','ps2_peds.py','ps2_weapons.py','prepare-weapon-models.py','prepare-campaign-vehicles.py','vehicle_config_fields.py','vehicle_config.py','mount-campaign-vehicles.py','validate-sa-paths.py','validate-ps2-map.py','validate-ps2-texture-pixels.py','validate-ps2-layout.py','validate-ps2-player.py')
scripts += ('vehicle_source_collision.py', 'ferrylib.py', 'rwgeom.py', 'rwtree.py', 'split_ferry_doors.py', 'ps2_effects.py')
for name in scripts:copy(root/'tools'/name,resources/'tools'/name)
copy(root/'tools/ped_rig_audit.py',resources/'tools/ped_rig_audit.py')
copy(root/'local/metadata/ps2-source-revision.json',resources/'local/metadata/ps2-source-revision.json')
copy(root/'local/metadata/ps2-texture-name-metadata.json',resources/'local/metadata/ps2-texture-name-metadata.json')
copy(root/'vendor/librwgta/librwgta-master/tools/storiesconv/hashes.inc',resources/'vendor/librwgta/librwgta-master/tools/storiesconv/hashes.inc')
# Public metadata contains identities only, never reference meshes, pixels,
# world bounding boxes, model vertices, screenshots or bundled ROM chunks.
keys=('level','worldId','model','entityPool','entityIndex','resources','resourceLods','frameOnly','frameAliases','sourceEntityModel','variantFromRelatedLodModel')
public=dict(format=1,links=[{k:x[k] for k in keys if k in x} for x in links['links']],
            prototypeLinks=[{k:x[k] for k in keys if k in x} for x in links.get('prototypeLinks',[])],
            unresolved=[{k:x[k] for k in ('level','worldId','resources','reason')} for x in links['unresolved']])
(resources/'local/metadata/ps2-source-links.json').write_text(json.dumps(public,separators=(',',':')),encoding='utf-8')
source=out/'source'
for name in scripts:copy(root/'tools'/name,source/'tools'/name)
copy(root/'tools/ped_rig_audit.py',source/'tools/ped_rig_audit.py')
copy(root/'tools/native_collisions.py',source/'tools/native_collisions.py')
for name in ('prepare-gameplay-peds.py','gameplay_ped_profile.py'):
    copy(root/'tools'/name,source/'tools'/name)
for name in ('vehicle-fleet-conversion.md','vehicle-class-conversion.md','weapon-model-conversion.md','gameplay-assets-integration.md','gameplay-peds-bindings-v18.md','ped-rig-population-v18.md'):
    copy(root/'docs'/name,out/'docs'/name)
    copy(root/'docs'/name,source/'docs'/name)
copy(root/'docs/native-object-collisions.md',out/'docs/native-object-collisions.md')
copy(root/'docs/native-object-collisions.md',source/'docs/native-object-collisions.md')
for name in ('converter_launcher.cpp','ps2_asset_writer.cpp','convert_rw.cpp','convert_vehicle.cpp','convert_vehicle.hpp','convert_vehicle_classes.cpp','convert_vehicle_classes.hpp','convert_vehicle_aircraft.cpp','convert_vehicle_aircraft.hpp','convert_vehicle_special.cpp','convert_vehicle_special.hpp','convert_ped.cpp','convert_ped.hpp','sa_map_lighting.hpp'):
    copy(root/'src'/name,source/'src'/name)
source_extensions={'.c','.cpp','.h','.hpp','.inc','.in','.cmake','.txt','.md','.lua','.rc','.def','.idl','.err'}
for name in ('convert_vehicle_ambient.cpp','convert_vehicle_ambient.hpp'):
    copy(root/'src'/name,source/'src'/name)
for vendor in ('librw/librw-master','librwgta/librwgta-master'):
    for path in (root/'vendor'/vendor).rglob('*'):
        if not path.is_file() or '.git' in path.parts:continue
        if path.suffix.lower() not in source_extensions and path.name!='LICENSE':continue
        copy(path,source/'vendor'/path.relative_to(root/'vendor'))
copy(root/'local/metadata/ps2-source-revision.json',source/'local/metadata/ps2-source-revision.json')
copy(root/'local/metadata/ps2-texture-name-metadata.json',source/'local/metadata/ps2-texture-name-metadata.json')
copy(resources/'local/metadata/ps2-source-links.json',source/'local/metadata/ps2-source-links.json')
copy(root/'docs/player-outfits.md',out/'docs/player-outfits.md')
copy(root/'docs/player-outfits.md',source/'docs/player-outfits.md')
cmake=(root/'CMakeLists.txt').read_text();start=cmake.index('set(LIBRW_SOURCE ');end=cmake.index('if(WIN32 AND EXISTS ',start)
(source/'CMakeLists.txt').write_text('cmake_minimum_required(VERSION 3.20)\nproject(StoriesConverter LANGUAGES C CXX)\nset(CMAKE_CXX_STANDARD 17)\nadd_executable(StoriesConverter src/converter_launcher.cpp)\n'+cmake[start:end],encoding='utf-8')
(out/'README.txt').write_text('Stories Converter - development asset exporter\n\n'
    'Usage: StoriesConverter.exe "LCS PS2.iso" "NEW_OUTPUT_FOLDER"\n'
    'Paths only: StoriesConverter.exe "LCS PS2.iso" "NEW_OUTPUT_FOLDER" --paths-only\n'
    'Player only: StoriesConverter.exe "LCS PS2.iso" "NEW_OUTPUT_FOLDER" --player-only --sa-game "SA_FOLDER"\n'
    'Gameplay peds: StoriesConverter.exe "LCS PS2.iso" "NEW_OUTPUT_FOLDER" --peds-only --sa-game "SA_FOLDER"\n'
    'An extracted LCS PS2 directory is also accepted. Keep the resources folder beside the executable.\n'
    'Only the source revision recorded in resources/local/metadata/ps2-source-revision.json is accepted.\n'
    'No ROM, textures, models, collision data, radar, audio or mission assets are included.\n'
    'All generated game data comes from the user input. The tool never launches GTA.\n\n'
    'This build exports and validates DFF/TXD/COL/water/radar and native SA paths.img assets.\n'
    'paths/ contains all 64 SA areas, original PS2 vehicle/ped graphs and source bindings.\n'
    'Independent validation compares every node, navigation record and connection to PS2.\n'
    'Player export preserves all 16 PS2 Toni outfits and texture names, adapting the\n'
    'weighted mesh to native SA animation axes while preserving LCS bone lengths.\n'
    'Native helper bones come from the user SA installation. player.img is not modified.\n'
    'Read docs/player-outfits.md for original mission labels and the clothes-change backend.\n'
    'Map placement/LOD/variant review remains incomplete; it does not install a finished map.\n'
    'manifest.json records incomplete coverage explicitly. Original texture names are recovered\n'
    'by exact pixel fingerprints. Name metadata contains no images. Different source images\n'
    'with the same name use separate TXD dictionaries, listed in dictionaryVariants.\n\n'
    'Sources are in source/. Native build: cmake -S source -B build -A Win32,\n'
    'then cmake --build build --config Release. Python scripts use the standard library.\n'
    'Dependencies: Python 3.12 (resources/python/LICENSE.txt); librw (source/vendor/librw/librw-master/LICENSE);\n'
    'librwgta by aap and contributors (source/vendor/librwgta/librwgta-master/README.md).\n'
    'Microsoft VC runtime libraries are included for Windows.\n',encoding='utf-8')
for path in out.rglob('*'):
    if path.is_file() and path.suffix.lower() in {'.iso','.img','.dtz','.lvz','.dff','.txd','.mdl','.tex','.col','.scm','.gxt','.ifp','.wav','.png','.tga','.bin'}:
        raise ValueError('Disallowed game/media asset in public tool: '+str(path))
manifest=dict(assetFree=True,developmentBuild=True,gameExecuted=False,
              files={p.relative_to(out).as_posix():hashlib.sha256(p.read_bytes()).hexdigest() for p in out.rglob('*') if p.is_file()})
(out/'bundle-manifest.json').write_text(json.dumps(manifest,indent=2),encoding='utf-8')
print('Asset-free standalone bundle files=',len(manifest['files']),'bytes=',sum(p.stat().st_size for p in out.rglob('*') if p.is_file()))
