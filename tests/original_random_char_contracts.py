"""Retail 1494 evidence only; passing does not grant runtime support."""
import importlib.util
from pathlib import Path
import sys

ROOT = Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location('random_char_original', ROOT / 'tools/inspect-original-command.py')
source = importlib.util.module_from_spec(spec)
sys.modules[spec.name] = source
spec.loader.exec_module(source)
elf = source.OriginalImage(ROOT / 'local/originals/lcs-ps2/SLUS_214.23')
elf.require_profile()

assert elf.handler(1494)['handler'] == 0x2502B0
assert elf.word(0x2502C4) == 0x24060004
# The submitted implementation omitted this actual WAITSTATE_FALSE filter.
assert elf.word(0x25036C) == 0x8E020360  # lw v0,0x360(s0)
assert elf.word(0x250370) == 0x54400033  # bnezl v0,skip-candidate
# The vertical tolerance is five, not the submitted two units.
assert elf.word(0x250398) == 0x3C0140A0  # lui at,0x40a0 (5.0f)
# CharCreatedBy is not an eligibility filter. Its mission-creation store
# follows the shared last-selection receipt.
for address in range(0x2502B0, 0x2503F8, 4):
    word = elf.word(address)
    assert not ((word >> 26) in (0x20, 0x21, 0x23, 0x24, 0x25, 0x27, 0x37)
                and ((word >> 21) & 31) == 16 and (word & 0xffff) == 0x1ac), hex(address)
for address, expected in {
    0x2503F8: 0xAC349C18,  # shared LastRandomPedId
    0x2503FC: 0x24030002,
    0x250400: 0xA20301AC,  # MISSION_CHAR
    0x250420: 0xAC239C60,  # mission population count
}.items():
    assert elf.word(address) == expected, hex(address)
print('PASS: retail1494 WAITSTATE, Z tolerance5, no random-creation filter and selection/promotion stores; runtime integration not approved; GTA not executed')
