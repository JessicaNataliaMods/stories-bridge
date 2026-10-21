"""Compare continuous PCM SDT boundaries with original PS2 VAG flags, data only.

SPU2 loops after decoding the complete terminal block (28 PCM16 samples).
Primary hardware-emulation reference: PCSX2/pcsx2 pcsx2/SPU2/Mixer.cpp,
UpdateBlockHeader and DecodeSamples (loop start flag4, end1, repeat2).
https://github.com/PCSX2/pcsx2/blob/master/pcsx2/SPU2/Mixer.cpp
This proves boundaries for these records, not a general audio conversion audit.
"""
import argparse,json,struct
from pathlib import Path

def verify(original,pcm_index):
    sdt=(original/'SFX0_PS.SDT').read_bytes()
    pcm=pcm_index.read_bytes()
    result=[]
    with (original/'SFX0_PS.RAW').open('rb') as raw:
        for sample in (4,18,183,290):
            offset,size,frequency=struct.unpack_from('<III',sdt,12*sample)
            raw.seek(offset);vag=raw.read(size)
            assert vag[:4]==b'VAGp' and size>=64 and (size-48)%16==0
            assert int.from_bytes(vag[12:16],'big')==size-48
            assert int.from_bytes(vag[16:20],'big')==frequency
            blocks=[vag[i:i+16] for i in range(48,size,16)]
            assert blocks[0]==bytes(16),'Expected omitted silent ADPCM lead block'
            ends=[i for i,b in enumerate(blocks) if b[1]&1]
            assert len(ends)==1
            terminal=ends[0]
            _,pcm_size,pcm_frequency,begin,end=struct.unpack_from('<IIIIi',pcm,20*sample)
            assert pcm_frequency==frequency
            # Skip the one silent lead block; include terminal block in full.
            assert pcm_size==terminal*28*2
            looping=bool(blocks[terminal][1]&2)
            if looping:
                starts=[i for i,b in enumerate(blocks[:terminal+1]) if b[1]&4]
                assert len(starts)==1 and starts[0]>=1
                assert begin==(starts[0]-1)*28*2
                assert end+2==pcm_size
            else:
                assert begin==0 and end==-1
            result.append(dict(sample=sample,pcmSize=pcm_size,start=begin,
                               inclusiveEnd=end,terminalBlock=terminal,looping=looping))
    return dict(valid=True,gameExecuted=False,audioDeviceOpened=False,records=result)

if __name__=='__main__':
    p=argparse.ArgumentParser();p.add_argument('--original',type=Path,required=True)
    p.add_argument('--pcm-index',type=Path,required=True)
    a=p.parse_args();print(json.dumps(verify(a.original,a.pcm_index),indent=2))
