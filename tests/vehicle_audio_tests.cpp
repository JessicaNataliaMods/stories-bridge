#include "lcs/audio.hpp"
#include "lcs/vehicle_audio.hpp"
#include "lcs/continuous_sound.hpp"
#include "audio_sample_ids.inc"
#include <algorithm>
#include <cmath>
#include <cstring>
#include <fstream>
#include <iostream>
#include <map>
#include <set>
#include <sstream>
#include <stdexcept>

namespace {
void check(bool value,const std::string& message){if(!value)throw std::runtime_error(message);}
int peak(const lcs::AudioSample& sample) {
    int value=0;
    for(size_t i=0;i+1<sample.pcm.size();i+=2){int16_t v;std::memcpy(&v,sample.pcm.data()+i,2);value=std::max(value,std::abs(int(v)));}
    return value;
}
struct Binding {int original,native;std::string original_name,native_name;};
}
// argv[1]: the original PS2 AUDIO folder; argv[2]: the package's native-equivalents.tsv.
// Proves which LCS vehicle sounds each converted SA slot plays: the audio row of
// an SA model is the row of the original model the slot stands for.
int main(int argc,char** argv){try {
    check(argc==3,"Expected the PS2 AUDIO folder and native-equivalents.tsv");
    std::vector<Binding> vehicles;
    {
        std::ifstream in(argv[2]);std::string line;
        while(std::getline(in,line)) {
            if(!line.empty() && line.back()=='\r')line.pop_back();
            std::istringstream fields(line);Binding b;std::string kind;
            if(fields>>b.original>>b.native>>b.original_name>>b.native_name>>kind && kind=="vehicle")vehicles.push_back(b);
        }
    }
    check(vehicles.size()==size_t(lcs::lcs_vehicle_model_count),"Every original vehicle has an SA slot: "+std::to_string(vehicles.size()));
    std::set<int> natives,originals;
    std::map<std::string,const lcs::VehicleSoundRow*> by_name;
    for(const auto& v:vehicles) {
        check(natives.insert(v.native).second,"Two original vehicles share SA slot "+std::to_string(v.native));
        check(originals.insert(v.original).second,"Duplicate original vehicle "+std::to_string(v.original));
        const auto* row=lcs::vehicle_sound_row(v.original);
        check(row,"No sound row for original vehicle "+v.original_name);
        by_name[v.original_name]=row;
    }
    check(*originals.begin()==lcs::first_lcs_vehicle_model && *originals.rbegin()==lcs::first_lcs_vehicle_model+lcs::lcs_vehicle_model_count-1,"Original ids are the contiguous audio table range");
    // Independent evidence that the row order is the model order: vehicles whose role is
    // known must own the matching siren, door and engine bank.
    const auto row=[&](const char* name)->const lcs::VehicleSoundRow&{
        const auto found=by_name.find(name);check(found!=by_name.end(),std::string("Unknown vehicle ")+name);return *found->second;};
    check(row("ambulan").siren_sample==SFX_AMBULANCE_SI,"Ambulance owns the ambulance siren");
    for(const char* police:{"police","enforcer","fbicar","firetruk"})
        check(row(police).siren_sample==SFX_POLICE_SIREN_SLOW,std::string("Police siren for ")+police);
    check(row("bus").door==lcs::VehicleDoorType::bus_door && row("coach").door==lcs::VehicleDoorType::bus_door,"Buses use the bus door");
    check(row("linerun").door==lcs::VehicleDoorType::truck_door && row("linerun").horn_sample==SFX_CAR_HORN_TRUCK,"Linerun is a truck");
    check(row("bus").horn_sample==SFX_CAR_HORN_BUS2 || row("bus").horn_sample==SFX_CAR_HORN_BUS,"Bus horn");
    check(row("faggio").bank==row("pizzaboy").bank && lcs::vehicle_engine_samples(row("faggio").bank)->idle==SFX_MOPED_IDLE,"Mopeds use the moped engine");
    check(lcs::vehicle_engine_samples(row("angel").bank)->idle==SFX_CAR_IDLE_VTWI,"Angel uses the V-twin engine");
    check(lcs::vehicle_engine_samples(row("pcj600").bank)->idle==SFX_CAR_IDLE_SPORTCAR,"PCJ-600 uses the sports bike engine");
    check(lcs::vehicle_engine_samples(row("sanchez").bank)->idle==SFX_CAR_IDLE_HONDA,"Sanchez uses the Honda engine");
    check(lcs::vehicle_engine_samples(row("stinger").bank)->idle==SFX_CAR_IDLE_PORSHE,"Stinger uses the Porsche engine");
    // Every referenced sample decodes to real audio with a usable loop.
    const lcs::AudioArchive archive(lcs::AudioArchive::Ps2Audio{},argv[1]);
    std::set<uint32_t> loops,shots,engines;
    for(const auto& v:vehicles) {
        const auto& r=*lcs::vehicle_sound_row(v.original);
        loops.insert(r.horn_sample);loops.insert(r.siren_sample);
        shots.insert(lcs::vehicle_door_sample(r.door,true));shots.insert(lcs::vehicle_door_sample(r.door,false));
        if(const auto pair=lcs::vehicle_engine_samples(r.bank)) {
            engines.insert(pair->idle);engines.insert(pair->rev);
            if(r.bank-4<20)engines.insert(r.accel_sample);   // aircraft banks never reach the car/bike engine path
        }
        check(r.horn_frequency>4000 && r.horn_frequency<48000 && r.siren_frequency>4000 && r.siren_frequency<48000,"Implausible horn/siren rate for "+v.original_name);
    }
    loops.insert(lcs::vehicle_fast_siren_sample());shots.insert(lcs::vehicle_engine_start_sample());
    for(const auto index:loops) {
        lcs::AudioSample sample;lcs::AudioRecord record;
        try {sample=archive.sample(index);record=archive.record(index);} catch(const std::exception& e) {throw std::runtime_error("horn/siren sample "+std::to_string(index)+": "+e.what());}
        check(sample.channels==1 && sample.pcm.size()>400 && peak(sample)>1000,"Silent horn/siren sample "+std::to_string(index));
        check(lcs::continuous_pcm_loop_span(record.size,record.loop_start,record.loop_end).has_value(),"Horn/siren sample has no usable loop "+std::to_string(index));
    }
    for(const auto index:shots) {
        lcs::AudioSample sample;
        try {sample=archive.sample(index);} catch(const std::exception& e) {throw std::runtime_error("door/starter sample "+std::to_string(index)+": "+e.what());}
        check(sample.channels==1 && sample.pcm.size()>400 && peak(sample)>1000,"Silent door/starter sample "+std::to_string(index));
    }
    size_t headerless=0,placeholders=0;
    for(const auto index:engines) {
        lcs::AudioSample sample;lcs::AudioRecord record;
        try {sample=archive.sample(index);record=archive.record(index);}
        catch(const std::runtime_error&) {sample=archive.sample(index,22050);record=archive.record(index,22050);++headerless;}
        // Retail uses 168-byte silent placeholders for the idle loop of the RC/golf-cart banks and the unused banks.
        const bool placeholder=sample.pcm.size()<=200 && peak(sample)==0;
        if(placeholder)++placeholders;
        check(sample.channels==1 && (placeholder || (sample.pcm.size()>400 && peak(sample)>1000)),"Silent engine sample "+std::to_string(index)+" size="+std::to_string(sample.pcm.size())+" peak="+std::to_string(peak(sample)));
        check(placeholder || lcs::continuous_pcm_loop_span(record.size,record.loop_start,record.loop_end).has_value(),"Engine sample has no usable loop "+std::to_string(index));
    }
    std::cout<<"LCS vehicle -> SA slot: horn(rate) siren(rate) door bank accel\n";
    for(const auto& v:vehicles) {
        const auto& r=*lcs::vehicle_sound_row(v.original);
        std::cout<<"  "<<v.original<<' '<<v.original_name<<" -> "<<v.native<<' '<<v.native_name<<": horn#"<<r.horn_sample<<'@'<<r.horn_frequency
                 <<" siren#"<<r.siren_sample<<'@'<<r.siren_frequency<<" door"<<int(r.door)<<" bank"<<int(r.bank)<<" accel#"<<r.accel_sample<<'\n';
    }
    // The engine model: idle below a pedal of 0.02, rate 14000..27000 under power, volume 75..90.
    lcs::VehicleEngineInputs in{0.f,1.5f,0.f,0.2f,0.f,0.f,false,true,false};
    auto idle=lcs::vehicle_engine_sound(in);
    check(idle.idle && idle.frequency>=22050 && idle.volume==lcs::vehicle_engine_base_volume,"Engine idle at rest");
    in.speed=1.2f;in.gas_pedal=1.f;in.gas_audio=1.f;in.gear_change_down=0.4f;
    auto rev=lcs::vehicle_engine_sound(in);
    check(!rev.idle && rev.frequency>14000 && rev.frequency<=27000 && rev.volume>=lcs::vehicle_engine_base_volume && rev.volume<=lcs::vehicle_engine_full_volume,"Engine under power");
    in.player=true;check(lcs::vehicle_engine_sound(in).volume==lcs::vehicle_player_engine_volume,"Player engine volume");
    std::cout<<"PASS: "<<vehicles.size()<<" original vehicles own one sound row each; "<<loops.size()<<" loop and "<<shots.size()<<" one-shot samples decode; "
             <<engines.size()<<" engine samples ("<<headerless<<" headerless); gameExecuted=false\n";
    return 0;
} catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}}
