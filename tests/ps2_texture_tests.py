"""Native TXD serialization regression with generated pixels, without GTA."""
import hashlib, pathlib, struct, subprocess, sys, tempfile, unittest
sys.path.insert(0,str(pathlib.Path(__file__).resolve().parents[1]/'tools'))
from ps2_geometry_audit import geometries

WRITER=pathlib.Path(sys.argv.pop(1)).resolve()
ROOT=pathlib.Path(__file__).resolve().parents[1]
TEST_TEMP=ROOT/'build'/'ps2-test-temp'
TEST_TEMP.mkdir(parents=True,exist_ok=True)


def chunks(raw,start,end):
    while start<end:
        ident,size,_version=struct.unpack_from('<3I',raw,start)
        payload=start+12;next_chunk=payload+size
        if next_chunk>end:raise ValueError('Truncated RenderWare chunk')
        yield ident,payload,next_chunk
        start=next_chunk
    if start!=end:raise ValueError('Misaligned RenderWare chunks')


def textures(raw):
    top=list(chunks(raw,0,len(raw)))
    if len(top)!=1 or top[0][0]!=0x16:raise ValueError('Expected one TXD')
    for ident,start,end in chunks(raw,*top[0][1:]):
        if ident!=0x15:continue
        native=next(chunks(raw,start,end))
        if native[0]!=1:raise ValueError('Expected native structure')
        data=raw[native[1]:native[2]]
        platform=struct.unpack_from('<I',data)[0]
        if platform!=9:raise ValueError('Expected D3D9')
        name=data[8:40].split(b'\0')[0].decode('ascii')
        fmt,d3d,w,h,depth,levels,_kind,flags,size=struct.unpack_from('<2I2H4BI',data,72)
        if levels!=1 or size!=w*h*4 or len(data)!=92+size:raise ValueError('Unexpected raster layout')
        yield name,fmt,d3d,w,h,depth,flags,data[92:]


