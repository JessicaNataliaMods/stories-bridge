"""Verify every original animation/keyframe and the declared block namespace."""
import hashlib,json,pathlib,runpy,sys
package=pathlib.Path(sys.argv[1]);root=pathlib.Path(__file__).resolve().parents[1]
sys.argv=[str(root/'tests/gameplay_animation_tests.py'),str(root/'out/gameplay-animations-v4')]
helpers=runpy.run_path(str(root/'tests/gameplay_animation_tests.py'));archive,role,thigh_follows_spine=helpers['archive'],helpers['role'],helpers['thigh_follows_spine']
manifest=json.loads((package/'manifest.json').read_text());rows=dict(line.split('\t') for line in (package/'index.tsv').read_text().splitlines())
assert set(rows)=={f['original'] for f in manifest['files']}
frames=0
for f in manifest['files']:
    assert rows[f['original']]==f['alias'] and f['alias'].startswith('LN') and len(f['alias'])==4
    source=pathlib.Path(f['source']);target=package/f['path']
    assert target.parent==package and target.name==f['alias']+'.ifp'
    assert hashlib.sha256(source.read_bytes()).hexdigest().upper()==f['sourceSha256']
    assert hashlib.sha256(target.read_bytes()).hexdigest().upper()==f['sha256']
    original_name,original=archive(source);native_name,native=archive(target)
    assert original_name.lower()==f['original'] and native_name==f['alias'] and set(original)==set(native)
    for name,sequences in original.items():
        assert set(sequences)==set(native[name])
        spine=next((b for b in sequences if role(b)=='spine'),None)
        for bone,(tag,records,raw) in sequences.items():
            nt,nr,native_raw=native[name][bone]
            if role(bone)=='thigh' and spine is not None and raw and sequences[spine][2]:
                assert thigh_follows_spine(raw,sequences[spine][2],native_raw),(name,bone,'thigh is not spine * thigh')
            else:assert nr==records
            if tag is not None and tag>=0:assert nt==tag
            if nt<0:assert sum(records.values())<=1
            frames+=sum(records.values())
print(f'PASS: {len(rows)} complete original named blocks, {frames} keyframes (thighs follow the spine, all others unchanged); gameExecuted=false')
