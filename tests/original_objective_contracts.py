"""Check original objective completion and native task lifecycle as data only.

This does not implement IS_CHAR_OBJECTIVE_PASSED or grant runtime support.
It prevents equating SA task disappearance with LCS objective success.
"""
import importlib.util
import pathlib
import re
import sys

ROOT = pathlib.Path(__file__).resolve().parents[1]


def module(name, path):
    spec = importlib.util.spec_from_file_location(name, path)
    loaded = importlib.util.module_from_spec(spec)
    sys.modules[name] = loaded
    spec.loader.exec_module(loaded)
    return loaded


original = module('objective_original_inspect', ROOT/'tools/inspect-original-command.py')
image = original.OriginalImage(ROOT / 'local/originals/lcs-ps2/SLUS_214.23')
image.require_profile()
assert image.handler(299)['handler'] == 0x111744
assert image.word(0x111750) == 0x24060001  # collects one actor handle
assert image.word(0x111760) == 0x0C04481A  # genuine ped pool GetAt
assert image.word(0x111768) == 0xDC430190  # ld v1, flags at ped+190
assert image.word(0x111770) == 0x2402FFFF  # -1
assert image.word(0x111774) == 0x000217FC  # dsll32 v0,v0,31 -> bit 63
assert image.word(0x11177C) == 0x00621824  # and v1,v1,v0
assert image.handler(534)['handler'] == 0x25BDDC
assert image.word(0x25BE1C) == 0xDE620190  # goto reads actor flags
assert image.word(0x25BE20) == 0x2403FFFF
assert image.word(0x25BE24) == 0x0003187A  # dsrl -1,1: all bits except completion
assert image.word(0x25CB68) == 0x00431024  # clear that bit only
assert image.word(0x25CB70) == 0xFE620190  # write before setting the objective
assert image.word(0x25CB7C) == 0x0C0C231A  # real coord-objective consumer
for objective in (24,25,49):
    assert image.word(0x3C7690+objective*4) == 0x30BF48
assert image.word(0x3C7520+(24-4)*4) == 0x308E94
assert image.word(0x308EC0) == 0x3C013F00  # .5f
assert image.word(0x308EC4) == 0x44811000  # mtc1 at,f2
assert image.word(0x308F0C) == 0xE602053C  # source radius field
for objective in (25,49):
    assert image.word(0x3C7520+(objective-4)*4) == 0x308DA4
assert image.word(0x308DCC) == 0x3C013F00
assert image.word(0x308DD0) == 0x44811000
assert image.word(0x308E20) == 0xE602053C
assert image.word(0x30BFC0) == 0xAFA00038  # ignore height component
assert image.word(0x30BFE4) == 0x46002834  # radius squared < distance squared
assert image.word(0x30BFEC) == 0x45000078  # inverse branch reaches completion
assert image.word(0x30C204) == 0xFE420190  # completion publishes source flags

control = ROOT / 'local/references/res-relcs/src/control'
query = re.search(r'case COMMAND_IS_CHAR_OBJECTIVE_PASSED:(.*?)(?=\n\s*(?:/\*|case COMMAND_))',
                  (control/'Script.cpp').read_text(), re.S)[1]
assert 'UpdateCompareFlag(pPed->bScriptObjectiveCompleted)' in query
assert not re.search(r'bScriptObjectiveCompleted\s*=', query)

# Genuine script command handlers clear the source latch. They cannot be
# replaced by a generic "no native task = success" query after translation.
resets = {}
for source in control.glob('Script*.cpp'):
    text = source.read_text()
    for name, body in re.findall(r'case (COMMAND_\w+):(.*?)(?=\n\s*case COMMAND_|\Z)', text, re.S):
        if 'bScriptObjectiveCompleted = false' in body:
            resets[name] = source.name
for name in ('SET_CHAR_OBJ_WAIT_ON_FOOT', 'SET_CHAR_OBJ_KILL_CHAR_ON_FOOT',
             'SET_CHAR_OBJ_GOTO_COORD_ON_FOOT', 'SET_CHAR_OBJ_ENTER_CAR_AS_PASSENGER',
             'SET_CHAR_OBJ_ENTER_CAR_AS_DRIVER'):
    assert 'COMMAND_'+name in resets, name

