"""Independent affine/bone-length checks; standard library, no embedded rig."""
import math
import struct


def palette(mesh):
    return [list(struct.unpack_from('<16f', mesh['matrices'], i * 64)) for i in range(len(mesh['bones']))]


def point(v, m):
    return [sum(v[j] * m[j*4+i] for j in range(3)) + m[12+i] for i in range(3)]


def inverse(m):
    a,b,c,d,e,f,g,h,i = [m[j] for j in (0,1,2,4,5,6,8,9,10)]
    det=a*(e*i-f*h)-b*(d*i-f*g)+c*(d*h-e*g)
    if not math.isfinite(det) or abs(det)<0.0001:
        raise ValueError('Singular bone matrix')
    r=[(e*i-f*h)/det,(c*h-b*i)/det,(b*f-c*e)/det,0,
       (f*g-d*i)/det,(a*i-c*g)/det,(c*d-a*f)/det,0,
       (d*h-e*g)/det,(b*g-a*h)/det,(a*e-b*d)/det,0,0,0,0,1]
    r[12:15]=[-sum(m[12+j]*r[j*4+k] for j in range(3)) for k in range(3)]
    return r


def distance(a,b):
    return math.sqrt(sum((x-y)**2 for x,y in zip(a,b)))


def multiply(a,b):
    return [sum(a[r*4+k]*b[k*4+c] for k in range(4)) for r in range(4) for c in range(4)]


def frame_palette_error(path, audit, mesh):
    raw=path.read_bytes();root=next(audit.chunks(raw,0,len(raw)))
    frames=audit.child(raw,root,14);data=audit.child(raw,frames,1)
    count=struct.unpack_from('<I',raw,data[1])[0]
    if data[2]-data[1] != 4+count*56:
        raise ValueError('Invalid frame list size')
    extensions=[c for c in audit.chunks(raw,frames[1],frames[2]) if c[0]==3]
    if len(extensions)!=count:raise ValueError('Missing frame extension')
    world=[];tags={}
    for i,ext in enumerate(extensions):
        f=struct.unpack_from('<12f2i',raw,data[1]+4+56*i)
        if f[12] < -1 or f[12] >= i:raise ValueError('Invalid frame parent')
        m=list(f[:3])+[0]+list(f[3:6])+[0]+list(f[6:9])+[0]+list(f[9:12])+[1]
        if f[12]>=0:m=multiply(m,world[f[12]])
        world.append(m)
        for c in audit.chunks(raw,ext[1],ext[2]):
            if c[0]==286:
                tag=struct.unpack_from('<i',raw,c[1]+4)[0]
                if tag>=0:
                    if tag in tags:raise ValueError('Duplicate frame bone tag')
                    tags[tag]=m
    palettes=palette(mesh);products=[]
    for i,tag in enumerate(mesh['bones']):
        if tag not in tags:raise ValueError('Skin bone missing its native frame')
        m=palettes[i];m[3]=m[7]=m[11]=0;m[15]=1
        products.append(multiply(m,tags[tag]))
    # Root/world orientation can be nonidentity; it must be common to all
    # bones, so a rest-pose vertex has the same transform under any weight.
    return max(abs(v-products[0][j]) for m in products for j,v in enumerate(m))


def hierarchy(path, audit):
    raw=path.read_bytes();root=next(audit.chunks(raw,0,len(raw)))
    frames=audit.child(raw,root,14)
    for ext in audit.chunks(raw,frames[1],frames[2]):
        if ext[0]!=3: continue
        for c in audit.chunks(raw,ext[1],ext[2]):
            if c[0]==286 and struct.unpack_from('<I',raw,c[1]+8)[0]:
                return list(struct.iter_unpack('<3i',raw[c[1]+20:c[2]]))
    raise ValueError('Missing rig hierarchy')


def verify(original, final, reference, audit):
    old,new,donor=(audit.mesh(p) for p in (original,final,reference))
    nodes=hierarchy(final,audit)
    if new['bones']!=donor['bones'] or nodes!=hierarchy(reference,audit):
        raise ValueError('Native animation IDs/order/stack changed')
    mapped={ {54:44,55:54,2000:44,2001:54}.get(tag,tag): i for i,tag in enumerate(old['bones']) }
    old_bind=[inverse(m) for m in palette(old)]
    new_bind=[inverse(m) for m in palette(new)]
    ref_bind=[inverse(m) for m in palette(donor)]
    frame_error=frame_palette_error(final,audit,new)
    if not math.isfinite(frame_error) or frame_error>0.00005:
        raise ValueError('Frame tree and skin bind palette disagree')
    parent=0;stack=[0];errors=[];helpers=[]
    for i,(tag,_,flags) in enumerate(nodes):
        for j in (0,1,2,4,5,6,8,9,10):
            if abs(new_bind[i][j]-ref_bind[i][j])>0.00005:
                raise ValueError('Bone orientation differs from native animation axes')
        if i:
            parent_tag=new['bones'][parent]
            actual=distance(new_bind[i][12:15],new_bind[parent][12:15])
            if tag in mapped and parent_tag in mapped:
                expected=distance(old_bind[mapped[tag]][12:15],old_bind[mapped[parent_tag]][12:15])
                errors.append(abs(actual-expected))
            else:
                expected=distance(ref_bind[i][12:15],ref_bind[parent][12:15])
                helpers.append(abs(actual-expected))
            if abs(actual-expected)>0.00005:
                raise ValueError('Original body proportions or native helper offset changed')
        if flags&2:stack.append(parent)
        parent=stack.pop() if flags&1 else i
    return dict(sourceBoneLengthsVerified=True,nativeAxesVerified=True,nativeHelperLengthsVerified=True,
                mappedEdges=len(errors),maxLengthErrorMeters=max(errors),
                frameSkinPaletteConsistent=True,maxFramePaletteError=frame_error,gameExecuted=False)
