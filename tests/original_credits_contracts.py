"""Check the retail SA CFont ABI used by credits without executing game code."""
import importlib.util
import pathlib
import sys

ROOT = pathlib.Path(__file__).resolve().parents[1]


def module(name, file):
    spec = importlib.util.spec_from_file_location(name, ROOT / "tools" / file)
    value = importlib.util.module_from_spec(spec)
    sys.modules[name] = value
    spec.loader.exec_module(value)
    return value


native = module("credits_native", "inspect-native-command.py").NativeImage(
    pathlib.Path(sys.argv[1])
)


def body(name, address, expected_hex):
    expected = bytes.fromhex(expected_hex)
    actual = native.read(address, len(expected))
    assert actual == expected, (
        f"{name} at 0x{address:08X}: got {actual.hex()}, expected {expected.hex()}"
    )


def instruction(address):
    decoded = list(native.instructions(address, 16))
    assert decoded and decoded[0].address == address, hex(address)
    return decoded[0]


def require_instruction(address, mnemonic, operands):
    actual = instruction(address)
    assert (actual.mnemonic, actual.op_str) == (mnemonic, operands), (
        hex(address), actual.mnemonic, actual.op_str, mnemonic, operands
    )


# Full small method bodies establish the actual stack arguments and the CFont
# state writes. The first SetFontStyle body ends at C3; the following byte is
# alignment padding, not part of the method.
for name, address, expected in [
    ("SetScale(float,float)", 0x719380, "8b4424048b4c2408a3641ac700890d681ac700c3"),
    ("SetColor(CRGBA by value)", 0x719430,
     "d905801ac7008b442404d81dac9a85008bc8a2601ac7008825611ac700c1e910dfe0"
     "880d621ac700882d631ac700f6c4057a210fb6c589442404db442404d80d801ac700"
     "d80d3c9a8500e8c1861000a2631ac700c3"),
    ("SetFontStyle(short)", 0x719490,
     "8a4c24040fb6c183e802741e48740e880d941ac700c605951ac70000c3b001a294"
     "1ac700a2951ac700c3c605941ac70000c605951ac70002c3"),
    ("SetWrapx(float)", 0x7194D0, "8b442404a3881ac700c3"),
    ("SetCentreSize(float)", 0x7194E0, "8b442404a38c1ac700c3"),
    ("SetRightJustifyWrap(float)", 0x7194F0, "8b442404a3901ac700c3"),
    ("SetAlphaFade(float)", 0x719500, "8b442404a3801ac700c3"),
    ("SetDropColor(CRGBA by value)", 0x719510,
     "8b442404d905801ac700d81dac9a8500a2971ac7008825981ac700c1e810a2991ac700"
     "88259a1ac700dfe0f6c4057a250fb605631ac70089442404db442404d80d801ac700"
     "d80d3c9a8500e8e0851000a29a1ac700c3"),
    ("SetDropShadowPosition(short)", 0x719570,
     "32c0a29b1ac700a29c1ac7008a442404a2961ac700c3"),
    ("SetEdge(short)", 0x719590,
     "8a442404c605961ac70000a29b1ac700a29c1ac700c3"),
    ("SetProportional(bool)", 0x7195B0, "8a442404a27d1ac700c3"),
    ("SetBackground(bool,bool)", 0x7195C0,
     "8a4424048a4c2408a27b1ac700880d7c1ac700c3"),
    ("SetBackgroundColor(CRGBA by value)", 0x7195E0,
     "8b442404a2841ac7008825851ac700c1e810a2861ac7008825871ac700c3"),
    ("SetJustify(bool)", 0x719600, "8a442404a2781ac700c3"),
    ("SetOrientation(eFontAlignment)", 0x719610,
     "0fb644240433c92bc17421487411487528880d791ac700c6057a1ac70001c3"
     "880d791ac700880d7a1ac700c3c605791ac70001880d7a1ac700c3"),
    ("GetNumberLines(float,float,const char*)", 0x71A5E0,
     "8b44240c8b4c24088b5424045051526a00e82afcffff83c410c3"),
    ("DrawFonts() thunk", 0x71A210, "e92bf6ffff"),
]:
    body(name, address, expected)

# SetOrientation writes C71A79/C71A7A. The shared CFont layout routine uses
# C71A79 for centre alignment and C71A7A for right alignment, while neither
# flag selects left alignment. Its associated widths are C71A8C/C71A90.
require_instruction(0x71A23B, "mov", "al, byte ptr [0xc71a79]")
require_instruction(0x71A240, "test", "al, al")
require_instruction(0x71A2D4, "test", "cl, cl")
require_instruction(0x71A2D8, "mov", "al, byte ptr [0xc71a7a]")
require_instruction(0x71A2E1, "fld", "dword ptr [esp + 0x144]")
require_instruction(0x71A2E8, "fsub", "dword ptr [0xc71a90]")
require_instruction(0x71A2F0, "fld", "dword ptr [0xc71a88]")
require_instruction(0x71A2F8, "fld", "dword ptr [0xc71a8c]")
assert native.read(0x71A210, 5) == bytes.fromhex("e92bf6ffff")
assert native.executable(0x719840), "DrawFonts thunk target must be executable"

# GetNumberLines consumes cdecl (x, y, text), forwards all three values to
# the same layout routine, and returns its signed 16-bit result to its caller.
require_instruction(0x71A5F1, "call", "0x71a220")
require_instruction(0x58B7EA, "movsx", "eax, ax")

# PrintString(float,float,text) lays out/enqueues text through the existing
# font path. DrawFonts remains a separate renderer flush; credits must not
# flush it themselves or change the render-state boundary.
require_instruction(0x71A703, "push", "ebx")
require_instruction(0x71A704, "mov", "ebx, dword ptr [esp + 0x20]")
require_instruction(0x71A77B, "call", "0x71a620")
require_instruction(0x71A802, "call", "0x71a220")
print_string = list(native.instructions(0x71A700, 0x111))
assert print_string and print_string[-1].address == 0x71A810
assert print_string[-1].mnemonic == "ret"
assert not any(
    ins.mnemonic == "call" and ins.op_str == "0x71a210" for ins in print_string
), "PrintString must enqueue/layout only; DrawFonts is the later shared flush"

# Retail Render2dStuff calls CHud::Draw, then reaches the existing DrawFonts
# flush in a straight-line epilogue. Credits can enqueue after CHud::Draw
# returns and leave that later flush and render-state boundary intact.
body("Render2dStuff -> CHud::Draw call", 0x53E4FF, "e8dc150500")
body("Render2dStuff -> DrawFonts tail jump", 0x53E52B, "e9e0bc1d00")
path = list(native.instructions(0x53E4FF, 0x31))
expected_path = [
    (0x53E4FF, "call", "0x58fae0"),
    (0x53E504, "call", "0x721660"),
    (0x53E509, "mov", "ecx, 0xba1788"),
    (0x53E50E, "call", "0x44cc20"),
    (0x53E513, "push", "1"),
    (0x53E515, "call", "0x69efc0"),
    (0x53E51A, "add", "esp, 4"),
    (0x53E51D, "call", "0x43cec0"),
    (0x53E522, "call", "0x447790"),
    (0x53E527, "pop", "esi"),
    (0x53E528, "add", "esp, 0x18"),
    (0x53E52B, "jmp", "0x71a210"),
]
assert [(ins.address, ins.mnemonic, ins.op_str) for ins in path] == expected_path
print("PASS: retail SA CFont credit ABI, exact state consumers and separate font flush; gameExecuted=false")
