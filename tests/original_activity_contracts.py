"""Verify activity contracts against the original retail PS2 machine code."""
import importlib.util
import pathlib
import re
import sys
ROOT = pathlib.Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location('original_inspect', ROOT / 'tools/inspect-original-command.py')
module = importlib.util.module_from_spec(spec)
sys.modules[spec.name] = module
spec.loader.exec_module(module)
image = module.OriginalImage(ROOT / 'local/originals/lcs-ps2/SLUS_214.23')
image.require_profile()
original = (ROOT / 'local/references/res-relcs/src/control/ScriptDebug.cpp').read_text()
signatures = {name: (len(re.findall('ARGTYPE_', inputs)), len(re.findall('ARGTYPE_', outputs)), predicate)
              for name, inputs, outputs, predicate in re.findall(
                  r'REGISTER_COMMAND\((COMMAND_[A-Z0-9_]+), INPUT_ARGUMENTS\(([^)]*)\), OUTPUT_ARGUMENTS\(([^)]*)\), (true|false)', original)}
specs = re.findall(r'\{(COMMAND_\w+),ActivityStatistic::\w+,(\d+)\}', (ROOT / 'src/native_activity_statistics.inc').read_text())
assert len(specs) == 37
# Independent retail opcode/case map; these addresses were read from all 17
# jump tables, not derived from the bridge's code or enum offsets.
cases = {
    'INDUSTRIAL_PASSED': (847,0x22652C),
    'COMMERCIAL_PASSED': (848,0x226560),
    'SUBURBAN_PASSED': (849,0x226594),
    'REGISTER_CAR_SOLD': (1513, 0x2F15F8), 'ADD_MONEY_MADE_WITH_CAR_SALES': (1514, 0x2F1610),
    'REGISTER_BIKE_SOLD': (1521, 0x2F1900), 'ADD_MONEY_MADE_WITH_BIKE_SALES': (1522, 0x2F1918),
    'ADD_MONEY_MADE_COLLECTING_TRASH': (1529, 0x2F1BC4), 'REGISTER_HITMAN_KILLED': (1530, 0x2F1BF8),
    'REGISTER_GUARDIAN_ANGEL_MISSION_PASSED': (1531, 0x2F1C10),
    'REGISTER_HIGHEST_GUARDIAN_ANGEL_JUSTICE_DISHED': (1532, 0x2F1C28),
    'REGISTER_BEST_BANDIT_LAP_TIME': (1533, 0x2F1C50), 'REGISTER_BEST_BANDIT_POSITION': (1534, 0x2F1C80),
    'REGISTER_MOST_TIME_LEFT_TRAIN_RACE': (1535, 0x2F1CB0),
    'REGISTER_STREET_RACE_FASTEST_TIME': (1540, 0x2F1D68), 'REGISTER_STREET_RACE_FASTEST_LAP': (1541, 0x2F1D98),
    'REGISTER_STREET_RACE_BEST_POSITION': (1542, 0x2F1DC8), 'REGISTER_MAX_SECONDS_ON_CARNAGE_LEFT': (1595, 0x2F3658),
    'REGISTER_MAX_KILLS_ON_RC_TRIAD': (1596, 0x2F3690), 'REGISTER_HIGHEST_LEVEL_SLASH_TV': (1597, 0x2F36C8),
    'ADD_MONEY_MADE_WITH_SLASH_TV': (1598, 0x2F3700), 'ADD_TOTAL_KILLS_ON_SLASH_TV': (1599, 0x2F3734),
    'REGISTER_DIRT_BIKE_FASTEST_LAP': (1608, 0x273280), 'REGISTER_DIRT_BIKE_FASTEST_TIME': (1609, 0x2732B0),
    'REGISTER_DIRT_BIKE_AIR_TIME': (1610, 0x2732E0), 'REGISTER_BEST_TIME_GOGO_FAGGIO': (1639, 0x27402C),
    'REGISTER_GUARDIAN_ANGEL_LEVEL': (1643, 0x27412C), 'ADD_UNLOCKED_COSTUME': (1629, 0x273A5C),
    'REGISTER_FACE_PLANT_DISTANCE': (1594, 0x2F3624),
    'REGISTER_SCRAPYARD_CHALLENGE_SCORE': (1601, 0x27310C),
    'REGISTER_9MM_MAYHEM_SCORE': (1602, 0x273144),
    'REGISTER_SCOOTER_SHOOTER_SCORE': (1603, 0x27317C),
    'REGISTER_WICHITA_WIPEOUT_SCORE': (1617, 0x2735B8),
    'ADD_PIZZAS_DELIVERED': (1337,0x1886EC),
    'ADD_NOODLES_DELIVERED': (1600,0x2730D4),
    'ADD_MONEY_MADE_FROM_TOURIST': (1604,0x2731B4),
    'ADD_TOURISTS_TAKEN_TO_SPOTS': (1605,0x2731EC),
}
for name, count in specs:
    if name in ('COMMAND_INDUSTRIAL_PASSED','COMMAND_COMMERCIAL_PASSED','COMMAND_SUBURBAN_PASSED'):
        # Debug metadata incorrectly says zero operands. Keep this discrepancy
        # explicit: the audited retail CollectParameters count below is one.
        assert signatures[name] == (0,0,'false'), name
        assert int(count)==1
    else:
        assert signatures[name] == (int(count), 0, 'false'), name
    opcode, handler = cases[name.removeprefix('COMMAND_')]
    assert image.handler(opcode)['handler'] == handler
    # CollectParameters() is 1094C8. Its count assignment is within this case,
    # before either the call or its delay slot; zero-argument counters have none.
    words = [image.word(handler + i * 4) for i in range(12 if int(count) else 6)]
    if int(count):
        assert 0x0C042532 in words and 0x24060000 | int(count) in words, name
    else:
        assert 0x0C042532 not in words, name

