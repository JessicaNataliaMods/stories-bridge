#include "lcs/vehicle_surface_audio.hpp"
#include "lcs/audio.hpp"
#include "lcs/continuous_sound.hpp"
#include <stdexcept>
#include <iostream>
#include <cstring>
#include <algorithm>
static void check(bool value,const char* message){if(!value)throw std::runtime_error(message);}
int main(int argc,char** argv){try{
    using namespace lcs;
    VehicleRoadNoiseInputs road;road.velocity=0.5f;road.max_velocity=1;road.road_noise_base_frequency=22050;road.wheels_on_ground=4;
    auto dry=vehicle_road_noise_sounds(road);
    check(dry && dry->rolling && !dry->wet && dry->rolling->volume==30,"Dry rolling source level");
    road.wet_roads=1;auto wet=vehicle_road_noise_sounds(road);
    check(wet && wet->wet && wet->wet->volume==23,"Wet roads add their separate source loop");
    road.surface=VehicleRoadSurface::water;auto water=vehicle_road_noise_sounds(road);
    check(water && water->rolling->sample==VehicleSurfaceSample::water_road_loop,"Wheels in water select water loop");
    road.wheels_on_ground=0;check(!vehicle_road_noise_sounds(road),"Airborne vehicle releases road loops");
    VehicleSkidInputs skid;skid.max_velocity=1;skid.velocity=0.5f;skid.gas_audio=0.7f;skid.wheels_on_ground=4;skid.wheel_count=4;
    skid.wheels[0]={VehicleWheelState::skidding,1,true};
    auto skid_dry=vehicle_skid_sound(skid);check(skid_dry && skid_dry->sample==VehicleSurfaceSample::skid,"Skidding wheel produces source skid");
    skid.surface=VehicleRoadSurface::grass;auto grass=vehicle_skid_sound(skid);
    check(grass && grass->sample==VehicleSurfaceSample::skid_rain && grass->volume<skid_dry->volume,"Grass uses quieter source friction");
    skid.wheels[0].contact_time=0;check(!vehicle_skid_sound(skid),"Wheel without contact cannot skid");
    VehicleReverseInputs reverse;reverse.max_reverse_velocity=0.25f;reverse.velocity=-0.125f;reverse.gas_audio=0.5f;reverse.engine_on=true;reverse.gear_is_reverse=true;reverse.drive_wheels_on_ground=true;
    auto rev=vehicle_reverse_sound(reverse);check(rev && rev->volume==12 && rev->frequency==10000,"Reverse uses normalized source pitch and gain");
    reverse.engine_on=false;check(!vehicle_reverse_sound(reverse),"Engine-off vehicle cannot own reverse loop");
    if(argc>1){AudioArchive archive(AudioArchive::Ps2Audio{},argv[1]);for(unsigned i=0;i<7;++i){
        auto sample=archive.sample(vehicle_surface_sample(VehicleSurfaceSample(i)));
        const auto record=archive.record(vehicle_surface_sample(VehicleSurfaceSample(i)));
        check(sample.frequency && sample.channels==1 && sample.pcm.size()==record.size && continuous_pcm_loop_span(record.size,record.loop_start,record.loop_end).has_value(),"Real vehicle loop format and span");
        int peak=0;for(size_t j=0;j+1<sample.pcm.size();j+=2){int16_t frame;std::memcpy(&frame,sample.pcm.data()+j,2);peak=std::max(peak,std::abs(int(frame)));}
        check(peak>0,"Vehicle loop contains audible PCM");
    }}
    std::cout<<"PASS: vehicle road/skid/reverse rules and 7 real loops; no audio device opened\n";return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
