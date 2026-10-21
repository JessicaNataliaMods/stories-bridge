"""Stream all available original inputs and CMake asset fixtures into bounded ZIP parts."""
import hashlib
import json
from pathlib import Path
import re
import zipfile

ROOT=Path(__file__).resolve().parents[1]
CHUNK=64*1024*1024
LIMIT=480_000_000

def main():
    out=ROOT/'out/web-dependencies-v57'
    if out.exists():raise ValueError('Refusing existing dependency output')
    out.mkdir();inputs={};missing=[]
    skip={'.git','.codex','.agents','__pycache__','node_modules'}
    def tree(base,prefix,group):
        if not base.exists():missing.append(str(base));return
        paths=sorted(base.rglob('*')) if base.is_dir() else [base]
        for path in paths:
            relative=path.relative_to(base).as_posix() if base.is_dir() else ''
            if any(x in skip for x in path.parts) or path.suffix.lower() in ('.exe','.dll','.asi','.pdb','.obj','.pyc','.zip','.lib'):continue
            if not path.is_file():continue
            if path.is_symlink():raise ValueError('Linked input refused')
            name=prefix+('/'+relative if relative else '')
            if name in inputs:
                if inputs[name][0]!=path:raise ValueError('Input path collision')
                inputs[name][1].add(group)
            else:inputs[name]=(path,{group})
    original=ROOT / 'local/originals/lcs-ps2'
    for name in ('ANIM','CHK','DATA','ICONS','MODELS','SPLASH','TEXT'):
        tree(original/name,'lcs-ps2/'+name,'core')
    for child in sorted((original/'AUDIO').iterdir()):
        if child.name!='MUSIC':tree(child,'lcs-ps2/AUDIO/'+child.name,'core')
    tree(original/'AUDIO/MUSIC/FINALE.VB','lcs-ps2/AUDIO/MUSIC/FINALE.VB','core')
    tree(ROOT/'out/lcs-converted','lcs-sa/out/lcs-converted','core')
    tree(ROOT/'vendor','lcs-sa/vendor','core')
    if (ROOT/'third_party').exists():tree(ROOT/'third_party','lcs-sa/third_party','core')
    cmake=(ROOT/'CMakeLists.txt').read_text(encoding='utf-8')
    fixtures=sorted(set(re.findall(r'\$\{CMAKE_CURRENT_SOURCE_DIR\}/out/([^/"\s]+)',cmake)))
    for name in fixtures:tree(ROOT/'out'/name,'lcs-sa/out/'+name,'tests')
    tree(ROOT/'out/campaign-copy4-v57','lcs-sa/out/campaign-copy4-v57','tests')
    files=[];blobs={};cache={};parts=[];archive=None
    def close():
        nonlocal archive
        if archive:archive.close();archive=None
    def put(data,key):
        nonlocal archive
        if key in blobs:return
        if archive and archive.fp.tell()+len(data)+len(data)//500+20_000_000+(len(archive.filelist)+1)*512>LIMIT:close()
        if archive is None:
            name='LCS_WEB_DEPENDENCIES_V57_PART%02d.zip'%(len(parts)+1)
            parts.append(name);archive=zipfile.ZipFile(out/name,'w',zipfile.ZIP_DEFLATED,compresslevel=6)
            print('Writing '+name,flush=True)
        archive.writestr('blobs/'+key,data)
        blobs[key]=dict(part=parts[-1],bytes=len(data))
    try:
        for i,(name,(path,groups)) in enumerate(sorted(inputs.items())):
            stat=path.stat();identity=(stat.st_dev,stat.st_ino,stat.st_size,stat.st_mtime_ns)
            if identity in cache:sha,keys=cache[identity]
            else:
                whole=hashlib.sha256();keys=[]
                with path.open('rb') as stream:
                    for block in iter(lambda:stream.read(CHUNK),b''):
                        whole.update(block);key=hashlib.sha256(block).hexdigest();put(block,key);keys.append(key)
                sha=whole.hexdigest();cache[identity]=(sha,keys)
            files.append(dict(path=name,bytes=stat.st_size,sha256=sha,chunks=keys,groups=sorted(groups)))
            if (i+1)%1000==0:print('Indexed '+str(i+1)+' files',flush=True)
    finally:close()
    index=dict(schema=1,files=files,blobs=blobs,parts=parts,missingLocalInputs=missing,
        sourceRelease=57,gameExecuted=False,installed=False,
        excluded=['Unrelated radio/music tracks except original FINALE','PS2 movies and console runtime modules','Windows toolchain/runtime executables; native compilation remains local'])
    raw=(json.dumps(index,indent=2)+'\n').encode();sha=hashlib.sha256(raw).hexdigest()
    (out/'dependency-index-v57.json').write_bytes(raw)
    (out/'dependency-index-v57.sha256').write_text(sha+'  dependency-index-v57.json\n',encoding='utf-8')
    helper=(ROOT/'handoff/web-v57/hydrate_dependencies.py').read_bytes()
    readme=('Use with LCS_WEB_HANDOFF_V57_LINUX_R2.zip. Keep all PART ZIPs beside the extracted source root. '
            'Extract ONLY hydrate_dependencies.py, dependency-index-v57.json and dependency-index-v57.sha256 from PART01 into that root. '
            'Run python -B hydrate_dependencies.py, then python -B hydrate_dependencies.py --verify. '
            'All inputs are read-only; no game or installer is executed. --group core can restore only development inputs if disk space is limited. '
            'The tests group contains actual CMake fixtures and approved v57 resources; older v21 fixtures are preserved under their true original names, never aliased. '
            'Run existing portable tests and new command tests. Windows-only CTest checks still require local Windows.\n')
    with zipfile.ZipFile(out/parts[0],'a',zipfile.ZIP_DEFLATED,compresslevel=6) as first:
        first.writestr('hydrate_dependencies.py',helper);first.writestr('dependency-index-v57.json',raw)
        first.writestr('dependency-index-v57.sha256',sha+'  dependency-index-v57.json\n');first.writestr('DEPENDENCIES_README.txt',readme)
    results=[]
    for name in parts:
        path=out/name
        if path.stat().st_size>=500_000_000:raise ValueError('ZIP exceeds500MB budget')
        with zipfile.ZipFile(path) as check:
            if check.testzip() is not None:raise ValueError('ZIP CRC failure')
            for item in check.infolist():
                if not item.filename.startswith('blobs/'):continue
                expected=item.filename.split('/')[1];value=hashlib.sha256()
                with check.open(item) as stream:
                    for block in iter(lambda:stream.read(1024*1024),b''):value.update(block)
                if value.hexdigest()!=expected:raise ValueError('Blob hash mismatch')
        value=hashlib.sha256()
        with path.open('rb') as stream:
            for block in iter(lambda:stream.read(1024*1024),b''):value.update(block)
        results.append(dict(path=str(path),bytes=path.stat().st_size,sha256=value.hexdigest()))
        print('Verified '+name,flush=True)
    report=dict(files=len(files),logicalBytes=sum(x['bytes'] for x in files),uniqueBlobBytes=sum(x['bytes'] for x in blobs.values()),
        indexSha256=sha,archives=results,missingLocalInputs=missing,allAvailableCmakeFixturesIncluded=True,gameExecuted=False,installed=False)
    (ROOT/'reports/web-dependencies-v57.json').write_text(json.dumps(report,indent=2)+'\n',encoding='utf-8')
    print(json.dumps(report,indent=2))

if __name__=='__main__':main()
