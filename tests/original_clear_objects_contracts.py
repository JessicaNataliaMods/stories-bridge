"""CLEAR_AREA_OF_OBJECTS retail filters/order/ABI; binaries inspected only as data."""
import importlib.util,pathlib,sys,struct
ROOT=pathlib.Path(__file__).resolve().parents[1]
def load(name,file):
    spec=importlib.util.spec_from_file_location(name,ROOT/'tools'/file)
    mod=importlib.util.module_from_spec(spec);sys.modules[name]=mod;spec.loader.exec_module(mod);return mod
original=load('clear_original','inspect-original-command.py')
image=original.OriginalImage(ROOT / 'local/originals/lcs-ps2/SLUS_214.23');image.require_profile()
assert image.handler(1563)['handler']==0x2F2A40
expected={
    0x2F2A50:0x24060006, # six floats, no output
    0x2F2A6C:0x2673FFFF, # descending object pool
    0x2F2AB8:0x0C088062, # actual CanBeDeleted
    0x2F2ADC:0x0C0BAD1E, # full XYZ inclusive origin check
    0x2F2AF8:0x0060F809, # DeleteRwObject
    0x2F2B00:0x0C05CDC6, # CWorld::Remove
    0x2F2B18:0x0060F809, # destructor/free after both effects
    0x2F2B28:0x2631FDE0, # source object stride 0x220
    0x2F2B3C:0x2673FFFF, # second pass: descending dummy pool
    0x2F2B54:0x3417E000,0x2F2B5C:0x34168000, # type OBJECT, not all dormant buildings
    0x2F2BCC:0x0C0BAD1E, # same six-coordinate predicate
    0x2F2BF0:0x0C05CDC6,0x2F2C08:0x0060F809,
    0x2F2C18:0x2652FFA0, # source dummy stride 0x60
    0x220188:0x908301A4, # source created-by byte
    0x2EB478:0x460C7834,0x2EB490:0x460D8034,0x2EB4A8:0x460E8834, # sort each axis
    0x2EB4C4:0x46006036,0x2EB4D4:0x460F0036,
    0x2EB4E8:0x46006836,0x2EB4F8:0x46100036,
    0x2EB50C:0x46007036,0x2EB51C:0x46110036, # <= on every min/max
}
for address,word in expected.items():assert image.word(address)==word,(hex(address),hex(image.word(address)),hex(word))
assert [image.word(0x3B87C0+i*4) for i in range(6)]==[0x2201C0,0x2201B0,0x2201B8,0x2201B0,0x2201B8,0x2201B8]
native=load('clear_native','inspect-native-command.py');pe=native.NativeImage(pathlib.Path(sys.argv[1]))
assert pe.read(0x59F120,7).hex()=='0fb6813c010000' # native created-by offset
assert list(struct.unpack('<6I',pe.read(0x59F13C,24)))==[0x59F137,0x59F134,0x59F137,0x59F134,0x59F134,0x59F134]
assert pe.read(0x59F134,3).hex()=='32c0c3' and pe.read(0x59F137,3).hex()=='b001c3'
assert pe.read(0x866F60,4).hex()=='d02b5a00' and pe.read(0x866E78,4).hex()=='50eb5900'
assert pe.read(0x866F60+32,4).hex()=='30405300' and pe.read(0x866E78+32,4).hex()=='30405300'
assert pe.read(0x563280,12).hex()=='568b7424088b068bceff500c' # world remove dispatches Remove
print('PASS: retail descending object/dummy passes, protected creation classes, inclusive normalized origin box, native class/destructor/RW/world ABI; gameExecuted=false')
