#include "lcs/cutscene_audio.hpp"
#include "lcs/continuous_sound.hpp"
#include <algorithm>
#include <array>
#include <cmath>
#include <fstream>
#include <cstring>
#include <stdexcept>

namespace lcs {
namespace {
constexpr size_t channel_block=0x2000,frame_size=16,samples_per_frame=28;
struct Predictor {
    double previous=0,older=0;
    std::array<int16_t,samples_per_frame> decode(const uint8_t* frame) {
        static constexpr double coefficients[5][2]={{0,0},{60.0/64,0},{115.0/64,-52.0/64},{98.0/64,-55.0/64},{122.0/64,-60.0/64}};
        const auto filter=frame[0]>>4,shift=frame[0]&15;
        if(filter>4 || frame[1]>7)throw std::runtime_error("Invalid PS2 cutscene ADPCM frame");
        std::array<int16_t,samples_per_frame> output{};
        // End/padding frames do not feed the predictor or expose uninitialised
        // output. Loop flags in other frames are decoded once for a cutscene.
        if(frame[1]==7)return output;
        for(size_t i=0;i<samples_per_frame;++i) {
            int nibble=(frame[2+i/2]>>(i%2*4))&15;
            if(nibble>=8)nibble-=16;
            const int residual=(nibble*4096)>>shift;
            const double value=residual+previous*coefficients[filter][0]+older*coefficients[filter][1];
            if(!std::isfinite(value))throw std::runtime_error("Nonfinite PS2 cutscene predictor");
            older=previous;previous=value;
            // The local reverse passes sample+0.5 to its quantizer, which adds
            // another 0.5 before truncation; retain its arithmetic here.
            const auto quantized=static_cast<int>(std::clamp(value+1.0,-32768.0,32767.0));
            output[i]=static_cast<int16_t>(quantized);
        }
        return output;
    }
};
// SPU2 ADPCM integer recurrence: rounded prediction, saturation before history.
// Unlike the legacy cutscene converter, a flag7 block still contains samples;
// stream ring-buffer flags control addresses, not predictor reset or silence.
// Reference: PCSX2 SPU2/Mixer.cpp XA_decode_block and DecodeSamples.
struct MusicPredictor {
    int32_t previous=0,older=0;
    std::array<int16_t,samples_per_frame> decode(const uint8_t* frame){
        constexpr int32_t factors[5][2]={{0,0},{60,0},{115,-52},{98,-55},{122,-60}};
        const auto filter=frame[0]>>4,shift=frame[0]&15;
        if(filter>4 || frame[1]>7)throw std::runtime_error("Invalid music ADPCM frame");
        std::array<int16_t,samples_per_frame> output{};
        for(size_t i=0;i<samples_per_frame;++i){
            int32_t nibble=(frame[2+i/2]>>(4*(i%2)))&15;if(nibble>=8)nibble-=16;
            const auto residual=(nibble*4096)>>shift;
            const auto predicted=(factors[filter][0]*previous+factors[filter][1]*older+32)>>6;
            const auto value=std::clamp(residual+predicted,-32768,32767);
            output[i]=int16_t(value);older=previous;previous=value;
        }
        return output;
    }
};
void u16(std::ostream& out,uint16_t value){out.put(char(value));out.put(char(value>>8));}
void u32(std::ostream& out,uint32_t value){u16(out,uint16_t(value));u16(out,uint16_t(value>>16));}
}
struct Ps2MusicStream::State {
    std::vector<uint8_t> bytes;size_t cursor;std::array<MusicPredictor,2> predictors;
};
Ps2MusicStream::Ps2MusicStream(std::vector<uint8_t> bytes,size_t encoded_offset){
    if(bytes.size()<0x8000 || bytes.size()>32*1024*1024 || bytes.size()%0x4000 ||
       encoded_offset%0x4000 || encoded_offset>bytes.size()-0x8000)
        throw std::runtime_error("Invalid retail CITY encoded stream/seek boundary");
    state_=std::make_unique<State>();state_->bytes=std::move(bytes);state_->cursor=encoded_offset;
}
Ps2MusicStream::~Ps2MusicStream()=default;
size_t Ps2MusicStream::retail_seek_offset(size_t size,uint32_t position_ms){
    if(size<0x8000 || size>32*1024*1024 || size%0x4000)
        throw std::runtime_error("Invalid retail CITY encoded size");
    // IOP CDSTREAM1FDC and149C..1764. Truncate time to whole seconds,
    // interpolate in eighths, then align encoded stereo blocks. Do not replace
    // this with exact PCM-millisecond slicing or floating-point interpolation.
    const uint32_t bytes=uint32_t(size),length_ms=(bytes*7u)>>8;
    if(position_ms>=length_ms || position_ms>INT32_MAX)return 0;
    const auto seconds=length_ms/1000u,p=position_ms/1000u;
    const auto half=seconds>>1,quarter=seconds>>2,eighth=seconds>>3;
    if(!p || !eighth)return 0;
    const auto a=bytes>>2,b=bytes>>3,c=bytes>>4;
    uint32_t start=0,base=0;
    if(p>half){
        if(p>half+quarter){
            if(p>half+quarter+eighth){start=half+quarter+eighth;base=a+b+c;}
            else{start=half+quarter;base=a+b;}
        }else if(p>half+eighth){start=half+eighth;base=a+c;}
        else{start=half;base=a;}
    }else if(p>quarter){
        if(p>quarter+eighth){start=quarter+eighth;base=b+c;}
        else{start=quarter;base=b;}
    }else if(p>eighth){start=eighth;base=c;}
    const auto offset=2u*(base+c*(p-start)/eighth);
    return offset>bytes-0x8000?0:offset&~uint32_t(0x3FFF);
}
std::shared_ptr<Ps2MusicStream> Ps2MusicStream::open_at_position(std::vector<uint8_t> bytes,uint32_t position_ms){
    const auto offset=retail_seek_offset(bytes.size(),position_ms);
    return std::make_shared<Ps2MusicStream>(std::move(bytes),offset);
}
std::shared_ptr<Ps2MusicStream> Ps2MusicStream::open_source_city_track(std::vector<uint8_t> bytes,uint8_t track,uint32_t stored_position,uint32_t service_age){
    if(track!=17 && track!=18)throw std::runtime_error("Track is outside verified retail CITY17/18 mapping");
    // Validate the encoded profile before deriving its actual track length.
    retail_seek_offset(bytes.size(),0);
    const auto position=continuous_music_start_position(track,stored_position,service_age,(uint32_t(bytes.size())*7u)>>8);
    if(!position)throw std::runtime_error("Source CITY track has no valid duration");
    return open_at_position(std::move(bytes),*position);
}
size_t Ps2MusicStream::encoded_position()const{return state_->cursor;}
AudioSample Ps2MusicStream::next_block(){
    auto& s=*state_;AudioSample sample;sample.frequency=32000;sample.channels=2;
    sample.pcm.reserve(channel_block/frame_size*samples_per_frame*4);
    for(size_t offset=0;offset<channel_block;offset+=frame_size){
        const auto left=s.predictors[0].decode(s.bytes.data()+s.cursor+offset);
        const auto right=s.predictors[1].decode(s.bytes.data()+s.cursor+channel_block+offset);
        for(size_t i=0;i<samples_per_frame;++i)for(const auto value:{left[i],right[i]}){
            sample.pcm.push_back(uint8_t(value));sample.pcm.push_back(uint8_t(uint16_t(value)>>8));
        }
    }
    s.cursor+=2*channel_block;if(s.cursor==s.bytes.size())s.cursor=0;
    return sample;
}
AudioSample decode_cutscene_vb(const std::vector<uint8_t>& bytes) {
    if(bytes.empty() || bytes.size()% (2*channel_block) || bytes.size()>32*1024*1024)
        throw std::runtime_error("Truncated, empty or oversized stereo PS2 cutscene stream");
    AudioSample sample;sample.frequency=32000;sample.channels=2;
    sample.pcm.reserve(bytes.size()/frame_size*samples_per_frame*2);
    std::array<Predictor,2> state;
    for(size_t block=0;block<bytes.size();block+=2*channel_block) {
        for(size_t offset=0;offset<channel_block;offset+=frame_size) {
            const auto left=state[0].decode(bytes.data()+block+offset);
            const auto right=state[1].decode(bytes.data()+block+channel_block+offset);
            for(size_t i=0;i<samples_per_frame;++i)for(const auto value:{left[i],right[i]}) {
                sample.pcm.push_back(uint8_t(value));sample.pcm.push_back(uint8_t(uint16_t(value)>>8));
            }
        }
    }
    return sample;
}
void write_pcm_wave(const std::filesystem::path& path,const AudioSample& sample) {
    if(!sample.frequency || sample.frequency>192000 || sample.channels<1 || sample.channels>2 ||
       sample.pcm.empty() || sample.pcm.size()%(sample.channels*2) || sample.pcm.size()>UINT32_MAX-36)
        throw std::runtime_error("Invalid PCM wave format or size");
    if(std::filesystem::exists(path))throw std::runtime_error("Refusing to overwrite a prepared audio stream");
    std::ofstream out(path,std::ios::binary);if(!out)throw std::runtime_error("Cannot create cutscene wave");
    out.write("RIFF",4);u32(out,uint32_t(sample.pcm.size()+36));out.write("WAVEfmt ",8);u32(out,16);
    u16(out,1);u16(out,sample.channels);u32(out,sample.frequency);u32(out,sample.frequency*sample.channels*2);
    u16(out,sample.channels*2);u16(out,16);out.write("data",4);u32(out,uint32_t(sample.pcm.size()));
    out.write(reinterpret_cast<const char*>(sample.pcm.data()),std::streamsize(sample.pcm.size()));
    if(!out)throw std::runtime_error("Cannot write complete cutscene wave");
}
AudioSample read_pcm_wave(const std::filesystem::path& path) {
    std::ifstream in(path,std::ios::binary|std::ios::ate);
    if(!in)throw std::runtime_error("Cannot read mounted cutscene audio");
    const auto length=in.tellg();if(length<44 || length>64*1024*1024)throw std::runtime_error("Invalid mounted wave size");
    in.seekg(0);std::array<uint8_t,44> h{};in.read(reinterpret_cast<char*>(h.data()),h.size());
    const auto word=[&](size_t at){return uint32_t(h[at])|uint32_t(h[at+1])<<8|uint32_t(h[at+2])<<16|uint32_t(h[at+3])<<24;};
    const auto half=[&](size_t at){return uint16_t(h[at])|uint16_t(h[at+1])<<8;};
    AudioSample sample;sample.frequency=word(24);sample.channels=uint16_t(half(22));
    if(!in || std::memcmp(h.data(),"RIFF",4) || std::memcmp(h.data()+8,"WAVEfmt ",8) || std::memcmp(h.data()+36,"data",4) ||
       word(16)!=16 || half(20)!=1 || half(34)!=16 || word(4)!=uint64_t(length)-8 || word(40)!=uint64_t(length)-44 ||
       !sample.frequency || sample.frequency>192000 || sample.channels<1 || sample.channels>2 || half(32)!=sample.channels*2 ||
       word(28)!=sample.frequency*sample.channels*2 || !word(40) || word(40)%(sample.channels*2))throw std::runtime_error("Invalid mounted canonical PCM wave");
    sample.pcm.resize(word(40));in.read(reinterpret_cast<char*>(sample.pcm.data()),std::streamsize(sample.pcm.size()));
    if(!in)throw std::runtime_error("Truncated mounted PCM audio");return sample;
}
}
