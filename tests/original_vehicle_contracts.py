"""Check vehicle/attachment contracts against retail PS2 machine code as data."""
import importlib.util
import pathlib
import re
import sys
ROOT = pathlib.Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location('original_vehicle_inspect', ROOT/'tools/inspect-original-command.py')
module = importlib.util.module_from_spec(spec)
sys.modules[spec.name] = module
spec.loader.exec_module(module)
image = module.OriginalImage(ROOT / 'local/originals/lcs-ps2/SLUS_214.23')
image.require_profile()
debug = (ROOT / 'local/references/res-relcs/src/control/ScriptDebug.cpp').read_text()
signatures = {name: (len(re.findall('ARGTYPE_', inputs)), len(re.findall('ARGTYPE_', outputs)), predicate)
              for name, inputs, outputs, predicate in re.findall(
                  r'REGISTER_COMMAND\((COMMAND_[A-Z0-9_]+), INPUT_ARGUMENTS\(([^)]*)\), OUTPUT_ARGUMENTS\(([^)]*)\), (true|false)', debug)}
cases = [
    ('HELI_GOTO_COORDS',1191,0x26AFC0,5,0x126608),
    ('SET_CAR_RAM_CAR',817,0x225C60,2,0x29C558),
    ('ATTACH_OBJECT_TO_CAR',1618,0x2735F0,5,0x221A70),
    ('DETACH_OBJECT_FROM_CAR',1619,0x27365C,1,0x221B30),
    ('ATTACH_CHAR_TO_OBJECT',1273,0x186F84,8,0x1F08D0),
]
for name, opcode, handler, count, helper in cases:
    assert image.handler(opcode)['handler'] == handler, name
    assert signatures['COMMAND_'+name] == (count,0,'false'), name
    words = [image.word(handler+i*4) for i in range(32)]
    assert 0x0C042532 in words and (0x24060000|count) in words, name
    assert (0x0C000000|helper>>2) in words, name
# Consuming the last damage entity is a real mutation. Retail GetVehicleRef
# lacked a type check, and its null source-car branch retained the input value;
# the bridge refuses invalid source cars and cannot generate out-of-pool refs.
assert image.handler(1496)['handler'] == 0x25052C
assert signatures['COMMAND_STORE_CAR_COLLIDED_WITH_NO_SAVE'] == (1,1,'false')
for address, word in [(0x25053C,0x24060001),(0x250568,0x8E250298),
                      (0x250574,0x0C04483A),(0x250584,0xAE200298),
                      (0x250570,0x2402FFFF),(0x2505A0,0x24060001)]:
    assert image.word(address) == word, hex(address)
# WRECK does not call BlowUpCar: fixed wheel 0 and six missing doors,
# clear light/panel states, engine 250; component helpers only, except RC Bandit.
assert image.handler(1527)['handler'] == 0x2F1A08
assert signatures['COMMAND_WRECK_CAR'] == (1,0,'false')
for address, word in [(0x2F1A10,0x24060001),(0x2F1A54,0xAE000268),
                      (0x2F1A68,0x0C0CBFE2),(0x2F1A74,0x240200A9),
                      (0x2F1A88,0x0C049A12),(0x2F1A9C,0x0C049A12),
                      (0x2F1AB0,0x0C049A84),(0x2F1B14,0x0C049A84),
                      (0x2F1B80,0xAE000268),(0x2F1B84,0xA6000286),(0x2F1B8C,0xA20002AD),
                      (0x32FF8C,0x24020002),(0x32FF94,0x24030003),
                      (0x32FFAC,0xA2020006),(0x32FFB0,0x24020005),
                      (0x32FFB8,0xA0830000),(0x330004,0xAE000010),
                      (0x33000C,0xAE000014),(0x330014,0x240500FA)]:
    assert image.word(address) == word, hex(address)
# Source heli speed is LBU, not a float altitude. Flight mission 20, XYZ and
# speed are separate stores in its real helper, with real heading initialization.
assert image.word(0x26AFFC) == 0x92050010
for address, word in [(0x126648,0xA204018E),(0x126654,0xE60C01B0),
                      (0x126658,0xE60D01B4),(0x12665C,0xE60E01B8),(0x126660,0xA2050198)]:
    assert image.word(address) == word, hex(address)
assert image.word(0x126640) == 0xC60006BC
# RAM owns a target reference, minimum signed-byte speed 6, mission 15, and
# the existing engine bit. It does not reset road state or invent speed 100.
for address, word in [(0x29C56C,0xAE0501E4),(0x29C570,0x0C0AD718),
                      (0x29C578,0x82020198),(0x29C57C,0x24060006),
                      (0x29C590,0x2407000F),(0x29C598,0x00C5100A),
                      (0x29C59C,0xA2020198),(0x29C5A0,0xA207018E),
                      (0x29C5A4,0xFE030250)]:
    assert image.word(address) == word, hex(address)
assert image.word(0x29C588) == 0x00042778  # 0x8000 << 29, original engine-on bit
# Original object attachment registers its parent and copies a local vector.
for address, word in [(0x221A9C,0xAE1201C8),(0x221AA4,0x0C0AD718),
                      (0x221ABC,0xE60101D0),(0x221AC4,0xE4400004),
                      (0x221ACC,0xE4410008),(0x221AD8,0xAE1201C4)]:
    assert image.word(address) == word, hex(address)
assert image.read(0x221B30,12).hex() == 'c40180ac0800e003c80180ac'
assert image.word(0x273684) >> 26 == 4 and image.word(0x273690) >> 26 == 4
# The reverse-engineered C++ has an integer-angle typo; retail really loads
# float operand six, multiplies by 3C8EFA36, and narrows facing to ushort.
for address, word in [(0x186FC0,0xC6000018),(0x186FC8,0x3C013C8E),
                      (0x186FCC,0x3421FA36),(0x186FE4,0x460C0302),
                      (0x186FEC,0x8E08001C),(0x186FF0,0x96070014)]:
    assert image.word(address) == word, hex(address)
native_fields = {
    'sdk-CPhysical.h': {'m_pAttachedTo':0xFC,'m_vecAttachOffset':0x100,'m_pEntityIgnoredCollision':0x128},
    'sdk-CVehicle.h': {'m_autoPilot':0x390,'m_nVehicleClass':0x590,'m_nVehicleSubClass':0x594,'m_pLastDamageEntity':0x50C,'m_fHealth':0x4C0,'m_wBombTimer':0x4DE},
    'Automobile.h': {'m_damageManager':0x5A0},
    'sdk-CAutoPilot.h': {'m_nCarMission':0x2A,'m_nCruiseSpeed':0x40,'m_vecDestinationCoors':0x5C,'m_pTargetCar':0x8C},
}
for filename, fields in native_fields.items():
    text = (ROOT/'vendor/sa-reference'/filename).read_text()
    for name, offset in fields.items():
        match = re.search(r'VALIDATE_OFFSET\(\w+, '+name+r', (0x[0-9a-fA-F]+)\)',text)
        assert match and int(match[1],16) == offset, (filename,name)
print('PASS: seven original vehicle/attachment/damage cases, real helper calls, byte speeds, reference lifetime, collision consumption, nonexplosive wreck contract, detach scope and float angle ABI; gameExecuted=false')
