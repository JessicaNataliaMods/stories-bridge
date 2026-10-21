"""Compare final D3D9 TXD pixels/alpha against decoded PS2 intermediates."""
import json,pathlib,struct,sys


def chunks(raw,start,end):
    while start<end:
        if end-start<12:raise ValueError('Truncated RenderWare header')
        kind,size,_version=struct.unpack_from('<3I',raw,start)
        payload=start+12;following=payload+size
        if following>end:raise ValueError('RenderWare chunk exceeds its parent')
        yield kind,payload,following
        start=following


def native_textures(path):
    raw=path.read_bytes();top=list(chunks(raw,0,len(raw)))
    if len(top)!=1 or top[0][0]!=0x16:raise ValueError('Expected one TXD')
    expected=None;images={}
    for kind,start,end in chunks(raw,*top[0][1:]):
        if kind==1:
            if end-start!=4:raise ValueError('Unexpected TXD header')
            expected=struct.unpack_from('<H',raw,start)[0]
        if kind!=0x15:continue
        structure=next(chunks(raw,start,end))
        if structure[0]!=1:raise ValueError('Missing native texture structure')
        data=raw[structure[1]:structure[2]]
        if len(data)<92 or struct.unpack_from('<I',data)[0]!=9:raise ValueError('Expected D3D9 raster')
        name=data[8:40].split(b'\0')[0].decode('ascii')
        fmt,d3d,w,h,depth,levels,_kind,flags,size=struct.unpack_from('<2I2H4BI',data,72)
        if levels!=1 or depth!=32 or size!=w*h*4 or len(data)!=92+size:raise ValueError('Unexpected raster storage')
        has_alpha=(flags&1)!=0
        expected_format=(0x500,21) if has_alpha else (0x600,22)
        if (fmt&0xf00,d3d)!=expected_format:
            raise ValueError('Raster format disagrees with its alpha flag')
        if name in images:raise ValueError('Duplicate native texture name')
        bgra=data[92:];rgba=bytearray(len(bgra))
        rgba[0::4]=bgra[2::4];rgba[1::4]=bgra[1::4];rgba[2::4]=bgra[0::4];rgba[3::4]=bgra[3::4]
        images[name]=(w,h,bytes(rgba),has_alpha)
    if expected!=len(images):raise ValueError('Native texture count mismatch')
    return images


def validate(package):
    count=0;byte_count=0;dictionaries=0
    for source in sorted((package/'intermediate').glob('*.pst')):
        raw=source.read_bytes()
        if len(raw)<8 or raw[:4]!=b'PST1':raise ValueError('Invalid intermediate')
        native=native_textures(package/'rw-assets'/(source.stem+'.txd'))
        declared=struct.unpack_from('<I',raw,4)[0];position=8
        for _ in range(declared):
            if position+40>len(raw):raise ValueError('Truncated texture intermediate')
            name=raw[position:position+32].split(b'\0')[0].decode('ascii')
            w,h=struct.unpack_from('<2I',raw,position+32);position+=40;size=w*h*4
            if not w or not h or position+size>len(raw):raise ValueError('Invalid intermediate pixel extent')
            rgba=raw[position:position+size];position+=size
            expected=(w,h,rgba,any(a!=255 for a in rgba[3::4]))
            if native.pop(name,None)!=expected:raise ValueError(f'Native pixels/alpha differ: {source.stem}:{name}')
            count+=1;byte_count+=size
        if position!=len(raw) or native:raise ValueError('Unparsed or extra textures')
        dictionaries+=1
    if not dictionaries:raise ValueError('No decoded source textures to validate')
    report=dict(dictionaries=dictionaries,textures=count,pixelBytes=byte_count,
                allSourcePixelsAndAlphaPreserved=True,opaqueRasterPadding=255,
                gameExecuted=False,complete=True)
    (package/'texture-pixel-validation.json').write_text(json.dumps(report,indent=2),encoding='utf-8')
    return report


if __name__=='__main__':print(json.dumps(validate(pathlib.Path(sys.argv[1])),indent=2))
