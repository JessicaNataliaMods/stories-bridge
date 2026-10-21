"""Retail continuous sound proof; optional compiled descriptor comparison.

Reads ELF as data only. Does not launch game code or an output device.
"""
from pathlib import Path
import importlib.util
import subprocess
import sys
import struct
import hashlib

ROOT=Path(__file__).resolve().parents[1]
spec=importlib.util.spec_from_file_location('continuous_original',ROOT/'tools/inspect-original-command.py')
mod=importlib.util.module_from_spec(spec)
sys.modules[spec.name]=mod
spec.loader.exec_module(mod)
elf=mod.OriginalImage(ROOT / 'local/originals/lcs-ps2/SLUS_214.23')
elf.require_profile()
# Original ELF REGINFO gp=3D6EF0; libc rand state is gp+2288.
# This proves the executable's initial data only, not the live state after
# other shared consumers have run. Runtime binding must not reset it here.
assert struct.unpack('<I',elf.read(0x3D9178,4))[0]==180371
# Stream names live in the original IOP module, not the main EE executable.
# Retail index lookup is (index*4+index)*8, unlike the reconstruction's enum
# containing an extra MP3 slot. Prove actual17/18 both resolve to CITY.VB.
irx=(ROOT / 'local/originals/lcs-ps2/MODULES/CDSTREAM.IRX').read_bytes()
assert hashlib.sha256(irx).hexdigest()=='fcb4fff15053652ab162e046d491697fdbf08eeb0954d3d870d6a1b07a2fed43'
irx_header=struct.unpack_from('<HHIIIIIHHHHHH',irx,16)
irx_sections=[struct.unpack_from('<IIIIIIIIII',irx,irx_header[5]+40*i) for i in range(irx_header[11])]
def irx_read(address,size):
    for section in irx_sections:
        if section[1]==1 and section[3]<=address and address+size<=section[3]+section[5]:
            offset=section[4]+address-section[3]
            return irx[offset:offset+size]
    raise AssertionError(hex(address))
for address,word in {0x11F0:0x00119080,0x1220:0x02511821,0x1224:0x000318C0,
                     0x1228:0x26C25884,0x122C:0x00628021}.items():
    assert struct.unpack('<I',irx_read(address,4))[0]==word,hex(address)
for track in (17,18):
    assert irx_read(0x5884+track*40,40).split(b'\0',1)[0]==b'\\AUDIO\\MUSIC\\CITY.VB;1'
for address,word in {0x2018:0x000410C0,0x201C:0x00441023,0x2020:0x000210C2,
    0x2034:0x00021142,0x14F8:0x0066182A,0x1504:0xAD606E10,
    0x1518:0x34634DD3,0x154C:0x00042183,0x1564:0x00033843,
    0x1568:0x000328C3,0x172C:0x00021040,0x1748:0x24428000,
    0x174C:0x0043102B,0x1754:0x00031382,0x1760:0x00021380}.items():
    assert struct.unpack('<I',irx_read(address,4))[0]==word,hex(address)