class NativePixels(unittest.TestCase):
    def test_mesh_preserves_nonwhite_prelight_and_valid_binmesh(self):
        colors=[(8,41,90,255),(255,100,0,0),(31,63,127,255)]
        with tempfile.TemporaryDirectory(dir=TEST_TEMP) as directory:
            directory=pathlib.Path(directory);source=directory/'input';source.mkdir()
            raw=struct.pack('<4s3I',b'PSM1',3,1,1)+b'fixture'.ljust(32,b'\0')
            for position,color in zip(((0,0,0),(1,0,0),(0,1,0)),colors):
                raw+=struct.pack('<5f4B',*position,0,0,*color)
            raw+=struct.pack('<4H',0,1,2,0)
            (source/'fixture.psm').write_bytes(raw)
            process=subprocess.run([str(WRITER),str(source),str(directory/'output')],capture_output=True,text=True)
            self.assertEqual(process.returncode,0,process.stdout+process.stderr)
            model=geometries(directory/'output/fixture.dff')[0]
            self.assertEqual(model['colors'],b''.join(bytes(c) for c in colors))
            self.assertTrue(model['prelit']);self.assertTrue(model['binmesh'])

    def test_original_empty_dictionary_has_a_native_txd(self):
        with tempfile.TemporaryDirectory(dir=TEST_TEMP) as directory:
            directory=pathlib.Path(directory);source=directory/'input';source.mkdir()
            (source/'subglass.pst').write_bytes(struct.pack('<4sI',b'PST1',0))
            output=directory/'output'
            process=subprocess.run([str(WRITER),str(source),str(output)],capture_output=True,text=True)
            self.assertEqual(process.returncode,0,process.stdout+process.stderr)
            raw=(output/'subglass.txd').read_bytes()
            self.assertEqual(list(textures(raw)),[])
            top=list(chunks(raw,0,len(raw)))
            header=next(chunks(raw,*top[0][1:]))
            self.assertEqual(header[0],1)
            self.assertEqual(struct.unpack_from('<H',raw,header[1])[0],0)

    def test_ps2_black_contribution_receives_emissive_and_native_night_colors(self):
        with tempfile.TemporaryDirectory(dir=TEST_TEMP) as directory:
            directory=pathlib.Path(directory);source=directory/'input';source.mkdir()
            # Actual GAME.DTZ clear-weather endpoints. Alpha remains independent.
            (source/'map-lighting.psl').write_bytes(b'PSL1'+bytes((156,191,182,60,57,30,156,163,171,34,31,40)))
            raw=struct.pack('<4s3I',b'PSM1',3,1,1)+b'fixture'.ljust(32,b'\0')
            for position in ((0,0,0),(1,0,0),(0,1,0)):
                raw+=struct.pack('<5f4B',*position,0,0,0,0,0,255)
            raw+=struct.pack('<4H',0,1,2,0);(source/'fixture.psm').write_bytes(raw)
            process=subprocess.run([str(WRITER),str(source),str(directory/'output')],capture_output=True,text=True)
            self.assertEqual(process.returncode,0,process.stdout+process.stderr)
            model=geometries(directory/'output/fixture.dff')[0]
            self.assertEqual(model['colors'],bytes((60,57,30,255))*3)
            self.assertEqual(model['night'],bytes((34,31,40,255))*3)
            self.assertEqual(model['pipeline'],0x53f20098)
            self.assertFalse(model['flags']&32)

    def test_pixels_alpha_flags_and_repeatable_bytes(self):
        images={
            'opaque':(4,2,bytes(v for i in range(8) for v in (i*29,255-i*17,i*13,255))),
            'alpha':(4,2,bytes(v for i,a in enumerate((0,1,128,255,254,32,64,255)) for v in (i*23,i*19,255-i*11,a))),
            'ocean':(1,1,b'\x21\x74\x93\xff'),
        }
        with tempfile.TemporaryDirectory(dir=TEST_TEMP) as directory:
            directory=pathlib.Path(directory);source=directory/'input';source.mkdir()
            raw=struct.pack('<4sI',b'PST1',len(images))
            for name,(w,h,pixels) in images.items():
                raw+=name.encode().ljust(32,b'\0')+struct.pack('<2I',w,h)+pixels
            (source/'fixture.pst').write_bytes(raw)
            results=[]
            for i in range(2):
                output=directory/str(i)
                process=subprocess.run([str(WRITER),str(source),str(output)],capture_output=True,text=True)
                self.assertEqual(process.returncode,0,process.stdout+process.stderr)
                results.append((output/'fixture.txd').read_bytes())
            self.assertEqual(results[0],results[1])
            process=subprocess.run([str(WRITER),'--texture-fingerprints',str(directory/'0')],capture_output=True,text=True)
            self.assertEqual(process.returncode,0,process.stdout+process.stderr)
            fingerprints={}
            for line in process.stdout.splitlines():
                dictionary,name,w,h,digest=line.split('\t')
                self.assertEqual(dictionary,'fixture')
                fingerprints[name]=(int(w),int(h),digest)
            self.assertEqual(fingerprints,{name:(w,h,hashlib.sha256(struct.pack('<2I',w,h)+rgba).hexdigest())
                                          for name,(w,h,rgba) in images.items()})
            decoded=list(textures(results[0]));self.assertEqual(len(decoded),len(images))
            for name,fmt,d3d,w,h,depth,flags,pixels in decoded:
                expected_w,expected_h,rgba=images[name]
                self.assertEqual((w,h),(expected_w,expected_h))
                expected_bgra=b''.join(bytes((rgba[i+2],rgba[i+1],rgba[i],rgba[i+3])) for i in range(0,len(rgba),4))
                self.assertEqual(pixels,expected_bgra)
                has_alpha=any(a!=255 for a in rgba[3::4])
                self.assertEqual((fmt&0xf00,d3d,depth,flags&1),
                                 (0x500,21,32,1) if has_alpha else (0x600,22,32,0))


if __name__=='__main__':unittest.main()
