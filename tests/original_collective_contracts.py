"""Retail collective table, area query and real consumer contracts; ELF/PE data only."""
from pathlib import Path
import importlib.util
import sys
ROOT=Path(__file__).resolve().parents[1]
def module(name,path):
    spec=importlib.util.spec_from_file_location(name,path);m=importlib.util.module_from_spec(spec);sys.modules[name]=m;spec.loader.exec_module(m);return m
original=module('collective_original',ROOT/'tools/inspect-original-command.py')
elf=original.OriginalImage(ROOT / 'local/originals/lcs-ps2/SLUS_214.23');elf.require_profile()
assert elf.handler(636)['handler']==0x25DC18
assert elf.handler(638)['handler']==0x25DCA4
assert elf.handler(616)['handler']==0x25D7E8
words={
    0x25DC2C:0x24060003,0x25DC44:0x0C05E53E,0x25DC58:0x0C0C0644,
    0x25D7F8:0x24060002,0x25D81C:0x2405000B,
    0x25DCC4:0x2406FFFF,0x25DCC8:0x2404001F,0x25DCDC:0xAC660000,0x25DCE0:0xAC600004,
    0x301924:0x8ED69C1C,0x3019A4:0x0C05DEB2,0x3019A8:0x240B0001,
    0x301A0C:0x922201AC,0x301A10:0x24030002,0x301A18:0x24030006,
    0x301A1C:0x8E220560,0x301A28:0x0C0C0632,0x301A38:0x000280C0,
    0x301A50:0x0C044828,0x301A54:0xAC560000,0x301B8C:0x02C0102D,
    0x30152C:0x0C04481A,0x301540:0xDC620190,0x30154C:0x00571024,
    0x301550:0x0C0C20CE,0x301554:0xFC620190,0x301560:0xAE160000,
    0x301564:0xAE200000,0x301570:0x2E420020,
}
for address,word in words.items():assert elf.word(address)==word,hex(address)
native=module('collective_native',ROOT/'tools/inspect-native-command.py')
pe=native.NativeImage(Path(sys.argv[1]))
assert pe.read(0x564A20,11).hex()=='83ec18535556578b7c242c'
assert pe.read(0x569660,11).hex()=='83ec388b44243c8b4c2440'
sdk=(ROOT/'vendor/sa-reference/sdk-CVehicle.h').read_text()
for field,offset in [('m_pDriver','0x460'),('m_apPassengers','0x464'),('m_nNumPassengers','0x484')]:
    assert f'VALIDATE_OFFSET(CVehicle, {field}, {offset});' in sdk
asi=(ROOT/'src/asi.cpp').read_text()
assert 'collectives.candidates=lcs::sa_collective_candidates;' in asi
assert 'lcs::try_engine_adapter(collectives,vm,t,op)' in asi
assert 'adapter.execute_collective_flee(actor,target)' in asi
assert 'call.inputs={actor.handle,target.handle,100,-1}' in (ROOT/'src/native.cpp').read_text()
# Native05DB converts the SCM integer distance with FILD rather than FLD.
assert pe.handler(0x05DB)[1]==0x490B7E
assert pe.read(0x490B87,6).hex()=='db05803ca400'
print('PASS: original636/638/616 selection/table/consumer and native query/occupant ABI; no game execution')
