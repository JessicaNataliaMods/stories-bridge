#pragma once
#include "continuous_sound.hpp"
#include "vm.hpp"
#include <cstring>

namespace lcs {
struct ContinuousEntityReceipt {bool entered=false,accepted=false;int32_t entity=-5;};
// Script lifetime boundary for retail402/403, bound to the campaign audio consumer.
// Backend ownership is keyed by the full token, including its internal epoch.
class ContinuousSoundAdapter {
    ContinuousSoundObjects objects_;
    std::optional<ContinuousSoundObjects::Snapshot> uncertain_;
public:
    std::function<bool()> available;
    std::function<bool()> music_available;
    std::function<ContinuousEntityReceipt(const ContinuousSoundObjects::Snapshot&)> create;
    std::function<ContinuousEntityReceipt(const ContinuousSoundObjects::Snapshot&)> release;
    static bool handles(uint16_t opcode){return opcode==402 || opcode==403;}
    const ContinuousSoundObjects& objects()const{return objects_;}
    bool quarantined()const{return uncertain_.has_value();}
    // Release must also resolve a create that entered without a confirmed
    // entity receipt. It may not manufacture success for that case.
    bool recover(){
        if(!uncertain_)return true;
        if(!release || !release(*uncertain_).accepted)return false;
        if(!objects_.erase_released(uncertain_->token))return false;
        uncertain_.reset();return true;
    }
    StepResult dispatch(Vm& vm,Thread& thread,uint16_t opcode){
        if(!handles(opcode))return StepResult::unsupported;
        if(uncertain_ || !available || !available() || !create || !release)
            return StepResult::unsupported;
        if(opcode==403){
            const auto object=objects_.find(vm.parameter(thread));
            if(!object)return StepResult::continued; // Original unresolved pool reference.
            uncertain_=object; // Retain even if an entered backend throws.
            const auto receipt=release(*object);
            if(!receipt.accepted){if(!receipt.entered)uncertain_.reset();return StepResult::unsupported;}
            objects_.erase_released(object->token);uncertain_.reset();
            return StepResult::continued;
        }
        std::array<float,3> position;
        for(auto& coordinate:position){const auto bits=vm.parameter(thread);std::memcpy(&coordinate,&bits,4);}
        const auto sound=vm.parameter(thread);
        // Parse and validate the destination on a copy before entering any
        // engine callback. Invalid script output must not create an orphan.
        auto preflight=thread;vm.variable(preflight);
        for(auto coordinate:position)if(!std::isfinite(coordinate))return StepResult::unsupported;
        if(continuous_sound_kind(sound)==ContinuousSoundKind::music_configuration && (!music_available || !music_available())){
            thread.diagnostic="Continuous music configuration requires the CITY MusicManager consumer";
            return StepResult::unsupported;
        }
        const auto object=objects_.allocate(sound,position);
        if(!object)return StepResult::unsupported;
        uncertain_=object;
        const auto receipt=create(*object);
        if(!receipt.accepted){
            if(!receipt.entered){objects_.erase_released(object->token);uncertain_.reset();}
            thread.diagnostic="Continuous sound selector "+std::to_string(int(uint8_t(uint16_t(sound))))+
                " or its audio backend is unavailable; no handle was published";
            return StepResult::unsupported;
        }
        objects_.attach_entity(object->token,receipt.entity);
        uncertain_=objects_.find(object->token.reference);
        vm.store(thread,object->token.reference);
        uncertain_.reset();return StepResult::continued;
    }
};
}
