// Directed test of the production conditional-layer transaction. The fixture
// implements only its callback contract, not a generic SA pool/VM simulator.
#include "../src/sa_world_sectors.hpp"

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <map>
#include <set>
#include <sstream>
#include <stdexcept>
#include <vector>

namespace {
using namespace lcs;

void check(bool value,const char* message) {
    if(!value)throw std::runtime_error(message);
}

NativeResult result(bool executed=true,bool condition=true,bool entered=true) {
    NativeResult value;
    value.executed=executed;value.condition=condition;value.native_entered=entered;
    return value;
}

bool same(const SaWorldSectorTransform& a,const SaWorldSectorTransform& b) {
    for(size_t i=0;i<a.position.size();++i)if(std::fabs(a.position[i]-b.position[i])>0.001f)return false;
    for(size_t i=0;i<a.quaternion.size();++i)if(std::fabs(a.quaternion[i]-b.quaternion[i])>0.001f)return false;
    return true;
}

struct Building { SaWorldSectorBuildingRef ref; bool present=true; };
struct Object { SaWorldSectorObjectRef ref; bool alive=true,active=false; };

struct World {
    uint64_t epoch=1;
    uint8_t hour=12;
    uint32_t next_building=1,next_object=1;
    bool refuse_delete=false;
    std::vector<Building> buildings;
    std::map<uint32_t,Object> objects;
    std::set<uint16_t> requested,ready;
    size_t building_changes=0,object_changes=0;

    bool building_live(const SaWorldSectorBuildingRef& ref) const {
        return std::any_of(buildings.begin(),buildings.end(),[&](const Building& row){
            return row.ref.address==ref.address && row.ref.slot==ref.slot &&
                row.ref.pool_generation==ref.pool_generation && row.ref.model==ref.model &&
                row.ref.world_epoch==ref.world_epoch;
        });
    }
    bool object_identity(const SaWorldSectorObjectRef& ref) const {
        const auto found=objects.find(ref.slot);
        return found!=objects.end() && found->second.alive &&
            found->second.ref.address==ref.address &&
            found->second.ref.pool_generation==ref.pool_generation &&
            found->second.ref.model==ref.model && found->second.ref.world_epoch==ref.world_epoch;
    }
    bool object_live(const SaWorldSectorObjectRef& ref) const {
        const auto found=objects.find(ref.slot);
        return object_identity(ref) && same(found->second.ref.transform,ref.transform);
    }
    SaWorldSectorEngine callbacks() {
        SaWorldSectorEngine engine;
        engine.epoch=[this]{return epoch;};
        engine.hour=[this]()->std::optional<uint8_t>{return hour;};
        engine.request_model=[this](uint16_t model){requested.insert(model);return result();};
        engine.model_ready=[this](uint16_t model)->std::optional<bool>{return ready.count(model)!=0;};
        engine.find_buildings=[this](uint16_t model,const SaWorldSectorTransform& transform)
            ->std::optional<std::vector<SaWorldSectorBuildingRef>> {
            std::vector<SaWorldSectorBuildingRef> found;
            for(const auto& row:buildings)
                if(row.ref.model==model && same(row.ref.transform,transform))found.push_back(row.ref);
            if(found.empty()) {
                SaWorldSectorBuildingRef ref;
                ref.address=0x200000u+next_building*0x40u;ref.slot=next_building++;
                ref.pool_generation=1;ref.model=model;ref.flags=129;
                ref.transform=transform;ref.world_epoch=epoch;
                buildings.push_back({ref,true});found.push_back(ref);
            }
            return found;
        };
        engine.building_live=[this](const auto& ref){return building_live(ref);};
        engine.remove_building=[this](const auto& ref){
            if(!building_live(ref))return result(false,false,false);
            for(auto& row:buildings)if(row.ref.slot==ref.slot)row.present=false;
            ++building_changes;return result();
        };
        engine.restore_building=[this](const auto& ref){
            if(!building_live(ref))return result(false,false,false);
            for(auto& row:buildings)if(row.ref.slot==ref.slot)row.present=true;
            ++building_changes;return result();
        };
        engine.prepare_object=[this](uint16_t model,const SaWorldSectorTransform& transform) {
            SaWorldSectorObjectReceipt receipt;
            const uint32_t slot=next_object++;
            SaWorldSectorObjectRef ref;
            ref.script_handle=static_cast<int32_t>((1u<<8)|slot);
            ref.address=0x300000u+slot*0x100u;ref.slot=slot;ref.pool_generation=1;
            ref.model=model;ref.transform=transform;ref.world_epoch=epoch;
            objects.emplace(slot,Object{ref,true,false});
            receipt.object=ref;receipt.result=result();++object_changes;
            return receipt;
        };
        engine.object_live=[this](const auto& ref){return object_live(ref);};
        engine.set_object_active=[this](const auto& ref,bool active){
            if(!object_live(ref))return result(false,false,false);
            objects.at(ref.slot).active=active;++object_changes;return result();
        };
        engine.destroy_object=[this](const auto& ref){
            if(!object_identity(ref) || refuse_delete)return result(false,false,false);
            objects.at(ref.slot).alive=false;++object_changes;return result();
        };
        return engine;
    }
};

std::vector<SaWorldSectorModelBinding> read_bindings(const std::filesystem::path& directory) {
    std::ifstream file(directory/"model-bindings.tsv");
    check(bool(file),"cannot read mounted model-bindings.tsv");
    std::vector<SaWorldSectorModelBinding> bindings;
    std::string line;
    while(std::getline(file,line)) {
        std::istringstream row(line);
        std::string original,target,asset,native_name;
        if(!std::getline(row,original,'\t') || !std::getline(row,target,'\t') ||
           !std::getline(row,asset,'\t') || !std::getline(row,native_name,'\t'))continue;
        bindings.push_back({asset,static_cast<uint16_t>(std::stoul(target))});
    }
    return bindings;
}

std::map<std::string,uint16_t> models_by_asset(const std::vector<SaWorldSectorModelBinding>& bindings) {
    std::map<std::string,uint16_t> result;
    for(const auto& binding:bindings)result.emplace(binding.asset,binding.model);
    return result;
}
} // namespace

