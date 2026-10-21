#include "lcs/continuous_sound.hpp"
#include <iostream>
#include <stdexcept>

int main() {
    try {
        lcs::ContinuousSourceRandom shared(180371);
        const auto table=shared.audio_table();
        if(table!=std::array<uint32_t,5>{884011750,1293612304,617551100,392871749,1630754565})
            throw std::runtime_error("Original shared RNG/table golden sequence changed");
        std::array<uint32_t,10> radio_lengths;radio_lengths.fill(600320);
        const auto captured=lcs::capture_continuous_music_initialization(shared,table,radio_lengths);
        const auto produced=lcs::initialize_continuous_music(*captured);
        if(shared.state()!=222008615 || captured->random_draws[0]!=1905670941 ||
           (*produced)[17].position!=56 || (*produced)[18].position!=68)
            throw std::runtime_error("Source random producer does not reach actual initial track consumer in shared call order");
        const auto saved=shared.state();radio_lengths[0]=0;
        if(lcs::capture_continuous_music_initialization(shared,table,radio_lengths) || shared.state()!=saved)
            throw std::runtime_error("Missing resource length consumed shared random draws before preflight");
        lcs::ContinuousMusicInitialization initial{};
        initial.random_draws.fill(1);initial.audio_random_table={3,5,0xF1234567u,0xFEDCBA98u,9};
        initial.radio_lengths.fill(600320);
        initial.random_draws[18]=0x12345678;initial.random_draws[19]=0x7FFFFFFF;initial.random_draws[25]=0x40000000;
        const auto initialized=lcs::initialize_continuous_music(initial);
        if(!initialized || (*initialized)[0].length!=600320 || (*initialized)[17].length!=100 ||
           (*initialized)[17].position!=4 || (*initialized)[18].position!=84 || (*initialized)[24].position!=72)
            throw std::runtime_error("Retail initialization draw indexing, high64 scale or wrapping low32 product changed");
        for(size_t i=0;i<66;++i)if((*initialized)[i].service_age || (i>=25 && (*initialized)[i].position))
            throw std::runtime_error("Retail initialization must clear all ages and positions25..65");
        initial.random_draws[0]=0x7FFFFFFF;
        if(lcs::initialize_continuous_music(initial)->at(17).position!=4)
            throw std::runtime_error("Discarded warmup random draw incorrectly affects a track");
        initial.random_draws[18]=0x80000000;
        if(lcs::initialize_continuous_music(initial))throw std::runtime_error("Invalid source rand domain accepted");
        initial.random_draws[18]=1;initial.radio_lengths[3]=0;
        if(lcs::initialize_continuous_music(initial))throw std::runtime_error("Missing actual radio length accepted");
        if(lcs::continuous_music_start_position(0,1000,3,600320)!=1599 ||
           lcs::continuous_music_start_position(17,672000,100,600320)!=0 ||
           lcs::continuous_music_start_position(18,509120,100,600320)!=0 ||
           lcs::continuous_music_start_position(18,0,0,0))
            throw std::runtime_error("Retail service-age formula/non-radio music start gate changed");
        const auto door_loop=lcs::continuous_pcm_loop_span(17024,9240,17022);
        const auto target_loop=lcs::continuous_pcm_loop_span(11312,5768,11310);
        const auto alarm_loop=lcs::continuous_pcm_loop_span(5264,0,-1);
        if(!door_loop || door_loop->begin!=9240 || door_loop->end!=17024 ||
           !target_loop || target_loop->end!=11312 || !alarm_loop || alarm_loop->end!=5264 ||
           lcs::continuous_pcm_loop_span(17024,9241,17022) ||
           lcs::continuous_pcm_loop_span(17024,9240,17024) ||
           lcs::continuous_pcm_loop_span(17024,9240,-1) ||
           lcs::continuous_pcm_loop_span(17024,9240,9241))
            throw std::runtime_error("Continuous PCM16 inclusive/end-sentinel translation changed");
        // Emit every byte selector for independent comparison with retail ELF.
        for(int id=0;id<256;++id) {
            const auto spec=lcs::continuous_sound_emission(id);
            if(!spec)continue;
            std::cout<<id<<' '<<spec->sample<<' '<<spec->frequency_sample<<' '
                <<spec->fixed_frequency<<' '<<spec->distance<<' '<<int(spec->volume)
                <<' '<<int(spec->priority)<<' '<<int(spec->frames)<<' '<<spec->speed<<'\n';
        }
        const auto wrapped=lcs::continuous_sound_emission(0x10002);
        const auto negative=lcs::continuous_sound_emission(-256);
        if(!wrapped || wrapped->sample!=183 || !negative || negative->sample!=4)
            throw std::runtime_error("Retail stored-16/consumed-8 sound ID narrowing changed");
        // No claim that no-emission selectors are a completed silent command.
        if(lcs::continuous_sound_emission(6) || lcs::continuous_sound_emission(36))
            throw std::runtime_error("Configuration-only retail branches became voice requests");
        if(lcs::continuous_sound_volume(90,80,0)!=90 || lcs::continuous_sound_volume(90,80,16)!=90 ||
           lcs::continuous_sound_volume(127,80,40)!=49 || lcs::continuous_sound_volume(90,80,60)!=8 ||
           lcs::continuous_sound_volume(90,80,80)!=0 || lcs::continuous_sound_volume(90,80,100)!=0 ||
           lcs::continuous_sound_volume(90,0,0)!=0 || lcs::continuous_sound_volume(255,80,1)!=255 ||
           lcs::continuous_sound_volume(90,80,-1))throw std::runtime_error("Retail attenuation gate, plateau, float order, truncation or byte boundary changed");
        const lcs::ContinuousSoundMotion approaching{30,0,343.f/30.f,30,false},retreating{0,30,343.f/30.f,30,false};
        if(lcs::continuous_sound_frequency(22050,2,approaching)!=25379 ||
           lcs::continuous_sound_frequency(22050,2,retreating)!=19492 ||
           lcs::continuous_sound_frequency(22050,2,{60,0,343.f/30.f,30,false})!=25379 ||
           lcs::continuous_sound_frequency(22050,2,{0,1000,343.f/30.f,30,false})!=22050 ||
           lcs::continuous_sound_frequency(22050,2,{30,0,343.f/30.f,0,true})!=22050 ||
           lcs::continuous_sound_frequency(22050,0,{30,0,343.f/30.f,0,false})!=22050 ||
           lcs::continuous_sound_frequency(22050,2,{30,0,343.f/30.f,0,false}))
            throw std::runtime_error("Retail Doppler sign/clamp, supersonic/camera bypass or observed-time gate changed");
        if(lcs::continuous_sound_pan(64,0)!=63 || lcs::continuous_sound_pan(64,1)!=60 ||
           lcs::continuous_sound_pan(64,-1)!=66 || lcs::continuous_sound_pan(64,64)!=20 ||
           lcs::continuous_sound_pan(64,-64)!=107 || lcs::continuous_sound_pan(0,0)!=107 ||
           lcs::continuous_sound_pan(-1,0))throw std::runtime_error("Retail camera-space pan table, clamps or degenerate case changed");
        const auto rise=lcs::continuous_frequency_step(12000,30000,1.f);
        const auto fall=lcs::continuous_frequency_step(30000,12000,0.5f);
        const auto unchanged=lcs::continuous_frequency_step(12000,12000,0.5f);
        const auto edge=lcs::continuous_frequency_step(12000,18000,0.999f);
        const auto capped=lcs::continuous_frequency_step(50000,60000,1.f);
        const auto scaled_cap=lcs::continuous_frequency_step(50000,60000,0.5f);
        const auto gain=lcs::continuous_volume_step(20,90,{true,63});
        const auto fade=lcs::continuous_volume_step(90,8,{false,127});
        const auto first_gain=lcs::continuous_volume_step(20,20,{true,63},true);
        const auto native_gain=lcs::continuous_volume_step(90,90,{false,127,0.5f});
        if(!gain || gain->cached_volume!=30 || gain->output_volume!=29 ||
           !fade || fade->cached_volume!=80 || fade->output_volume!=80 ||
           !first_gain || first_gain->cached_volume!=20 || first_gain->output_volume!=19 ||
           !native_gain || native_gain->cached_volume!=90 || native_gain->output_volume!=45 ||
           lcs::continuous_volume_step(20,30,{false,127,std::numeric_limits<float>::quiet_NaN()}) ||
           lcs::continuous_volume_step(20,30,{false,128}))
            throw std::runtime_error("Retail volume slew, unscaled gain cache or multiplier range changed");
        if(!rise || rise->cached_frequency!=18000 || rise->output_frequency!=18000 || !rise->write ||
           !fall || fall->cached_frequency!=24000 || fall->output_frequency!=18000 || !fall->write ||
           !unchanged || unchanged->write || !edge || edge->cached_frequency!=18000 || edge->output_frequency!=13500 ||
           !capped || capped->cached_frequency!=56000 || capped->output_frequency!=44000 ||
           !scaled_cap || scaled_cap->cached_frequency!=56000 || scaled_cap->output_frequency!=42000 ||
           lcs::continuous_channel_frequency(44001)!=44000 || lcs::continuous_channel_frequency(44000)!=44000 ||
           lcs::continuous_frequency_step(0,12000,1.f) ||
           lcs::continuous_frequency_step(12000,12000,std::numeric_limits<float>::quiet_NaN()))
            throw std::runtime_error("Retail existing-channel slew, unscaled cache or slow-motion write gate changed");
        return 0;
    }catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
}
