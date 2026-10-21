"""Inspect the reported noclip/teleport crash profiles as file data only.

This bounded diagnostic requires the installed versions' verified instructions.
It neither edits nor loads CLEO, scripts, DLLs or gta_sa.exe.
"""
import argparse
import hashlib
import importlib.util
import json
import pathlib
import struct
from bootstrap_player_exports import export_player_globals


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def read_rva(data, rva, length):
    nt = struct.unpack_from('<I', data, 60)[0]
    if data[:2] != b'MZ' or data[nt:nt+4] != b'PE\0\0' or struct.unpack_from('<H', data, nt+4)[0] != 0x14c:
        raise ValueError('Unsupported module PE')
    sections = nt + 24 + struct.unpack_from('<H', data, nt+20)[0]
    for i in range(struct.unpack_from('<H', data, nt+6)[0]):
        _, start, size, raw = struct.unpack_from('<IIII', data, sections+i*40+8)
        if start <= rva and rva+length <= start+size:
            return data[raw+rva-start:raw+rva-start+length]
    raise ValueError('Crash RVA outside module file data')


def audit(game):
    files = dict(executable=game/'gta_sa.exe', cleoplus=game/'CLEO/CLEO+.cleo',
                 noclip=game/'modloader/NoClip/CLEO/No Clip (Junior_Djjr).cs',
                 teleport=game/'CLEO/Teletrans (Junior_Djjr).cs',
                 bootstrap=game/'modloader/stories_bridge/data/script/main.scm')
    cleo = files['cleoplus'].read_bytes()
    nt = struct.unpack_from('<I', cleo, 60)[0]
    base = struct.unpack_from('<I', cleo, nt+52)[0]
    if read_rva(cleo, 0x11f8f, 12) != b'\x68'+struct.pack('<I', base+0x38040)+b'\x68\x49\x0e\0\0\xff\xd6' or \
       read_rva(cleo, 0x3806e, 3) != b'\x8b\x04\x88':
        raise ValueError('CLEO+ differs from the reported 0E49 crash profile')
    if b'\x49\x0e\x02\x0c\0\x4a\x0e\x02\x0c\0' not in files['noclip'].read_bytes():
        raise ValueError('Installed noclip player operand differs')
    if b'\x72\x01\x02\x0c\0\x03\0\0' not in files['teleport'].read_bytes():
        raise ValueError('Installed teleport player operand differs')
    spec = importlib.util.spec_from_file_location('native_profile', pathlib.Path(__file__).with_name('inspect-native-command.py'))
    native = importlib.util.module_from_spec(spec);spec.loader.exec_module(native)
    image = native.NativeImage(files['executable'])
    if image.handler(0x0172)[1] != 0x47caa9 or image.read(0x47cac3, 6) != b'\x8b\x88\x6c\x04\0\0':
        raise ValueError('Teleport crash is outside the verified character-heading handler')
    bootstrap = files['bootstrap'].read_bytes()
    repaired = export_player_globals(bootstrap)
    return dict(gameExecuted=False, modulesLoaded=False, modsModified=False,
                cause='native bootstrap omitted standard player actor global exports',
                bootstrapPlayerExportsPresent=bootstrap == repaired,
                nativeActorGlobalByteOffset=12,
                noclip=dict(module='CLEO+.cleo', crashRva='0x3806E', opcode='0x0E49',
                            command='IS_CHAR_ENTERING_ANY_CAR', actorOperand='global byte offset 12'),
                teleport=dict(module='gta_sa.exe', instruction='0x0047CAC3', opcode='0x0172',
                              command='GET_CHAR_HEADING', nullPed=True, actorOperand='global byte offset 12'),
                files=[dict(role=role, path=str(path), sha256=digest(path)) for role, path in files.items()],
                runtimeModCompatibilityVerified=False)


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--game', type=pathlib.Path, required=True)
    args = parser.parse_args()
    print(json.dumps(audit(args.game.resolve()), indent=2))
