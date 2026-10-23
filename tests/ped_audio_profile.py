"""Static retail-SA pins for the CPed::Say speech-slot hook; the PE is read as data only.

Usage: ped_audio_profile.py <gta_sa.exe>
"""
import pathlib
import struct
import sys


ROOT = pathlib.Path(__file__).resolve().parents[1]


def image_reader(path):
    data = pathlib.Path(path).read_bytes()
    pe = struct.unpack_from("<I", data, 0x3C)[0]
    section_count = struct.unpack_from("<H", data, pe + 6)[0]
    optional_size = struct.unpack_from("<H", data, pe + 20)[0]
    image_base = struct.unpack_from("<I", data, pe + 24 + 28)[0]
    sections = []
    for index in range(section_count):
        offset = pe + 24 + optional_size + index * 40
        virtual_size, rva, raw_size, raw_offset = struct.unpack_from("<IIII", data, offset + 8)
        sections.append((image_base + rva, max(virtual_size, raw_size), raw_offset, raw_size))

    def read(address, size):
        for va, span, raw, raw_size in sections:
            if va <= address and address + size <= va + span:
                offset = raw + address - va
                assert offset + size <= raw + raw_size, hex(address)
                return data[offset:offset + size]
        raise AssertionError("address is outside the PE image: " + hex(address))

    return read


def call_target(read, address):
    instruction = read(address, 5)
    assert instruction[0] == 0xE8, hex(address)
    displacement = struct.unpack_from("<i", instruction, 1)[0]
    return address + 5 + displacement


def main(executable):
    read = image_reader(executable)
    # CPed::Say passes its embedded speech entity at ped+0x294 and event 52,
    # then directly calls the accepted/refused native AddSayEvent function.
    assert read(0x5F0003, 8) == bytes.fromhex("6A 34 81 C1 94 02 00 00")
    assert call_target(read, 0x5F000B) == 0x4E6550

    # AddSayEvent accumulates and commits the dB volume in speech slot +0xAC,
    # then enters the native scheduling path. The hook runs after this call
    # returns; muting the accepted slot before the later service pass preserves
    # the native queue/callback rather than claiming a made-up success.
    assert read(0x4E68A5, 6) == bytes.fromhex("D8 86 AC 00 00 00")
    assert read(0x4E68B2, 6) == bytes.fromhex("D9 9E AC 00 00 00")
    assert call_target(read, 0x4E68B8) == 0x4E4840
    # Service later reads the same +0xAC float into the CAESound volume arg.
    assert read(0x4E5D94, 7) == bytes.fromhex("8B 96 AC 00 00 00 52")

    # The source predicates for COP chase-foot and bump use the retail player
    # ped/wanted accessors. Their wrapper bodies and the native +0x2C wanted
    # read are pinned before the adapter is allowed to mute those lines.
    assert read(0x56E210, 28) == bytes.fromhex(
        "8B 44 24 04 85 C0 7D 07 0F B6 05 74 CD B7 00 69 C0 90 01 00 00 "
        "8B 80 98 CD B7 00 C3")
    assert read(0x56E230, 28) == bytes.fromhex(
        "8B 44 24 04 85 C0 7D 07 0F B6 05 74 CD B7 00 69 C0 90 01 00 00 "
        "8B 80 9C CD B7 00 C3")
    assert call_target(read, 0x562362) == 0x56E230
    assert read(0x562360, 26) == bytes.fromhex(
        "6A FF E8 C9 BE 00 00 8A 48 1E 83 C4 04 F6 C1 07 75 0F "
        "8B 40 2C 83 F8 03 74 0E")

    header = (ROOT / "include/lcs/ped_audio.hpp").read_text(encoding="utf-8")
    adapter = (ROOT / "src/sa_ped_audio.cpp").read_text(encoding="utf-8")
    mapping = (ROOT / "src/ped_audio.cpp").read_text(encoding="utf-8")
    assert "constexpr uint32_t ped_audio_entity_offset = 0x294" in adapter
    assert "constexpr uint32_t speech_volume_offset = 0xAC" in adapter
    assert "constexpr uintptr_t speech_service_volume_load = 0x4E5D94" in adapter
    assert "constexpr uintptr_t find_player_ped_target = 0x56E210" in adapter
    assert "constexpr uintptr_t find_player_wanted_target = 0x56E230" in adapter
    assert "constexpr uint32_t ped_model_offset = 0x22" in adapter
    assert "constexpr uint32_t ped_state_offset = 0x530" in adapter
    assert "constexpr uint32_t wanted_level_offset = 0x2C" in adapter
    assert "player_context_profile_matches()" in adapter
    assert "model_role_resolver_(profile.native_model)" in adapter
    assert "set_model_resolver(ModelRoleResolver resolver)" in adapter
    assert "speech_service_profile_matches()" in adapter
    assert "ped + 0x598" in adapter and "ped + 0x480" in adapter
    assert "return accepted;" in adapter
    assert "if (accepted == 0xFFFF || !ped_audio_provider) return accepted;" in adapter
    for name, context in (("pain_high",344),("pain_low",345),("burning",346),("panic",347),("sprayed",348)):
        assert f"{name} = {context}" in header
    for name, context in (("arrest",13),("arrest_criminal",14),("bump",28),
                          ("car_crash",29),("chase_foot",43),("chat",45),
                          ("dodge",74),("fight",89),("jacked_car",118),
                          ("rope",177),("shocked",181),("shoot",182),
                          ("shoot_generic",184),("van",231),("victim",232)):
        assert f"{name} = {context}" in header
    assert "constexpr int32_t ped_die = 54" in adapter
    assert "constexpr int32_t ped_dead = 55" in adapter
    assert "constexpr int32_t ped_arrested = 63" in adapter
    assert "lcs_ped_voice_requires_live_player_gate" in mapping
    assert "SFX_COP2_ARREST_1" in mapping and "SFX_SWAT_VOICE_2_ROPE_1" in mapping
    assert "SFX_FBI_VOICE_2_SHOOT_1" in mapping
    assert "SFX_AMBULAN_VOICE_2_VICTIM_1" in mapping
    assert "SFX_MARIA_SHOCKED_1" in mapping
    assert "SFX_DONALD_SHOCKED_1" in mapping
    assert "SFX_JD_SHOCKED_1" in mapping
    print("PASS: retail CPed::Say/AddSayEvent slot, volume and scheduling pins; gameExecuted=false")


if __name__ == "__main__":
    if len(sys.argv) != 2:
        raise SystemExit("Usage: ped_audio_profile.py <gta_sa.exe>")
    main(sys.argv[1])
