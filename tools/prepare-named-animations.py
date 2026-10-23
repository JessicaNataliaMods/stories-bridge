"""Prepare original named blocks requested by the unmodified campaign."""
import hashlib, json, pathlib, re, subprocess
import lcs_ifp
root=pathlib.Path(__file__).resolve().parents[1]
out=root/'out/named-animations-v2';out.mkdir(exist_ok=True)
if (out/'manifest.json').exists(): raise RuntimeError('Use the existing validated package; refusing overwrite')
audit=json.loads((root/'reports/campaign-current-audit.json').read_text(encoding='utf-8-sig'))
names=sorted({name.lower() for command in audit['commands'] if command['name'] in ('COMMAND_REQUEST_ANIMATION','COMMAND_HAS_ANIMATION_LOADED','COMMAND_REMOVE_ANIMATION') for name in command['textOperands']})
files=[]
def run(exe,*args):
    r=subprocess.run([str(root/'build/Release'/f'{exe}.exe'),*map(str,args)],capture_output=True,text=True)
    if r.returncode:raise RuntimeError(r.stdout+r.stderr)
    return r.stdout
for i,name in enumerate(names):
    alias=f'LN{i:02}';source=root/'out/img-v1'/f'{name}.ifp'
    if not source.is_file():raise RuntimeError('Original named block missing: '+name)
    mapping=[]
    original=source;source=out/f'{alias}-retargeted.ifp'
    source.write_bytes(lcs_ifp.retarget_anpk(original.read_bytes())[0]) # thighs hang from Pelvis in SA
    for anim,bone,tag,frames in re.findall(r'binding animation=(\S+) name=(.+) bone_tag=(-?\d+) keyframes=(\d+)',run('lcs-ifp-inspect',source,'--bindings')):
        if int(tag)<0 and int(frames):
            if bone in ('L Toe0','R Toe0'):mapping.append(f'{anim}\t{bone}\t{44 if bone=="L Toe0" else 54}')
            elif int(frames)>1:raise RuntimeError('Unmapped moving bone: '+bone)
    tags=out/f'{alias}-tags.tsv';tags.write_text('\n'.join(mapping)+'\n')
    tagged=out/f'{alias}-tagged.ifp';run('lcs-ifp-adapt',source,tagged,'--bone-tags',tags)
    destination=out/f'{alias}.ifp';run('lcs-ifp-adapt',tagged,destination,'--block-name',alias)
    files.append(dict(original=name,alias=alias,source=str(original),sourceSha256=hashlib.sha256(original.read_bytes()).hexdigest().upper(),path=destination.name,sha256=hashlib.sha256(destination.read_bytes()).hexdigest().upper()))
(out/'index.tsv').write_text(''.join(f'{f["original"]}\t{f["alias"]}\n' for f in files))
(out/'manifest.json').write_text(json.dumps(dict(files=files,gameExecuted=False),indent=2))
print(f'Prepared {len(files)} original named animation blocks; gameExecuted=false')