assert elf.handler(402)['handler']==0x20A2B0
assert elf.handler(403)['handler']==0x20A344
words={
    0x238C90:0x3C013C80,0x238CA0:0x46026302,0x238CB4:0x460C0B03,
    0x238CBC:0x46006024,0x238CE8:0x2404006B,0x238CEC:0x24632700,
    0x238D08:0x24050014,0x238D2C:0x304200FF,
    # Consecutive distance delta / observed time * speed, strict subsonic
    # condition, symmetric1.5 clamp and frequency ratio.
    0x238D8C:0x4615A301,0x238DA0:0x92226C78,0x238DA4:0xC6220008,
    0x238DBC:0x46006003,0x238DC0:0x46160302,0x238DC4:0x46006045,
    0x238DC8:0x46011036,0x238DF0:0x3C013FC0,0x238DFC:0x46006029,
    0x238E08:0x3C01BFC0,0x238E14:0x46006028,
    0x238E4C:0x46036302,0x238E54:0x46006303,0x238E58:0x0C0B8FA5,
    # Float attenuation: max*.2 plateau, ordered subtraction, squared ratio,
    # CVT.W.S then low-byte narrowing and clamp127, not a linear fade.
    0x238A30:0x3C013E4C,0x238A34:0x3421CCCD,0x238A40:0x46006042,
    0x238A54:0x46016801,0x238A58:0x46016041,0x238A68:0x46000801,
    0x238A70:0x46010003,0x238A74:0x46000002,0x238A78:0x46020002,
    0x238A7C:0x46000064,0x238A84:0x304200FF,
    # Actual 192-slot pool constructor, initial cursor-1 and flag0x80.
    0x22E764:0x0C08BAE8,0x22E768:0x240500C0,0x22E774:0xAF82EC10,
    0x22EBD0:0x2403FFFF,0x22EBD8:0xAE03000C,0x22EBE4:0xAE110008,
    0x22EBF4:0x34420080,0x22EC10:0x30420080,
    # New probes after cursor, wraps, requires free bit, increments modulo128.
    0x13CB80:0x24420001,0x13CB88:0xACE2000C,0x13CB90:0xACE0000C,
    0x13CBB0:0x000411C2,0x13CBBC:0x3082007F,
    0x13CBDC:0x24420001,0x13CBE4:0x3042007F,
    # Delete marks free and rewinds cursor when the removed index is smaller.
    0x13CC38:0x34420080,0x13CC44:0x00A3182A,0x13CC4C:0xACC5000C,
    0x13CAB8:0x24020059,0x13CABC:0x2403FFFB,0x13CAC4:0xAC830020,
    0x20A2BC:0x24060004, # collect xyz and ID; no reconstructed audio-init gate
    0x20A308:0x9602000C,0x20A310:0xA6620000, # store uint16 ID
    0x20A30C:0x0C0A87FA,0x20A314:0xAE620020, # create entity, store receipt
    0x20A31C:0x0C04F316,0x20A330:0x24060001, # pool reference, one output
    0x20A360:0x0C083158,0x20A36C:0x1260000A, # resolve pool generation, absent guard
    0x20A37C:0x0C0A8810,0x20A380:0x8E650020, # destroy actual entity
    0x20A388:0x0C04F286, # delete only resolved script object
    # Pool references encode slot<<8 | flag byte, with 0x30-byte PS2 stride.
    # Bridge must additionally bounds-check hostile handles before resolving.
    0x13CC8C:0x00052200,0x13CC94:0x90620000,0x13CC9C:0x00821025,
    0x20C564:0x00053203,0x20C568:0x30A500FF,0x20C570:0x90430000,
    0x20C574:0x10650003,0x20C578:0x24020030,
    0x1B1DC0:0x90E50000,0x1B1DD8:0x0C06CAA6, # consume uint8 ID
    0x1B2A9C:0x30A300FF,0x1B2AA4:0x2C620034, # uint8 dispatch, 52 entries
    0x1B2AC4:0x244228A0,
    # Retail alarm selects sample4 but the frequency of sample5607.
    0x1B2CF4:0x24020004,0x1B2D10:0x240515E7,
    0x1B2D00:0xE6000048,0x1B2CEC:0x3C0142A0,
    0x1B2D24:0x24030002,0x1B2D28:0x24040003,0x1B2D34:0x2411005A,
    0x1B2D4C:0x240200B7,0x1B2D68:0x240500B7,0x1B2D84:0x2411007F,
    0x1B2D7C:0x24030003,0x1B2D88:0xAE030064,
    0x1B2D98:0x3C014220,0x1B2DA0:0x24020122,0x1B2DBC:0x24050122,
    0x1B2DD0:0x24030004,0x1B2DD4:0x24040003,0x1B2DE0:0x2411003C,
    0x1B2DF8:0x24050012,0x1B2E04:0x2402514C,0x1B2E08:0x24030004,
    0x1B2E0C:0x24040009,0x1B2E14:0x2411001E,0x1B2DF0:0x3C0142A0,
    0x1B2D1C:0x3C014000,0x1B2D74:0x3C014000,
    0x1B2DC8:0x3C014000,0x1B2DFC:0x3C014000,
    0x1B2D14:0x3C0145C8,0x1B2D6C:0x3C0145C8,
    0x1B2DC0:0x3C0144C8,0x1B2E2C:0x3C0145C8,
    0x1B2E40:0x0C0679BC,0x1B2E48:0x46140034, # actual squared distance, strict bound
    0x1B2E60:0x46000004,0x1B2E70:0x0C08E284, # sqrt + original attenuation
    0x1B2E7C:0x10600009,0x1B2E94:0xAE000040, # zero-volume skip, infinite loop
    0x1B2E9C:0x0C08E3DC,
    # Building and snore configuration paths return before requesting audio.
    0x1B2CE4:0x1000006F,0x1B2CE8:0xA200002D,
}
words.update({
    # Real source random producer and five-word audio table, not a host LCG.
    0x34FCE0:0x8F842288,0x34FCE4:0x3C030001,0x34FCE8:0x3463F31D,
    0x34FCEC:0x240741A7,0x34FCF0:0x0083001A,0x34FCF4:0x24060B14,
    0x34FD18:0x00A42823,0x34FD1C:0x1CA00005,0x34FD24:0x3C027FFF,
    0x34FD28:0x3442FFFF,0x34FD2C:0x00A21021,
    0x23A8AC:0x24906C64,0x23A8B8:0x0C0D3F38,0x23A8C0:0xAE020000,
    0x23A8C4:0x2E220005,0x23A8C8:0x1440FFFB,
    # Distinct source audio entity allocator and insertion-ordered service list.
    0x2A2008:0x0C08E058,0x2A200C:0x24050004,0x2A2020:0x0C08E0C8,
    0x2381A0:0x90822DF8,0x2381AC:0xA0882DF8,0x238208:0xAC6B0000,
    0x238218:0x2D6200FA,0x238228:0x2402FFFD,0x2382C8:0x0C0DD028,
    0x2382D4:0x2442FFFF,0x2382DC:0xAE0258E8,
    # Retail initial manager lengths: actual file length only below10;
    # non-radio slots receive100. Do not conflate this with IOP duration.
    0x16786C:0x2E32000A,0x167888:0x12400008,0x167898:0x0C045456,
    0x1678B4:0x24020064,0x1678BC:0xAC620000,0x167930:0xAC400000,
    0x167934:0x24020043,0x16796C:0xA2020353,0x16797C:0xA202034E,
    0x167D34:0xAC62001C,
    # Initialization consumes one discarded rand, then66 per-slot draws;
    # first25 positions use actual AudioManager random words and low32 product.
    0x16783C:0x0C0D3F38,0x167868:0x0C0D3F38,0x167874:0x3405FFFF,
    0x167878:0x0C0D6D1C,0x167880:0x00021078,0x167884:0x0002103F,
    0x16788C:0x0002A280,0x1678C0:0x2E220019,0x1678CC:0x0235001B,
    0x1678F4:0x8C620000,0x1678F8:0x00541018,0x1678FC:0x0044001B,
    0x167910:0xAC400000,0x167928:0x2E240042,0x16792C:0x1480FFCE,
    # Actual track-position consumer and both stream-start call chains.
    0x169FD8:0x24420001,0x169FDC:0x1480FFF8,0x169FF0:0x30A600FF,
    0x16A00C:0x8C440020,0x16A010:0x00041940,0x16A018:0x00644821,
    0x16A028:0x2CC6000A,0x16A02C:0x8C43001C,0x16A034:0x8CA40018,
    0x16A038:0x00691821,0x16A03C:0x246301F4,0x16A048:0x0064001B,
    0x16A054:0x0006100A,0x168294:0x0C05A7FC,0x1682C8:0x0200302D,
    0x1682D0:0x0C05ABA0,0x1696D8:0x0C05A7FC,0x169704:0x0200302D,
    0x169710:0x0C05ABA0,
    # LoopCount0 skips finite completion handling, but still tests channel-used
    # at239FB0: a non-looping VAG is retired and reacquired by the frame queue.
    0x239F2C:0x8E620020,0x239F30:0x50400015,0x239FA0:0x8E620020,
    0x239FB0:0x0C045318,0x239FB8:0x54400004,0x239FC0:0xA2600048,
    0x239FC8:0xA2200048,
    0x23A0E4:0x92260018,0x23A0E8:0x92640018,0x23A0F8:0x24C2000A,
    0x23A104:0x24C2FFF6,0x23A110:0x0083300A,0x23A114:0xA2260018,
    0x23A118:0x92A20004,0x23A120:0x2402003F,0x23A130:0x00029040,
    0x23A178:0x92A66C60,0x23A194:0x00D6001A,0x23A1AC:0x92A26C60,
    0x23A1B0:0x2C42007F,0x23A1C8:0x02421018,
    # User-pause edge stops40 generic channels, clears both queues and active
    # samples; script-object interrogation is gated while pause remains set.
    0x23882C:0x92426C79,0x238838:0x92426C7A,0x238850:0x0C04533C,
    0x238860:0x2E030028,0x2388D4:0x0C08E9E2,0x2388F0:0x0C08E9E2,
    0x238900:0x0C08E9E2,0x238910:0x0C08E9F8,
    0x19EBEC:0x90C26C79,0x19EBF0:0x14400046,0x19EBF8:0x0C06C75A,
    0x1B1DBC:0x54440006,0x1B1DD8:0x0C06CAA6,
    0x114C38:0x3403ABE0,0x114C4C:0x0066182B,0x114C50:0x00C3200A,0x114C5C:0xAC4402E4,
    # Initial active request copy, non-2D frequency and pre-start pitch setup.
    0x23A4C0:0x0220182D,0x23A4C4:0x0260102D,0x23A4C8:0x26640060,
    0x23A564:0x9222000D,0x23A56C:0x8E300014,0x23A570:0x0C0AD41A,
    0x23A5BC:0x3C013F40,0x23A5C4:0x0C0B8FA5,0x23A5DC:0x0C04530E,
    0x23A074:0x1090001B,0x23A080:0x26021770,0x23A08C:0x2602E890,
    0x23A098:0x0083800A,0x23A09C:0x0C0AD41A,0x23A0A0:0xAE300014,
    0x23A0B8:0x3C013F40,0x23A0C4:0x46010002,0x23A0C8:0x46000064,
    0x23A0DC:0x0C04530E,0x2B5068:0x3C01003E,0x2B506C:0xC4219CE4,
    0x2B5070:0x3C013F80,0x2B507C:0x46000834,
})
for address,expected in words.items():
    assert elf.word(address)==expected,hex(address)
