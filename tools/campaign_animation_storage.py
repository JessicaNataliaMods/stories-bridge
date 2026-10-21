"""Store mission animation media privately and use two native block slots.

Only container headers change: original NAME/DGAN records remain byte-exact.
Logical gesture groups stay in the fifth index column to preserve LOAD/UNLOAD.
"""
import hashlib, json, pathlib, shutil, struct

def digest(path):
    with path.open('rb') as stream:
        return hashlib.file_digest(stream, 'sha256').hexdigest()

def sections(data):
    at = 0
    while at < len(data):
        if len(data)-at < 8:
            raise ValueError('Truncated ANPK section')
        tag, size = struct.unpack_from('<4sI', data, at)
        end, padded = at+8+size, at+8+((size+3)&~3)
        if padded > len(data):
            raise ValueError('ANPK section exceeds parent')
        yield tag, data[at+8:end], data[at:padded]
        at = padded

def records(path):
    data = path.read_bytes()
    tag, size = struct.unpack_from('<4sI', data)
    if tag != b'ANPK' or 8+size > len(data) or any(data[8+size:]):
        raise ValueError('Invalid prepared ANPK root')
    children = list(sections(data[8:8+size]))
    tag, info, _ = children.pop(0)
    count = struct.unpack_from('<I', info)[0]
    if tag != b'INFO' or len(children) != 2*count:
        raise ValueError('Invalid prepared ANPK record count')
    output = {}
    for i in range(count):
        name, body = children[2*i:2*i+2]
        label = name[1].split(b'\0')[0].decode('ascii')
        if name[0] != b'NAME' or body[0] != b'DGAN' or label in output:
            raise ValueError('Invalid/duplicate prepared animation')
        output[label] = name[2]+body[2]
    return info[4:].split(b'\0')[0].decode('ascii'), output

def pack(tag, payload):
    return struct.pack('<4sI', tag, len(payload))+payload+b'\0'*((-len(payload))%4)

def prepare(gestures, named, output):
    if output.exists():
        raise ValueError('Choose a new animation storage directory')
    rows = [line.split('\t') for line in (gestures/'index.tsv').read_text().splitlines()]
    if len(rows) != 71 or len({r[1] for r in rows}) != 25:
        raise ValueError('Incomplete original gesture inventory')
    manifest = json.loads((gestures/'manifest.json').read_text())
    combined, source_files = {}, []
    for file in manifest['files']:
        path = gestures/file['path']
        if digest(path).upper() != file['sha256']:
            raise ValueError('Prepared gesture source changed')
        alias, clips = records(path)
        if alias != file['alias']:
            raise ValueError('Prepared gesture alias mismatch')
        for label, record in clips.items():
            if label.lower() in {x.lower() for x in combined}:
                raise ValueError('Gesture names collide; refusing ambiguous bundling')
            combined[label] = record
        source_files.append(dict(alias=alias, path=str(path.resolve()), sha256=digest(path)))
    if not 1 <= len(combined) <= 256:
        raise ValueError('Gesture bundle exceeds parser/native limits')
    output.mkdir(parents=True)
    payload = pack(b'INFO', struct.pack('<I', len(combined))+b'LG00\0')
    payload += b''.join(combined.values())
    (output/'LG00.ifp.bin').write_bytes(pack(b'ANPK', payload))
    (output/'index.tsv').write_text(''.join('\t'.join((state,'LG00',clip,flags,logical))+'\n'
                                           for state,logical,clip,flags in rows))
    (output/'named').mkdir()
    named_rows = [line.split('\t') for line in (named/'index.tsv').read_text().splitlines()]
    if len(named_rows) != 1 or named_rows[0] != ['riot','LN00']:
        raise ValueError('Unexpected named-block inventory; re-audit native capacity')
    named_manifest = json.loads((named/'manifest.json').read_text())
    for file in named_manifest['files']:
        path = named/file['path']
        if digest(path).upper() != file['sha256']:
            raise ValueError('Prepared named animation source changed')
        shutil.copyfile(path, output/'named'/(file['path']+'.bin'))
        source_files.append(dict(alias=file['alias'],path=str(path.resolve()),sha256=digest(path)))
    shutil.copyfile(named/'index.tsv',output/'named/index.tsv')
    report = dict(version=21, gameExecuted=False, states=71, logicalGestureGroups=25,
                  gestureClips=len(combined), nativeBlocks=2, automaticStreamingBlocks=0,
                  originalNameDganRecordsPreserved=True, sourceFiles=source_files,
                  files=[dict(path=str(p.relative_to(output)).replace('\\','/'),sha256=digest(p))
                         for p in sorted(output.rglob('*')) if p.is_file()])
    (output/'storage.json').write_text(json.dumps(report,indent=2))
    return report