int main(int argc,char** argv) {
    try {
        check(argc==2,"usage: world-sector-tests <mounted-data-lcs-directory>");
        const std::filesystem::path directory=argv[1];
        const auto bindings=read_bindings(directory);
        const auto models=models_by_asset(bindings);
        const uint16_t alternate_model=models.at("whouse_int_5bd38e");
        const uint16_t default_model=models.at("whouse_int_03b7a6");

        World world;
        auto engine=world.callbacks();
        // Construction cross-checks every generated alias against this actual
        // mounted v63 binding file, while transactions use real table predicates.
        SaWorldSectors sectors(directory,bindings,engine);

        const auto legacy_group=sectors.set(21,1);
        check(legacy_group.executed && legacy_group.condition && !legacy_group.native_entered,
              "valid legacy model-swap groups without a mapped sector predicate must remain accepted");

        // Slot 26 is a real source sector alternative: observatory_tem_07a449
        // is a mounted alias in the package, but its placement is not in the
        // selected default IPL and becomes visible only while state 26 is one.
        World observatory_world;
        auto observatory_engine=observatory_world.callbacks();
        SaWorldSectors observatory(directory,bindings,observatory_engine);
        const uint16_t observatory_model=models.at("observatory_tem_07a449");
        const auto observatory_stage=observatory.set(26,1);
        check(observatory_stage.executed && observatory_stage.condition && !observatory_stage.native_entered,
              "slot 26 should stage its source sector alternative");
        const auto observatory_pending=observatory.request();
        check(observatory_pending.executed && !observatory_pending.condition && observatory_pending.native_entered &&
              observatory_world.requested.count(observatory_model),
              "slot 26 should request its mounted alias and wait for residency");
        const auto observatory_wait=observatory.complete();
        check(observatory_wait.has_value() && !*observatory_wait,
              "slot 26 should remain pending until the alias dependencies are resident");
        for(const auto model:observatory_world.requested)observatory_world.ready.insert(model);
        const auto observatory_request=observatory.request();
        check(observatory_request.executed && observatory_request.condition,
              "slot 26 request should complete after model/TXD/COL residency");
        const auto observatory_ready=observatory.complete();
        check(observatory_ready.has_value() && *observatory_ready,
              "slot 26 should prepare the selected conditional entity");
        const auto observatory_forward=observatory.commit();
        check(observatory_forward.executed && observatory_forward.condition && observatory_forward.native_entered,
              "slot 26 commit should activate the selected alias");
        auto observatory_overlay=std::find_if(observatory_world.objects.begin(),observatory_world.objects.end(),
            [&](const auto& row){return row.second.ref.model==observatory_model && row.second.active;});
        check(observatory_overlay!=observatory_world.objects.end(),
              "slot 26 should expose the actual observatory_tem_07a449 conditional placement");
        const auto observatory_reverse_stage=observatory.set(26,0);
        check(observatory_reverse_stage.executed && observatory_reverse_stage.condition,
              "slot 26 reverse state should stage");
        const auto observatory_reverse_request=observatory.request();
        check(observatory_reverse_request.executed && observatory_reverse_request.condition,
              "slot 26 reverse request should need no new model residency");
        const auto observatory_reverse_ready=observatory.complete();
        check(observatory_reverse_ready.has_value() && *observatory_reverse_ready,
              "slot 26 reverse plan should be complete");
        const auto observatory_reverse=observatory.commit();
        check(observatory_reverse.executed && observatory_reverse.condition && observatory_reverse.native_entered &&
              !observatory_overlay->second.active,
              "slot 26 reverse commit should hide the alias again");
        check(observatory.cleanup().executed && !observatory_overlay->second.alive,
              "slot 26 cleanup should release its retained overlay");
        const auto staged=sectors.set(12,1);
        check(staged.executed && staged.condition && !staged.native_entered,"set must stage the story state without native mutation");
        const auto pending=sectors.request();
        check(pending.executed && !pending.condition && pending.native_entered && world.requested.count(alternate_model),
              "the slot-12 alternate alias should be requested and remain pending until resident");
        const auto waiting=sectors.complete();
        check(waiting.has_value() && !*waiting,"complete must wait while selected model dependencies are unavailable");
        for(const auto model:world.requested)world.ready.insert(model);
        const auto ready=sectors.request();
        check(ready.executed && ready.condition,"request should become ready after model/TXD/COL dependencies are resident");
        const auto prepared=sectors.complete();
        check(prepared.has_value() && *prepared,"complete should prepare the selected conditional entities");
        world.hour=13; // A clock rollover between predicate and commit must not deadlock1592.
        const auto forward=sectors.commit();
        check(forward.executed && forward.condition && forward.native_entered,"forward commit should mutate the native CWorld/object callbacks");

        auto original=std::find_if(world.buildings.begin(),world.buildings.end(),[&](const Building& row){return row.ref.model==default_model;});
        auto replacement=std::find_if(world.objects.begin(),world.objects.end(),[&](const auto& row){
            return row.second.ref.model==alternate_model && row.second.active;
        });
        check(original!=world.buildings.end() && !original->present,"forward commit should remove the mounted default CBuilding");
        check(replacement!=world.objects.end(),"forward commit should activate the story alias CObject");

        const auto reverse_stage=sectors.set(12,0);
        check(reverse_stage.executed && reverse_stage.condition,"reverse state should stage");
        const auto reverse_request=sectors.request();
        check(reverse_request.executed && reverse_request.condition,"reverse plan should reuse already resident models");
        const auto reverse_ready=sectors.complete();
        check(reverse_ready.has_value() && *reverse_ready,"reverse plan should be complete");
        const auto reverse=sectors.commit();
        check(reverse.executed && reverse.condition && reverse.native_entered,"reverse commit should restore the original world layer");
        check(original->present && !replacement->second.active,"reverse commit should restore default and hide its alternate");
        const auto stable_count=world.building_changes+world.object_changes;
        const auto idle=sectors.commit();
        check(idle.executed && idle.condition && world.building_changes+world.object_changes==stable_count,
              "commit without a staged plan should be idempotent");

        // Pose validation can fail while the exact generation/model receipt is
        // still known. A refused delete must retain that receipt for retry.
        auto& owned=world.objects.at(replacement->first);
        owned.ref.transform.position[0]+=1.0f;
        world.refuse_delete=true;
        const auto refused=sectors.cleanup();
        check(!refused.executed && owned.alive,"refused delete must preserve overlay ownership");
        world.refuse_delete=false;
        const auto cleaned=sectors.cleanup();
        check(cleaned.executed && cleaned.condition && cleaned.native_entered && !owned.alive,
              "cleanup retry should delete by retained pool generation/model even when pose differs");

        World rebuilt_world;
        SaWorldSectors before_load(directory,bindings,rebuilt_world.callbacks());
        check(before_load.set(26,1).executed && before_load.request().executed,"Epoch test stages owned overlay");
        rebuilt_world.ready=rebuilt_world.requested;
        check(before_load.request().condition && before_load.complete().value_or(false) && before_load.commit().executed,
              "Epoch test owns a native overlay before shutdown");
        const auto changes_before_load=rebuilt_world.object_changes+rebuilt_world.building_changes;
        ++rebuilt_world.epoch; // Actual CWorld shutdown boundary, not Campaign construction.
        const auto retired=before_load.cleanup();
        check(retired.executed && !retired.native_entered &&
              rebuilt_world.object_changes+rebuilt_world.building_changes==changes_before_load,
              "Retired world receipts must not mutate/delete recycled pool identities");
        std::cout<<"PASS: slot-26 source alternative, legacy group acceptance, slot-12 readiness/forward/reverse, idempotence, retained ownership and world retirement\n";
        return 0;
    } catch(const std::exception& error) {
        std::cerr<<error.what()<<'\n';
        return 1;
    }
}
