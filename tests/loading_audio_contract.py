"""Read the native loading path as PE data; never start the game."""
import pathlib
import struct
import sys
from task_observer_install_contract import image

read = image(sys.argv[1])
assert read(0x507410, 15) == bytes.fromhex('68 00 00 80 3f 6a 00 6a 32 81 c1 b4 00 00 00')
call = read(0x50741f, 5)
assert call[0] == 0xe8 and 0x507424 + struct.unpack_from('<i', call, 1)[0] == 0x4dd4a0
# The filtered call is followed by the native sound-manager service, untouched.
assert read(0x507424, 10) == bytes.fromhex('b9 b0 2c b6 00 e9 d2 8b fe ff')
root = pathlib.Path(__file__).resolve().parents[1]
source = (root / 'src/asi.cpp').read_text(encoding='utf-8')
body = source.split('void __fastcall on_loading_frontend(', 1)[1].split('\n}', 1)[0]
assert 'if(event==50)return;' in body
assert '(original_loading_frontend)(self,event,boost,speed)' in body
assert 'setting(L"LcsSilentLoading")!=L"0"' in source
print('Loading tune: checked native start call, preserved service and event fallback')
