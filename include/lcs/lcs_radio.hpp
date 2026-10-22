#pragma once
#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>

namespace lcs {
// Original LCS radio: ten stations streamed from AUDIO/MUSIC/<NAME>.VB
// (32 kHz stereo SPU2 ADPCM, 0x2000-byte channel blocks). Order follows the
// retail streamed-sound table (reLCS audio_enums.h STREAMED_SOUND_RADIO_*).
constexpr size_t lcs_radio_station_count=10;
constexpr int32_t lcs_radio_off=10;
inline constexpr std::array<const char*,lcs_radio_station_count> lcs_radio_files{
    "HEAD","DOUBLE","KJAH","RISE","LIPS","MUNDO","MSX","FLASH","LCJ","LCFR"};

// SA keeps its own station selection, retune and on/off logic (eRadioID raw
// values: 0 emergency scanner, 1..11 stations, 12 user tracks, 13 off). Each SA
// station slot plays the LCS station of the closest genre; SA's eleven slots
// map onto ten LCS stations, so Radio Los Santos shares The Liberty Jam with
// Playback FM. User tracks, the scanner and off stay native.
inline std::optional<uint32_t> lcs_station_for_sa(int32_t raw) {
    // raw:              1  2  3  4  5  6  7  8  9 10 11
    static constexpr int8_t table[]={-1,8, 1, 7, 6, 3, 8, 0, 4, 2, 5, 9};
    if(raw<1 || raw>11)return {};
    return uint32_t(table[raw]);
}
// Inverse used by SET_RADIO_CHANNEL: one SA slot per LCS station.
inline int32_t sa_station_for_lcs(int32_t station) {
    static constexpr int8_t table[]={7,2,9,5,8,10,4,3,1,11};
    return station>=0 && station<int32_t(lcs_radio_station_count) ? table[station] : 13;
}
// GET_RADIO_CHANNEL while the LCS radio plays: the LCS station of the active
// SA slot; scanner -1; user tracks and off report LCS RADIO_OFF.
inline int32_t lcs_radio_channel_for_sa(int32_t raw) {
    if(raw==0)return -1;
    if(const auto station=lcs_station_for_sa(raw))return int32_t(*station);
    return lcs_radio_off;
}

// Retail CDSTREAM duration of an encoded stream: (bytes * 7) >> 8 ms.
inline uint32_t lcs_radio_length_ms(uint64_t encoded_bytes){return uint32_t((encoded_bytes*7u)>>8);}
// Encoded stereo block (0x4000 bytes) holding a playback position.
inline uint64_t lcs_radio_block_offset(uint64_t encoded_bytes,uint32_t position_ms) {
    if(encoded_bytes<0x4000)return 0;
    const auto offset=((uint64_t(position_ms)<<8)/7u)&~uint64_t(0x3FFF);
    return offset>=encoded_bytes ? 0 : offset;
}

// Per-station position, cMusicManager::GetTrackStartPos: a station keeps
// "playing" while unheard, advancing by elapsed time capped at 270 s per
// check, wrapped to the track length.
struct LcsRadioTrack {uint32_t length_ms=0,position_ms=0,last_check_ms=0;};
inline uint32_t lcs_radio_start_position(LcsRadioTrack& track,uint32_t now_ms) {
    if(!track.length_ms)return 0;
    uint64_t position=track.position_ms;
    if(now_ms>track.last_check_ms)position+=std::min<uint32_t>(now_ms-track.last_check_ms,270000u);
    else track.last_check_ms=now_ms;
    if(position>track.length_ms)position%=track.length_ms;
    return uint32_t(position);
}
// New-game positions from the reLCS reference (NewGameRadioTimers), wrapped
// to each actual stream length.
inline constexpr std::array<uint32_t,lcs_radio_station_count> lcs_radio_new_game_positions{
    948160,452150,2438150,3538230,3513100,4246050,1418050,3178240,471210,0};
}
