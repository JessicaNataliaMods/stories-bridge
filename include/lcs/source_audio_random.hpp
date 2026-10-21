#pragma once
#include <array>
#include <cstdint>
namespace lcs {
// cAudioManager's shared random producer. The source regenerates a five-entry
// integer table from myrand() on every Service() frame and RandomDisplacement
// keeps two static values (Adjustment, bPos) across calls. The PS2 myrand
// (USE_PS2_RAND) is a 64-bit LCS: seed = 0x5851F42D4C957F2D*seed+1, result =
// (seed>>32)&0x7FFFFFFF. The seed itself is run-time state in the original, so
// the starting value here is a free choice; the arithmetic is the source's.
class SourceAudioRandom {
    uint64_t seed_;
    std::array<uint32_t,5> table_{};
    uint32_t adjustment_=0;
    bool positive_=true;
    uint32_t next() {
        seed_=0x5851F42D4C957F2Dull*seed_+1;
        return uint32_t((seed_>>32)&0x7FFFFFFFu);
    }
public:
    explicit SourceAudioRandom(uint64_t seed=0x4c4353u):seed_(seed){regenerate();}
    // GenerateIntegerRandomNumberTable: one new frame of the shared table.
    void regenerate() {for(auto& value:table_)value=next();}
    uint32_t table(size_t index)const {return table_.at(index);}
    // cAudioManager::RandomDisplacement(seed): zero seed gives zero; otherwise a
    // table-derived value below `seed` with a sign that flips on odd values.
    int32_t displacement(uint32_t seed) {
        if(seed==0)return 0;
        int32_t value=int32_t(table_[(adjustment_+seed)%5]%seed);
        adjustment_+=uint32_t(value);
        if(value%2)positive_=!positive_;
        return positive_?value:-value;
    }
};
}
