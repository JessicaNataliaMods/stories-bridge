#include "lcs/audio.hpp"
#include "lcs/phone_audio_policy.hpp"
#include "ScriptCommands.h"
#include "audio_sample_ids.inc"
#include <cstring>
#include <iostream>
#include <stdexcept>
using namespace lcs;
static void check(bool value,const char* message){if(!value)throw std::runtime_error(message);}
static int32_t bits(float v){int32_t result;std::memcpy(&result,&v,4);return result;}
int main(int argc,char** argv){try {
    const auto vertical_ring=original_phone_static_audio_start(22050,{0,0,40},{0,0,0},{1,0,0},true);
    check(vertical_ring && vertical_ring->distance==26 && vertical_ring->frequency==16538 && vertical_ring->pan==63,"Retail static ring uses vertical0.65 distance weight and initial slow-motion0.75 frequency");
    const auto absent_ring=original_phone_static_audio_start(22050,{80,0,0},{0,0,0},{1,0,0},false);
    check(absent_ring && absent_ring->volume==0,"Genuine authored80 range culls static ring before playback");
    const PhoneActiveAudioState active_audio{16,22050,80,63};
    const auto moving_audio=original_phone_active_audio_update(active_audio,22050,48,20,false,false);
    check(moving_audio && moving_audio->next.distance==48 && moving_audio->next.volume==70 && moving_audio->next.pan==53 && moving_audio->next.frequency<22050,"Active phone frame combines actual distance history, volume/pan slew and Doppler");
    check(active_audio.distance==16 && active_audio.volume==80,"A proposed audio frame never commits state before accepted playback");
    check(!original_phone_active_audio_update(active_audio,22050,80,0,false,false),"Out-of-range voice leaves active queue update unavailable for explicit lifecycle handling");
    const auto switched_audio=original_phone_active_audio_update(active_audio,22050,48,0,true,true);
    check(switched_audio && switched_audio->next.frequency==22050 && !switched_audio->write_frequency,"Camera switch bypasses Doppler and unchanged frequency preserves original no-write branch");
    check(original_phone_pan_update(107,63)==73 && original_phone_pan_update(20,63)==53 && original_phone_pan_update(68,63)==68,"Retail active phone pan slews by10 while retaining small deltas");
    check(original_phone_pan_update(63,63)==63 && !original_phone_pan_update(0,63),"Active pan retains centre and refuses unproven state");
    check(original_phone_relative_x({12,24,36},{10,20,30},{1,0,0})==2 && original_phone_relative_x({12,24,36},{10,20,30},{0,1,0})==4,"Original inverse camera transform subtracts position then projects onto camera right");
    check(original_phone_relative_x({12,24,36},{10,20,30},{0,0,-1})==-6,"Original camera-relative pan preserves full pitched basis and sign");
    const auto rising=original_phone_frequency_update(40000,22050,false),falling=original_phone_frequency_update(10000,22050,false);
    check(rising->active_frequency==28050 && rising->channel_frequency==28050 && falling->active_frequency==16050,"Retail active voice frequency slews by6000 in either direction");
    const auto slowed=original_phone_frequency_update(22050,20000,true);
    check(slowed->active_frequency==22050 && slowed->channel_frequency==16538,"Retail slow-motion channel scales0.75 with ties-even but retains unscaled active history");
    check(!original_phone_frequency_update(22050,22050,true)->write_channel && !original_phone_frequency_update(0xFFFFFFFFu,22050,false),"Unchanged active frequency does not write channel and unproven signed domain refuses");
    check(original_phone_ring_frequency(22050,10,10,false)==22050 && original_phone_ring_frequency(22050,10,40,true)==22050,"Original camera switch and stationary distance keep authored frequency");
    check(original_phone_ring_frequency(22050,10,40,false).value()<22050 && original_phone_ring_frequency(22050,40,10,false).value()>22050,"Original approaching/receding Doppler retains signed distance delta");
    check(original_phone_ring_frequency(22050,10,100,false)==original_phone_ring_frequency(22050,10,40,false),"Original subsonic velocity clamps at1.5");
    check(original_phone_ring_frequency(22050,10,1000,false)==22050 && !original_phone_ring_frequency(22050,-1,10,false),"Supersonic source keeps old frequency and invalid distance refuses");
    check(original_phone_ring_volume(0)==80 && original_phone_ring_volume(16)==80,"Phone ring near range preserves original volume80");
    check(original_phone_ring_volume(19)==73 && original_phone_ring_volume(48)==20,"Retail phone attenuation uses nearest rounding rather than reconstructed truncation");
    check(original_phone_ring_volume(80)==0 && original_phone_ring_volume(81)==0 && !original_phone_ring_volume(-1),"Original phone ring rejects range edge and unknown negative distance");
    check(original_phone_ring_pan(64,0.5f)==63 && original_phone_ring_pan(64,1.5f)==55 && original_phone_ring_pan(64,-1.5f)==71,"Retail pan indexes round nearest ties-even and retain original sign/table");
    check(original_phone_ring_pan(16,8)==20 && original_phone_ring_pan(16,-8)==107,"Phone pan uses current distance/64, with original20..107 clamps");
    check(original_phone_ring_pan(80,0)==63,"Coincident phone/listener still uses authored maximum80 and produces original centre pan");
    check(original_phone_ring_pan(80,20)==24 && original_phone_ring_pan(80,-20)==102,"Retail phone caller uses fixed maximum80 rather than current distance");
    check(!original_phone_ring_pan(0,0),"Unknown zero maximum distance refuses invalid denominator");
    check(argc==3,"Original MAIN and audio required");
    auto image=ScmImage::load(argv[1]);AudioArchive archive(argv[2]);AudioAdapter audio(archive);
    const auto phone=archive.phone_ring_sample(),raw_phone=archive.sample(236);
    check(phone.pcm==raw_phone.pcm && phone.frequency==raw_phone.frequency && !phone.pcm.empty(),"Retail phone ring uses actual sample236 PCM and authored base frequency rather than name/index guessing");
    std::array<float,3> camera{};audio.listener=[&](){return std::optional<std::array<float,3>>(camera);};
    bool playback_ok=true;size_t calls=0;AudioSample played;
    audio.play_effect=[&](const AudioSample& sample){++calls;played=sample;return playback_ok;};
    Vm original(image);Thread airport;airport.name="aportpk";airport.ip=206453;
    auto args=airport;args.ip+=2;
    for(float value:{1.f,2.f,3.f})original.store(args,bits(value));
    check(original.parameter(args)==63,"Reported airport sound is original ID 63");
    original.backend=[&](Vm& vm,Thread& t,uint16_t op){return audio.dispatch(vm,t,op);};
    check(original.step(airport,0)==StepResult::continued && airport.ip==args.ip && calls==1,"Exact unmodified airport command advances only after PCM playback");
    // Retail jump table 0x3B26C0: 63 is a fixed 12000Hz/127 SFX_DOCK_BB one-shot, inside range/5 at full volume.
    check(played.pcm==archive.sample(SFX_DOCK_BB).pcm && played.frequency==12000,"Airport ID 63 uses the retail sample and fixed frequency");
    auto command=[&](int32_t id){Vm vm(image);Thread thread;thread.ip=206453;auto arguments=thread;arguments.ip+=2;
        for(float value:{1.f,2.f,3.f})vm.store(arguments,bits(value));
        vm.memory[arguments.ip+1]=uint8_t(id);vm.backend=original.backend;return vm.step(thread,0);};
    playback_ok=false;check(command(63)==StepResult::unsupported,"PCM backend failure cannot silently succeed");playback_ok=true;
    camera={1000,1000,1000};const auto before=calls;check(command(63)==StepResult::continued && calls==before,"Original out-of-range effect culling performs no playback");
    camera={};for(int id:{0,3,5,12,18,35,52,53,54,55,56,57,58,59,60,61,62,63,64,65,66,67,68,69,72,73,74,76,77,78,79,80,81,82,83,84,85,87})
        check(command(id)==StepResult::continued,"Supported spatial sound family failed");
    check(command(68)==StepResult::continued && played.pcm.size()==archive.sample(SFX_GATE_START_CLU).pcm.size() && played.pcm!=archive.sample(SFX_GATE_START_CLU).pcm && played.frequency==20159,"Retail 68 is the gate start clunk, not an unknown ID");
    check(command(77)==StepResult::continued && played.pcm.size()==archive.sample(SFX_GLASS_SMASH).pcm.size(),"Retail glass break is 77, shifted past the LCS-only 52..71 entries");
    {Vm gate_vm(image);Thread gate;gate.ip=99173;gate_vm.backend=original.backend;camera={1250,-812,14};const auto before_gate=calls;
     check(gate_vm.step(gate,0)==StepResult::continued && gate.ip==99190 && calls==before_gate+1 && played.frequency==20159,"Reported MAIN gate thread IP 99173 (sound 68 before SLIDE_OBJECT) advances with the retail gate clunk");camera={};}
    for(int id:{19,20,75,86,88})check(command(id)==StepResult::unsupported,"Unknown or unmounted IDs remain explicit rather than forwarding unrelated SA audio");
    audio.listener={};camera={1000,1000,1000};
    for(int id:{1,7,8,9,10,13,14,16,70}){const auto before_frontend=calls;
        check(command(id)==StepResult::continued && calls==before_frontend+1,"Frontend effect ignores world distance and does not require a spatial listener");
        check(!played.pcm.empty() && played.channels==(id==16 || id==70?1:2),"Original authored frontend channels must reach the independent effect player");}
    const auto before_silence=calls;audio.play_effect={};check(command(71)==StepResult::continued && calls==before_silence,"Original SOUND_NO_SOUND must not fabricate PCM or require an output device");
    audio.play_effect=[&](const AudioSample& sample){++calls;played=sample;return playback_ok;};
    const auto purchase=archive.frontend_effect(13),denied=archive.frontend_effect(14);
    check(purchase.pcm!=denied.pcm && denied.pcm.size()>=purchase.pcm.size(),"Denied purchase preserves original lowered left/right pitch and centre layer");
    playback_ok=false;check(command(13)==StepResult::unsupported,"Purchase sound backend failure cannot be reported as played");
    std::cout<<"PASS: exact aportpk IP 206453, original PCM variants, spatial attenuation/range, dedicated effect callback, playback failure and 38 retail sound IDs; gameExecuted=false\n";return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
