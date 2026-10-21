"""Validate staged original marker geometry/pixels; never open the game."""
import hashlib
import json
from pathlib import Path
import subprocess
import sys
from ps2_geometry_audit import geometries

ROOT=Path(__file__).resolve().parents[1]


def validate(package):
    campaign=json.loads((package/'metadata/campaign.json').read_text())
    policy=campaign.get('scriptMarkers63',{})
    if policy.get('sourceCommands')!=[1579,1611] or policy.get('capacity')!=32 or \
       policy.get('nativeTypeAliases') is not False or policy.get('visualGameplayVerified') is not False:
        raise ValueError('Invalid source marker scope')
    manifest=json.loads((package/'metadata/script-marker-resources.json').read_text())
    if not manifest['valid'] or manifest['gameExecuted'] or manifest['renderingExecuted']:
        raise ValueError('Invalid offline resource provenance')
    if [row['type'] for row in manifest['meshes']]!=[1,2,5]:raise ValueError('Original marker mesh set incomplete')
    total=0
    for row in manifest['meshes']:
        path=package/'markers/rw-assets'/row['file']
        if path.parent!=(package/'markers/rw-assets') or hashlib.sha256(path.read_bytes()).hexdigest()!=row['sha256']:
            raise ValueError('Marker geometry missing/changed')
        geometry=geometries(path)
        if len(geometry)!=1 or not geometry[0]['binmesh'] or \
           geometry[0]['vertices']!=row['vertices'] or geometry[0]['triangles']!=row['triangles']:
            raise ValueError('Invalid native marker topology')
        total+=geometry[0]['vertices']
    texture=manifest['texture'];path=package/'markers/texture-assets/lcs_markers.txd'
    if hashlib.sha256(path.read_bytes()).hexdigest()!=texture['sha256']:raise ValueError('Marker texture missing/changed')
    fingerprints=subprocess.check_output([str(ROOT/'build/Release/lcs-ps2-write.exe'),
        '--texture-fingerprints',str(path.parent)],text=True).strip().split('\t')
    expected=['lcs_markers','zonewhite64','64','64',texture['originalPixelSha256']]
    if texture.get('originalPixelsPreserved') is not True or fingerprints!=expected:
        raise ValueError('Original marker texture pixels changed')
    return dict(valid=True,meshes=3,vertices=total,textures=1,sourceCommands=[1579,1611],
                gameExecuted=False,visualGameplayVerified=False)


if __name__=='__main__':
    print(json.dumps(validate(Path(sys.argv[1]).resolve())))
    print('PASS: original marker meshes, topology and texture pixels preserved')