# Retail stores boolean-normalized words, not the reverse's raw int32. Both
# branches and the independent zero initializers fix the actual field contract.
for handler, offset, initializer, absolute_low in [
        (0x22652C,0x2C8C,0x2300D0,0x9B7C),
        (0x226560,0x2C88,0x2300DC,0x9B78),
        (0x226594,0x2DAC,0x2300EC,0x9C9C)]:
    assert image.word(handler+24)==0x8E03F320 # actual argument
    assert image.word(handler+28)==0x10600003 # zero selects reset
    assert image.word(handler+32)==0x24020001 # every nonzero becomes one
    assert image.word(handler+40)==0xAF820000|offset
    assert image.word(handler+48)==0xAF800000|offset
    assert image.read(initializer,8)==bytes.fromhex('3e00013c')+(0xAC200000|absolute_low).to_bytes(4,'little')

# Array initialization supplies exact capacities and distinct empty records.
for address, count, displacement in [(0x22FF70, 3, 8), (0x22FFF8, 6, 20), (0x230020, 6, 20),
                                     (0x230048, 10, 36), (0x230070, 10, 36)]:
    words = [image.word(address + i * 4) for i in range(10)]
    assert 0x24040000 | (count - 1) in words
    assert 0x24420000 | displacement in words
    assert 0xAC400000 in words  # SW zero
for address, count, displacement in [(0x22FF98, 3, 8), (0x22FFC8, 6, 20)]:
    words = [image.word(address + i * 4) for i in range(12)]
    assert 0x3C037FFF in words and 0x3463FFFF in words  # INT32_MAX
    assert 0x24040000 | (count - 1) in words and 0x24420000 | displacement in words
    assert 0xAC430000 in words  # SW max

# Signed minimum with zero as unset; signed position minima without that rule.
for address in (0x2302E8, 0x230368, 0x230398, 0x2303C8, 0x2303F8):
    assert image.word(address + 20) == 0x14600003  # BNEZ old
    assert image.word(address + 24) == 0x0065102A  # SLT old,new
    assert image.word(address + 36) == 0x00A2180A  # MOVZ old,new,condition
