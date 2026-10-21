"""Pins of every SA engine fact src/sa_ferry.cpp relies on, as data only (the game is never started).

Usage: ferry_abi_pin_tests.py <gta_sa.exe> [ferry-docked.col]

Evidence tool: capstone 5.0.7 (x86-32 decoder) over the SA 1.0 US image; the reversed sources in vendor/sa-reference
(BaseModelInfo.cpp, source_game_sa_FileLoader.cpp, sdk-CPhysical.h, sdk-CEntity.h, sdk-CVehicle.h) are the naming/layout
reference and are cross-checked against the image bytes. Limits: static decoding of the entry points actually called;
the loader's internal allocations are the engine's own and are exercised only in game.

Pinned facts
* model info: table 0xA9B0C8, m_pColModel at +0x14 (CBaseModelInfo::SetColModel 0x4C4BC0 writes it and the 0x80 flag at +0x12);
  CEntity::GetColModel 0x535300 reads +0x22 -> table -> +0x14, and for vehicles first honours m_nSpecialColModel (+0x48B, 0xFF none)
  from the 0x30-byte special models at 0xC1CC78: sizeof(CColModel) == 0x30.
* CColModel: constructor 0x40FB60 (-> 0x156C690) zeroes +0x28/+0x2C and sets flag bit 2; loader CFileLoader::LoadCollisionModel
  0x537580 (cdecl, data after the 32-byte file header, CColModel*) fills bounds, allocates the CCollisionData (0x30 bytes, ctor
  0x40F030) and stores it at +0x2C; destructor 0x40F700 frees it through RemoveCollisionVolumes 0x40F070 + operator delete.
* frames: CClumpModelInfo::GetFrameFromName 0x4C5400 -> RwFrameForAllChildren 0x7F0DC0 (child +0x98, next +0x9C) with the
  name callback 0x4C52A0 (NodeNameGetName 0x72FB30); CEntity::UpdateRwFrame 0x532B00 calls RwFrameUpdateObjects on the clump frame.
* CPhysical flag byte +0x40: bit1 gravity, bit4 turn force off, bit5 move force off, bit6 infinite mass (sdk-CPhysical.h).
* The packaged docked collision has the retail shape (COL1, sphere 35, one box 12.6 x 45.56 x 4.8).
"""
import pathlib
import re
import struct
import sys

import capstone

ROOT = pathlib.Path(__file__).resolve().parents[1]


def image(path):
    data = pathlib.Path(path).read_bytes()
    pe = struct.unpack_from('<I', data, 0x3C)[0]
    count = struct.unpack_from('<H', data, pe + 6)[0]
    optional = struct.unpack_from('<H', data, pe + 20)[0]
    base = struct.unpack_from('<I', data, pe + 52)[0]
    sections = []
    for i in range(count):
        o = pe + 24 + optional + i * 40
        size, va, raw_size, raw = struct.unpack_from('<IIII', data, o + 8)
        sections.append((base + va, max(size, raw_size), raw))

    def read(address, size):
        for va, length, raw in sections:
            if va <= address and address + size <= va + length:
                return data[raw + address - va:raw + address - va + size]
        raise AssertionError(hex(address))
    return read


def listing(read, address, count=60):
    md = capstone.Cs(capstone.CS_ARCH_X86, capstone.CS_MODE_32)
    return list(md.disasm(read(address, count * 8), address))[:count]


def text(ins):
    return '%s %s' % (ins.mnemonic, ins.op_str)


def contains(read, address, needle, count=80):
    return any(needle in text(i) for i in listing(read, address, count))


