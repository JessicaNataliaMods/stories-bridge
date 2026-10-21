"""Independent checks of generated SA text IPLs, source transforms and LODs.

Only validates the review layout; it never declares pending source coverage or
runtime scene swaps complete. No GTA or map viewer is executed.
"""
import argparse,collections,json,math,pathlib,struct


def validate(package):
    manifest=json.loads((package/'manifest.json').read_text())
    review=json.loads((package/'layout-review/manifest.json').read_text())
    source=json.loads((package/'source-placements.json').read_text())['placements']
    definitions={};section=None
    for line in (package/'layout-review/world.ide').read_text().splitlines():
        if line in ('objs','tobj','txdp','end'):section=None if line=='end' else line;continue
        if not line.strip() or section=='txdp':continue
        fields=[v.strip() for v in line.split(',')]
        if len(fields)!=(7 if section=='tobj' else 5):raise ValueError('Incorrect SA IDE field count')
        ident=int(fields[0]);name,txd=fields[1:3]
        if ident in definitions or not 0<=ident<20000:raise ValueError('Invalid/duplicate native model ID')
        if not name or len(name)>22 or not txd or len(txd)>23:raise ValueError('Native model/texture name capacity exceeded')
        if not math.isfinite(float(fields[3])) or float(fields[3])<0:raise ValueError('Invalid draw distance')
        if not (package/'rw-assets'/(name+'.dff')).is_file() or not (package/'rw-assets'/(txd+'.txd')).is_file():
            raise ValueError('IDE refers to a missing asset')
        if section=='tobj' and any(not 0<=int(v)<24 for v in fields[5:7]):raise ValueError('Invalid time-model hours')
        definitions[ident]=(name,txd)
    if len(definitions)!=review['modelDefinitions']:raise ValueError('Review definition count differs')
    expected=collections.defaultdict(list)
    for p in source:expected[p['asset'],p['area']].append(p)
    children=collections.Counter();instances=links=0;maximum_error=0.
    for info in review['reviewSnapshot']['files']:
        lines=(package/'layout-review'/info['file']).read_text().splitlines()
        if lines[0]!='inst' or lines[-1]!='end':raise ValueError('Unexpected text IPL structure')
        rows=[]
        for line in lines[1:-1]:
            f=[v.strip() for v in line.split(',')]
            if len(f)!=11:raise ValueError('Incorrect SA text IPL field count')
            ident=int(f[0]);name=f[1];area=int(f[2]);position=list(map(float,f[3:6]));q=list(map(float,f[6:10]));lod=int(f[10])
            if ident not in definitions or definitions[ident][0]!=name:raise ValueError('IPL model identity mismatch')
            if not all(math.isfinite(v) for v in (*position,*q)) or abs(sum(v*v for v in q)-1)>.00001:
                raise ValueError('Invalid native IPL transform')
            candidates=[p for p in expected[name,area] if max(abs(a-b) for a,b in zip(position,p['position']))<.00001]
            if not candidates:raise ValueError('Placement lacks an original source entity')
            # Native SA conjugates the file quaternion before building its
            # matrix; verify this against original entity columns independently.
            x,y,z,w=q;x,y,z=-x,-y,-z
            basis=((1-2*(y*y+z*z),2*(x*y+w*z),2*(x*z-w*y)),
                   (2*(x*y-w*z),1-2*(x*x+z*z),2*(y*z+w*x)),
                   (2*(x*z+w*y),2*(y*z-w*x),1-2*(x*x+y*y)))
            error=min(max(abs(a-b) for old,new in zip(p['basis'],basis) for a,b in zip(old,new)) for p in candidates)
            if error>.0001:raise ValueError('Native placement rotation differs from original entity')
            maximum_error=max(maximum_error,error);rows.append((ident,name,area,lod))
        if len(rows)!=info['instances'] or len(rows)>4000:raise ValueError('Native IPL capacity/count mismatch')
        for i,(_ident,_name,area,lod) in enumerate(rows):
            if lod==-1:continue
            if not 0<=lod<len(rows) or lod==i or rows[lod][2]!=area:raise ValueError('Invalid local LOD parent')
            seen={i};cursor=lod
            while cursor!=-1:
                if cursor in seen:raise ValueError('Cyclic LOD graph')
                seen.add(cursor);cursor=rows[cursor][3]
            children[info['file'],lod]+=1;links+=1
        instances+=len(rows)
    if instances!=review['reviewSnapshot']['instances'] or links!=review['reviewSnapshot']['nativeLodLinks']:
        raise ValueError('Native snapshot count mismatch')
    if any(count>255 for count in children.values()):raise ValueError('LOD child count exceeds the native byte field')
    # Additional components own render bounds, never copied physical faces.
    data=(package/'world.col').read_bytes();pos=0;bounds={}
    while pos<len(data):
        if data[pos:pos+4]!=b'COL2':raise ValueError('Unexpected collision format')
        length=struct.unpack_from('<I',data,pos+4)[0]+8
        if length<108 or pos+length>len(data):raise ValueError('Invalid collision entry size')
        name=data[pos+8:pos+30].split(b'\0')[0].decode('ascii')
        if name.lower() in bounds:raise ValueError('Duplicate native collision identity')
        bounds[name.lower()]=(pos,length);pos+=length
    for layer in manifest['renderLayers']:
        pos,length=bounds[layer['name'].lower()]
        if length!=108 or struct.unpack_from('<3H2BI6I',data,pos+72)!=(0,)*12:
            raise ValueError('Render component duplicates physical collision surfaces')
        values=struct.unpack_from('<10f',data,pos+32);mn=values[:3];mx=values[3:6];center=values[6:9];radius=values[9]
        b=layer['bounds']
        if any(mn[i]>b[i] or mx[i]<b[i+3] for i in range(3)):raise ValueError('Render component bounds exclude geometry')
        if radius<math.dist(center,mx)-.0001:raise ValueError('Render component sphere excludes its bounds')
    result=dict(valid=True,modelDefinitions=len(definitions),instances=instances,nativeLodLinks=links,
                maximumLodChildren=max(children.values(),default=0),sourceBasisMaximumError=maximum_error,
                renderLayerBounds=len(manifest['renderLayers']),gameExecuted=False,installed=False,
                sourceCoverageComplete=manifest['complete'])
    (package/'layout-validation.json').write_text(json.dumps(result,indent=2),encoding='utf-8')
    return result


if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('package',type=pathlib.Path)
    print(json.dumps(validate(p.parse_args().package),indent=2))