for address in (0x230318, 0x230340):
    assert image.word(address + 20) == 0x0045182A  # signed old < new
    assert image.word(address + 24) == 0x00A3100A  # preserve lower old

# The deceptively named justice helper adds exactly one, ignoring its operand.
assert image.read(0x231B88, 20).hex() == '3e00023c789f428c010042240800e003883082af'
assert image.word(0x2F1C40) == 0x0C08C6E2  # JAL 231B88
# Costume handler narrows to byte, helper bounds against 16 and ORs the bit.
assert image.word(0x273A78) == 0x9204F320  # LBU operand into A0
assert image.read(0x2318C8, 8).hex() == 'ff0084301000822c'
assert image.word(0x2318F0) == 0x00621025  # OR old mask,new bit
assert image.word(0x230170) == 0xA4209F2C  # SH zero, costume initialization
# These records really are float maxima, although four SCM inputs are integers.
# Check the numeric conversion, current record and exact destination separately.
for handler, field, initializer in [(0x27310C, 0x3D9EF0, 0x22FE88),
                                  (0x273144, 0x3D9EF4, 0x22FE90),
                                  (0x27317C, 0x3D9EF8, 0x22FE98),
                                  (0x2735B8, 0x3D9EFC, 0x22FEA0)]:
    assert image.word(handler + 28) == 0x46800020  # CVT.S.W f0,f0
    assert image.word(handler + 32) == 0x3C01003E
    assert image.word(handler + 36) == 0xC4210000 | (field & 0xFFFF)  # LWC1 old
    assert image.word(handler + 44) == 0x46010028  # MAX.S f0,f0,f1
    assert image.word(handler + 52) == 0xE7800000 | (field - 0x3D6EF0)  # SWC1 new
    assert image.read(initializer, 8) == bytes.fromhex('3e00013c') + (0xAC200000 | (field & 0xFFFF)).to_bytes(4, 'little')
assert image.word(0x2F3648) == 0xC601F320  # face plant raw float operand
assert image.word(0x2F364C) == 0x46000868  # MAX.S f1,f1,f0, no integer conversion
assert image.word(0x2F3654) == 0xE7813010
assert image.read(0x22FED8, 8).hex() == '3e00013c009f20ac'  # initial +0 bits
assert 'INPUT_ARGUMENTS(ARGTYPE_FLOAT,)' in re.search(r'REGISTER_COMMAND\(COMMAND_REGISTER_FACE_PLANT_DISTANCE,[^\n]+', original).group()
assert image.word(0x188704)==0x0C08C612  # pizza helper 231848
for operation, field, initializer, conversion, load, add, store in [
        (0x231848,0x3D9ED0,0x22FE48,0x231850,0x231858,0x23185C,0x231864),
        (0x2730D4,0x3D9ED4,0x22FE50,0x2730F0,0x2730F8,0x273100,0x273108),
        (0x2731B4,0x3D9ED8,0x22FE58,0x2731D0,0x2731D8,0x2731E0,0x2731E8),
        (0x2731EC,0x3D9EDC,0x22FE60,0x273208,0x273210,0x273218,0x273220)]:
    assert image.word(conversion)==0x46800860  # CVT.S.W f1,f1
    assert image.word(load)==0xC4200000|(field&65535)  # LWC1 old into f0
    assert image.word(add)==0x46010000  # ADD.S f0,f0,f1
    assert image.word(store)==0xE7800000|(field-0x3D6EF0)  # distinct stored float
    assert image.read(initializer,8)==bytes.fromhex('3e00013c')+(0xAC200000|(field&65535)).to_bytes(4,'little')
print('PASS: original activity contracts: 37 arities/cases, three boolean-normalized island records, four real CVT.S.W/ADD.S counters, indexed capacities, signed minima, justice increment, costume mask and float maxima; gameExecuted=false')
