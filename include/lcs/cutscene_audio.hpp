#pragma once
#include "audio.hpp"
#include <memory>

namespace lcs {
// PS2 cutscene streams: 32 kHz stereo, alternating 0x2000-byte channel
// blocks. Layout and predictor arithmetic follow reLCS oal/stream.cpp.
AudioSample decode_cutscene_vb(const std::vector<uint8_t>& bytes);
// Owned encoded CITY stream: decode one stereo0x4000-byte block at a time,
// retaining predictor history across refills and file wrap. Seek starts a new
// decoder at an already resolved retail encoded-block boundary.
class Ps2MusicStream {
    struct State;std::unique_ptr<State> state_;
public:
    Ps2MusicStream(std::vector<uint8_t> bytes,size_t encoded_offset);
    static size_t retail_seek_offset(size_t encoded_size,uint32_t position_ms);
    static std::shared_ptr<Ps2MusicStream> open_at_position(std::vector<uint8_t> bytes,uint32_t position_ms);
    static std::shared_ptr<Ps2MusicStream> open_source_city_track(std::vector<uint8_t> bytes,uint8_t track,uint32_t stored_position,uint32_t service_age);
    ~Ps2MusicStream();
    AudioSample next_block();
    size_t encoded_position()const;
};
// One mono SPU2 voice from key-on: zero predictor history, saturating
// integer recurrence (same arithmetic as the CITY stream decoder).
std::vector<uint8_t> decode_spu2_voice(const uint8_t* frames,size_t frame_count);
// Canonical RIFF PCM bytes exactly as write_pcm_wave stores them.
std::vector<uint8_t> pcm_wave_bytes(const AudioSample& sample);
void write_pcm_wave(const std::filesystem::path& path,const AudioSample& sample);
// Disk-streamed original 32 kHz stereo .VB (radio stations exceed the
// in-memory stream limit): one 0x4000-byte encoded block per refill, keeping
// predictor history across refills and looping at the end of the file.
class Ps2FileMusicStream {
    struct State;std::unique_ptr<State> state_;
public:
    Ps2FileMusicStream(const std::filesystem::path& path,uint64_t encoded_offset);
    ~Ps2FileMusicStream();
    AudioSample next_block();
    uint64_t encoded_size()const;
};
AudioSample read_pcm_wave(const std::filesystem::path& path);
// Reads and decodes an original 32 kHz stereo PS2 .VB stream (cutscene/news).
AudioSample read_ps2_stream(const std::filesystem::path& path);
}
