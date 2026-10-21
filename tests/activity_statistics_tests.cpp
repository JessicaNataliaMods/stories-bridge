#include "lcs/native.hpp"
#include "ScriptCommands.h"
#include <iostream>
#include <stdexcept>
#include <functional>
#include <algorithm>
#include <xmmintrin.h>
#include <emmintrin.h>
using namespace lcs;
static void check(bool ok,const char* message){if(!ok)throw std::runtime_error(message);}
static Vm program(uint16_t opcode,const std::vector<int32_t>& operands,uint8_t type=6) {
    std::vector<uint8_t> bytes(64);bytes.push_back(uint8_t(opcode));bytes.push_back(uint8_t(opcode>>8));
    for(const auto operand:operands){bytes.push_back(type);for(int n=0;n<4;++n)bytes.push_back(uint8_t(uint32_t(operand)>>(n*8)));}
    return Vm(std::move(bytes),64,64);
}
static std::vector<int32_t> fields(const ActivityStatistics& s) {
    std::vector<int32_t> f={s.industrial_passed,s.commercial_passed,s.suburban_passed,
        s.cars_sold,s.car_sales_money,s.bikes_sold,s.bike_sales_money,s.trash_money,
        s.hitmen_killed,s.guardian_missions,s.guardian_justice};
    auto append=[&](const auto& a){f.insert(f.end(),a.begin(),a.end());};
    append(s.bandit_laps);append(s.bandit_positions);f.push_back(s.train_time_left);
    append(s.street_times);append(s.street_laps);append(s.street_positions);
    f.insert(f.end(),{s.carnage_seconds,s.rc_triad_kills,s.slash_level,s.slash_money,s.slash_kills});
    append(s.dirt_laps);append(s.dirt_times);f.insert(f.end(),{s.dirt_air_time,s.gogo_time});
    append(s.guardian_levels);f.push_back(s.unlocked_costumes);
    for(uint32_t bits:{s.pizzas_delivered_bits,s.noodles_delivered_bits,s.tourist_money_bits,s.tourist_spots_bits}) {
        int32_t value;std::memcpy(&value,&bits,4);f.push_back(value);
    }
    for(uint32_t bits:{s.face_plant_distance_bits,s.scrapyard_score_bits,s.mayhem_score_bits,s.scooter_score_bits,s.wichita_score_bits}) {
        int32_t value;std::memcpy(&value,&bits,4);f.push_back(value);
    }
    return f;
}
int main(){try{
    const ActivityStatistics initial;
    check(std::all_of(initial.bandit_positions.begin(),initial.bandit_positions.end(),[](int32_t v){return v==INT32_MAX;}) &&
        std::all_of(initial.street_positions.begin(),initial.street_positions.end(),[](int32_t v){return v==INT32_MAX;}),"PS2 initialization uses INT32_MAX positions");
    struct Scenario {uint16_t opcode;std::vector<int32_t> operands;std::function<int32_t(const ActivityStatistics&)> read;int32_t expected;size_t changed=1;};
    const Scenario scenarios[]={
        {COMMAND_INDUSTRIAL_PASSED,{37},[](const auto& s){return s.industrial_passed;},1},
        {COMMAND_COMMERCIAL_PASSED,{-7},[](const auto& s){return s.commercial_passed;},1},
        {COMMAND_SUBURBAN_PASSED,{INT32_MIN},[](const auto& s){return s.suburban_passed;},1},
        {COMMAND_REGISTER_CAR_SOLD,{},[](const auto& s){return s.cars_sold;},1},
        {COMMAND_ADD_MONEY_MADE_WITH_CAR_SALES,{20000001},[](const auto& s){return s.car_sales_money;},20000001},
        {COMMAND_REGISTER_BIKE_SOLD,{},[](const auto& s){return s.bikes_sold;},1},
        {COMMAND_ADD_MONEY_MADE_WITH_BIKE_SALES,{-20000001},[](const auto& s){return s.bike_sales_money;},-20000001},
        {COMMAND_ADD_MONEY_MADE_COLLECTING_TRASH,{98765},[](const auto& s){return s.trash_money;},98765},
        {COMMAND_REGISTER_HITMAN_KILLED,{},[](const auto& s){return s.hitmen_killed;},1},
        {COMMAND_REGISTER_GUARDIAN_ANGEL_MISSION_PASSED,{},[](const auto& s){return s.guardian_missions;},1},
        {COMMAND_REGISTER_HIGHEST_GUARDIAN_ANGEL_JUSTICE_DISHED,{20000001},[](const auto& s){return s.guardian_justice;},1},
        {COMMAND_REGISTER_BEST_BANDIT_LAP_TIME,{2,123456},[](const auto& s){return s.bandit_laps[2];},123456},
        {COMMAND_REGISTER_BEST_BANDIT_POSITION,{2,2},[](const auto& s){return s.bandit_positions[2];},2},
        {COMMAND_REGISTER_MOST_TIME_LEFT_TRAIN_RACE,{-20},[](const auto& s){return s.train_time_left;},-20},
        {COMMAND_REGISTER_STREET_RACE_FASTEST_TIME,{5,91828},[](const auto& s){return s.street_times[5];},91828},
        {COMMAND_REGISTER_STREET_RACE_FASTEST_LAP,{5,28392},[](const auto& s){return s.street_laps[5];},28392},
        {COMMAND_REGISTER_STREET_RACE_BEST_POSITION,{5,3},[](const auto& s){return s.street_positions[5];},3},
        {COMMAND_REGISTER_MAX_SECONDS_ON_CARNAGE_LEFT,{40000000},[](const auto& s){return s.carnage_seconds;},40000000},
        {COMMAND_REGISTER_MAX_KILLS_ON_RC_TRIAD,{24},[](const auto& s){return s.rc_triad_kills;},24},
        {COMMAND_REGISTER_HIGHEST_LEVEL_SLASH_TV,{6},[](const auto& s){return s.slash_level;},6},
        {COMMAND_ADD_MONEY_MADE_WITH_SLASH_TV,{1337},[](const auto& s){return s.slash_money;},1337},
        {COMMAND_ADD_TOTAL_KILLS_ON_SLASH_TV,{127},[](const auto& s){return s.slash_kills;},127},
        {COMMAND_REGISTER_DIRT_BIKE_FASTEST_LAP,{9,17234},[](const auto& s){return s.dirt_laps[9];},17234},
        {COMMAND_REGISTER_DIRT_BIKE_FASTEST_TIME,{9,142342},[](const auto& s){return s.dirt_times[9];},142342},
        {COMMAND_REGISTER_DIRT_BIKE_AIR_TIME,{2340},[](const auto& s){return s.dirt_air_time;},2340},
        {COMMAND_REGISTER_BEST_TIME_GOGO_FAGGIO,{1880},[](const auto& s){return s.gogo_time;},1880},
        {COMMAND_REGISTER_GUARDIAN_ANGEL_LEVEL,{3,4,5},[](const auto& s){return s.guardian_levels[0]+s.guardian_levels[1]+s.guardian_levels[2];},12,3},
        {COMMAND_ADD_UNLOCKED_COSTUME,{15},[](const auto& s){return s.unlocked_costumes;},32768},
        {COMMAND_REGISTER_FACE_PLANT_DISTANCE,{0x429B8000},[](const auto& s){return s.face_plant_distance_bits;},0x429B8000},
        {COMMAND_REGISTER_SCRAPYARD_CHALLENGE_SCORE,{16777219},[](const auto& s){return s.scrapyard_score_bits;},0x4B800001},
        {COMMAND_REGISTER_9MM_MAYHEM_SCORE,{INT32_MAX},[](const auto& s){return s.mayhem_score_bits;},0x4EFFFFFF},
        {COMMAND_REGISTER_SCOOTER_SHOOTER_SCORE,{83},[](const auto& s){return s.scooter_score_bits;},0x42A60000},
        {COMMAND_REGISTER_WICHITA_WIPEOUT_SCORE,{900},[](const auto& s){return s.wichita_score_bits;},0x44610000},
        {COMMAND_ADD_PIZZAS_DELIVERED,{20000001},[](const auto& s){return s.pizzas_delivered_bits;},0x4B989680},
        {COMMAND_ADD_NOODLES_DELIVERED,{-13},[](const auto& s){return s.noodles_delivered_bits;},int32_t(0xC1500000u)},
        {COMMAND_ADD_MONEY_MADE_FROM_TOURIST,{INT32_MAX},[](const auto& s){return s.tourist_money_bits;},0x4EFFFFFF},
        {COMMAND_ADD_TOURISTS_TAKEN_TO_SPOTS,{16777219},[](const auto& s){return s.tourist_spots_bits;},0x4B800001}
    };
    ScmImage image;
    for(const auto& scenario:scenarios) {
        NativeAdapter adapter(image);adapter.world_ready=true;
        size_t native_calls=0;adapter.execute=[&](const NativeCall&){++native_calls;throw std::runtime_error("Original-only statistics must not borrow SA counters");return NativeResult{};};
        auto vm=program(scenario.opcode,scenario.operands,scenario.opcode==COMMAND_REGISTER_FACE_PLANT_DISTANCE?9:6);Thread thread;thread.ip=64;thread.condition=true;
        vm.backend=[&](Vm& v,Thread& t,uint16_t op){return adapter.dispatch(v,t,op);};
        const auto before=fields(adapter.activity_statistics());
        check(vm.step(thread,0)==StepResult::continued && thread.ip==vm.memory.size(),"The original instruction must consume exactly its operand count");
        check(scenario.read(adapter.activity_statistics())==scenario.expected,"Activity commands must change the genuine source record");
        check(thread.condition && !native_calls,"A statistic must preserve the script condition and SA state");
        const auto after=fields(adapter.activity_statistics());size_t changed=0;
        for(size_t j=0;j<after.size();++j)changed+=after[j]!=before[j];
        check(changed==scenario.changed,"Distinct activities, races and counters must remain isolated");
        thread.ip=64;thread.condition=false;
        check(vm.step(thread,0)==StepResult::continued && !thread.condition,"Non-predicate activity records must also preserve a false condition");
        // Restore the once-mutated reference before testing failed decoding.
        const auto committed=fields(adapter.activity_statistics());
        NativeAdapter gated(image);auto blocked=program(scenario.opcode,scenario.operands);Thread wait;wait.ip=64;
        blocked.backend=[&](Vm& v,Thread& t,uint16_t op){return gated.dispatch(v,t,op);};
        check(blocked.step(wait,0)==StepResult::unsupported && wait.ip==64 && fields(gated.activity_statistics())==before,"No activity writes before campaign initialization");
        if(!scenario.operands.empty()) {
            auto malformed=program(scenario.opcode,scenario.operands);malformed.memory.pop_back();Thread bad;bad.ip=64;
            malformed.backend=vm.backend;
            check(malformed.step(bad,0)==StepResult::failed && fields(adapter.activity_statistics())==committed,"Truncated last operand must not partially commit even a three-field update");
        }
    }
    for(const auto opcode:{COMMAND_INDUSTRIAL_PASSED,COMMAND_COMMERCIAL_PASSED,COMMAND_SUBURBAN_PASSED}) {
        NativeAdapter adapter(image);adapter.world_ready=true;
        adapter.execute=[](const NativeCall&)->NativeResult{throw std::runtime_error("Island statistics must not force native mission progression");};
        for(const auto value:{INT32_MIN,-1,0,INT32_MAX,0,1,0}) {
            auto vm=program(uint16_t(opcode),{value});Thread thread;thread.ip=64;
            vm.backend=[&](Vm& v,Thread& t,uint16_t op){return adapter.dispatch(v,t,op);};
            check(vm.step(thread,0)==StepResult::continued,"Original island record accepts all signed source values");
            const auto& stats=adapter.activity_statistics();
            const auto actual=opcode==COMMAND_INDUSTRIAL_PASSED?stats.industrial_passed:
                opcode==COMMAND_COMMERCIAL_PASSED?stats.commercial_passed:stats.suburban_passed;
            check(actual==int32_t(value!=0),"Each original island record can be reset after set; retail normalization is not an accumulating or forced completion flag");
        }
    }
    const std::pair<ActivityStatistic,int32_t> indexed[]={
        {ActivityStatistic::bandit_lap,3},{ActivityStatistic::bandit_position,3},{ActivityStatistic::street_time,6},
        {ActivityStatistic::street_lap,6},{ActivityStatistic::street_position,6},{ActivityStatistic::dirt_lap,10},{ActivityStatistic::dirt_time,10}};
    for(const auto& entry:indexed) {
        ActivityStatistics stats;
        for(int32_t i=0;i<entry.second;++i)check(update_activity_statistic(stats,entry.first,{{i,100+i,0}}),"Every real PS2 array slot must exist");
        const auto before=fields(stats);
        for(int32_t i:{INT32_MIN,-1,entry.second,INT32_MAX})check(!update_activity_statistic(stats,entry.first,{{i,1,0}}) && fields(stats)==before,"Invalid signed indices cannot overwrite an adjacent activity");
    }
    ActivityStatistics stats;
    stats.car_sales_money=INT32_MAX;update_activity_statistic(stats,ActivityStatistic::car_sales_money,{{1,0,0}});
    check(stats.car_sales_money==INT32_MIN,"PS2 additive counters wrap in 32 bits without float truncation");
    stats.bike_sales_money=INT32_MIN;update_activity_statistic(stats,ActivityStatistic::bike_sales_money,{{-1,0,0}});
    check(stats.bike_sales_money==INT32_MAX,"Negative increments retain signed overflow semantics");
    for(int32_t value:{100,120,85,0,400,-1,INT32_MIN,INT32_MAX})update_activity_statistic(stats,ActivityStatistic::dirt_lap,{{0,value,0}});
    check(stats.dirt_laps[0]==INT32_MIN && stats.dirt_times[0]==0,"Zero means unset only before a time record exists; negative minimum remains signed");
    for(int32_t value:{-8,0,9,-1,INT32_MAX,INT32_MIN})update_activity_statistic(stats,ActivityStatistic::carnage_seconds,{{value,0,0}});
    check(stats.carnage_seconds==INT32_MAX,"Maximum records cannot fall on negative or lower values");
    update_activity_statistic(stats,ActivityStatistic::guardian_levels,{{10,4,7}});
    update_activity_statistic(stats,ActivityStatistic::guardian_levels,{{3,9,-8}});
    check(stats.guardian_levels==std::array<int32_t,3>{{10,9,7}},"Three Guardian Angel levels are independent maxima");
    for(unsigned costume=0;costume<256;++costume) {
        ActivityStatistics s;check(update_activity_statistic(s,ActivityStatistic::unlocked_costume,{{int32_t(costume),0,0}}),"Original out-of-range costume returns without a mutation");
        check(s.unlocked_costumes==(costume<16?1u<<costume:0),"Only the original sixteen costume bits may change");
    }
    stats.unlocked_costumes=0;update_activity_statistic(stats,ActivityStatistic::unlocked_costume,{{256,0,0}});
    update_activity_statistic(stats,ActivityStatistic::unlocked_costume,{{257,0,0}});
    update_activity_statistic(stats,ActivityStatistic::unlocked_costume,{{-1,0,0}});
    update_activity_statistic(stats,ActivityStatistic::unlocked_costume,{{1,0,0}});
    check(stats.unlocked_costumes==3,"The source handler passes a byte and duplicate costumes remain idempotent");
    // Independent SSE CVT reference uses chop only in this isolated process.
    // The bridge's integer implementation must not change either rounding or
    // exception status, even under a different environment left by an SA mod.
    struct FloatingControl {unsigned saved=_mm_getcsr();~FloatingControl(){_mm_setcsr(saved);}} control;
    for(unsigned mode:{_MM_ROUND_NEAREST,_MM_ROUND_DOWN,_MM_ROUND_UP,_MM_ROUND_TOWARD_ZERO}) {
        uint32_t random=0x31415926;
        for(unsigned n=0;n<65536;++n) {
            random=random*1664525u+1013904223u;int32_t value;std::memcpy(&value,&random,4);
            _mm_setcsr((control.saved&~_MM_ROUND_MASK)|_MM_ROUND_TOWARD_ZERO);
            const __m128 native=_mm_cvtsi32_ss(_mm_setzero_ps(),value);
            uint32_t expected;const float sample=_mm_cvtss_f32(native);std::memcpy(&expected,&sample,4);
            _mm_setcsr((control.saved&~_MM_ROUND_MASK)|mode);
            const unsigned before=_mm_getcsr();
            check(activity_float_from_int(value)==expected,"All signed magnitudes must match CVT.S.W chop, including precision boundaries");
            check(_mm_getcsr()==before,"A statistics update must preserve the host FP environment");
        }
    }
    check(activity_float_from_int(INT32_MIN)==0xCF000000u && activity_float_from_int(-16777219)==0xCB800001u &&
        activity_float_from_int(0)==0,"Conversion must handle signed endpoints and signed chopped mantissas");
    const std::array<uint32_t,14> patterns{{0,0x80000000u,1,0x807FFFFFu,0x00800000u,0x80800000u,
        0x3F800000u,0xBF800000u,0x7F7FFFFFu,0xFF7FFFFFu,0x7F800000u,0xFF800000u,0x7FFFFFFFu,0xFFFFFFFFu}};
    for(uint32_t a:patterns)for(uint32_t b:patterns) {
        // Numeric rank is deliberately distinct from the production branches.
        const auto rank=[](uint32_t bits)->int64_t {
            return (bits&0x80000000u)?-1-int64_t(bits&0x7FFFFFFFu):int64_t(bits);
        };
        const uint32_t expected=rank(a)>rank(b)?a:b;
        check(activity_float_max(a,b)==expected,"MAX.S must preserve ordered raw bits, signed zeros, subnormal and extended exponent encodings");
        ActivityStatistics subject;subject.face_plant_distance_bits=a;const auto before=fields(subject);
        int32_t operand;std::memcpy(&operand,&b,4);
        update_activity_statistic(subject,ActivityStatistic::face_plant_distance,{{operand,0,0}});
        auto after=fields(subject);after[after.size()-5]=before[before.size()-5];
        check(subject.face_plant_distance_bits==expected && before==after,"Only the selected float record may change");
    }
    for(auto record:{ActivityStatistic::scrapyard_score,ActivityStatistic::mayhem_score,ActivityStatistic::scooter_score,ActivityStatistic::wichita_score}) {
        ActivityStatistics subject;
        for(int32_t value:{INT32_MIN,-1,0,83,82,16777219,16777218,INT32_MAX,1})
            check(update_activity_statistic(subject,record,{{value,0,0}}),"Every original high score input must update its real float record");
        const uint32_t final=record==ActivityStatistic::scrapyard_score?subject.scrapyard_score_bits:
            record==ActivityStatistic::mayhem_score?subject.mayhem_score_bits:
            record==ActivityStatistic::scooter_score?subject.scooter_score_bits:subject.wichita_score_bits;
        check(final==0x4EFFFFFFu,"Maximum records keep chopped PS2 float precision and ignore lower or negative scores");
    }
    // Independent oracle: normalize input encodings, mask the smaller SSE
    // operand to the EE's single guard, then run actual hardware addition.
    // Production instead adds scaled integer significands and never uses SSE.
    const auto sum_reference=[](uint32_t a,uint32_t b) {
        if(!(a&0x7F800000u))a&=0x80000000u;
        if(!(b&0x7F800000u))b&=0x80000000u;
        const int difference=int((a>>23)&255)-int((b>>23)&255);
        if(difference>=25)b&=0x80000000u;
        else if(difference<=-25)a&=0x80000000u;
        else if(difference>0)b&=~((1u<<unsigned(difference-1))-1u);
        else if(difference<0)a&=~((1u<<unsigned(-difference-1))-1u);
        const auto lhs=_mm_castsi128_ps(_mm_cvtsi32_si128(int32_t(a)));
        const auto rhs=_mm_castsi128_ps(_mm_cvtsi32_si128(int32_t(b)));
        const auto sum=_mm_add_ss(lhs,rhs);
        return uint32_t(_mm_cvtsi128_si32(_mm_castps_si128(sum)));
    };
    for(unsigned mode:{_MM_ROUND_NEAREST,_MM_ROUND_DOWN,_MM_ROUND_UP,_MM_ROUND_TOWARD_ZERO}) {
        uint32_t random=0x51525354u;
        for(unsigned i=0;i<65536;++i) {
            random=random*1664525u+1013904223u;const uint32_t old=random;
            random=random*1664525u+1013904223u;int32_t increment;std::memcpy(&increment,&random,4);
            const auto converted=activity_float_from_int(increment);
            // Exponent 255 is finite on the EE but not on SSE. With an integer
            // delta its exponent gap is at least 97: it must remain unchanged.
            _mm_setcsr((control.saved&~_MM_ROUND_MASK)|_MM_ROUND_TOWARD_ZERO|0x8040u|_MM_MASK_MASK);
            const auto expected=(old&0x7F800000u)==0x7F800000u?old:sum_reference(old,converted);
            _mm_setcsr((control.saved&~_MM_ROUND_MASK)|mode);
            const auto before=_mm_getcsr();
            check(activity_float_add_int(old,increment)==expected,"Counter sum must match the independent EE-aligned SSE oracle across all signs/exponents");
            check(_mm_getcsr()==before,"Counter sums cannot alter rounding, denormal mode or exception status left by native mods");
        }
    }
    check(activity_float_add_int(0x4B800000u,-1)==0x4B7FFFFFu &&
          activity_float_add_int(0x4B800000u,1)==0x4B800000u &&
          activity_float_add_int(0x4C000000u,-1)==0x4C000000u,
          "One guard distinguishes half-ULP cancellation from an integer below the source alignment range");
    check(activity_float_add_int(0xCB800000u,1)==0xCB7FFFFFu &&
          activity_float_add_int(0x80000000u,0)==0 && activity_float_add_int(0x807FFFFFu,0)==0,
          "Negative cancellation, zero signs and denormal inputs use original addition semantics");
    for(auto counter:{ActivityStatistic::pizzas_delivered,ActivityStatistic::noodles_delivered,ActivityStatistic::tourist_money,ActivityStatistic::tourist_spots}) {
        ActivityStatistics subject;const auto empty=fields(subject);
        for(int32_t value:{INT32_MIN,INT32_MAX,0,20000001,-20000001,16777219,-1,1})
            check(update_activity_statistic(subject,counter,{{value,0,0}}),"Every signed delivery/tourist increment updates its own real float state");
        auto changed=fields(subject);
        const size_t index=changed.size()-9+size_t(counter==ActivityStatistic::pizzas_delivered?0:counter==ActivityStatistic::noodles_delivered?1:counter==ActivityStatistic::tourist_money?2:3);
        changed[index]=empty[index];
        check(changed==empty,"A delivery or tourist sum cannot mutate any adjacent statistic, costume or record");
    }
    check(!update_activity_statistic(stats,ActivityStatistic(-1),{}),"Unknown statistic cannot invent a mutation");
    std::cout<<"PASS: 37 original activity commands, boolean island records, exact arities, PS2 initial records, independent delivery/tourist float counters, signed arithmetic, indices, costume bits, raw float maxima, 262144 signed CVT and 262144 guarded ADD comparisons, FP state preservation and atomic decode; GTA not executed\n";
    return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
