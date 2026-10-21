"""Check private bundled animations against all original prepared records."""
import hashlib,json,pathlib,struct,sys
package=pathlib.Path(sys.argv[1]);gestures=pathlib.Path(sys.argv[2]);named=pathlib.Path(sys.argv[3])

def records(path):
    data=path.read_bytes()
    assert data[:4]==b'ANPK'
    end=8+struct.unpack_from('<I',data,4)[0]
    at=8
    def take():
        nonlocal at
        tag,size=struct.unpack_from('<4sI',data,at);start=at;at+=8+((size+3)&~3)
        assert at<=end
        return tag,data[start+8:start+8+size],data[start:at]
    tag,info,_=take();assert tag==b'INFO'
    count=struct.unpack_from('<I',info)[0];result={}
    for _ in range(count):
        tag,label,a=take();assert tag==b'NAME'
        tag,_,b=take();assert tag==b'DGAN'
        name=label.split(b'\0')[0].decode('ascii');assert name not in result
        result[name]=a+b
    assert at==end and not any(data[end:])
    return result

meta=json.loads((package/'storage.json').read_text())
assert meta['nativeBlocks']==2 and meta['automaticStreamingBlocks']==0
assert not list(package.rglob('*.ifp'))
combined=records(package/'LG00.ifp.bin');expected={}
for row in json.loads((gestures/'manifest.json').read_text())['files']:
    expected.update(records(gestures/row['path']))
assert combined==expected, 'NAME/DGAN records, including bone tags and every keyframe, changed'
assert (package/'named/LN00.ifp.bin').read_bytes()==(named/'LN00.ifp').read_bytes()
source=[line.split('\t') for line in (gestures/'index.tsv').read_text().splitlines()]
mounted=[line.split('\t') for line in (package/'index.tsv').read_text().splitlines()]
assert len(source)==len(mounted)==71
for old,new in zip(source,mounted):
    assert new==[old[0],'LG00',old[2],old[3],old[1]], 'State, clip, flags or logical LOAD group changed'
for row in meta['files']:
    assert hashlib.sha256((package/row['path']).read_bytes()).hexdigest()==row['sha256']
print(f'PASS: 71 states, 25 logical groups, {len(combined)} byte-exact gesture records and seven original RIOT clips; two private blocks, zero loose IFPs; gameExecuted=false')
