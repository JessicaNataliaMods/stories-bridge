"""Independent retail table checks; never execute game code or grant support."""
import importlib.util
import pathlib
import struct
import sys
from types import SimpleNamespace

ROOT = pathlib.Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location('native_dispatcher_inspect', ROOT/'tools/inspect-native-command.py')
module = importlib.util.module_from_spec(spec)
spec.loader.exec_module(module)
image = module.NativeImage(pathlib.Path(sys.argv[1]))
# Reviewed directly from each PE prologue: base, count, default, pointer table,
# optional compact byte table. Tests do not interpret instruction operands.
tables = (
 (0,100,0x466C3A,0x466C50,0), (100,97,0x46834F,0x468364,0),
 (214,84,0x469DBD,0x469DD4,0x469E4C), (311,89,0x47D0EA,0x47D0F4,0x47D1B4),
 (400,100,0x47DF40,0x47DF58,0x47DFE0), (500,99,0x47F235,0x47F24C,0x47F304),
 (659,39,0x47F9AD,0x47F9BC,0x47FA04), (703,96,0x480F00,0x480F10,0x480FD8),
 (801,99,0x4835CC,0x483600,0x4836B8), (900,100,0x485699,0x4856B8,0x4857C4),
 (1000,100,0x48A1E6,0x48A204,0x48A2B4), (1101,99,0x48B406,0x48B418,0x48B4D8),
 (1200,100,0x48CC3D,0x48CC5C,0x48CD64), (1301,97,0x48DD27,0x48DD34,0x48DDD4),
 (1403,97,0x490C0F,0x490C2C,0), (1500,94,0x492AA9,0x492AC8,0),
 (1602,98,0x495DE1,0x495E00,0), (1700,100,0x498EF1,0x498F10,0),
 (1800,100,0x46ED96,0x46EDB4,0), (1900,100,0x46CBCF,0x46CBEC,0),
 (2000,100,0x474372,0x474390,0), (2100,100,0x472158,0x472178,0),
 (2200,100,0x476120,0x476140,0), (2300,100,0x477BFC,0x477C0C,0),
 (2400,100,0x479ADC,0x479AFC,0), (2500,100,0x47BF2F,0x47BF4C,0),
 (2600,39,0x47A6A6,0x47A6C4,0),
)
for opcode in range(2700):
    base, count, default, table, compact = tables[opcode//100]
    if base <= opcode < base+count:
        index = opcode-base
        if compact:
            index = image.read(compact+index,1)[0]
        expected = image.word(table+4*index)
    else:
        expected = default
    group, actual = image.handler(opcode)
    assert group == image.word(0x8A6168+4*(opcode//100))
    assert actual == expected and image.executable(actual), (opcode,hex(actual),hex(expected))

def rejects(action):
    try:
        action()
    except ValueError:
        return
    raise AssertionError('Unsafe native dispatcher target/index accepted')

for opcode in (-1,2700,0x8000):
    rejects(lambda: image.handler(opcode))
for address,length in ((0x400000,0),(0x400000,-1),(-1,4),(0xffffffff,4)):
    rejects(lambda: image.read(address,length))
for data in (b'',b'MZ',image.data[:63],image.data[:100],image.data[:-1]):
    rejects(lambda: module.NativeImage(SimpleNamespace(read_bytes=lambda:data)))

def changed_word(address,value):
    data=bytearray(image.data)
    for _,start,size,raw in image.spans:
        if start <= address-0x400000 < start+size:
            struct.pack_into('<I',data,raw+address-0x400000-start,value)
            return module.NativeImage(SimpleNamespace(read_bytes=lambda:bytes(data)))
    raise AssertionError('Fixture edit must be file-backed')

for bad in (0,0xDEADBEEF,0xA43C78):
    changed=changed_word(0x466C50,bad)
    rejects(lambda: changed.handler(0))
# Regression: opcode 799 is above the group upper bound. A poisoned byte
# after its compact table must never be consulted as a legitimate table entry.
changed=changed_word(0x480FD8+96,0xffffffff)
assert changed.handler(799)[1] == 0x480F00
# Underflow is unsigned too; 1400..1402 and 1600..1601 must take default.
assert image.handler(1400)[1] == 0x490C0F
assert image.handler(1600)[1] == 0x495DE1
print('PASS: 2700 native indices, 27 independent tables, compact ECX/EAX, unsigned defaults and invalid targets; gameExecuted=false, runtimeSupportGranted=false')
