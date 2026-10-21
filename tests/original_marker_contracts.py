"""Retail marker writer/consumer and native type admission, as ELF/PE data only."""
from pathlib import Path
import importlib.util
import sys

ROOT = Path(__file__).resolve().parents[1]


def module(name, path):
    spec = importlib.util.spec_from_file_location(name, path)
    result = importlib.util.module_from_spec(spec)
    sys.modules[name] = result
    spec.loader.exec_module(result)
    return result


original = module('marker_original', ROOT/'tools/inspect-original-command.py')
elf = original.OriginalImage(ROOT / 'local/originals/lcs-ps2/SLUS_214.23')
elf.require_profile()
assert elf.handler(1579)['handler'] == 0x2F3008
assert elf.handler(1611)['handler'] == 0x273318
words = {
    # Peek variable ADDRESS; collect seven; ground sentinel; copy id-1,1,1.
    0x2F301C: 0x0C04268A, 0x2F305C: 0x24060007,
    0x2F306C: 0x3C01C2C8, 0x2F3090: 0x0C05E53E,
    0x2F30FC: 0x2685FFFF, 0x2F3100: 0x24060001,
    0x2F3104: 0x0C092CD2, 0x2F3108: 0x24070001,
    # Point type5, size*.7, vertical size100, pulse128, alpha255.
    0x2F3118: 0x3C013F33, 0x2F311C: 0x34213333,
    0x2F3130: 0x3C0142C8, 0x2F3144: 0x24050005,
    0x2F3154: 0x240A00FF, 0x2F315C: 0x240B0080,
    0x2F3170: 0xAFA30010, 0x2F3178: 0xAFA30000,
    0x2F3180: 0x0C0927FC, 0x2F3184: 0xAFA00018,
    # Arrow ten operands; height+7 then positionZ; type2, size3.2,
    # pulse fraction1, vertical bound0, target is a pointer to WORLD inputXYZ.
    0x27332C: 0x0C04268A, 0x273344: 0x2406000A,
    0x273358: 0x3C0140E0, 0x27336C: 0x46010000,
    0x273380: 0x24050002, 0x273384: 0x46001080,
    0x273398: 0x240A00C8, 0x2733A0: 0x240B0001,
    0x2733B8: 0x3C01404C, 0x2733BC: 0x3421CCCD,
    0x2733D4: 0x3C013F80, 0x2733E8: 0x44807000,
    0x2733F8: 0xAFAC0008, 0x273400: 0x0C0927FC,
    # Consumer type-1<2 is BOTH types1/2, not source type2 only.
    0x24A0F4: 0x261EFFFF,
    # First-frame vertical value stored before proximity/current size update;
    # source floor retry uses squared XY distance, not rounded sqrt range.
    0x24AC1C: 0xE6360088, 0x24AC20: 0xE6200088,
    0x24AD00: 0xE6200078, 0x24B030: 0xE622007C,
    0x24AB60: 0x3C01461C, 0x24AB64: 0x34214000,
    # Exact matrix postmultiply and cross product operand order.
    0x24A8C0: 0x0C0B5FD0, 0x24A9BC: 0x0C09ADF0,
    0x26B7D8: 0x0C09B1A6,
    0x26BB10: 0xE6150028, 0x26BB14: 0xE6150014,
    0x26BB20: 0xE6020024, 0x26BB2C: 0xE6000018,
    # Actual render: X/Y current size; Z independent +88, not a distance.
    0x249BEC: 0xC60C007C, 0x249BF4: 0xC60E0088,
    0x249BF8: 0x0C09ADE0, 0x249BFC: 0x46006346,
    # Ghost render decrements alpha12 and retains used flag while fading.
    0x249E98: 0x9202006B, 0x249E9C: 0x2442FFF4,
    0x249F80: 0x8E02008C, 0x249F88: 0xAE00005C,
    # Copy reads CURRENT size +7C before invoking PlaceMarker.
    0x24B3D0: 0x0C0927FC,
}
for address, expected in words.items():
    assert elf.word(address) == expected, f'ELF {address:X}'

assert len(sys.argv) == 2, 'Pass SA reference PE as data'
native = module('marker_native', ROOT/'tools/inspect-native-command.py')
pe = native.NativeImage(Path(sys.argv[1]))
# Native TUBE2 does have a real backing clump: AddMarker explicitly reads
# array[1] (C7C6E0). Init not loading array[2] alone is NOT missing support.
assert pe.read(0x72224A, 4).hex() == '6683fd02'
assert pe.read(0x722258, 6).hex() == '8b15e0c6c700'
assert pe.read(0x725040,11).hex() == '535633db57891dd8c6c700'
assert pe.read(0x72506C,5).hex() == 'be88ddc700'
assert pe.read(0x723390,1) == b'\x56'
assert pe.read(0x723396,5).hex() == 'a118c7c700'
for site,target in [(0x725067,0x723240),(0x723391,0x722710),
                    (0x731EC6,0x7F36A0),(0x59ADBB,0x7F18A0),
                    (0x7225D8,0x7F0910)]:
    call=pe.read(site,5)
    assert call[0]==0xE8 and site+5+int.from_bytes(call[1:],'little',signed=True)==target,hex(site)
assert pe.read(0x7222D8,5).hex() == '8b4b208b11'  # geometry material table -> first material
print('PASS: retail marker writer/vertical-scale/fade contracts and native type2 clump remap; no renderer executed or command coverage granted')
