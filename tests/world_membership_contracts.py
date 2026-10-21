"""Read-only retail PE contracts for physical membership and world retirement."""
import importlib.util
from pathlib import Path
import sys

root = Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location('world_native', root/'tools/inspect-native-command.py')
native = importlib.util.module_from_spec(spec)
sys.modules[spec.name] = native
spec.loader.exec_module(native)
assert len(sys.argv) == 2, 'Pass the reference SA PE as data'
pe = native.NativeImage(Path(sys.argv[1]))

def word(address):
    return int.from_bytes(pe.read(address, 4), 'little')

# Add(CRect&) is slot 1; parameterless Add() is slot 2; Remove() is slot 3.
assert word(0x863928 + 8) == 0x533020
assert word(0x863928 + 12) == 0x534AE0
for table in (0x871E80, 0x8721A0):  # CVehicle / CBoat
    assert word(table + 8) == 0x544A30
    assert word(table + 12) == 0x5424C0
assert pe.read(0x56418F, 3) == bytes.fromhex('ff500c')
assert word(0x863C40 + 12) == 0x42480000  # cannot dispatch Remove from destroyed CPlaceable
for site, target in ((0x53C9BC, 0x564050), (0x53C5CB, 0x564360)):
    call = pe.read(site, 5)
    assert call[0] == 0xE8 and site + 5 + int.from_bytes(call[1:], 'little', signed=True) == target
asi = (root/'src/asi.cpp').read_text()
for hook, original in (('on_world_shutdown','original_world_shutdown'),('on_world_restart','original_world_restart')):
    body = asi.split('void __cdecl '+hook+'(){',1)[1].split('\n}',1)[0]
    assert body.index('retire_campaign_world();') < body.index('sa_world_sector_world_retiring();') < body.index(original+'();')
retire = asi.split('void retire_campaign_world() {',1)[1].split('\n}',1)[0]
assert 'campaign.reset();' in retire and 'player_skins.reset();' in retire
assert 'inspected_world=false;world_failure=false;' in retire
# Load and New Game share ClearForRestart; never treat a native load as a new
# LCS campaign merely because the old VM was retired at that boundary.
assert pe.read(0x53C6A7,4) == bytes.fromhex('a867ba00')
assert 'campaign_load_requires_restore=loading;' in asi
load_guard = asi.split('if(campaign_load_requires_restore) {',1)[1].split('\n    }',1)[0]
assert 'return;' in load_guard and 'start_main' not in load_guard
preview = (root/'src'/'sa_world_preview.cpp').read_text()
assert '0xB7449C' in preview and 'object+0x170' in preview
assert 'preview_apply_physical_membership(bits,rule,was_big,is_dummy_pool,promoted' in preview
assert 'promoted_dummy_matches(promoted_dummies,entity)' in preview
object_sdk = (root/'vendor'/'sa-reference'/'sdk-CObject.h').read_text()
assert 'VALIDATE_OFFSET(CObject, m_pDummyObject, 0x170)' in object_sdk
garages = (root/'src/sa_garages.cpp').read_text()
register = garages.split('bool register_native(',1)[1].split('\n}\n}',1)[0]
assert register.index('reinterpret_cast<AddOne>(0x4471E0)') < register.index('0x96C024)!=count+1') < register.index('(0x447600)(garage)')
print('PASS: physical membership, promoted dummy ownership, shutdown dispatch, and both world retirement CALLs; PE read only')
