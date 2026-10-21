"""Independent RenderWare prelight/index audit using only the standard library."""
import collections,json,struct
from ps2_lighting import adapt_colors


def chunks(raw,start,end):
    while start<end:
        if start+12>end:raise ValueError('Truncated RW header')
        ident,size,version=struct.unpack_from('<3I',raw,start);stop=start+12+size
        if stop>end:raise ValueError('RW chunk exceeds parent')
        yield ident,start+12,stop,version
        start=stop


def geometries(path):
    raw=path.read_bytes();result=[]
    top=list(chunks(raw,0,len(raw)))
    if len(top)!=1 or top[0][0]!=16:raise ValueError('Expected a single DFF clump')
    pipelines={}
    for ident,start,end,version in chunks(raw,top[0][1],top[0][2]):
        if ident==20:
            parts=list(chunks(raw,start,end));geometry=struct.unpack_from('<I',raw,parts[0][1]+4)[0]
            for plugin,p,q,_ in parts[1:]:
                if plugin!=3:continue
                for plugin,p,q,_ in chunks(raw,p,q):
                    if plugin==0x253f2f3:pipelines[geometry]=struct.unpack_from('<I',raw,p)[0]
    for ident,start,end,version in chunks(raw,top[0][1],top[0][2]):
        if ident!=26:continue
        for ident,start,end,version in chunks(raw,start,end):
            if ident!=15:continue
            parts=list(chunks(raw,start,end));ident,p,q,version=parts[0]
            if ident!=1:raise ValueError('Geometry structure missing')
            flags,nt,nv,nm=struct.unpack_from('<4I',raw,p)
            if not 0<nv<=65535 or flags&0x1000000 or nm!=1:raise ValueError('Unsupported native/oversized geometry')
            p+=16
            library=(((version>>14)&0x3ff00)+0x30000)|((version>>16)&0x3f)
            if library<0x34000:p+=12
            colors=raw[p:p+4*nv] if flags&8 else b'\xff'*(4*nv)
            if flags&8:p+=4*nv
            texsets=(flags>>16)&255
            if not texsets:texsets=2 if flags&128 else int(bool(flags&4))
            p+=8*nv*texsets
            faces=[(a,b,c,mat) for b,a,mat,c in struct.iter_unpack('<4H',raw[p:p+8*nt])]
            if any(max(f[:3])>=nv for f in faces):raise ValueError('Triangle index exceeds vertex count')
            p+=8*nt
            positions,normals=struct.unpack_from('<2I',raw,p+16);p+=24
            if positions!=1 or normals not in (0,1) or p+12*nv*(positions+normals)!=q:raise ValueError('Invalid morph target extent')
            points=raw[p:p+12*nv]
            binmesh=[];found=False;night=None;textured=[]
            for ident,start,end,_ in parts[1:]:
                if ident==8:
                    materials=list(chunks(raw,start,end));_,p,q,_=materials[0]
                    count=struct.unpack_from('<I',raw,p)[0];indices=struct.unpack_from('<'+str(count)+'i',raw,p+4)
                    actual=iter(materials[1:])
                    for index in indices:
                        if index>=0:textured.append(textured[index]);continue
                        mid,p,q,_=next(actual)
                        if mid!=7:raise ValueError('Invalid material list')
                        textured.append(any(i==6 for i,*_ in chunks(raw,p,q)))
                if ident!=3:continue
                for ident,start,end,_ in chunks(raw,start,end):
                    if ident==0x253f2f9:
                        if end-start!=4+4*nv or not struct.unpack_from('<I',raw,start)[0]:raise ValueError('Invalid night prelight')
                        night=raw[start+4:end]
                    if ident!=0x50e:continue
                    mode,meshes,total=struct.unpack_from('<3I',raw,start);cursor=start+12;indices=0
                    if mode not in (0,1):raise ValueError('Unsupported BinMesh primitive')
                    for i in range(meshes):
                        n,mat=struct.unpack_from('<2I',raw,cursor);cursor+=8
                        if (mode==0 and n%3) or cursor+4*n>end:raise ValueError('Invalid BinMesh extent')
                        ids=struct.unpack_from('<'+str(n)+'I',raw,cursor);cursor+=4*n;indices+=n
                        if any(v>=nv for v in ids):raise ValueError('BinMesh index exceeds vertex count')
                        if mode==0:binmesh.extend((*ids[j:j+3],mat) for j in range(0,n,3))
                        else:
                            binmesh.extend((ids[j-2],ids[j-1],ids[j],mat) if j%2==0 else
                                           (ids[j-1],ids[j-2],ids[j],mat) for j in range(2,n))
                    if cursor!=end or indices!=total:raise ValueError('BinMesh count mismatch')
                    def canonical(face):
                        a,b,c,mat=face
                        return (*min((a,b,c),(b,c,a),(c,a,b)),mat)
                    expected=collections.Counter(canonical(f) for f in faces if len(set(f[:3]))==3)
                    actual=collections.Counter(canonical(f) for f in binmesh if len(set(f[:3]))==3)
                    if actual!=expected:raise ValueError('BinMesh topology differs from geometry triangles: '+path.name)
                    found=True
            vertex_textured=[False]*nv
            for *ids,mat in faces:
                if textured[mat]:
                    for v in ids:vertex_textured[v]=True
            result.append(dict(vertices=nv,triangles=nt,colors=colors,points=points,prelit=bool(flags&8),binmesh=found,
                               flags=flags,night=night,pipeline=pipelines.get(len(result),0),textured=vertex_textured))
    if not result:raise ValueError('No DFF geometries')
    return result


