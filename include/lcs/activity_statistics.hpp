#pragma once
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <limits>

namespace lcs {
// Original-only CStats fields have no matching SA counter. Keep their actual
// values with the LCS campaign, rather than borrowing unrelated native stats.
// PS2 SLUS_214.23: initialization 22FF70..230094, helpers 2302E8..230424.
struct ActivityStatistics {
    // Retail normalizes these three CStats records to 0/1, despite int32
    // assignment in the reconstructed handlers. They do not unlock an island.
    int32_t industrial_passed=0,commercial_passed=0,suburban_passed=0;
    int32_t cars_sold=0,car_sales_money=0,bikes_sold=0,bike_sales_money=0;
    int32_t trash_money=0,hitmen_killed=0,guardian_missions=0,guardian_justice=0;
    std::array<int32_t,3> bandit_laps{};
    std::array<int32_t,3> bandit_positions{{INT32_MAX,INT32_MAX,INT32_MAX}};
    int32_t train_time_left=0;
    std::array<int32_t,6> street_times{},street_laps{};
    std::array<int32_t,6> street_positions{{INT32_MAX,INT32_MAX,INT32_MAX,INT32_MAX,INT32_MAX,INT32_MAX}};
    int32_t carnage_seconds=0,rc_triad_kills=0,slash_level=0,slash_money=0,slash_kills=0;
    std::array<int32_t,10> dirt_laps{},dirt_times{};
    int32_t dirt_air_time=0,gogo_time=0;
    std::array<int32_t,3> guardian_levels{};
    uint16_t unlocked_costumes=0;
    // PS2 CStats stores these as FPR bit patterns, including exponent 255.
    uint32_t face_plant_distance_bits=0,scrapyard_score_bits=0,mayhem_score_bits=0;
    uint32_t scooter_score_bits=0,wichita_score_bits=0;
    uint32_t pizzas_delivered_bits=0,noodles_delivered_bits=0;
    uint32_t tourist_money_bits=0,tourist_spots_bits=0;
};
enum class ActivityStatistic {
    industrial_passed,commercial_passed,suburban_passed,
    cars_sold,car_sales_money,bikes_sold,bike_sales_money,trash_money,
    hitmen_killed,guardian_missions,guardian_justice,bandit_lap,bandit_position,
    train_time_left,street_time,street_lap,street_position,carnage_seconds,
    rc_triad_kills,slash_level,slash_money,slash_kills,dirt_lap,dirt_time,
    dirt_air_time,gogo_time,guardian_levels,unlocked_costume,face_plant_distance,
    scrapyard_score,mayhem_score,scooter_score,wichita_score,
    pizzas_delivered,noodles_delivered,tourist_money,tourist_spots
};
// CVT.S.W on the EE uses chop, independently of the host FPU control word.
// Normalize the unsigned magnitude and discard the unrepresentable low bits;
// uint32 arithmetic also handles INT32_MIN without signed negation overflow.
inline uint32_t activity_float_from_int(int32_t value) {
    if(!value)return 0;
    const uint32_t sign=value<0?0x80000000u:0u;
    const uint32_t magnitude=value<0?0u-uint32_t(value):uint32_t(value);
    unsigned exponent=0;for(uint32_t top=magnitude;top>1;top>>=1)++exponent;
    const uint32_t mantissa=exponent>23?magnitude>>(exponent-23):magnitude<<(23-exponent);
    return sign|((exponent+127u)<<23)|(mantissa&0x7fffffu);
}
// Retail delivery counters do CVT.S.W then ADD.S. The EE alignment keeps one
// guard bit, with chop at the final precision. Work in a 25-bit integer scale;
// this cannot observe/change an SA plugin's MXCSR, x87 rounding or FP flags.
// The second operand is always a converted signed integer, never a float input.
inline uint32_t activity_float_add_int(uint32_t current,int32_t increment) {
    constexpr uint32_t sign_bit=0x80000000u,mantissa_mask=0x7fffffu;
    uint32_t a=(current&0x7f800000u)?current:current&sign_bit;
    uint32_t b=activity_float_from_int(increment);
    const auto magnitude=[](uint32_t bits){return bits&0x7fffffffu;};
    if(!(magnitude(a)|magnitude(b)))return (a&b)&sign_bit;
    if(magnitude(a)<magnitude(b)){const auto swap=a;a=b;b=swap;}
    if(!magnitude(b))return a;
    const auto exponent=int((a>>23)&255u);
    const auto distance=unsigned(exponent-int((b>>23)&255u));
    if(distance>=25)return a;
    const int64_t large=int64_t((0x800000u|(a&mantissa_mask))<<1);
    const int64_t small=int64_t(((0x800000u|(b&mantissa_mask))<<1)>>distance);
    const int64_t sum=((a&sign_bit)?-large:large)+((b&sign_bit)?-small:small);
    if(!sum)return 0;
    const uint32_t sign=sum<0?sign_bit:0;
    const uint64_t absolute=uint64_t(sum<0?-sum:sum);
    unsigned highest=0;for(uint64_t top=absolute;top>1;top>>=1)++highest;
    const int result_exponent=exponent+int(highest)-24;
    if(result_exponent<=0)return sign;
    // In this nonignored int-delta path the larger exponent is at most 182;
    // even a carry cannot overflow. Extended exponents take the earlier path.
    const auto significand=highest>23?absolute>>(highest-23):absolute<<(23-highest);
    return sign|(uint32_t(result_exponent)<<23)|uint32_t(significand&mantissa_mask);
}
// MAX.S is an ordering of the original bit patterns, not IEEE std::max:
// exponent 255 remains a value, positive zero wins over negative zero and
// two negative operands have reversed magnitude order. No host FP operation.
inline uint32_t activity_float_max(uint32_t a,uint32_t b) {
    if((a^b)&0x80000000u)return (a&0x80000000u)?b:a;
    return (a&0x80000000u)?(a<b?a:b):(a>b?a:b);
}
inline int32_t activity_sum(int32_t current,int32_t value) {
    const uint32_t bits=uint32_t(current)+uint32_t(value);
    int32_t result;std::memcpy(&result,&bits,4);return result;
}
inline int32_t activity_max(int32_t current,int32_t value) {return current<value?value:current;}
inline int32_t activity_min(int32_t current,int32_t value) {return current<value?current:value;}
inline int32_t activity_time(int32_t current,int32_t value) {return current==0?value:activity_min(current,value);}
// All operands must have been decoded before calling this mutator. Invalid
// indexed commands refuse before touching any field; the caller owns rollback
// of the VM instruction pointer, not replay of native engine side effects.
inline bool update_activity_statistic(ActivityStatistics& stats,ActivityStatistic stat,const std::array<int32_t,3>& p) {
    auto index=[&](auto& values,bool time) {
        if(p[0]<0 || std::size_t(p[0])>=values.size())return false;
        auto& value=values[std::size_t(p[0])];value=time?activity_time(value,p[1]):activity_min(value,p[1]);return true;
    };
    switch(stat) {
    case ActivityStatistic::industrial_passed:stats.industrial_passed=p[0]!=0;break;
    case ActivityStatistic::commercial_passed:stats.commercial_passed=p[0]!=0;break;
    case ActivityStatistic::suburban_passed:stats.suburban_passed=p[0]!=0;break;
    case ActivityStatistic::cars_sold:stats.cars_sold=activity_sum(stats.cars_sold,1);break;
    case ActivityStatistic::car_sales_money:stats.car_sales_money=activity_sum(stats.car_sales_money,p[0]);break;
    case ActivityStatistic::bikes_sold:stats.bikes_sold=activity_sum(stats.bikes_sold,1);break;
    case ActivityStatistic::bike_sales_money:stats.bike_sales_money=activity_sum(stats.bike_sales_money,p[0]);break;
    case ActivityStatistic::trash_money:stats.trash_money=activity_sum(stats.trash_money,p[0]);break;
    case ActivityStatistic::hitmen_killed:stats.hitmen_killed=activity_sum(stats.hitmen_killed,1);break;
    case ActivityStatistic::guardian_missions:stats.guardian_missions=activity_sum(stats.guardian_missions,1);break;
    // Retail consumes one parameter but its actual helper at 231B88 increments
    // the counter by one. It does not compute the maximum suggested by its name.
    case ActivityStatistic::guardian_justice:stats.guardian_justice=activity_sum(stats.guardian_justice,1);break;
    case ActivityStatistic::bandit_lap:return index(stats.bandit_laps,true);
    case ActivityStatistic::bandit_position:return index(stats.bandit_positions,false);
    case ActivityStatistic::train_time_left:
        stats.train_time_left=stats.train_time_left==0?p[0]:activity_max(stats.train_time_left,p[0]);break;
    case ActivityStatistic::street_time:return index(stats.street_times,true);
    case ActivityStatistic::street_lap:return index(stats.street_laps,true);
    case ActivityStatistic::street_position:return index(stats.street_positions,false);
    case ActivityStatistic::carnage_seconds:stats.carnage_seconds=activity_max(stats.carnage_seconds,p[0]);break;
    case ActivityStatistic::rc_triad_kills:stats.rc_triad_kills=activity_max(stats.rc_triad_kills,p[0]);break;
    case ActivityStatistic::slash_level:stats.slash_level=activity_max(stats.slash_level,p[0]);break;
    case ActivityStatistic::slash_money:stats.slash_money=activity_sum(stats.slash_money,p[0]);break;
    case ActivityStatistic::slash_kills:stats.slash_kills=activity_sum(stats.slash_kills,p[0]);break;
    case ActivityStatistic::dirt_lap:return index(stats.dirt_laps,true);
    case ActivityStatistic::dirt_time:return index(stats.dirt_times,true);
    case ActivityStatistic::dirt_air_time:stats.dirt_air_time=activity_max(stats.dirt_air_time,p[0]);break;
    case ActivityStatistic::gogo_time:stats.gogo_time=activity_time(stats.gogo_time,p[0]);break;
    case ActivityStatistic::guardian_levels:
        for(std::size_t i=0;i<3;++i)stats.guardian_levels[i]=activity_max(stats.guardian_levels[i],p[i]);break;
    case ActivityStatistic::unlocked_costume: {
        // The original handler passes an unsigned byte. The helper accepts
        // only its lower 16 costume IDs and leaves duplicates/outliers alone.
        const uint8_t costume=uint8_t(p[0]);
        if(costume<16)stats.unlocked_costumes|=uint16_t(1u<<costume);
        break;
    }
    case ActivityStatistic::face_plant_distance:
        stats.face_plant_distance_bits=activity_float_max(stats.face_plant_distance_bits,uint32_t(p[0]));break;
    case ActivityStatistic::scrapyard_score:
        stats.scrapyard_score_bits=activity_float_max(stats.scrapyard_score_bits,activity_float_from_int(p[0]));break;
    case ActivityStatistic::mayhem_score:
        stats.mayhem_score_bits=activity_float_max(stats.mayhem_score_bits,activity_float_from_int(p[0]));break;
    case ActivityStatistic::scooter_score:
        stats.scooter_score_bits=activity_float_max(stats.scooter_score_bits,activity_float_from_int(p[0]));break;
    case ActivityStatistic::wichita_score:
        stats.wichita_score_bits=activity_float_max(stats.wichita_score_bits,activity_float_from_int(p[0]));break;
    case ActivityStatistic::pizzas_delivered:
        stats.pizzas_delivered_bits=activity_float_add_int(stats.pizzas_delivered_bits,p[0]);break;
    case ActivityStatistic::noodles_delivered:
        stats.noodles_delivered_bits=activity_float_add_int(stats.noodles_delivered_bits,p[0]);break;
    case ActivityStatistic::tourist_money:
        stats.tourist_money_bits=activity_float_add_int(stats.tourist_money_bits,p[0]);break;
    case ActivityStatistic::tourist_spots:
        stats.tourist_spots_bits=activity_float_add_int(stats.tourist_spots_bits,p[0]);break;
    default:return false;
    }
    return true;
}
}