assert list(elf.read(0x392700,64))==[0,3,8,12,16,19,22,24,26,28,30,31,33,34,36,37,39,40,41,42,44,45,46,47,48,49,49,50,51,52,53,53,54,55,55,56,56,57,57,58,58,58,59,59,59,60,60,61,61,61,61,62,62,62,62,62,63,63,63,63,63,63,63,63]
targets={0:0x1B2CEC,2:0x1B2D44,4:0x1B2D44,6:0x1B2C98,11:0x1B2D98,
    36:0x1B2AD8,37:0x1B2AE4,38:0x1B2AF0,39:0x1B2AFC,40:0x1B2B08,
    41:0x1B2B40,42:0x1B2B78,43:0x1B2BB0,44:0x1B2BE8,45:0x1B2C20,
    46:0x1B2C58,51:0x1B2DF0}
for selector in range(52):
    assert elf.word(0x3B28A0+selector*4)==targets.get(selector,0x1B2EA4),selector
if len(sys.argv)==2:
    result=subprocess.run([sys.argv[1]],capture_output=True,text=True,check=True,timeout=20)
    actual=[list(map(int,line.split())) for line in result.stdout.splitlines()]
    assert actual==[
        [0,4,5607,0,80,90,2,3,2],
        [2,183,183,0,80,127,3,3,2],
        [4,183,183,0,80,127,3,3,2],
        [11,290,290,0,40,60,4,3,2],
        [51,18,18,20812,80,30,4,9,2]],actual
print('PASS: retail 402/403 pool lifecycle and all52 audio selectors; optional compiled emission descriptors; no game/audio execution, no runtime registration')
