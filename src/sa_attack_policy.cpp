#include "sa_attack_policy.hpp"
#include "lcs/route_vehicle_native.hpp"
#include "lcs/sa_executor.hpp"
#include <cmath>

namespace lcs {
namespace {
constexpr int32_t ped_type_cop=6;
constexpr uint8_t created_by_mission=2;
constexpr float cop_radius=120.0f;
}

std::optional<NativeAdapter::AttackPolicyActor> sa_attack_policy_actor(const ObjectiveActor& actor) {
    const auto live=sa_live_objective_actor(actor.handle);
    if(!live || !(*live==actor))return std::nullopt;
    const auto ped=sa_route_actor_vehicle_snapshot(actor);
    const auto native=sa_cop_snapshot(actor);
    if(!ped || !(ped->actor==actor) || !native || !(native->actor==actor))return std::nullopt;
    NativeAdapter::AttackPolicyActor facts;
    facts.alive=ped->alive;
    facts.on_foot=!ped->in_vehicle;
    facts.cop=native->native_type==ped_type_cop;
    facts.released=native->created_by!=created_by_mission && !native->removed;
    return facts;
}

std::optional<NativeAdapter::AttackPolicyWorld> sa_attack_policy_world() {
    const auto player_ped=execute_sa({0x01F5,{0},{},0,1});
    if(!player_ped.executed || player_ped.values.empty() || player_ped.values[0]<0)return std::nullopt;
    const auto player=sa_live_objective_actor(player_ped.values[0]);
    if(!player)return std::nullopt;
    const auto snapshot=sa_route_actor_vehicle_snapshot(*player);
    const auto position=sa_focused_player_position();
    if(!snapshot || !(snapshot->actor==*player) || !position)return std::nullopt;
    NativeAdapter::AttackPolicyWorld world;
    world.player_ped=player_ped.values[0];
    world.player_alive=snapshot->alive;
    const auto wanted=execute_sa({0x010F,{0,0},{},0,0,true});
    if(!wanted.executed)return std::nullopt;
    if(!wanted.condition || !world.player_alive)return world;
    const auto peds=sa_peds_descending();
    if(!peds)return std::nullopt;
    for(const auto& candidate:*peds) {
        const auto native=sa_cop_snapshot(candidate);
        if(!native || native->native_type!=ped_type_cop || native->removed)continue;
        const auto facts=sa_route_actor_vehicle_snapshot(candidate);
        if(!facts || !facts->alive)continue;
        const float dx=native->position[0]-(*position)[0],dy=native->position[1]-(*position)[1],dz=native->position[2]-(*position)[2];
        if(std::isfinite(dx+dy+dz) && dx*dx+dy*dy+dz*dz<=cop_radius*cop_radius){world.cops_present=true;break;}
    }
    return world;
}
}
