"""Data-only proof of the retail SA save/load call contracts; never starts the game."""
import importlib.util
from pathlib import Path
import sys

root = Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location("native_source", root / "tools/inspect-native-command.py")
native_source = importlib.util.module_from_spec(spec)
sys.modules[spec.name] = native_source
spec.loader.exec_module(native_source)

assert len(sys.argv) == 2, "Pass the packaged SA PE path as data, not as an executable"
pe = native_source.NativeImage(Path(sys.argv[1]))


def call_target(site):
    instruction = pe.read(site, 5)
    assert instruction[0] == 0xE8, hex(site)
    relative = int.from_bytes(instruction[1:], "little", signed=True)
    return site + 5 + relative


# The menu pushes its selected slot, and SaveSlot receives it as a callee-clean
# one-byte argument. This call returns into the exact in-function writer hook.
assert pe.read(0x578DEE, 6).hex() == "8a8e5f010000"  # m_nSelectedSaveGame at +0x15F
assert call_target(0x578DFA) == 0x619060
assert pe.read(0x619060, 6).hex() == "0fbe44240450"
assert call_target(0x619066) == 0x5D0E90  # format this slot's actual native path
assert pe.read(0x5D0F37, 7).hex() == "81c404010000c3"  # plain ret leaves pushed slot on caller stack
assert pe.read(0x61906B, 2).hex() == "c705"  # no add esp after formatter returns

# SaveSlot builds the path before its writer call. The path builder consumes
# the runtime prefix at C16F18, appends slot+1 and '.b', and writes C16DB8.
assert pe.read(0x86BAE0, 5) == b"%s%i\0"
assert pe.read(0x86BADC, 3) == b".b\0"
assert pe.read(0x5D0E9F, 20).hex() == "68186fc1008d4c240868e0ba860051c644241000"
assert pe.read(0x5D0F17, 5).hex() == "b8b86dc100"  # output path buffer C16DB8

# At 619081 the native writer returns bool success. SaveSlot inverts that
# result into its own API: true means failure to MenuManager_Process.cpp.
assert call_target(0x619081) == 0x5D13E0
assert pe.read(0x61907F, 2) == b"\x6A\x00"
assert pe.read(0x619086, 16).hex() == "83c40884c00f95c0fec883e002c20400"
# The wrapper sees flags, formatter slot, SaveSlot return address, and original slot.

# Initial and menu-triggered loads converge on GenericLoad(&loaded). Its
# reader opens the exact current filename in C15FC8; an invalid sidecar must
# not prevent this native call or change its result/out-parameter.
assert call_target(0x53C70B) == 0x5D17B0
assert pe.read(0x53C706, 5).hex() == "8d44240750"
assert pe.read(0x53C710, 7).hex() == "83c40484c07407"
assert pe.read(0x5D17D2, 8).hex() == "5353e847f5ffff83"
assert call_target(0x5D17D4) == 0x5D0D20
assert pe.read(0x5D0D5C, 18).hex() == "686c9c850068c85fc100e8957bf6ff83c408"
assert call_target(0x5D0D66) == 0x538900  # OpenFile(C15FC8, "rb")

print("PASS: retail save/load call ABIs, slot/path buffers, and success meanings verified from PE bytes; game not executed")

# CStreaming::ReInit resets special ped model keys, including Toni slots.
# This executable relocates the routine behind its original entry JMP.
assert pe.read(0x40E560,1)==b"\xe9"
reset=0x40E565+int.from_bytes(pe.read(0x40E561,4),"little",signed=True)
assert reset==0x156D000
assert pe.read(reset+0x7E,5).hex()=="be22010000"  # first special ped=290
assert pe.read(reset+0x90,5).hex()=="68548b8500"  # empty string
assert pe.read(0x858B54,1)==b"\0"
assert call_target(reset+0x95)==0x53CF30  # uppercase name key
assert pe.read(reset+0x9E,9).hex()=="81fe2b010000894704"  # through299; write model+4
print("PASS: native streaming reset writes empty-name keys to special ped slots290..299")

assert pe.read(0x53CF38,3).hex()=="83ceff"  # empty-name seed is FFFFFFFF
assert pe.read(0x53CF68,2).hex()=="8bc6"  # return unchanged seed on empty input
