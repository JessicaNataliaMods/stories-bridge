"""Retail cop-query prerequisites, inspected as ELF data; no runtime support granted."""
from pathlib import Path
import importlib.util,sys
r=Path(__file__).resolve().parents[1]
s=importlib.util.spec_from_file_location('cop_original',r/'tools/inspect-original-command.py');m=importlib.util.module_from_spec(s);sys.modules[s.name]=m;s.loader.exec_module(m)
i=m.OriginalImage(r.parent/'lcs-ps2/SLUS_214.23');i.require_profile()
assert i.handler(1134)['handler']==0x269BD4
assert i.word(0x1DED9C)==0x28420027 # original control strictly state<39, unlike SA native control
for a,w in {0x269BE8:0x24060009,0x269C14:0x1233007A,0x269C80:0x24020006,0x269CA8:0x0C099CE2,0x269CD8:0x24020032,0x269CE0:0x24020027,0x269D48:0x3C014120,0x269DB0:0xA24301AC,0x269DE8:0x24060002,0x269DF0:0x12360004}.items():assert i.word(a)==w,(hex(a),hex(i.word(a)),hex(w))
print('PASS: retail includes slot zero; nine operands, COP model filter, driving/abseil, height 10, promotion/cleanup; gameExecuted=false')

if len(sys.argv)>1:
    spec=importlib.util.spec_from_file_location('cop_native',r/'tools/inspect-native-command.py');native=importlib.util.module_from_spec(spec);spec.loader.exec_module(native)
    pe=native.NativeImage(Path(sys.argv[1]))
    assert pe.handler(0x0469)[1]==0x48B3F7 and pe.read(0x48B3F7,6).hex()=='32c05f5e5d5b' # release command is empty, not a usable alias
    assert pe.read(0x5F7E46,5).hex()=='b84899c000' # group leader address, group+0x28
    assert pe.read(0x5F7E62,5).hex()=='05d4020000' # native group stride 0x2D4
    assert pe.read(0x5F7E68,5).hex()=='3de8afc000' # eight groups
    assert pe.read(0x401881,10).hex()=='be2899c000e902661f00' # reviewed retail trampoline
    assert pe.read(0x5F7EA0,3).hex()=='8d5604' # follower slots membership+4
    assert pe.read(0x5F7EB0,3).hex()=='397e20' # separate leader slot
    assert pe.read(0x5F69A0,4).hex()=='8b4120c3'
    print('PASS: native active group/member/leader layout and retail trampoline; read-only proof, no game execution')
    # CREATE_CHAR's genuine accounting is separate from SetCharCreatedBy.
    # Review the actual counter load/increment/store, rather than relying on
    # a similarly named population constant or assuming the setter counts it.
    assert pe.handler(0x009A)[1]==0x46754F
    assert pe.read(0x46766F,9).hex()=='6a028bcfe868d11700'
    assert pe.read(0x46772B,21).hex()=='a124ecc0008b0d9044b70083c4044057a324ecc000'
    assert pe.read(0x467761,19).hex()=='8b0d783ca4006a0251b95008a900e86cc0ffff'
    setter=list(pe.instructions(0x5E47E0,0x98))
    assert setter[-1].mnemonic=='ret' and setter[-1].op_str=='4'
    assert all('0xc0ec24' not in op.op_str for op in setter)
    print('PASS: mission counter C0EC24 increments separately after creation; CHAR cleanup uses type 2 and A90850')
    # A disk trampoline is evidence of a different cleanup implementation,
    # not permission to call it under an unmodified-retail ABI assumption.
    cleanup=next(pe.instructions(0x4637E0,5))
    if cleanup.mnemonic=='jmp':
        assert pe.read(0x4637E0,5).hex()=='e95b251001'
        assert pe.executable(0x1565D40)
        assert pe.read(0x1565D40,44).hex()==('33d28bc1803800740d83c008426683fa4b72f1c20800'
            '8b5424048950048a5424088810fe8158020000c20800')
        print('PASS: disk cleanup trampoline/target reviewed; 75 eight-byte slots, first free type byte, handle at +4, count at +258, full list returns without insertion')
