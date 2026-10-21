"""Read-only ISO9660 extraction of the inputs required by the map converter."""
import pathlib
import struct

REQUIRED = ['CHK/PS2/GAME.DTZ', 'MODELS/GTA3PS2.IMG'] + [
    f'MODELS/{name}.{ext}' for name in ('INDUST','COMMER','SUBURB','UNDERG')
    for ext in ('LVZ','IMG')]


def extract_iso(path, output):
    output=pathlib.Path(output)
    if output.exists():raise ValueError('ISO extraction destination already exists')
    files={};size=path.stat().st_size
    with path.open('rb') as stream:
        def read(offset,n):
            if offset<0 or n<0 or offset+n>size:raise ValueError('ISO extent outside image')
            stream.seek(offset);value=stream.read(n)
            if len(value)!=n:raise ValueError('Truncated ISO extent')
            return value
        pvd=read(16*2048,2048)
        if pvd[:7]!=b'\x01CD001\x01':raise ValueError('Expected a PS2 ISO9660 image')
        if struct.unpack_from('<H',pvd,128)[0]!=2048:raise ValueError('Unsupported ISO block size')
        def extent(record):
            sector=struct.unpack_from('<I',record,2)[0];count=struct.unpack_from('<I',record,10)[0]
            if sector!=struct.unpack_from('>I',record,6)[0] or count!=struct.unpack_from('>I',record,14)[0]:
                raise ValueError('Inconsistent ISO extent fields')
            if record[26] or record[27] or record[25]&128:raise ValueError('Unsupported interleaved/multi-extent ISO file')
            if sector*2048+count>size:raise ValueError('ISO file extent exceeds image')
            return sector*2048,count
        visited=set()
        def directory(record,prefix,depth):
            if depth>10:raise ValueError('ISO directory nesting exceeds limit')
            start,n=extent(record)
            if (start,n) in visited:raise ValueError('ISO directory cycle')
            visited.add((start,n))
            if n>16*1024*1024:raise ValueError('ISO directory exceeds limit')
            raw=read(start,n);p=0
            while p<n:
                length=raw[p]
                if length==0:p=(p//2048+1)*2048;continue
                entry=raw[p:p+length]
                if len(entry)!=length or length<34 or 33+entry[32]>length:raise ValueError('Invalid ISO directory record')
                p+=length;name=entry[33:33+entry[32]]
                if name in (b'\0',b'\1'):continue
                name=name.decode('ascii').split(';')[0].rstrip('.')
                if not name or any(c not in 'ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789_.-' for c in name):
                    raise ValueError('Unsafe ISO file name')
                full=(prefix+'/'+name).lstrip('/')
                if entry[25]&2:
                    if any(x.startswith(full+'/') for x in REQUIRED):directory(entry,full,depth+1)
                elif full in REQUIRED:
                    if full in files:raise ValueError('Duplicate required file in ISO')
                    files[full]=extent(entry)
        directory(pvd[156:156+pvd[156]],'',0)
        missing=set(REQUIRED)-set(files)
        if missing:raise ValueError('ISO lacks LCS PS2 inputs: '+', '.join(sorted(missing)))
        output.mkdir(parents=True)
        for name,(offset,n) in files.items():
            target=output/name;target.parent.mkdir(parents=True,exist_ok=True)
            stream.seek(offset)
            with target.open('xb') as out:
                remaining=n
                while remaining:
                    data=stream.read(min(4*1024*1024,remaining))
                    if not data:raise ValueError('Truncated ISO file')
                    out.write(data);remaining-=len(data)
    return output
