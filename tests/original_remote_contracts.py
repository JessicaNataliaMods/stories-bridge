"""Compare the original RC explosion to the real native service, as data only."""
import importlib.util
import pathlib
import sys

ROOT = pathlib.Path(__file__).resolve().parents[1]

def module(name, path):
    spec = importlib.util.spec_from_file_location(name, path)
    loaded = importlib.util.module_from_spec(spec)
    sys.modules[name] = loaded
    spec.loader.exec_module(loaded)
    return loaded

original = module('remote_original', ROOT/'tools/inspect-original-command.py')
image = original.OriginalImage(ROOT / 'local/originals/lcs-ps2/SLUS_214.23')
image.require_profile()
assert image.handler(273)['handler'] == 0x110DAC
assert image.word(0x110DC0) == 0x24060005  # five inputs; player input is unused
assert image.word(0x110DF0) == 0x0C05E53E  # <=-100 ground query
assert image.word(0x110E18) == 0x240400A9  # fixed original RCBANDIT169
assert image.word(0x110E1C) == 0x0C089476  # actual RC factory
assert image.word(0x110E2C) == 0x0C04483A  # real vehicle-pool handle output
assert image.word(0x2253D0) == 0x24060003  # ground RC camera BEHINDCAR
assert image.word(0x2253D8) == 0x0C0A5278  # actual camera consumer
assert image.handler(1038)['handler'] == 0x267E00
assert image.word(0x267E10) == 0x24050001  # no SCM operand; actually=true
assert image.word(0x267E1C) == 0x0C041EB8  # PlayerInfo::BlowUpRCBuggy
assert image.word(0x107AF4) == 0x8E220004  # registered remote car
assert image.word(0x107AF8) == 0x10400013  # absent branch
assert image.word(0x107B00) == 0xDC420048  # removal bit in original entity flags
assert image.word(0x107B04) == 0x30420001
assert image.word(0x107B10) == 0x0C089504  # real remote-control release service
assert image.word(0x107B18) == 0x1200000C  # explode only for actually=true
assert image.word(0x107B30) == 0x0C041CDA  # genuine focused actor lookup
assert image.word(0x107B40) == 0x0060F809  # virtual vehicle explosion

native = module('remote_native', ROOT/'tools/inspect-native-command.py')
pe = native.NativeImage(pathlib.Path(sys.argv[1]))
assert pe.read(0x45AB10,12).hex() == '6aff68117a830064a1000000'
assert pe.read(0x45AC51,10).hex() == '8a463624070c40884636'  # remote status8
assert pe.read(0x45ACFF,7).hex() == '8930e86a6e1100'  # actual registered remote slot
assert pe.read(0x45AD0A,2).hex() == '6a12'  # SA differs: CAM_ON_A_STRING
assert pe.read(0x527979,10).hex() == '0fbf460c83c0fd83f83e'  # real native camera mode switch
assert pe.read(0x527F4C,1) == pe.read(0x527F4C+18-3,1) == b'\x00'  # modes3/18 reach actual5245B0 consumer
assert pe.handler(0x0409)[1] == 0x48A1E2
assert pe.read(0x48A1E2,4).hex() == '32c0eb02'  # release command has no explosion
assert pe.read(0x56EA30,21).hex() == '568bf18b86b000000085c0743f8b481cf6c5087537'
assert pe.read(0x56EA4A,6).hex() == '53e830c4eeff'  # passes actual boolean to CRemote
assert pe.read(0x56EA58,25).hex() == '0fb60574cdb7008b8eb000000069c0900100008b8098cdb700'
assert pe.read(0x56EA73,11).hex() == '6a0050ff92a40000005ec2'  # native virtual explosion, extra native hide flag=false
assert pe.read(0x45AE80,5).hex() == 'e9db721001'  # known retail trampoline
assert pe.read(0x15621F7,31).hex() == '8a4c2404899078ceb700c68075ceb70001888876ceb700c68077ceb70001c3'
print('PASS: original RC release/explosion contract and native real service; gameExecuted=false')
