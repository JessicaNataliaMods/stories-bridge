#include "lcs/audio.hpp"
#include "lcs/cutscene_audio.hpp"
#include "ScriptCommands.h"
#include <algorithm>
#include <fstream>
#include <stdexcept>
#include <cmath>
#include <cstring>
#include "audio_sample_ids.inc"

namespace lcs {
#include "original_audio_events.inc"
static_assert(SOUND_FRONTEND_ENTER_OR_ADJUST==0xC4 && SOUND_NO_SOUND==0xCD,
              "Original Script2 frontend event identities changed");
AudioMix mission_audio_mix(const std::array<float,3>& source,const SpatialListener& listener) {
    // Original AudioLogic: MISSION_AUDIO_MAX_DIST=80, volume=127. Preserve
    // AudioManager's quadratic attenuation and integer pan table.
    static constexpr uint8_t pan_table[]={0,3,8,12,16,19,22,24,26,28,30,31,33,34,36,37,
        39,40,41,42,44,45,46,47,48,49,49,50,51,52,53,53,54,55,55,56,56,57,57,58,58,58,
        59,59,59,60,60,61,61,61,61,62,62,62,62,62,63,63,63,63,63,63,63,63};
    float squared=0,right=0;
    for(size_t i=0;i<3;++i){
        if(!std::isfinite(source[i]) || !std::isfinite(listener.position[i]) || !std::isfinite(listener.right[i]))
            throw std::runtime_error("Nonfinite original mission audio transform");
        const auto delta=source[i]-listener.position[i];squared+=delta*delta;right+=delta*listener.right[i];
    }
    if(squared>=80.f*80.f)return {0,63};
    const auto distance=std::sqrt(squared);
    const auto volume=distance<16.f?127:std::min(127,int(127.f*std::pow((80.f-distance)/64.f,2.f)));
    const auto index=std::min(63,int(std::fabs(right)/(80.f/64.f)));
    const auto pan=right>0?std::max(20,63-int(pan_table[index])):std::min(107,63+int(pan_table[index]));
    return {uint8_t(volume),uint8_t(pan)};
}
static const struct NamedSample {const char* name;uint32_t index;} names[]={
#include "audio_names.inc"
};
static std::string upper(std::string name) {for(auto& c:name)if(c>='a' && c<='z')c-=32;return name;}
std::vector<std::string> AudioArchive::mission_names() {
    std::vector<std::string> result;for(const auto& entry:names)result.emplace_back(entry.name);return result;
}
std::vector<AudioRecord> AudioArchive::parse_index(const std::vector<uint8_t>& bytes,uint64_t raw_size) {
    if(bytes.empty() || bytes.size()%20)throw std::runtime_error("Invalid LCS audio index size");
    std::vector<AudioRecord> result;
    for(size_t i=0;i<bytes.size();i+=20) {
        AudioRecord record{read32(bytes,i),read32(bytes,i+4),read32(bytes,i+8)};
        record.loop_start=read32(bytes,i+12);
        const auto loop_end=read32(bytes,i+16);
        std::memcpy(&record.loop_end,&loop_end,sizeof(loop_end));
        if(record.offset>raw_size || record.size>raw_size-record.offset || record.size%2 ||
           (record.size && (!record.frequency || record.frequency>192000)))
            throw std::runtime_error("Invalid LCS PCM record "+std::to_string(i/20));
        result.push_back(record);
    }
    return result;
}
AudioArchive::AudioArchive(const std::filesystem::path& directory,const std::string& suffix):raw_(directory/("sfx.RAW"+suffix)),news_(directory/"news"),suffix_(suffix) {
    if(!suffix.empty() && suffix!=".bin")throw std::runtime_error("Invalid original source file suffix");
    std::ifstream index(directory/("sfx.sdt"+suffix),std::ios::binary|std::ios::ate);
    if(!index)throw std::runtime_error("LCS audio index is missing");
    const auto length=index.tellg();
    if(length!=std::streamoff(20u*TOTAL_AUDIO_SAMPLES))throw std::runtime_error("Audio index does not match the original LCS sample enum");
    std::vector<uint8_t> bytes(static_cast<size_t>(length));index.seekg(0);
    if(!index.read(reinterpret_cast<char*>(bytes.data()),length))throw std::runtime_error("Truncated audio index");
    records_=parse_index(bytes,std::filesystem::file_size(raw_));
}
AudioArchive::AudioArchive(Ps2Audio,const std::filesystem::path& directory):news_(directory/"NEWS"),layout_mutex_(std::make_shared<std::mutex>()),ps2_mode_(true) {
    // The original sample enum lists bank 0, then banks 1 and 3..66 (the
    // per-area sets), then the large bank 2.
    std::vector<int> order{0,1};for(int bank=3;bank<=66;++bank)order.push_back(bank);order.push_back(2);
    for(const auto bank:order) {
        const auto set=directory/("SET"+std::to_string(bank<10?0:bank/10));
        const auto stem="SFX"+std::to_string(bank)+"_PS";
        std::ifstream index(set/(stem+".SDT"),std::ios::binary|std::ios::ate);
        if(!index)throw std::runtime_error("Original PS2 sample bank index is missing: "+(set/(stem+".SDT")).string());
        const auto length=uint64_t(index.tellg());
        if(!length || length%12 || length>12ull*TOTAL_AUDIO_SAMPLES)throw std::runtime_error("Invalid original PS2 sample bank index: "+stem);
        std::vector<uint8_t> bytes(static_cast<size_t>(length));index.seekg(0);
        if(!index.read(reinterpret_cast<char*>(bytes.data()),std::streamsize(length)))throw std::runtime_error("Truncated original PS2 sample bank index");
        const auto raw=set/(stem+".RAW");
        std::error_code error;const auto raw_size=std::filesystem::file_size(raw,error);
        if(error)throw std::runtime_error("Original PS2 sample bank data is missing: "+raw.string());
        for(size_t at=0;at<bytes.size();at+=12) {
            const Ps2Record record{uint16_t(banks_.size()),read32(bytes,at),read32(bytes,at+4),read32(bytes,at+8)};
            if(record.offset>raw_size || record.size>raw_size-record.offset || record.size%16)
                throw std::runtime_error("Invalid original PS2 sample record in "+stem);
            ps2_.push_back(record);
        }
        banks_.push_back(raw);
    }
    if(ps2_.size()!=TOTAL_AUDIO_SAMPLES)throw std::runtime_error("PS2 AUDIO banks do not match the original LCS sample enum (expected SLUS-21423)");
    layouts_.resize(ps2_.size());
    for(int index=0;index<14;++index) {
        const auto news=news_/(std::string("NEWS_")+char('A'+index)+".VB");
        if(!std::filesystem::is_regular_file(news))throw std::runtime_error("Original PS2 news stream is missing: "+news.string());
    }
}
std::vector<uint8_t> AudioArchive::ps2_bytes(uint32_t index) const {
    if(index>=ps2_.size())throw std::runtime_error("Invalid original audio sample index");
    const auto& record=ps2_[index];
    if(record.size>16*1024*1024)throw std::runtime_error("Oversized original PS2 sample");
    std::vector<uint8_t> bytes(record.size);
    std::ifstream raw(banks_[record.bank],std::ios::binary);raw.seekg(record.offset);
    if(!raw.read(reinterpret_cast<char*>(bytes.data()),std::streamsize(bytes.size())))throw std::runtime_error("Truncated original PS2 sample");
    return bytes;
}
AudioArchive::Ps2Layout AudioArchive::ps2_layout(uint32_t index,const std::vector<uint8_t>& bytes) const {
    // VAG samples: 48-byte big-endian header, then one omitted lead frame.
    // Headerless samples begin at their loop-start frame. Playback ends after
    // the first end-flag frame (bit 0); a repeat flag (bit 1) on it loops from
    // the loop-start frame (bit 2), as the SPU2 voice does.
    const auto& source=ps2_[index];
    const bool vag=bytes.size()>=64 && !std::memcmp(bytes.data(),"VAGp",4);
    const size_t data=vag?48:0;
    if(bytes.size()<data+16 || (bytes.size()-data)%16)throw std::runtime_error("Invalid original PS2 sample frame size");
    const auto frames=uint32_t((bytes.size()-data)/16);
    auto flags=[&](uint32_t frame){return bytes[data+16*size_t(frame)+1];};
    if(vag) {
        const auto big=[&](size_t at){return uint32_t(bytes[at])<<24|uint32_t(bytes[at+1])<<16|uint32_t(bytes[at+2])<<8|bytes[at+3];};
        if(big(12)!=bytes.size()-48 || big(16)!=source.frequency)throw std::runtime_error("Original VAG header differs from its bank index");
    }
    if(!source.frequency || source.frequency>192000)
        throw std::runtime_error("Original PS2 sample "+std::to_string(index)+" has no source frequency (headerless engine bank)");
    uint32_t first=vag?1:frames;
    if(!vag)for(uint32_t frame=0;frame<frames;++frame)if(flags(frame)&4){first=frame;break;}
    uint32_t terminal=frames;
    for(uint32_t frame=first;frame<frames;++frame)if(flags(frame)&1){terminal=frame;break;}
    if(first>=frames || terminal>=frames)throw std::runtime_error("Original PS2 sample has no playable frame range");
    Ps2Layout layout{first,terminal-first+1,{}};
    layout.record.size=layout.frames*56;layout.record.frequency=source.frequency;
    if(flags(terminal)&2) {
        uint32_t start=terminal+1;
        for(uint32_t frame=first;frame<=terminal;++frame)if(flags(frame)&4){start=frame;break;}
        if(start>terminal)throw std::runtime_error("Original looping PS2 sample has no loop start");
        layout.record.loop_start=(start-first)*56;layout.record.loop_end=int32_t(layout.record.size)-2;
    }
    return layout;
}
AudioSample AudioArchive::sample(const std::string& name) const {
    const auto key=upper(name);
    const auto entry=std::find_if(std::begin(names),std::end(names),[&](const NamedSample& value){return upper(value.name)==key;});
    if(entry==std::end(names) || entry->index>=sample_count())throw std::runtime_error("Original mission audio label is missing: "+key);
    return sample(entry->index);
}
AudioSample AudioArchive::sample(uint32_t index) const {
    if(ps2_mode_) {
        const auto bytes=ps2_bytes(index);const auto layout=ps2_layout(index,bytes);
        const size_t data=bytes.size()>=64 && !std::memcmp(bytes.data(),"VAGp",4)?48:0;
        AudioSample result;result.frequency=layout.record.frequency;
        result.pcm=decode_spu2_voice(bytes.data()+data+16*size_t(layout.first),layout.frames);
        return result;
    }
    const auto& record=this->record(index);
    if(!record.size || record.size>16*1024*1024)throw std::runtime_error("Empty or oversized mission audio sample");
    AudioSample result;result.frequency=record.frequency;result.pcm.resize(record.size);
    std::ifstream raw(raw_,std::ios::binary);raw.seekg(record.offset);
    if(!raw.read(reinterpret_cast<char*>(result.pcm.data()),record.size))throw std::runtime_error("Truncated mission audio PCM");
    return result;
}
const AudioRecord& AudioArchive::record(uint32_t index) const {
    if(ps2_mode_) {
        if(index>=ps2_.size())throw std::runtime_error("Invalid original audio sample index");
        std::lock_guard lock(*layout_mutex_);
        if(!layouts_[index])layouts_[index]=ps2_layout(index,ps2_bytes(index)).record;
        return *layouts_[index];
    }
    if(index>=records_.size())throw std::runtime_error("Invalid original audio sample index");
    return records_[index];
}
AudioSample AudioArchive::announcement(int32_t index) const {
    if(index<0 || index>=14)throw std::runtime_error("Invalid original radio announcement");
    const auto sample=ps2_mode_ ? read_ps2_stream(news_/(std::string("NEWS_")+char('A'+index)+".VB"))
                                : read_pcm_wave(news_/(std::string("NEWS_")+char('A'+index)+".wav"+suffix_));
    if(sample.frequency!=32000 || sample.channels!=2)throw std::runtime_error("Original news stream format differs");
    return sample;
}
bool AudioAdapter::tick_announcements(bool eligible) {
    if(!play || !finished || !stop)return false;
    if(announcement_started_) {
        if(!eligible){stop(2);announcement_started_=false;pending_announcement_.reset();return true;}
        if(finished(2)){stop(2);announcement_started_=false;pending_announcement_.reset();}
    }
    if(eligible && pending_announcement_ && !announcement_started_) {
        if(!play(2,archive_.announcement(*pending_announcement_)))return false;
        announcement_started_=true;
    }
    return true;
}
AudioSample AudioArchive::phone_ring_sample() const {
    return sample(236); // independently verified retail dispatcher1B2768
}
AudioSample AudioArchive::frontend_effect(int32_t id) const {
    if(id==16)return sample(SFX_ARM_BOMB);
    if(id==70)return sample(SFX_FE_SELECT);
    uint32_t left=0,right=0,centre=0;
    if(id==13 || id==14){left=SFX_WEAPON_LEFT;right=SFX_WEAPON_RIGHT;centre=SFX_WEAPON_CENTRE;}
    else if(id==1 || (id>=7 && id<=9)){left=SFX_PART_MISSION_COMPLETE_LEFT;right=SFX_PART_MISSION_COMPLETE_RIGHT;centre=SFX_PART_MISSION_COMPLETE_CENTRE;}
    else if(id==10){left=SFX_GO_LEFT;right=SFX_GO_RIGHT;centre=SFX_GO_CENTRE;}
    else throw std::runtime_error("Unsupported original frontend sound");
    auto a=sample(left),b=sample(right),c=sample(centre);
    // Script2 uses nonspatial frontend sounds. AudioLogic's old sequential
    // centre indexing differs from LCS's named PS2 enum; use the authored
    // LEFT/RIGHT/CENTRE names, never the adjacent 'WEST' speech sample.
    if(id==14)a.frequency=b.frequency=5382;
    AudioSample result;result.frequency=std::max({a.frequency,b.frequency,c.frequency});result.channels=2;
    const auto frames=[&](const AudioSample& x){return (uint64_t(x.pcm.size()/2)*result.frequency+x.frequency-1)/x.frequency;};
    const auto count=std::max({frames(a),frames(b),frames(c)});
    if(!count || count>4*1024*1024)throw std::runtime_error("Original frontend PCM mix exceeds effect capacity");
    result.pcm.resize(size_t(count)*4);
    const auto value=[&](const AudioSample& x,uint64_t i){
        const auto position=double(i)*x.frequency/result.frequency;const auto at=size_t(position);
        if(at>=x.pcm.size()/2)return 0.0;
        int16_t first=0,next=0;std::memcpy(&first,x.pcm.data()+at*2,2);
        if(at+1<x.pcm.size()/2)std::memcpy(&next,x.pcm.data()+(at+1)*2,2);
        return double(first)+(next-first)*(position-at);
    };
    for(uint64_t i=0;i<count;++i){const auto middle=value(c,i)*0.7071067811865476;
        for(size_t channel=0;channel<2;++channel){const auto mixed=value(channel?b:a,i)+middle;
            const auto pcm=int16_t(std::clamp(mixed,-32768.0,32767.0));std::memcpy(result.pcm.data()+size_t(i)*4+channel*2,&pcm,2);}}
    return result;
}
bool AudioAdapter::tick_spatial() {
    std::optional<SpatialListener> transform;
    for(size_t i=0;i<slots_.size();++i){
        const auto& slot=slots_[i];if(!slot.started || !slot.position)continue;
        if(!set_mix || !spatial_listener)return false;
        if(!transform){transform=spatial_listener();if(!transform)return false;}
        if(!set_mix(i,mission_audio_mix(*slot.position,*transform)))return false;
    }
    return true;
}
ContinuousMixContext AudioAdapter::service_source_mix(float native_scale) {
    // Source ProcessMissionAudio runs once in the audio service, not once per
    // VM thread or sound object. Keep phone ducking and its recovery shared.
    bool phone=false;for(const auto& slot:slots_)phone|=slot.phone_call;
    if(phone)global_sfx_volume_=64;
    else if(global_sfx_volume_<127)global_sfx_volume_=uint8_t(std::min(127,int(global_sfx_volume_)+5));
    return {false,global_sfx_volume_,native_scale}; // source constructor: DoubleVolume=false
}
bool AudioAdapter::play_car_jerk(const std::array<float,3>& position) {
    if(!play_positioned_effect || !spatial_listener)return false;
    for(const auto value:position)if(!std::isfinite(value))return false;
    const auto listener_state=spatial_listener();
    if(!listener_state)return false;
    float squared=0;
    for(size_t axis=0;axis<3;++axis){
        if(!std::isfinite(listener_state->position[axis]) || !std::isfinite(listener_state->right[axis]))return false;
        const float delta=position[axis]-listener_state->position[axis];squared+=delta*delta;
    }
    // AudioLogic SOUND_CAR_JERK: bank-0 SFX_SHAG_SUSPENSION, priority 5, max distance 35,
    // frequency += RandomDisplacement(frequency>>3), volume = table[1]%15 + 55.
    constexpr float range=35.0f;
    audio_random_.regenerate(); // Event-owned variation; the SA/project transport does not recreate the LCS audio-frame scheduler.
    auto effect=archive_.sample(uint32_t(SFX_SHAG_SUSPENSION));
    effect.frequency=uint32_t(int32_t(effect.frequency)+audio_random_.displacement(effect.frequency>>3));
    const unsigned emitting=audio_random_.table(1)%15+55;
    if(!(squared<range*range))return true; // original rejects out-of-range one-shots without a voice
    // ComputeVolume: full volume inside range/5, then a squared linear fall-off (uint8, capped at 127).
    const float distance=std::sqrt(squared),minimum=range/5.0f;
    unsigned volume=emitting;
    if(!(minimum>distance))volume=std::min<unsigned>(127,uint8_t(float(emitting)*std::pow((range-distance)/(range-minimum),2.0f)));
    if(volume==0)return true;
    for(size_t i=0;i+1<effect.pcm.size();i+=2){int16_t value;std::memcpy(&value,effect.pcm.data()+i,2);value=int16_t(int(value)*int(volume)/127);std::memcpy(effect.pcm.data()+i,&value,2);}
    const auto positioned_mix=mission_audio_mix(position,*listener_state);
    return play_positioned_effect(effect,{127,positioned_mix.pan});
}
bool AudioAdapter::handles(uint16_t opcode) {
    switch(opcode) {
    case COMMAND_ADD_ONE_OFF_SOUND:case COMMAND_PLAY_ANNOUNCEMENT:
    case COMMAND_LOAD_MISSION_AUDIO:case COMMAND_HAS_MISSION_AUDIO_LOADED:
    case COMMAND_PLAY_MISSION_AUDIO:case COMMAND_HAS_MISSION_AUDIO_FINISHED:
    case COMMAND_CLEAR_MISSION_AUDIO:case COMMAND_SET_MISSION_AUDIO_POSITION:return true;
    default:return false;
    }
}
StepResult AudioAdapter::dispatch(Vm& vm,Thread& t,uint16_t opcode) {
    if(opcode==COMMAND_SET_MISSION_AUDIO_POSITION) {
        const auto index=vm.parameter(t);std::array<float,3> position{};
        for(auto& value:position){const auto raw=vm.parameter(t);std::memcpy(&value,&raw,4);}
        if(index<1 || index>2)throw std::runtime_error("Invalid original positioned speech slot");
        for(auto value:position)if(!std::isfinite(value))throw std::runtime_error("Invalid original mission audio position");
        if(!set_mix || !spatial_listener){t.diagnostic="Original spatial speech mixer backend unavailable";return StepResult::unsupported;}
        slots_[size_t(index-1)].position=position;return StepResult::continued;
    }
    if(opcode==COMMAND_ADD_ONE_OFF_SOUND) {
        std::array<float,3> position{};
        for(auto& value:position){const auto bits=vm.parameter(t);std::memcpy(&value,&bits,4);}
        const auto id=vm.parameter(t);
        // Original Script2 explicitly sends 0xCD (SOUND_NO_SOUND) for 71.
        // This is an original silent event, not a missing sound workaround.
        if(id==71)return StepResult::continued;
        if(!play_effect){t.diagnostic="Original one-off sound playback backend unavailable";return StepResult::unsupported;}
        for(auto value:position)if(!std::isfinite(value))throw std::runtime_error("Invalid original sound position");
        if(id==1 || (id>=7 && id<=10) || id==13 || id==14 || id==16 || id==70) {
            if(!play_effect(archive_.frontend_effect(id))){t.diagnostic="Original frontend PCM playback failed";return StepResult::unsupported;}
            return StepResult::continued;
        }
        if(!listener){t.diagnostic="Original one-off spatial listener unavailable";return StepResult::unsupported;}
        audio_random_.regenerate(); // Event-owned variation, as in play_car_jerk.
        uint32_t index=0;float range=0;int volume=0;std::optional<uint32_t> frequency;
        // Fixed frequency, or the sample's base frequency moved by
        // RandomDisplacement(base>>shift) when shift is set.
        unsigned shift=0;int32_t displacement=0;
        // Retail LCS PS2 cAudioManager::ProcessOneShotScriptObject
        // (SLUS_214.23 0x1B1DF0, 88-entry jump table 0x3B26C0). The script IDs
        // are the retail numbering: the re-LCS eScriptSounds order is the VC one
        // and lacks the 20 LCS entries 52..71, so bullet/glass/box/collision
        // sounds start at 72. Sample indices are the retail bank-0 operands.
        // Script sound IDs are not SA IDs; read original samples, never 018C.
        const auto fixed=[&](uint32_t sample,float r,int v,uint32_t f){index=sample;range=r;volume=v;frequency=f;};
        switch(id) {
        case 0:fixed(SFX_ALARM,80,90,14000);break;
        case 3:fixed(SFX_COL_GATE,40,60,10600);displacement=audio_random_.displacement(10600>>5);break;
        case 5:fixed(SFX_COL_CAR_PANEL_2,80,60,22000);displacement=audio_random_.displacement(22000>>5);break;
        case 18:fixed(SFX_BOMB_BEEP,40,70,20159);break;
        case 35:index=SFX_SEAPLANE_LOW;range=1000;volume=100;break;
        // 52..63: retail fixed 12000Hz, volume127, 40m one-shots.
        case 52:case 53:case 54:fixed(SFX_AMMU_AA+uint32_t(id-52),40,127,12000);break;
        case 55:case 56:case 57:case 58:case 59:fixed(SFX_C_OR_D_UNK_1+uint32_t(id-55),40,127,12000);break;
        case 60:fixed(SFX_C_OR_D_UNK_3,40,127,12000);break;
        case 61:fixed(SFX_C_OR_D_UNK_6,40,127,12000);break;
        case 62:case 63:fixed(SFX_DOCK_BA+uint32_t(id-62),40,127,12000);break;
        case 64:case 65:fixed(SFX_COLT45_LEFT,40,70,20159);break;
        case 66:fixed(SFX_PIZZA_THROW,40,70,20159);break;
        case 67:fixed(SFX_R_OR_S_UNK1,40,70,20159);break;
        case 68:fixed(SFX_GATE_START_CLU,70,100,20159);break;
        case 69:fixed(SFX_GATE_STOP_CLU,40,50,20159);break;
        case 12:case 72:case 73:case 74:
            index=SFX_BULLET_WALL_1+audio_random_.table(one_shot_counter_%5)%3;range=35;shift=5;
            volume=int(audio_random_.table(2)%20)+90;break;
        case 76:index=SFX_PAY_PHONE;range=80;volume=80;break;
        case 77:case 78:index=SFX_GLASS_SMASH;range=60;volume=id==77?70:60;break;
        case 79:index=SFX_GLASS_CRACK;range=60;volume=70;break;
        case 80:
            index=SFX_GLASS_SHARD_1+(audio_random_.table(4)&3);range=55;
            frequency=uint32_t(19000+audio_random_.displacement(2000));volume=uint8_t(audio_random_.displacement(11)+25);break;
        case 81:case 82:
            index=id==81?SFX_WOODEN_BOX_SMASH:SFX_CARDBOARD_BOX_SMASH;range=60;
            frequency=uint32_t(18600+audio_random_.displacement(1500));volume=int(audio_random_.table(2)%20)+80;break;
        case 83:index=SFX_COL_CAR_1+audio_random_.table(3)%5;range=60;shift=4;volume=int(audio_random_.table(2)%30)+70;break;
        case 84:case 85:index=id==84?SFX_TYRE_BUMP:SFX_HIT_BALL;range=60;shift=4;volume=int(audio_random_.table(2)%30)+60;break;
        case 87:
            index=SFX_BULLET_SHELL_HIT_GROUND_2;range=10;
            frequency=uint32_t(10600+audio_random_.displacement(600));volume=int(audio_random_.table(2)%7)+10;break;
        // 19/20 queue ped comments 200/201 and 86 selects its sample from the
        // player's contact surface; neither backend is mounted here.
        default:t.diagnostic="Original one-off sound requires implementation: "+std::to_string(id);return StepResult::unsupported;
        }
        const auto camera=listener();
        if(!camera){t.diagnostic="Original spatial sound listener unavailable";return StepResult::unsupported;}
        float distance=0;for(size_t i=0;i<3;++i){if(!std::isfinite((*camera)[i]))throw std::runtime_error("Invalid native sound listener");distance+=std::pow(position[i]-(*camera)[i],2.f);}distance=std::sqrt(distance);
        // Original engine rejects out-of-range effects; this is not a fake
        // playback success. Keep the exact quadratic attenuation and clamp.
        if(distance>=range)return StepResult::continued;
        if(distance>=range/5)volume=int(volume*std::pow((range-distance)/(range-range/5),2.f));
        volume=std::min(127,volume);if(volume<=0)return StepResult::continued;
        ++one_shot_counter_; // retail counter is advanced for every queued one-shot
        auto effect=archive_.sample(index);
        if(frequency)effect.frequency=*frequency;
        if(shift)effect.frequency=uint32_t(int64_t(effect.frequency)+audio_random_.displacement(effect.frequency>>shift));
        effect.frequency=uint32_t(int64_t(effect.frequency)+displacement);
        for(size_t i=0;i<effect.pcm.size();i+=2){int16_t value;std::memcpy(&value,effect.pcm.data()+i,2);value=int16_t(int(value)*volume/127);std::memcpy(effect.pcm.data()+i,&value,2);}
        if(!play_effect(effect)){t.diagnostic="Original spatial one-off PCM playback failed";return StepResult::unsupported;}
        return StepResult::continued;
    }
    if(opcode==COMMAND_PLAY_ANNOUNCEMENT) {
        const auto index=vm.parameter(t);
        // Original MusicManager queues NEWS_A + index only when no news
        // broadcast is already in progress. Keep speech slots independent.
        if(index<0 || index>=14 || !play || !stop || !finished){t.diagnostic="Original news playback backend unavailable or invalid news ID";return StepResult::unsupported;}
        if(!announcement_started_){archive_.announcement(index);pending_announcement_=index;}
        return StepResult::continued;
    }
    if(opcode!=COMMAND_LOAD_MISSION_AUDIO && opcode!=COMMAND_HAS_MISSION_AUDIO_LOADED &&
       opcode!=COMMAND_PLAY_MISSION_AUDIO && opcode!=COMMAND_HAS_MISSION_AUDIO_FINISHED && opcode!=COMMAND_CLEAR_MISSION_AUDIO)
        return StepResult::unsupported;
    if(!play || !stop || !finished){t.diagnostic="Original mission speech playback backend is missing";return StepResult::unsupported;}
    const auto index=vm.parameter(t);
    if(index<1 || index>2)throw std::runtime_error("Invalid LCS mission audio slot (expected 1 or 2)");
    const auto n=size_t(index-1);auto& slot=slots_[n];
    switch(opcode) {
    case COMMAND_LOAD_MISSION_AUDIO: {
        auto sample=archive_.sample(vm.text_label(t));
        stop(n);slot.sample=std::move(sample);slot.started=false;slot.position.reset();slot.phone_call=false;break;
    }
    case COMMAND_HAS_MISSION_AUDIO_LOADED:vm.compare(t,slot.sample.has_value());break;
    case COMMAND_PLAY_MISSION_AUDIO:
        if(slot.sample && !slot.started) {
            if(!play(n,*slot.sample)){t.diagnostic="Original mission speech playback failed";return StepResult::unsupported;}
            slot.started=true;
        }
        break;
    case COMMAND_HAS_MISSION_AUDIO_FINISHED:vm.compare(t,slot.started && finished(n));break;
    case COMMAND_CLEAR_MISSION_AUDIO:stop(n);slot=Slot{};break;
    }
    return StepResult::continued;
}
}