def validate(package):
    compiled=streamed=vertices=nonwhite=missing_source_prelight=0
    lighting_path=package/'lighting-source.json'
    lighting=json.loads(lighting_path.read_text()) if lighting_path.exists() else None
    def expected_colors(raw,endpoint,textured):
        return b''.join(adapt_colors(raw[4*i:4*i+4],endpoint,flag) for i,flag in enumerate(textured))
    for file in (package/'rw-assets').glob('*.dff'):
        native=geometries(file)
        if len(native)!=1 or not native[0]['prelit'] or not native[0]['binmesh']:
            raise ValueError('Expected one prelit, indexed near atomic: '+file.name)
        geometry=native[0];nv=geometry['vertices'];colors=geometry['colors']
        psm=package/'intermediate'/(file.stem+'.psm')
        if psm.exists():
            raw=psm.read_bytes();magic,count,nt,nm=struct.unpack_from('<4s3I',raw);start=16+32*nm
            if magic!=b'PSM1' or count!=nv or len(raw)!=start+24*nv+8*nt:raise ValueError('Intermediate extent mismatch')
            expected=b''.join(raw[start+24*i+20:start+24*i+24] for i in range(nv))
            original=expected
            compiled+=1
        else:
            source=geometries(package/'streamed-source'/file.name)
            matches=[g for g in source if g['vertices']==nv and g['points']==geometry['points']]
            if not matches:raise ValueError('Original MDL prelight/positions changed: '+file.name)
            if not matches[0]['prelit']:missing_source_prelight+=1
            original=matches[0]['colors']
            streamed+=1
        if lighting:
            expected=expected_colors(original,lighting['day'],geometry['textured'])
            night=expected_colors(original,lighting['night'],geometry['textured'])
            if expected!=colors or night!=geometry['night']:raise ValueError('Native colors differ from original PS2 lighting equation: '+file.name)
            if geometry['flags']&32 or geometry['pipeline']!=0x53f20098:raise ValueError('Map lighting does not use an unlit native SA DN pipeline: '+file.name)
        elif original!=colors:raise ValueError('PS2 source prelight changed: '+file.name)
        vertices+=nv;nonwhite+=sum(colors[i:i+3]!=b'\xff\xff\xff' for i in range(0,len(colors),4))
    result=dict(valid=True,compiledSourceLightingValidated=compiled,originalMdlLightingValidated=streamed,
                originalMdlWithoutPrelight=missing_source_prelight,vertices=vertices,nonwhitePrelitVertices=nonwhite,
                nativeDayNightLighting=bool(lighting),intermediateHoursAndWeatherApproximate=bool(lighting),
                nativeBinMeshMatchesTriangles=compiled+streamed,allIndicesWithinVertexCapacity=True,gameExecuted=False)
    (package/'geometry-validation.json').write_text(json.dumps(result,indent=2),encoding='utf-8')
    return result


if __name__=='__main__':
    import pathlib,sys
    print(json.dumps(validate(pathlib.Path(sys.argv[1])),indent=2))
