#include "sa_vehicle_audio.hpp"
#include <cmath>
#include <iostream>
#include <initializer_list>
#include <memory>
#include <stdexcept>
#include <vector>

namespace {
void check(bool value,const char* message) { if(!value) throw std::runtime_error(message); }

std::shared_ptr<const lcs::AudioSample> sample(std::initializer_list<int16_t> frames) {
    auto result=std::make_shared<lcs::AudioSample>();
    result->frequency=lcs::SaAudioMixerCore::output_rate;
    result->channels=1;
    for(const auto frame:frames) {
        const auto* bytes=reinterpret_cast<const uint8_t*>(&frame);
        result->pcm.insert(result->pcm.end(),bytes,bytes+sizeof(frame));
    }
    return result;
}
}

int main() { try {
    using lcs::SaVehicleEventSound;
    check(lcs::sa_vehicle_event_sound(77)==SaVehicleEventSound::none,"event77 is not a vehicle door");
    check(lcs::sa_vehicle_event_sound(78)==SaVehicleEventSound::none,"bonnet opening after a collision keeps SA audio");
    check(lcs::sa_vehicle_event_sound(83)==SaVehicleEventSound::door_open,"event83 opens a door");
    check(lcs::sa_vehicle_event_sound(84)==SaVehicleEventSound::none,"bonnet closing keeps SA audio");
    check(lcs::sa_vehicle_event_sound(89)==SaVehicleEventSound::door_close,"event89 closes a door");
    check(lcs::sa_vehicle_event_sound(90)==SaVehicleEventSound::none,"event90 is outside the door range");
    check(lcs::sa_vehicle_event_sound(103)==SaVehicleEventSound::engine_start,"event103 starts an engine");
    check(lcs::sa_vehicle_event_sound(104)==SaVehicleEventSound::none,"event104 is not a vehicle starter");

    check(lcs::sa_vehicle_event_sound(79)==SaVehicleEventSound::none && lcs::sa_vehicle_event_sound(85)==SaVehicleEventSound::none,"boot motion keeps its native damage audio");
    check(lcs::sa_vehicle_event_sound(80)==SaVehicleEventSound::door_open && lcs::sa_vehicle_event_sound(86)==SaVehicleEventSound::door_close,"real passenger doors retain LCS open/close audio");
    lcs::SaAudioMixerCore mixer;
    const auto loop=sample({1000,2000,6000,8000});
    check(mixer.publish_loop(1,loop,0,8,44100,127,63),"valid loop is accepted");
    std::vector<int16_t> output;
    mixer.render(2,output);
    check(output.size()==4 && output[0]==1000 && output[2]==2000,"loop starts at its first PCM frame");
    check(mixer.publish_loop(1,loop,0,8,44100,63,0),"same loop ID accepts an in-place mix update");
    mixer.render(1,output);
    check(std::abs(int(output[0])-2976)<=2 && output[1]==0,"loop update preserves phase and pans mono left");
    mixer.release_loop(1);
    check(mixer.publish_loop(1,loop,0,8,44100,127,63),"a released ID can start a fresh loop");
    mixer.render(1,output);
    check(output[0]==1000 && output[1]==1000,"release followed by republish resets phase");
    mixer.release_loop(1);
    mixer.render(1,output);
    check(output[0]==0 && output[1]==0,"released loop contributes no later output");

    const auto loud=sample({30000,30000,30000,30000});
    for(uint64_t id=2;id<10;++id)
        check(mixer.play_one_shot(loud,44100,127,63),"bounded mixer accepts a burst of vehicle events");
    mixer.render(1,output);
    check(output[0]<=32767 && output[0]>28000 && output[1]==output[0],"overlapping door events are limited without clipping");
    for(int frame=0;frame<3;++frame)mixer.render(1,output);
    const auto short_event=sample({1000});
    for(size_t voice=0;voice<lcs::SaAudioMixerCore::max_one_shots;++voice)
        check(mixer.play_one_shot(short_event,44100,127,63),"one-shot pool admits voices up to its bound");
    check(!mixer.play_one_shot(short_event,44100,127,63),"one-shot pool refuses overflow instead of evicting a voice");
    mixer.render(20,output);
    check(mixer.play_one_shot(short_event,44100,127,63),"finished one-shots release their bounded slots");
    mixer.render(2,output);
    std::cout<<"PASS: vehicle event classification, loop continuity, spatial pan and bounded mixed peak; no device opened\n";
    return 0;
} catch(const std::exception& error) {
    std::cerr<<error.what()<<'\n';
    return 1;
} }
