#pragma once
#include <array>
#include <filesystem>
#include <vector>
#include <cstdint>

namespace lcs {
// Bind the current native callbacks of a verified converted ped clump.
// The original renderer is forwarded; only its cull state is scoped.
bool bind_sa_skin_double_sided(std::uintptr_t clump);
// Apply the approved map atomic draw policy to a private story-overlay CObject.
bool bind_sa_world_overlay_culling(std::uintptr_t entity);
struct WorldRenderRule { bool owned=false,lod=false; float minimum=0; int related=-1; unsigned levels=31; bool physical=false; };
inline unsigned preview_physical_flags(unsigned flags,const WorldRenderRule& rule) {
    return rule.physical && !rule.lod ? (flags|1u)&~(1u<<8) : flags;
}
template<class Remove,class Add>
inline void preview_apply_physical_membership(unsigned& flags,const WorldRenderRule& rule,
        bool was_big,bool dummy_pool,bool promoted_dummy,bool lod_linked,Remove remove,Add add) {
    // Population detaches an owned CDummyObject from CSector::Dummies while
    // leaving it allocated. Keep it detached and preserve its BIG flag until
    // native demotion; all other physical entities still receive collision flags.
    if(dummy_pool && promoted_dummy)return;
    // Relink only an entity that is actually in SA's LOD lists. An entity
    // detached by its owner (story overlay, population) stays detached: an
    // Add here would be repeated by that owner's own re-Add, leaving a second
    // CSector node that CWorld::ShutDown dispatches after deleting the entity.
    const bool relink=was_big && lod_linked;
    if(relink)remove();
    flags=preview_physical_flags(flags,rule);
    if(relink)add();
}
struct IslandZone { int level; std::array<float,6> bounds; };
inline int preview_level(const std::vector<IslandZone>& zones,const float* position) {
    for(size_t i=1;i<zones.size();++i) {
        const auto& z=zones[i];bool inside=true;
        for(int j=0;j<3;++j)inside&=position[j]>=z.bounds[j] && position[j]<=z.bounds[j+3];
        if(inside)return z.level;
    }
    return zones.empty()?0:zones[0].level;
}
inline bool preview_lod_visible(const WorldRenderRule& rule,int level,float distance,float multiplier,bool near_loaded) {
    if(!(rule.levels&(1u<<level)))return false;
    // LCS SetupBigBuildingVisibility: hide inside the minimum range only
    // when no related model exists or its near atomic has finished loading.
    return !(rule.lod && distance<rule.minimum*multiplier && distance<330.f &&
             (rule.related<0 || near_loaded));
}
class SaWorldPreview {
    std::array<WorldRenderRule,20000> rules_{};
    std::vector<IslandZone> zones_;
public:
    explicit SaWorldPreview(const std::filesystem::path& directory);
    bool service();
    std::uint64_t double_sided_draws() const;
    bool culling_healthy() const;
};
}
