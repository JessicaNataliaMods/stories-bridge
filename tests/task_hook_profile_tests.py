"""Independent PE/instruction-boundary proof for the proposed native hook sites."""
import importlib.util
import pathlib
import re
import sys
ROOT=pathlib.Path(__file__).resolve().parents[1]
spec=importlib.util.spec_from_file_location('task_hook_native_inspect',ROOT/'tools/inspect-native-command.py')
module=importlib.util.module_from_spec(spec)
spec.loader.exec_module(module)
image=module.NativeImage(pathlib.Path(sys.argv[1]))
source=(ROOT/'include/lcs/task_hook_plan.hpp').read_text()
rows=re.findall(r'\{(0x[0-9A-Fa-f]+),(\d+),\{\{([^}]+)\}\}\}',source)
assert len(rows)==11
expected=(0x608330,0x608350,0x608390,0x6083E0,0x608440,0x6084A0,0x608500,0x608520,0x61A5B0,0x60860C,0x4B0A50)
for row,address in zip(rows,expected):
    assert int(row[0],16)==address
    length=int(row[1])
    prefix=bytes(int(word.strip(),0) for word in row[2].split(','))
    assert len(prefix)==length and image.read(address,length)==prefix,(hex(address),prefix.hex())
    instructions=list(image.instructions(address,length))
    assert sum(instruction.size for instruction in instructions)==length
    # Only these straight-line non-relative instructions were reviewed for
    # relocation. Any new prefix requires a new ABI/control-flow audit.
    assert all(instruction.mnemonic in ('mov','push','xor') for instruction in instructions)
    assert image.executable(address) and image.executable(address+length)
assert image.read(0x465CC8,10).hex()=='f7867404000000008000' # script-brain rejection in actual assignment
assert image.read(0x4B0A19,9).hex()=='c70078b38500895010' # actual script event vtable and task +16
assert image.read(0x4B0A73,5).hex()=='8b4e1085c9' # destructor reads the owned task before virtual delete
assert image.read(0x60860C,22).hex()=='893e897ef8897efcc7460403000000c746f4ffffffff' # inline bank Flush, not a call to 608350
script_call=list(image.instructions(0x53BFC7,5))
world_call=list(image.instructions(0x53C095,5))
assert len(script_call)==len(world_call)==1
assert script_call[0].mnemonic=='call' and script_call[0].op_str=='0x46a000'
assert world_call[0].mnemonic=='call' and world_call[0].op_str=='0x5684a0'
# Existing ASI callback resumes after Scripts::Process and returns to the
# collision/population/world update chain. This proves ordering, not arbitrary
# foreign-thread exclusion or authorization to activate unfinished consumers.
chain=list(image.instructions(0x53BFC7,0x53C09A-0x53BFC7))
assert chain[0].address==0x53BFC7 and chain[-1].address==0x53C095
print('PASS: 11 native whole-instruction mutation/free sites, event task layout and Process inline Flush; no detour installed, gameExecuted=false')

# Exact unassigned route task disposal, inspected as data only.
assert image.read(0x86FDA8,4).hex()=='80db6600'
assert image.read(0x66DB80,30).hex()=='568bf1e8b8a6fffff644240801740956e81bcafaff83c4048bc65ec20400'
assert image.read(0x61A3B0,17).hex()=='8bc133c9894804c700ccd48600894808c3'
