"""Read original LCS/SA HUD contracts as data; never execute game code."""
import importlib.util,pathlib,struct,sys
ROOT=pathlib.Path(__file__).resolve().parents[1]
def module(name,file):
    spec=importlib.util.spec_from_file_location(name,ROOT/'tools'/file)
    value=importlib.util.module_from_spec(spec);sys.modules[name]=value;spec.loader.exec_module(value);return value
original=module('hud_original','inspect-original-command.py').OriginalImage(ROOT / 'local/originals/lcs-ps2/SLUS_214.23')
original.require_profile()
for opcode,address in {1576:0x2F2ED4,1577:0x2F2F44,1634:0x273DE4,1636:0x273E9C,1644:0x2741A0}.items():
    assert original.handler(opcode)['handler']==address,opcode
words={
    0x2F2EE4:0x24060004,0x2F2EF4:0x9245F320,0x2F2F20:0xA0249B51,
    0x2F2F28:0xA0239B50,0x2F2F30:0xA0259B52,0x2F2F38:0xA0269B53,
    0x273E2C:0x0C0B9222,0x273ECC:0x24060002,0x273F64:0x96460004,
    0x2741D0:0x24060003,0x2742CC:0x0220502D,0x2742D0:0x0000402D,
    0x2742D8:0x964B0008,0x2742DC:0x96460004,0x2742E0:0x0C0B916A,
    0x2E460C:0x54600065,0x2E4614:0xAE2C0000,0x2E461C:0xAE290004,
    0x2E4658:0xA632001C,0x2E4660:0xA633001E,0x2E4668:0xAE200058,
    0x2E4670:0x8C429B50,0x2E4708:0x8C639B58,
    0x2E4888:0x24840048,0x2E48A0:0xAC860058,0x2E48A8:0x2C620003,
    0x2E48B0:0x2484005C,
    # Actual format consumer differs from reconstructed Min: unsigned total,
    # signed numerator; both capped99 whenever a denominator exists.
    0x2E3F4C:0x8E030058,0x2E3F7C:0x8E030004,0x2E3F84:0x1062000D,
    0x2E3F8C:0x2C620063,0x2E3F98:0x2A430063,0x2E3F9C:0x0082380A,
    0x2E3FA0:0x0083900A,0x2E3FB8:0xAE070004,0x2E3FCC:0x24A57B68,
}
for address,expected in words.items():assert original.word(address)==expected,(hex(address),hex(original.word(address)),hex(expected))
for address,expected in {0x3D7B58:b'$',0x3D7B60:b'/%d',0x3D7B68:b'%s%d%s'}.items():assert original.read(address,len(expected)+1)==expected+b'\0'
native=module('hud_native','inspect-native-command.py').NativeImage(pathlib.Path(sys.argv[1]))
for address,expected in {
    0x58FBEE:'e88db5ffff',0x58B180:'a0c0aeba0081ecf0000000',
    0x719430:'d905801ac7008b442404',0x719380:'8b4424048b4c2408a3641ac700',
    0x71A700:'83ec10538b5c24208a033c2a',0x728640:'a1247bc90083ec286a006a01',
}.items():assert native.read(address,len(bytes.fromhex(expected)))==bytes.fromhex(expected),hex(address)
assert b'\xe8' in native.read(0x58B1DA,5) # Native mission HUD reaches its viewport helper.
print('PASS: five original HUD handlers, low-byte colours, halfword types, labels/total/prefix, retail format clamp and native draw/font ABI; gameExecuted=false')