def main(executable, docked=None):
    read = image(executable)
    source = (ROOT / 'src/sa_ferry.cpp').read_text(encoding='utf-8')

    def const(name):
        match = re.search(r'%s\s*=\s*(0x[0-9A-Fa-f]+)' % re.escape(name), source)
        assert match, name
        return int(match[1], 16)

    # the constants the source uses are the ones pinned here
    assert const('model_info_table') == 0xA9B0C8 and const('model_info_col') == 0x14 and const('entity_clump') == 0x18
    assert const('vehicle_special_col') == 0x48B and const('physical_flags') == 0x40 and const('frame_matrix') == 0x10
    assert const('colmodel_size') == 0x30 and const('colmodel_data') == 0x2C
    for name, value in (('col_ctor', 0x40FB60), ('col_dtor', 0x40F700), ('col_load_v1', 0x537580), ('frame_from_name', 0x4C5400), ('update_rw_frame', 0x532B00)):
        assert const(name) == value, name

    # --- model info collision pointer --------------------------------------------------------------------------
    set_col = [text(i) for i in listing(read, 0x4C4BC0, 20)]
    assert 'mov dword ptr [ecx + 0x14], esi' in set_col and 'or byte ptr [ecx + 0x12], 0x80' in set_col, set_col
    assert any('0xa9b0c8' in t for t in set_col), 'SetColModel walks the model info table'
    get_col = [text(i) for i in listing(read, 0x535300, 24)]
    assert 'movsx ecx, word ptr [ecx + 0x22]' in get_col and 'mov edx, dword ptr [ecx*4 + 0xa9b0c8]' in get_col and 'mov eax, dword ptr [edx + 0x14]' in get_col, get_col
    assert 'lea eax, [eax + eax*2]' in get_col and 'shl eax, 4' in get_col and 'add eax, 0xc1cc78' in get_col, 'special models are 0x30 bytes apart'
    assert 'mov al, byte ptr [ecx + 0x48b]' in [text(i) for i in listing(read, 0x401644, 2)], 'vehicles read m_nSpecialColModel (+0x48B)'
    assert 'cmp al, 0xff' in get_col, '0xFF means no special model'
    # reversed sources agree
    base_info = (ROOT / 'vendor/sa-reference/BaseModelInfo.h').read_text(encoding='utf-8')
    assert re.search(r'CColModel\*\s+m_pColModel;\s*//\s*20', base_info)
    entity = (ROOT / 'vendor/sa-reference/sdk-CEntity.h').read_text(encoding='utf-8')
    vehicle = (ROOT / 'vendor/sa-reference/sdk-CVehicle.h').read_text(encoding='utf-8')
    assert 'VALIDATE_OFFSET(CEntity, m_pRwClump, 0x18)' in entity and 'VALIDATE_OFFSET(CVehicle, m_nSpecialColModel, 0x48B)' in vehicle

    # --- CColModel constructor / loader / destructor -------------------------------------------------------------
    ctor_jump = listing(read, 0x40FB60, 1)[0]
    assert ctor_jump.mnemonic == 'jmp' and int(ctor_jump.op_str, 16) == 0x156C690
    ctor = [text(i) for i in listing(read, 0x156C690, 14)]
    assert 'call 0x40faf0' in ctor and 'mov byte ptr [esi + 0x28], al' in ctor and 'mov dword ptr [esi + 0x2c], eax' in ctor, ctor
    assert 'and al, 0xfc' in ctor and 'or al, 4' in ctor and 'mov byte ptr [esi + 0x29], al' in ctor, 'active flag set by the constructor'
    loader = [text(i) for i in listing(read, 0x537580, 80)]
    assert loader[0] == 'push -1', loader[0]
    for needle in ('mov ebp, dword ptr [esp + 0x2c]', 'mov esi, dword ptr [esp + 0x28]', 'mov dword ptr [ebp + 0x24], eax', 'lea ecx, [ebp + 0x18]',
                   'lea ecx, [ebp + 0xc]', 'push 0x30', 'call 0x82119a', 'call 0x40f030', 'mov dword ptr [ebp + 0x2c], edi'):
        assert needle in loader, needle
    ret = [i for i in listing(read, 0x537580, 400) if i.mnemonic == 'ret']
    assert ret and ret[0].op_str == '', 'the loader is cdecl: its first return pops nothing'
    cd_ctor = [text(i) for i in listing(read, 0x40F030, 10)]
    assert 'mov word ptr [eax], cx' in cd_ctor and 'mov byte ptr [eax + 6], cl' in cd_ctor
    dtor = [text(i) for i in listing(read, 0x40F700, 24)]
    for needle in ('test al, 4', 'mov ecx, dword ptr [esi + 0x2c]', 'call 0x40f070', 'call 0x8214bd'):
        assert needle in dtor, needle
    assert 'mov al, byte ptr [esi + 0x29]' in dtor
    file_loader = (ROOT / 'vendor/sa-reference/source_game_sa_FileLoader.cpp').read_text(encoding='utf-8')
    assert 'RH_ScopedInstall(LoadCollisionModel, 0x537580)' in file_loader and 'cm.m_pColData = cd;' in file_loader

    # --- RW frame lookup -----------------------------------------------------------------------------------------
    by_name = [text(i) for i in listing(read, 0x4C5400, 14)]
    assert 'push 0x4c52a0' in by_name and 'call 0x7f0dc0' in by_name and 'mov eax, dword ptr [edx + 4]' in by_name, by_name
    callback = [text(i) for i in listing(read, 0x4C52A0, 20)]
    assert 'call 0x72fb30' in callback and 'call 0x8229b6' in callback, callback
    children = [text(i) for i in listing(read, 0x7F0DC0, 18)]
    assert 'mov eax, dword ptr [ebp + 0x98]' in children and 'mov esi, dword ptr [eax + 0x9c]' in children, children
    sync = [text(i) for i in listing(read, 0x532B00, 6)]
    assert sync[0] == 'mov eax, dword ptr [ecx + 0x18]' and 'call 0x7f0910' in sync and 'mov eax, dword ptr [eax + 4]' in sync, sync

    # --- CPhysical flag byte ------------------------------------------------------------------------------------
    physical = (ROOT / 'vendor/sa-reference/sdk-CPhysical.h').read_text(encoding='utf-8')
    order = re.findall(r'bool (\w+) : 1;', physical[physical.index('bool bIsHeavy'):])
    assert order[:8] == ['bIsHeavy', 'bApplyGravity', 'bDisableCollisionForce', 'bCollidable', 'bDisableTurnForce', 'bDisableMoveForce', 'bInfiniteMass', 'bDisableZ'], order[:8]
    assert (const('flag_gravity'), const('flag_turn'), const('flag_move'), const('flag_infinite_mass')) == (1 << 1, 1 << 4, 1 << 5, 1 << 6)
    assert 'VALIDATE_OFFSET(CPhysical, m_vecMoveSpeed, 0x44)' in physical

    # --- packaged docked collision --------------------------------------------------------------------------------
    if docked:
        file = pathlib.Path(docked).read_bytes()
        assert file[:4] == b'COLL' and struct.unpack_from('<I', file, 4)[0] == len(file) - 8 and len(file) == 120
        body = file[32:]
        bounds = struct.unpack_from('<10f', body, 0)
        assert bounds[0] == 35.0 and bounds[1:4] == (0.0, 0.0, 0.0), bounds
        spheres, lines, boxes = struct.unpack_from('<3I', body, 40)
        assert (spheres, lines, boxes) == (0, 0, 1)
        box = struct.unpack_from('<6f', body, 52)
        assert all(abs(a - b) < 1e-4 for a, b in zip(box, (-6.3, -22.78, -2.0, 6.3, 22.78, 2.8))), box
        assert body[76] == 51 and struct.unpack_from('<2I', body, 80) == (0, 0), 'surface material and empty vertex/face blocks'
        # src/sa_ferry.cpp accepts exactly this block structure
        assert 'boxes<1||boxes>4' in source and 'file.size()!=tail+8' in source
    print('PASS: ferry docked-collision swap, CColModel loader/destructor, frame lookup and physics flags match the SA image')


if __name__ == '__main__':
    main(*sys.argv[1:])
