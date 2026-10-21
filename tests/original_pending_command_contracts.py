"""Retail-data preflight for still-unported command families. NO game execution.

This is NOT a runtime-support grant. In particular, the reconstructed debug
commands and the retail default handler are distinct sources of evidence.
"""
import importlib.util
from pathlib import Path
import sys

ROOT=Path(__file__).resolve().parents[1]
module=ROOT/'tools/inspect-original-command.py'
spec=importlib.util.spec_from_file_location('retail_pending_v57',module)
retail=importlib.util.module_from_spec(spec)
sys.modules[spec.name]=retail
spec.loader.exec_module(retail)
elf=retail.OriginalImage(ROOT / 'local/originals/lcs-ps2/SLUS_214.23')
elf.require_profile()

# A source reconstruction implementing DebugFlag does not establish retail
# executable support: both commands fall through the actual retail dispatcher.
for opcode in (195,196):
    dispatch=elf.handler(opcode)
    assert dispatch['default'] and dispatch['handler']==0x10FEF0,(opcode,dispatch)
# Likewise, a guessed building swap operation is unsupported at its retail site.
assert elf.handler(1568)['default'] and elf.handler(1568)['handler']==0x2F3768

# Retail 1655 actually changes the vehicle flags in place, using bit 30 at
# CVehicle +0x258. This LCS layout is NOT the SA layout; do not write it to SA.
reward=elf.handler(1655)
assert not reward['default'] and reward['handler']==0x2746DC,reward
assert [elf.word(i) for i in (0x274718,0x27471C,0x274720,0x274724,0x274728,0x27472C,0x274730,0x274734,0x274738)]==[
    0xDC820258,0x10000005,0x00431025,0xDC820258,0x3C03BFFF,0x3463FFFF,0x00431024,0x1000FADC,0xFC820258]

# Source 1588 is an unsigned byte write, not a floating-point multiplier or a
# replacement for native crane/garage accounting. No backend consumer yet.
crush=elf.handler(1588)
assert not crush['default'] and crush['handler']==0x2F33E8,crush
assert elf.word(0x2F3400)==0x9203F320 and elf.word(0x2F340C)==0xA3832E44

# Both 1570/1574 point at the same short retail exit. Supplying invented
# multiplayer IDs to LCS script outputs would be wrong.
assert elf.handler(1570)['handler']==elf.handler(1574)['handler']==0x2F0E2C
assert elf.word(0x2F0E2C)==0x10000A4F and elf.word(0x2F0E30)==0x0000102D

print('PASS: retail defaults, vehicle reward-bit setter, crusher reward-byte and multiplayer exit; source-only evidence, no runtime support')
