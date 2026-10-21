#pragma once
#include "owned_route_task.hpp"
#include "sa_executor.hpp"
#include "script_task_epochs.hpp"
namespace lcs {
struct OwnedRouteReaders {
    std::function<std::optional<ScriptTaskSnapshot>()> snapshot=sa_script_task_snapshot;
    std::function<std::optional<ObjectiveActor>(uint32_t)> actor;
    std::function<std::optional<uint64_t>(const CapturedScriptTask&,ObjectiveActor)> epoch=sa_owned_route_binding_epoch;
};
// A bank snapshot does not mint ownership. The actual private task constructor
// and native Clone/destructor chain supply a non-reusable lineage epoch. The
// resolver MUST check the registered native weak actor cell, not only the pool.
class OwnedRouteTaskHost {
    OwnedRouteReaders readers_;
    std::optional<EpochScriptTask> read(size_t slot,uint64_t expected,const ScriptTaskRecord* captured) {
        if(slot>=128 || !readers_.actor || !readers_.snapshot || !readers_.epoch)return {};
        const auto bank=readers_.snapshot();if(!bank)return {};
        const auto& record=(*bank)[slot];
        if(captured && !(record==*captured))return {};
        const auto actor=readers_.actor(record.ped);if(!actor)return {};
        CapturedScriptTask binding{slot,record};
        const auto epoch=readers_.epoch(binding,*actor);
        if(!epoch || !*epoch || (expected && *epoch!=expected))return {};
        const auto after=readers_.snapshot();const auto current=readers_.actor(record.ped);
        if(!after || !current || !(*current==*actor) || !((*after)[slot]==record) || readers_.epoch(binding,*actor)!=epoch)return {};
        return EpochScriptTask{binding,*epoch};
    }
public:
    explicit OwnedRouteTaskHost(OwnedRouteReaders readers):readers_(std::move(readers)){}
    std::optional<EpochScriptTask> accept(const CapturedScriptTask& captured) {
        try {return read(captured.slot,0,&captured.record);}catch(...){return {};}
    }
    std::optional<EpochScriptTask> refresh(const EpochScriptTask& expected) {
        try {
            if(!expected.epoch)return {};
            const auto current=read(expected.binding.slot,expected.epoch,nullptr);
            if(!current || current->binding.record.ped!=expected.binding.record.ped || current->binding.record.opcode!=expected.binding.record.opcode)return {};
            return current;
        }catch(...){return {};}
    }
};
}
