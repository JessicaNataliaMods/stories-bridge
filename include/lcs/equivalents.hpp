#pragma once
#include "native.hpp"
#include <istream>
namespace lcs {
struct EquivalentEntity {
    int32_t original=0,native=0;
    std::string name,native_name;
    ModelKind kind=ModelKind::object;
};
// This profile's object entries come from SA's WEAP section. Weapon model
// infos use class 4 even though pickups expose them as objects to scripts.
inline uint8_t equivalent_model_class(const EquivalentEntity& e) {
    return e.kind==ModelKind::ped ? 7 : e.kind==ModelKind::vehicle ? 6 : 4;
}
// SA's PEDS.IDE declares slot zero as "null". CREATE_PLAYER streams the
// special model "player" into that slot before the world bootstrap completes.
// Validate its live name, while keeping the original profile unchanged.
inline const std::string& equivalent_runtime_name(const EquivalentEntity& e) {
    static const std::string player="player";
    return e.kind==ModelKind::ped && e.original==0 && e.native==0 ? player : e.native_name;
}
// Read the complete explicit profile before applying any binding.
std::vector<EquivalentEntity> read_equivalents(std::istream&);
void bind_equivalents(NativeAdapter&,const std::vector<EquivalentEntity>&);
}
