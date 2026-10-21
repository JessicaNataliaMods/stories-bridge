"""Verify two offline conversions and ZIP only the asset-free tool bundle."""
import argparse,hashlib,json,pathlib,zipfile


def digest(path):
    with path.open('rb') as stream:return hashlib.file_digest(stream,'sha256').hexdigest()


def outputs(directory):
    files={}
    for folder in ('rw-assets','radar','layout-review'):
        for path in (directory/folder).rglob('*'):
            if path.is_file():files[path.relative_to(directory).as_posix()]=digest(path)
    for name in ('world.col','water.dat','source-placements.json','layout-validation.json','material-validation.json','texture-pixel-validation.json'):
        files[name]=digest(directory/name)
    return files


def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('development',type=pathlib.Path);p.add_argument('standalone',type=pathlib.Path)
    p.add_argument('bundle',type=pathlib.Path);p.add_argument('report',type=pathlib.Path)
    a=p.parse_args();first=outputs(a.development);second=outputs(a.standalone)
    if first!=second:raise ValueError('Standalone output differs: '+str(sorted(n for n in first.keys()|second.keys() if first.get(n)!=second.get(n))))
    development=json.loads((a.development/'manifest.json').read_text());standalone=json.loads((a.standalone/'manifest.json').read_text())
    for field in ('sourceHashes','models','renderLayers','collisionModels','renderLayerBounds','layoutReview','textureNames','dictionaryVariants'):
        if development[field]!=standalone[field]:raise ValueError('Standalone semantic manifest differs: '+field)
    keys=('level','worldId','model','entityPool','entityIndex','resources','frameOnly','frameAliases','sourceEntityModel','variantFromRelatedLodModel')
    frames=lambda m:[{k:r[k] for k in keys if k in r} for r in m.get('prototypeFramesWithoutEntityBinding',[])]
    if frames(development)!=frames(standalone):raise ValueError('Standalone prototype frame/binding metadata differs')
    bundle_manifest=json.loads((a.bundle/'bundle-manifest.json').read_text())
    if bundle_manifest['assetFree'] is not True:raise ValueError('Tool bundle is not marked asset-free')
    actual={path.relative_to(a.bundle).as_posix():digest(path) for path in a.bundle.rglob('*') if path.is_file() and path.name!='bundle-manifest.json'}
    if bundle_manifest['files']!=actual:raise ValueError('Public bundle file/hash mismatch')
    archive=a.bundle.with_suffix('.zip')
    if archive.exists():raise ValueError('Public ZIP already exists')
    with zipfile.ZipFile(archive,'w',compression=zipfile.ZIP_DEFLATED) as zipped:
        for path in sorted(a.bundle.rglob('*')):
            if path.is_file():zipped.write(path,path.relative_to(a.bundle).as_posix())
    with zipfile.ZipFile(archive) as zipped:
        expected={**actual,'bundle-manifest.json':digest(a.bundle/'bundle-manifest.json')}
        if set(zipped.namelist())!=set(expected):raise ValueError('ZIP file list differs')
        for name,value in expected.items():
            if hashlib.sha256(zipped.read(name)).hexdigest()!=value:raise ValueError('ZIP file/hash mismatch: '+name)
    result=dict(development=str(a.development.resolve()),standalone=str(a.standalone.resolve()),
                comparedFiles=len(first),allFinalOutputsIdentical=True,sourceManifestIdentical=True,
                outputGroups=dict(collections_counts(first)),bundleFiles=len(actual),bundleAssetFree=True,
                bundleHashesVerified=True,zip=str(archive.resolve()),zipBytes=archive.stat().st_size,
                zipSHA256=digest(archive),zipHashesVerified=True,gameExecuted=False,installed=False,
                mapComplete=development['complete'])
    a.report.write_text(json.dumps(result,indent=2),encoding='utf-8');print(json.dumps(result,indent=2))


def collections_counts(files):
    counts={}
    for name in files:
        group=name.split('/')[0] if '/' in name else 'root'
        counts[group]=counts.get(group,0)+1
    return counts.items()


if __name__=='__main__':main()
