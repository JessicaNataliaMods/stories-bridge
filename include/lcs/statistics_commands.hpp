#pragma once
#include <cstdint>
#include <cstring>
#include <optional>

namespace lcs {
// These are real native counters with the same source meaning; no unrelated
// stat is borrowed to hide an unsupported original statistic.
enum class SourceStatistic:uint16_t {
    taxi_money=149,taxi_passengers=150,ambulance_lives=151,vigilante_criminals=152,
    extinguished_fires=153,vigilante_level=157,ambulance_level=158,fire_level=159
};
inline bool source_stat_record(SourceStatistic stat) {
    return stat==SourceStatistic::vigilante_level || stat==SourceStatistic::ambulance_level || stat==SourceStatistic::fire_level;
}
inline std::optional<uint16_t> source_stat_slot(SourceStatistic stat) {
    switch(stat) {
    case SourceStatistic::taxi_money:case SourceStatistic::taxi_passengers:case SourceStatistic::ambulance_lives:
    case SourceStatistic::vigilante_criminals:case SourceStatistic::extinguished_fires:
    case SourceStatistic::vigilante_level:case SourceStatistic::ambulance_level:case SourceStatistic::fire_level:
        return uint16_t(uint16_t(stat)-120);
    default:return {};
    }
}
inline int32_t source_stat_update(SourceStatistic stat,int32_t current,int32_t value) {
    if(source_stat_record(stat))return current>value?current:value;
    // Original 32-bit counter arithmetic, including negative taxi income.
    const uint32_t sum=uint32_t(current)+uint32_t(value);int32_t result;std::memcpy(&result,&sum,4);return result;
}
}