native = module('objective_native_inspect', ROOT/'tools/inspect-native-command.py')
packaged_reference=ROOT.parent/'evidence/native-sa/gta_sa.exe'
pe = native.NativeImage(pathlib.Path(sys.argv[1]) if len(sys.argv)>1 else packaged_reference if packaged_reference.is_file() else ROOT/'local/references/native-sa/gta_sa.exe')
# GetStatus scans records of size 14h between C0B1E8 and C0BBE8: 128 slots.
assert pe.read(0x60871C, 5).hex() == 'b9e8b1c000'
assert pe.read(0x60872F, 3).hex() == '83c114'
assert pe.read(0x608732, 6).hex() == '81f9e8bbc000'
assert (0xC0BBE8-0xC0B1E8)//0x14 == 128
assert pe.read(0x608719, 3).hex() == '83c8ff'  # eax=-1 before scan
assert pe.read(0x60873A, 2).hex() == '5ec3'  # exhausted scan keeps -1
assert pe.read(0x60873C, 3).hex() == '8b4110'  # otherwise returns status only
# Flush destroys ownership and sets FLUSHED=3, not a success code.
assert pe.read(0x608379, 7).hex() == 'c7461003000000'
assert pe.read(0x608380, 6).hex() == 'c706ffffffff'
assert pe.read(0x60836B, 7).hex() == 'c7460400000000'
assert pe.read(0x608372, 7).hex() == 'c7460800000000'
assert pe.read(0x608520,4).hex() == '8b442404'
assert pe.read(0x608524,7).hex() == 'c7410400000000'
assert pe.read(0x60852B,3).hex() == '894108'
assert pe.read(0x60852E,7).hex() == 'c7411001000000'
# The retail compiler inlined Flush in all four Set variants. Observing only
# calls to 608350 would miss record reuse, including same pointer/opcode reuse.
assert pe.read(0x6083AC,19).hex() == '895e04895e08c7461003000000c706ffffffff'
for address in (0x6083F9,0x608459,0x6084B9):
    assert pe.read(address,27).hex() == 'c7460400000000c7460800000000c7461003000000c706ffffffff'
assert pe.read(0x4BA8A0,8).hex() == '8bc853e878dc1400' # actual event association call
assert pe.read(0x608679,5).hex() == 'e8d2fcffff' # Process invalidation call
assert pe.read(0x681720,12).hex() == '33c0833c810075094083f805' # first of five primary roots
assert pe.read(0x61A5A0,11).hex() == '8b0da844b700e955ffffff' # actual task pool
assert pe.read(0x61A55A,3).hex() == 'c1e007' # allocator stride 128
assert pe.word(0x86FDA8+8) == 0x421190
assert pe.read(0x421190,4).hex() == '8b4108c3' # complex task child getter
# The source predicate is specifically the current LEAVE_CAR_AND_DIE goal,
# not dead health, task completion, cancellation or the native empty 050D.
assert image.handler(1298)['handler']==0x187948
assert image.word(0x187970)==0x8C430250
assert image.word(0x187978)==0x38630026
assert pe.word(0x86DD00+16)==0x62F240
assert pe.read(0x62F240,6).hex()=='b8c1020000c3' # actual native task type 705
assert pe.handler(0x050D)[1]==0x48CC3D
assert pe.read(0x48CC3D,2).hex()=='0cff' # unimplemented dispatcher return
record = (ROOT/'vendor/sa-reference/PedScriptedTaskRecord.cpp').read_text()
assert 'taskRecordData.Flush();' in record
assert 'taskManager.HasTaskPrimary(taskRecordData.m_task)' in record
assert 'taskManager.HasTaskSecondary(taskRecordData.m_task)' in record
assert 'GetTaskScriptCommand(taskRecordData.m_ped)' in record
assert 'CScriptedBrainTaskStore::GetTask(taskRecordData.m_ped)' in record
assert 'scriptedTask != taskRecordData.m_task' in record
assert 'GetEventGroup().HasEvent(taskRecordData.m_event)' in record
print(f'PASS: original non-consuming completion latch, {len(resets)} source reset handlers, '
      '128 native lifecycle records and ambiguous flush/absence semantics; '
      'gameExecuted=false, runtimeSupportGranted=false')
