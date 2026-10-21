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
void write_pcm_wave(const std::filesystem::path& path,const AudioSample& sample);
AudioSample read_pcm_wave(const std::filesystem::path& path);
}
