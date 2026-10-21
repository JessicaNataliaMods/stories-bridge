#include "sa_world_sectors.hpp"
#include <algorithm>
#include <cmath>
#include <fstream>
#include <iterator>
#include <limits>
#include <sstream>
#include <stdexcept>
#include <unordered_map>
#include <unordered_set>

namespace lcs {
namespace {
struct RawPair { uint8_t off, on; };
struct RawSectorTerm {
    RawPair on;
    uint8_t on_count;
    RawPair off;
    uint8_t off_count;
};
struct RawSectorLayer {
    char pool;
    uint32_t source_index;
    int32_t source_model;
    const char* asset;
    bool mounted_default;
    bool story_mutable;
    std::array<float,3> position;
    std::array<float,4> quaternion;
    bool has_instance;
    RawPair instance;
    uint8_t resource_count;
    std::array<RawPair,2> resources;
    uint8_t interior_count;
    std::array<RawPair,2> interiors;
    uint8_t sector_term_count;
    std::array<RawSectorTerm,1> sector_terms;
    uint8_t atomic_lod;
};
#include "world_sector_layers.inc"

static constexpr int32_t story_slots[] = {1,9,10,11,12,14,15,26,30,31};

NativeResult success(bool condition=true, bool entered=false) {
    NativeResult result;
    result.executed=true;
    result.condition=condition;
    result.native_entered=entered;
    return result;
}
NativeResult failure(const std::string& message, bool entered=false) {
    NativeResult result;
    result.diagnostic=message;
    result.native_entered=entered;
    return result;
}

bool close_float(float a,float b) {
    return std::isfinite(a) && std::isfinite(b) && std::fabs(a-b)<=0.001f;
}
bool same_transform(const SaWorldSectorTransform& a,const SaWorldSectorTransform& b) {
    for(size_t i=0;i<a.position.size();++i)if(!close_float(a.position[i],b.position[i]))return false;
    for(size_t i=0;i<a.quaternion.size();++i)if(!close_float(a.quaternion[i],b.quaternion[i]))return false;
    return true;
}

bool pair_visible(const RawPair& pair,uint8_t hour,const std::map<int32_t,int32_t>& states) {
    if(pair.off==255)return false;
    if(pair.off&128) {
        const int end=pair.off&127;
        const int start=pair.on;
        if(end>=24 || start>=24)return false;
        return start<end ? start<=hour && hour<end : hour>=start || hour<end;
    }
    const auto found=states.find(pair.off);
    const int32_t state=found==states.end()?0:found->second;
    return state==pair.on;
}

bool layer_visible(const RawSectorLayer& row,uint8_t hour,const std::map<int32_t,int32_t>& states) {
    if(row.has_instance && !pair_visible(row.instance,hour,states))return false;
    if(row.resource_count) {
        bool resource=false;
        for(size_t i=0;i<row.resource_count;++i)resource|=pair_visible(row.resources[i],hour,states);
        if(!resource)return false;
    }
    if(row.interior_count) {
        bool interior=false;
        for(size_t i=0;i<row.interior_count;++i) {
            const auto found=states.find(row.interiors[i].off);
            const int32_t state=found==states.end()?0:found->second;
            interior|=state==row.interiors[i].on;
        }
        if(!interior)return false;
    }
    if(row.sector_term_count) {
        bool selected=false;
        for(size_t t=0;t<row.sector_term_count;++t) {
            const auto& term=row.sector_terms[t];
            bool term_on=true,term_off=true;
            if(term.on_count) {
                const auto found=states.find(term.on.off);
                const int32_t state=found==states.end()?0:found->second;
                term_on&=state==term.on.on;
            }
            if(term.off_count) {
                const auto found=states.find(term.off.off);
                const int32_t state=found==states.end()?0:found->second;
                term_off&=state!=term.off.on;
            }
            selected|=term_on && term_off;
        }
        if(!selected)return false;
    }
    return true;
}

void read_binding_file(const std::filesystem::path& path,
                       std::unordered_map<std::string,uint16_t>& models) {
    std::ifstream file(path);
    if(!file)throw std::runtime_error("Cannot open mounted world model-bindings.tsv for sector validation");
    std::string line;
    size_t rows=0;
    while(std::getline(file,line)) {
        if(line.empty())continue;
        std::istringstream row(line);
        std::string original,target,asset,native_name;
        if(!std::getline(row,original,'\t') || !std::getline(row,target,'\t') ||
           !std::getline(row,asset,'\t') || !std::getline(row,native_name,'\t'))
            throw std::runtime_error("Malformed mounted world model-bindings.tsv row");
        const long id=std::stol(target);
        if(id<10000 || id>=20000 || asset.empty() || native_name.empty())
            throw std::runtime_error("Invalid mounted world binding in sector profile");
        const auto inserted=models.emplace(asset,static_cast<uint16_t>(id));
        if(!inserted.second && inserted.first->second!=id)
            throw std::runtime_error("Ambiguous mounted world alias in sector profile: "+asset);
        ++rows;
    }
    if(!file.eof() || rows==0)throw std::runtime_error("Invalid mounted world model-bindings.tsv");
}

std::map<int32_t,int32_t> merge_states(const std::map<int32_t,int32_t>& base,
                                       const std::map<int32_t,int32_t>& staged) {
    auto merged=base;
    for(const auto& [group,state]:staged)merged[group]=state;
    return merged;
}
}

struct SaWorldSectors::Layer {
    const RawSectorLayer* raw=nullptr;
    uint16_t model=0;
    SaWorldSectorTransform transform;
};

SaWorldSectors::SaWorldSectors(const std::filesystem::path& directory,
                               const std::vector<SaWorldSectorModelBinding>& world_bindings,
                               SaWorldSectorEngine& engine)
    : directory_(directory),engine_(engine) {
    if(!engine_.epoch || !engine_.hour || !engine_.request_model || !engine_.model_ready ||
       !engine_.find_buildings || !engine_.building_live || !engine_.remove_building ||
       !engine_.restore_building || !engine_.prepare_object || !engine_.object_live ||
       !engine_.set_object_active || !engine_.destroy_object)
        throw std::runtime_error("Incomplete production SA world-sector engine profile");

    std::unordered_map<std::string,uint16_t> package_models;
    read_binding_file(directory_/"model-bindings.tsv",package_models);
    std::unordered_map<std::string,uint16_t> passed_models;
    for(const auto& binding:world_bindings) {
        if(binding.asset.empty() || binding.model<10000 || binding.model>=20000)
            throw std::runtime_error("Invalid supplied world-sector binding");
        const auto inserted=passed_models.emplace(binding.asset,binding.model);
        if(!inserted.second && inserted.first->second!=binding.model)
            throw std::runtime_error("Conflicting supplied world-sector binding: "+binding.asset);
    }

    layers_.reserve(std::size(raw_sector_layers));
    for(const auto& raw:raw_sector_layers) {
        const auto passed=passed_models.find(raw.asset);
        const auto packaged=package_models.find(raw.asset);
        if(passed==passed_models.end() || packaged==package_models.end() || passed->second!=packaged->second)
            throw std::runtime_error(std::string("Conditional sector alias is not bound by the mounted world: ")+raw.asset);
        if(raw.pool!='b' && raw.pool!='t')throw std::runtime_error("Invalid conditional source pool in sector table");
        if(raw.resource_count>raw.resources.size() || raw.interior_count>raw.interiors.size() ||
           raw.sector_term_count>raw.sector_terms.size() || raw.atomic_lod!=0)
            throw std::runtime_error("Invalid generated conditional sector record");
        for(size_t i=0;i<raw.sector_term_count;++i)
            if(raw.sector_terms[i].on_count>1 || raw.sector_terms[i].off_count>1)
                throw std::runtime_error("Generated sector alternatives exceed the represented predicate width");
        Layer layer;
        layer.raw=&raw;
        layer.model=passed->second;
        layer.transform.position=raw.position;
        layer.transform.quaternion=raw.quaternion;
        layers_.push_back(layer);
    }
    if(layers_.size()!=229)throw std::runtime_error("Conditional sector table version/count mismatch");
    const auto managed=std::count_if(layers_.begin(),layers_.end(),[](const Layer& layer){return layer.raw->story_mutable;});
    if(managed!=143)throw std::runtime_error("Conditional story-layer table version/count mismatch");
    applied_.reserve(layers_.size());
    for(const auto& layer:layers_)applied_.push_back(layer.raw->mounted_default);
    originals_.resize(layers_.size());
    overlays_.resize(layers_.size());
}

SaWorldSectors::~SaWorldSectors() = default;

bool SaWorldSectors::synchronize_epoch(std::string& diagnostic) {
    const auto current=engine_.epoch();
    if(current==0) { diagnostic="SA world-sector epoch is not initialized";return false; }
    if(world_epoch_==current)return true;
    world_epoch_=current;
    std::fill(applied_.begin(),applied_.end(),false);
    for(size_t i=0;i<layers_.size();++i)applied_[i]=layers_[i].raw->mounted_default;
    for(auto& ref:originals_)ref.reset();
    for(auto& ref:overlays_)ref.reset();
    committed_states_.clear();staged_states_.clear();plan_.reset();requested_models_.clear();
    faulted_=false;uncertain_ownership_=false;asynchronous_native_entry_=false;fault_diagnostic_.clear();
    uncertain_building_membership_=false;
    unresolved_overlay_ownership_=false;
    state_revision_=0;
    return true;
}

bool SaWorldSectors::rebuild_plan(uint8_t hour,bool& native_entered,std::string& diagnostic) {
    if(hour>=24) { diagnostic="Native clock hour is outside 0..23";return false; }
    Plan next;
    next.world_epoch=world_epoch_;
    next.state_revision=state_revision_;
    next.hour=hour;
    next.desired.reserve(layers_.size());
    std::set<uint16_t> required;
    const auto states=merge_states(committed_states_,staged_states_);
    for(size_t i=0;i<layers_.size();++i) {
        const auto& layer=layers_[i];
        // Pure time-of-day TOBJ rows
        // remain under SA's mounted-world streaming. Only proven story slots
        // participate in the overlay transaction.
        const bool desired=layer.raw->story_mutable?layer_visible(*layer.raw,hour,states):applied_[i];
        next.desired.push_back(desired);
        // Converted default IPL CBuildings remain owned by SA's normal streamer.
        // Only alternate layers need a CObject and a pinned DFF/TXD/COL request.
        if(desired && !layer.raw->mounted_default)required.insert(layer.model);
    }
    next.models.assign(required.begin(),required.end());
    plan_=std::move(next);
    for(const auto model:plan_->models) {
        if(requested_models_.count(model))continue;
        const auto result=engine_.request_model(model);
        native_entered|=result.native_entered;
        if(!result.executed) {
            diagnostic=result.diagnostic.empty()?"Native SA model request failed":result.diagnostic;
            if(result.native_entered) {
                faulted_=true;uncertain_ownership_=true;fault_diagnostic_=diagnostic;
            }
            return false;
        }
        native_entered=true;
        requested_models_.insert(model);
    }
    return true;
}

std::optional<bool> SaWorldSectors::check_plan_models() const {
    if(!plan_)return std::nullopt;
    for(const auto model:plan_->models) {
        const auto ready=engine_.model_ready(model);
        if(!ready)return std::nullopt;
        if(!*ready)return false;
    }
    return true;
}

NativeResult SaWorldSectors::set(int32_t group,int32_t state) {
    std::string diagnostic;
    if(!synchronize_epoch(diagnostic))return failure(diagnostic);
    if(faulted_)return failure(fault_diagnostic_,uncertain_ownership_ || asynchronous_native_entry_);
    if(group<0 || group>127)return failure("World-sector state group is outside the supported source range");
    // Keep valid legacy SWAP_BUILDINGS groups flowing to NativeAdapter's
    // individual model-swap helper. Groups without conditional map predicates
    // are deliberately not retained by this overlay state machine.
    if(std::find(std::begin(story_slots),std::end(story_slots),group)==std::end(story_slots))
        return success(true,false);
    const auto current=committed_states_.find(group);
    if(current!=committed_states_.end() && current->second==state)staged_states_.erase(group);
    else staged_states_[group]=state;
    ++state_revision_;
    return success(true,false);
}

NativeResult SaWorldSectors::request() {
    std::string diagnostic;
    if(!synchronize_epoch(diagnostic))return failure(diagnostic);
    if(faulted_)return failure(fault_diagnostic_,uncertain_ownership_ || asynchronous_native_entry_);
    const auto hour=engine_.hour();
    if(!hour)return failure("Native SA clock is unavailable for conditional world layers");
    bool entered=false;
    if(!plan_ || plan_->world_epoch!=world_epoch_ || plan_->state_revision!=state_revision_ || plan_->hour!=*hour) {
        if(!rebuild_plan(*hour,entered,diagnostic))return failure(diagnostic,entered);
    }
    const auto ready=check_plan_models();
    if(!ready)return failure("Cannot verify requested model, TXD and collision residency",entered);
    return success(*ready,entered);
}

std::optional<bool> SaWorldSectors::complete() {
    std::string diagnostic;
    if(!synchronize_epoch(diagnostic))return std::nullopt;
    if(faulted_)return std::nullopt;
    const auto hour=engine_.hour();
    if(!hour)return std::nullopt;
    bool entered=false;
    if(!plan_ || plan_->world_epoch!=world_epoch_ || plan_->state_revision!=state_revision_ || plan_->hour!=*hour) {
        if(!rebuild_plan(*hour,entered,diagnostic)) {
            if(!diagnostic.empty()) { faulted_=true;fault_diagnostic_=diagnostic; }
            return std::nullopt;
        }
    }
    const auto ready=check_plan_models();
    if(!ready)return std::nullopt;
    if(!*ready)return false;
    for(size_t i=0;i<layers_.size();++i) {
        if(!layers_[i].raw->story_mutable || !plan_->desired[i] || layers_[i].raw->mounted_default)continue;
        auto& object=overlays_[i];
        if(object) {
            if(object->world_epoch!=world_epoch_ || object->model!=layers_[i].model ||
               object->script_handle<0 || !object->address) {
                faulted_=true;uncertain_ownership_=true;unresolved_overlay_ownership_=true;
                fault_diagnostic_="Prepared conditional overlay has an invalid retained pool receipt";
                return std::nullopt;
            }
            if(!engine_.object_live(*object)) {
                if(applied_[i]) {
                    faulted_=true;uncertain_ownership_=true;
                    fault_diagnostic_="Active conditional overlay no longer matches its generation-bearing SA object receipt";
                    return std::nullopt;
                }
                // object_live also checks pose. destroy_object checks the
                // retained handle, model and pool generation without pose, so
                // a prepared object with a bad transform can still be retired
                // safely instead of losing its ownership receipt.
                const auto retired=engine_.destroy_object(*object);
                asynchronous_native_entry_|=retired.native_entered || retired.executed;
                if(!retired.executed) {
                    faulted_=true;uncertain_ownership_=retired.native_entered;
                    fault_diagnostic_=retired.diagnostic.empty()?"Cannot retire stale prepared conditional overlay":retired.diagnostic;
                    return std::nullopt;
                }
                object.reset();
            }
        }
        if(object)continue;
        const auto receipt=engine_.prepare_object(layers_[i].model,layers_[i].transform);
        asynchronous_native_entry_|=receipt.result.native_entered;
        if(receipt.object) {
            object=receipt.object;
            // A receipt proves the CObject allocation happened even if a
            // callback did not propagate its dispatcher-entry bit.
            asynchronous_native_entry_=true;
        }
        if(!receipt.result.executed || !object) {
            faulted_=true;
            fault_diagnostic_=receipt.result.diagnostic.empty()?"Could not prepare hidden native conditional overlay":receipt.result.diagnostic;
            if((receipt.result.native_entered || receipt.result.executed) && !receipt.object) {
                uncertain_ownership_=true;
                unresolved_overlay_ownership_=true;
            }
            return std::nullopt;
        }
        if(object->world_epoch!=world_epoch_ || object->model!=layers_[i].model ||
           object->script_handle<0 || !object->address || !same_transform(object->transform,layers_[i].transform) ||
           !engine_.object_live(*object)) {
            faulted_=true;fault_diagnostic_="Prepared conditional overlay does not match the requested model, transform and object-pool generation";
            unresolved_overlay_ownership_=true;
            return std::nullopt;
        }
    }
    return true;
}

NativeResult SaWorldSectors::rollback(const std::vector<std::pair<size_t,bool>>& building_ops,
                                     const std::vector<std::pair<size_t,bool>>& object_ops,
                                     bool entered,const std::string& cause) {
    bool rollback_ok=true;
    // Invert confirmed object visibility/collision mutations first, then CWorld
    // list mutations. An operation that failed after native entry is deliberately
    // absent from these journals: its final state is unknown and cannot be replayed.
    for(auto it=object_ops.rbegin();it!=object_ops.rend();++it) {
        const auto index=it->first;const bool was_active=it->second;
        if(!overlays_[index] || !engine_.object_live(*overlays_[index])) { rollback_ok=false;continue; }
        const auto undone=engine_.set_object_active(*overlays_[index],was_active);
        entered|=undone.native_entered || undone.executed;
        rollback_ok&=undone.executed;
    }
    for(auto it=building_ops.rbegin();it!=building_ops.rend();++it) {
        const auto index=it->first;const bool was_present=it->second;
        if(!originals_[index] || !engine_.building_live(*originals_[index])) {
            rollback_ok=false;uncertain_building_membership_=true;uncertain_ownership_=true;continue;
        }
        const auto undone=was_present?engine_.restore_building(*originals_[index]):engine_.remove_building(*originals_[index]);
        entered|=undone.native_entered || undone.executed;
        rollback_ok&=undone.executed;
        if(!undone.executed) {
            // The prior journaled mutation changed CWorld membership. If its
            // inverse cannot be confirmed, the applied-state ledger is stale;
            // cleanup must retain/quarantine rather than issue another Add.
            uncertain_building_membership_=true;
            uncertain_ownership_=true;
        }
    }
    if(!rollback_ok) {
        faulted_=true;fault_diagnostic_="Conditional sector transaction failed and could not restore its previous CWorld/object-pool state: "+cause;
        return failure(fault_diagnostic_,true);
    }
    return failure(cause,entered);
}

NativeResult SaWorldSectors::commit() {
    std::string diagnostic;
    if(!synchronize_epoch(diagnostic))return failure(diagnostic);
    if(faulted_)return failure(fault_diagnostic_,uncertain_ownership_ || asynchronous_native_entry_);
    // A repeated commit is finished, not an instruction that waits forever.
    if(!plan_ && staged_states_.empty())return success(true,false);
    // Rebuild stale plans here too: time can cross an hour boundary after1591
    // passed and before1592 executes. Waiting on that stale plan cannot resolve.
    const auto prepared=complete();
    if(!prepared)return failure(fault_diagnostic_.empty()?"Conditional world entities are not readable":fault_diagnostic_,
                                asynchronous_native_entry_);
    if(!*prepared)return success(false,false);

    // Resolve and revalidate every entity before the first CWorld mutation.
    // This binds original IPL buildings by exact model+position+quaternion and
    // the engine's pool generation/flags snapshot, never by nearest distance.
    for(size_t i=0;i<layers_.size();++i) {
        if(!layers_[i].raw->story_mutable)continue;
        if(plan_->desired[i]==applied_[i])continue;
        const auto& layer=layers_[i];
        if(layer.raw->mounted_default) {
            if(!originals_[i] && !applied_[i] && plan_->desired[i]) {
                faulted_=true;fault_diagnostic_="Default conditional building was removed without a retained pool-generation receipt";
                return failure(fault_diagnostic_,asynchronous_native_entry_);
            }
            if(!originals_[i] && applied_[i] && !plan_->desired[i]) {
                const auto found=engine_.find_buildings(layer.model,layer.transform);
                if(!found) return failure("Cannot inspect CBuilding pool for exact conditional IPL identity",asynchronous_native_entry_);
                if(found->size()!=1) return failure(found->empty()?"Default conditional IPL building is absent from the mounted CWorld":"Conditional IPL identity is ambiguous in the CBuilding pool",asynchronous_native_entry_);
                originals_[i]=found->front();
            }
            if(!originals_[i] && !applied_[i]) {
                faulted_=true;fault_diagnostic_="Conditional CBuilding ownership receipt is missing";
                return failure(fault_diagnostic_,asynchronous_native_entry_);
            }
            if(originals_[i] && (originals_[i]->world_epoch!=world_epoch_ ||
               originals_[i]->model!=layer.model || !same_transform(originals_[i]->transform,layer.transform) ||
               !engine_.building_live(*originals_[i])))
                return failure("Conditional CBuilding identity/generation changed before commit",asynchronous_native_entry_);
        } else if(plan_->desired[i]) {
            if(!overlays_[i] || overlays_[i]->world_epoch!=world_epoch_ ||
               overlays_[i]->model!=layer.model || !same_transform(overlays_[i]->transform,layer.transform) ||
               !engine_.object_live(*overlays_[i]))
                return failure("Prepared conditional CObject identity/generation changed before commit",asynchronous_native_entry_);
        } else if(applied_[i] && (!overlays_[i] || !engine_.object_live(*overlays_[i]))) {
            faulted_=true;fault_diagnostic_="Active conditional CObject overlay was lost before commit";
            return failure(fault_diagnostic_,true);
        }
    }

    std::vector<std::pair<size_t,bool>> building_ops,object_ops;
    bool entered=asynchronous_native_entry_;
    auto fail_and_rollback=[&](const std::string& why,bool operation_entered) {
        entered|=operation_entered;
        auto result=rollback(building_ops,object_ops,entered,why);
        if(!result.native_entered && entered)result.native_entered=true;
        return result;
    };
    auto apply_building=[&](size_t i,bool present)->std::optional<std::string> {
        const auto result=present?engine_.restore_building(*originals_[i]):engine_.remove_building(*originals_[i]);
        entered|=result.native_entered || result.executed;
        if(!result.executed) {
            if(result.native_entered) {
                faulted_=true;uncertain_ownership_=true;uncertain_building_membership_=true;
                fault_diagnostic_=result.diagnostic;
            }
            return result.diagnostic.empty()?"CWorld building mutation failed":result.diagnostic;
        }
        building_ops.emplace_back(i,!present);
        return std::nullopt;
    };
    auto apply_object=[&](size_t i,bool active)->std::optional<std::string> {
        const auto result=engine_.set_object_active(*overlays_[i],active);
        entered|=result.native_entered || result.executed;
        if(!result.executed) {
            if(result.native_entered) { faulted_=true;uncertain_ownership_=true;fault_diagnostic_=result.diagnostic; }
            return result.diagnostic.empty()?"CWorld CObject visibility/collision mutation failed":result.diagnostic;
        }
        object_ops.emplace_back(i,!active);
        return std::nullopt;
    };

    // Remove default IPL layers first, activate the selected overlay, then
    // retire old overlays or restore default layers. All operations are on the
    // game thread; no render can observe an intermediate state within this call.
    for(size_t i=0;i<layers_.size();++i)if(layers_[i].raw->story_mutable && layers_[i].raw->mounted_default && applied_[i] && !plan_->desired[i])
        if(const auto error=apply_building(i,false))return fail_and_rollback(*error,entered);
    for(size_t i=0;i<layers_.size();++i)if(layers_[i].raw->story_mutable && !layers_[i].raw->mounted_default && !applied_[i] && plan_->desired[i])
        if(const auto error=apply_object(i,true))return fail_and_rollback(*error,entered);
    for(size_t i=0;i<layers_.size();++i)if(layers_[i].raw->story_mutable && !layers_[i].raw->mounted_default && applied_[i] && !plan_->desired[i])
        if(const auto error=apply_object(i,false))return fail_and_rollback(*error,entered);
    for(size_t i=0;i<layers_.size();++i)if(layers_[i].raw->story_mutable && layers_[i].raw->mounted_default && !applied_[i] && plan_->desired[i])
        if(const auto error=apply_building(i,true))return fail_and_rollback(*error,entered);

    for(size_t i=0;i<layers_.size();++i)if(layers_[i].raw->story_mutable)applied_[i]=plan_->desired[i];
    committed_states_=merge_states(committed_states_,staged_states_);
    staged_states_.clear();
    ++state_revision_;
    plan_.reset();
    asynchronous_native_entry_=false;
    return success(true,entered);
}

NativeResult SaWorldSectors::cleanup() {
    const auto current=engine_.epoch();
    if(current==0)return failure("SA world-sector epoch is not initialized");
    if(world_epoch_!=0 && world_epoch_!=current) {
        // SA already retired the old pools/world. Do not touch stale pointers or
        // handles; initialize ownership against the new static IPL generation.
        world_epoch_=0;
        std::string diagnostic;
        if(!synchronize_epoch(diagnostic))return failure(diagnostic);
        return success(true,false);
    }
    std::string diagnostic;
    if(!synchronize_epoch(diagnostic))return failure(diagnostic);
    if(uncertain_building_membership_) {
        faulted_=true;
        if(fault_diagnostic_.empty())fault_diagnostic_="SA CWorld building membership is uncertain after a native mutation failure; retain ownership until world epoch reset";
        return failure(fault_diagnostic_,true);
    }
    bool entered=asynchronous_native_entry_;
    bool ok=!unresolved_overlay_ownership_;
    std::vector<size_t> restore_failures;
    for(size_t i=0;i<layers_.size();++i) {
        if(!layers_[i].raw->story_mutable || !layers_[i].raw->mounted_default || applied_[i])continue;
        if(!originals_[i] || originals_[i]->world_epoch!=world_epoch_ || !engine_.building_live(*originals_[i])) {
            ok=false;restore_failures.push_back(i);continue;
        }
        const auto result=engine_.restore_building(*originals_[i]);
        entered|=result.native_entered || result.executed;
        if(result.executed)applied_[i]=true;
        else {
            ok=false;restore_failures.push_back(i);
            if(result.native_entered) {
                uncertain_building_membership_=true;uncertain_ownership_=true;faulted_=true;
                fault_diagnostic_=result.diagnostic.empty()?"CWorld building restore entered SA and left membership uncertain":result.diagnostic;
                return failure(fault_diagnostic_,true);
            }
        }
    }
    if(!restore_failures.empty()) {
        // Leave all alternate entities owned and visible until original IPL
        // content can be restored without risking a blank or duplicated source.
        faulted_=true;fault_diagnostic_="Cannot restore one or more original CBuilding pool identities during cleanup";
        return failure(fault_diagnostic_,entered);
    }
    for(size_t i=0;i<layers_.size();++i) {
        if(!layers_[i].raw->story_mutable || layers_[i].raw->mounted_default || !overlays_[i])continue;
        auto& object=*overlays_[i];
        if(object.world_epoch!=world_epoch_) {
            // Epoch mismatch is only discarded by the branch above after the
            // whole old SA pool generation is known to have been retired.
            ok=false;
            continue;
        }
        const bool live=engine_.object_live(object);
        if(applied_[i] && live) {
            const auto inactive=engine_.set_object_active(object,false);
            entered|=inactive.native_entered || inactive.executed;
            if(!inactive.executed) {ok=false;continue;}
            applied_[i]=false;
        }
        // Even if pose validation failed, this receipt is still useful: the
        // production delete callback checks the object-pool generation/model
        // and can release the owned CObject without matching its transform.
        const auto removed=engine_.destroy_object(object);
        entered|=removed.native_entered || removed.executed;
        if(removed.executed) {overlays_[i].reset();applied_[i]=false;}
        else { ok=false; if(removed.native_entered)uncertain_ownership_=true; }
    }
    if(!ok) {
        faulted_=true;fault_diagnostic_="SA world-sector cleanup could not release every owned CObject overlay";
        return failure(fault_diagnostic_,entered);
    }
    std::fill(applied_.begin(),applied_.end(),false);
    for(size_t i=0;i<layers_.size();++i)applied_[i]=layers_[i].raw->mounted_default;
    committed_states_.clear();staged_states_.clear();plan_.reset();requested_models_.clear();
    faulted_=false;uncertain_ownership_=false;asynchronous_native_entry_=false;fault_diagnostic_.clear();
    uncertain_building_membership_=false;
    unresolved_overlay_ownership_=false;
    ++state_revision_;
    return success(true,entered);
}

} // namespace lcs
