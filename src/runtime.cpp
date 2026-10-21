#include "lcs/runtime.hpp"
#include "ScriptCommands.h"
#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace lcs {
namespace {
uint32_t code_region_end(const Vm& vm,uint32_t address) {
    if(address<vm.main_size)return vm.main_size<=vm.memory.size()?vm.main_size:0;
    const uint64_t mission_end=uint64_t(vm.main_size)+vm.mission_size();
    if(vm.mission_size() && uint64_t(address)<mission_end && mission_end<=vm.memory.size())return uint32_t(mission_end);
    return 0;
}
bool skip_parameter_encoding(const Vm& vm,uint32_t& ip,uint32_t end) {
    if(ip>=end || ip>=vm.memory.size())return false;
    const auto kind=vm.memory[ip++];
    size_t extra=0;
    if(kind==0)return false;
    if(kind==1 || kind==2)extra=0;
    else if(kind==3 || kind==7)extra=1;
    else if(kind==4 || kind==8)extra=2;
    else if(kind==5)extra=3;
    else if(kind==6 || kind==9)extra=4;
    else if(kind>=230)extra=3;
    else if(kind>=204)extra=1;
    else if(kind>=108)extra=2;
    else if(kind>=10)extra=0;
    else return false;
    if(uint64_t(ip)+extra>end || uint64_t(ip)+extra>vm.memory.size())return false;
    ip+=uint32_t(extra);
    return true;
}
bool valid_plain_recovery_frame(const Vm& vm,uint32_t frame) {
    // Plain LCS frames are return addresses from GOSUB/GOSUB_FILE. Reject
    // packed function flags and addresses in allocated-but-unloaded padding.
    if(frame&(0xffc00000u))return false;
    const auto end=code_region_end(vm,frame);
    if(!end)return false;
    const auto begin=frame<vm.main_size?0u:vm.main_size;
    const auto earliest=frame>12?std::max(begin,frame-12):begin;
    for(uint32_t start=earliest;start+2<=frame;++start) {
        const auto op=uint16_t(read16(vm.memory,start)&0x7fff);
        if(op!=COMMAND_GOSUB && op!=COMMAND_GOSUB_FILE)continue;
        uint32_t cursor=start+2;
        if(!skip_parameter_encoding(vm,cursor,end))continue;
        if(op==COMMAND_GOSUB_FILE && !skip_parameter_encoding(vm,cursor,end))continue;
        if(cursor==frame)return true;
    }
    return false;
}
bool valid_function_recovery_frame(const Vm& vm,const Thread& thread,uint32_t frame) {
    const auto metadata=frame&((1u<<22)-1);
    if(metadata<2)return false;
    const auto region_end=code_region_end(vm,metadata-2);
    if(!region_end || uint64_t(metadata)+3>region_end)return false;
    const auto call_opcode=uint16_t(read16(vm.memory,metadata-2)&0x7fff);
    if(call_opcode!=COMMAND_CALL && call_opcode!=COMMAND_CALLNOT)return false;
    if(bool(frame&(1u<<22))!=(call_opcode==COMMAND_CALLNOT))return false;
    const auto inputs=vm.memory[metadata],outputs=vm.memory[metadata+1],offset=vm.memory[metadata+2];
    if(unsigned(inputs)+outputs+offset>96 || thread.locals_base<offset ||
       size_t(thread.locals_base)+inputs+outputs>96)return false;
    const uint64_t args_end=uint64_t(metadata)+3+(frame>>24);
    if(args_end>region_end)return false;
    uint32_t cursor=metadata+3;
    if(!skip_parameter_encoding(vm,cursor,region_end))return false; // CALL target
    for(unsigned i=0;i<inputs;++i)if(!skip_parameter_encoding(vm,cursor,region_end))return false;
    return cursor==args_end;
}
bool valid_recovery_frame(const Vm& vm,const Thread& thread) {
    if(!thread.stack_size || thread.stack_size>thread.stack.size() || thread.locals_base>96)return false;
    const auto frame=thread.stack[0];
    if(frame&(1u<<23))return valid_function_recovery_frame(vm,thread,frame);
    return valid_plain_recovery_frame(vm,frame);
}
bool active_code_address(const Vm& vm,uint32_t address) {
    return code_region_end(vm,address)!=0;
}
}
Runtime::Runtime(ScmImage image) : image_(std::move(image)), vm_(image_) {
    const uint64_t end=8ull+4ull*(image_.true_globals+image_.most_globals);
    if (end>image_.variable_space) throw std::runtime_error("Mission globals exceed variable space");
    vm_.backend=[this](Vm&,Thread& thread,uint16_t opcode) { return dispatch(thread,opcode); };
}
Script& Runtime::find(ScriptId id) {
    const auto p=std::find_if(scripts_.begin(),scripts_.end(),[id](const Script& s) { return s.id==id; });
    if (!id || p==scripts_.end()) throw std::runtime_error("Unknown or expired LCS script ID");
    return *p;
}
const Script& Runtime::script(ScriptId id) const { return const_cast<Runtime*>(this)->find(id); }
bool Runtime::is_mission_script(const Thread& thread) const {
    const auto found=std::find_if(scripts_.begin(),scripts_.end(),[&](const Script& s){return s.id && &s.thread==&thread;});
    if(found==scripts_.end())throw std::runtime_error("Native cleanup requires a managed LCS script");
    return found->mission;
}
Script& Runtime::owner(Thread& thread) {
    const auto p=std::find_if(scripts_.begin(),scripts_.end(),[&thread](Script& s) { return &s.thread==&thread; });
    if (p==scripts_.end() || !p->id) throw std::runtime_error("Scheduler command requires a managed LCS thread");
    return *p;
}
bool Runtime::uses_mission(const Script& s) const {
    if (s.resident || s.thread.ip>=vm_.main_size) return true;
    for (size_t i=0;i<s.thread.stack_size;++i) {
        const auto frame=s.thread.stack[i];
        const auto ip=(frame & (1u<<23)) ? frame & ((1u<<22)-1) : frame;
        if (ip>=vm_.main_size) return true;
    }
    return false;
}
const char* failure_scope_name(FailureScope scope) {
    switch(scope) {
    case FailureScope::thread:return "thread";
    case FailureScope::mission:return "mission";
    case FailureScope::initialization:return "initialization";
    }
    return "unknown";
}
std::optional<ScriptFailure> Runtime::failure(ScriptId id) const {
    const auto slot=std::find_if(scripts_.begin(),scripts_.end(),[id](const Script& s){return id && s.id==id;});
    if(slot==scripts_.end())return {};
    const auto& worker=*slot;
    if(!worker.blocked || !worker.thread.active || worker.processing ||
       (worker.result!=StepResult::unsupported && worker.result!=StepResult::failed))return {};
    ScriptFailure fault{id,FailureScope::thread,worker.result==StepResult::unsupported,uses_mission(worker),{}};
    if(worker.mission || fault.mission_code_leased)fault.scope=FailureScope::mission;
    if(worker.bootstrap)fault.scope=FailureScope::initialization;
    std::vector<ScriptId> waiting_on{id};
    for(size_t i=0;i<waiting_on.size();++i)for(const auto& parent:scripts_) {
        if(!parent.id || !parent.thread.active || parent.awaiting_initial_slice!=waiting_on[i] ||
           std::find(waiting_on.begin(),waiting_on.end(),parent.id)!=waiting_on.end())continue;
        fault.dependents.push_back(parent.id);waiting_on.push_back(parent.id);
        if(parent.mission && fault.scope==FailureScope::thread)fault.scope=FailureScope::mission;
        if(parent.bootstrap)fault.scope=FailureScope::initialization;
    }
    return fault;
}
std::vector<ScriptFailure> Runtime::failures() const {
    std::vector<ScriptFailure> result;
    for(const auto& worker:scripts_)if(worker.id)if(auto fault=failure(worker.id))result.push_back(std::move(*fault));
    return result;
}
bool Runtime::can_isolate_failure(ScriptId id) const {
    const auto fault=failure(id);return fault && fault->scope==FailureScope::thread;
}
bool Runtime::mission_buffer_in_use() const {
    return std::any_of(scripts_.begin(),scripts_.end(),[this](const Script& s){return s.id && s.thread.active && uses_mission(s);});
}
ScriptId Runtime::start(uint32_t ip,const std::vector<int32_t>& args,bool mission,bool resident,bool pending_mission) {
    if (args.size()>96) throw std::runtime_error("Too many LCS thread arguments");
    if ((pending_mission && ip!=vm_.main_size) ||
        (!pending_mission && ip>=uint64_t(vm_.main_size)+vm_.mission_size()))
        throw std::runtime_error("Script entry outside loaded code");
    const auto slot=std::find_if(scripts_.begin(),scripts_.end(),[](const Script& s) {
        return !s.id || (!s.thread.active && !s.processing);
    });
    if (slot==scripts_.end()) throw std::runtime_error("LCS script pool exhausted (128 threads)");
    if (!next_id_) throw std::runtime_error("LCS script ID overflow");
    const auto id=next_id_++;
    // Reserve the queue insertion before reusing a slot, to avoid a half-started
    // thread if allocation fails. Slot addresses remain stable during callbacks.
    active_.insert(active_.begin(),id);
    *slot=Script{}; slot->id=id; slot->thread.ip=ip;
    slot->mission=mission; slot->resident=resident || ip>=vm_.main_size;
    std::copy(args.begin(),args.end(),slot->thread.locals.begin());
    return id;
}
ScriptId Runtime::start_main() { const auto id=start_script(0);find(id).bootstrap=true;return id; }
ScriptId Runtime::start_script(uint32_t ip,const std::vector<int32_t>& args) { return start(ip,args,false,false); }
ScriptId Runtime::load_mission(size_t index) {
    if (image_.exclusive_missions) throw std::runtime_error("Exclusive mission selection not implemented");
    // A mission can leave helper threads using its functions. Keep the slot
    // leased until all its descendants end, including threads blocked on an adapter.
    for (const auto& s:scripts_)
        if (s.id && s.thread.active && uses_mission(s))
            throw std::runtime_error("Previous mission code is still used by an active LCS thread");
    const auto code=image_.mission(index);
    if (code.empty() || code.size()>vm_.memory.size()-vm_.main_size)
        throw std::runtime_error("Mission exceeds allocated script buffer");
    if (active_.size()==scripts_.size()) throw std::runtime_error("LCS script pool exhausted (128 threads)");
    // The image and global range were validated before any active state changes.
    const auto id=start(vm_.main_size,{},true,true,true);
    try { vm_.load_mission(code); }
    catch (...) { terminate(id); throw; }
    mission_index_=index;
    const size_t begin=8u+4u*image_.true_globals;
    std::fill(vm_.memory.begin()+begin,vm_.memory.begin()+begin+4u*image_.most_globals,uint8_t(0));
    return id;
}
void Runtime::terminate(ScriptId id) {
    auto& s=find(id); s.thread.active=false; s.blocked=false; s.result=StepResult::terminated;
    active_.erase(std::remove(active_.begin(),active_.end(),id),active_.end());
}
void Runtime::resume(ScriptId id) {
    auto& s=find(id);
    if (!s.thread.active || !s.blocked) throw std::runtime_error("Only blocked LCS threads can be resumed");
    if(s.result!=StepResult::unsupported)throw std::runtime_error("Failed script requires repair; unsafe automatic command replay rejected");
    s.blocked=false; s.thread.diagnostic.clear(); s.result=StepResult::continued;
}
bool Runtime::notify_player_restart() {
    if(!on_mission_offset_ || *on_mission_offset_<8 || *on_mission_offset_%4 ||
       uint64_t(*on_mission_offset_)+4>vm_.variable_space ||
       uint64_t(*on_mission_offset_)+4>vm_.memory.size() ||
       read32(vm_.memory,*on_mission_offset_)!=1)return false;
    Script* recovery=nullptr;
    for(auto& script:scripts_) {
        auto& t=script.thread;
        if(!script.id || !script.mission || !t.active || !t.death_arrest_enabled)continue;
        recovery=&script;break;
    }
    // Recovery abandons a quarantined instruction through the original root
    // frame; it does not resume/replay that instruction or its native effect.
    if(!recovery || recovery->processing)return false;
    const bool faulted=recovery->result==StepResult::unsupported || recovery->result==StepResult::failed;
    const bool running=recovery->result==StepResult::continued || recovery->result==StepResult::yielded;
    if(recovery->blocked?!faulted:!running)return false;
    const auto& thread=recovery->thread;
    if(!active_code_address(vm_,thread.ip) || !valid_recovery_frame(vm_,thread))return false;

    // Apply the exact VM unwind to copies first. Malformed function metadata,
    // output references, or recovery targets must not truncate the live stack
    // or clear the shared mission flag before validation completes.
    Vm staged_vm=vm_;
    Thread staged_thread=thread;
    try { staged_vm.restart_after_death_arrest(staged_thread); }
    catch(const std::exception&) { return false; }
    if(!active_code_address(staged_vm,staged_thread.ip) || staged_thread.stack_size ||
       staged_thread.locals_base || staged_thread.waiting || !staged_thread.death_arrest_executed)return false;
    staged_thread.diagnostic.clear();
    std::fill(staged_vm.memory.begin()+*on_mission_offset_,staged_vm.memory.begin()+*on_mission_offset_+4,uint8_t(0));

    // Commit on the owning game thread without replacing ScriptSpace storage:
    // marker/HUD services can retain variable identities inside this buffer.
    // Byte copy and default-allocator Thread move assignment cannot throw.
    recovery->thread=std::move(staged_thread);
    std::copy(staged_vm.memory.begin(),staged_vm.memory.end(),vm_.memory.begin());
    recovery->blocked=false;recovery->result=StepResult::continued;
    return true;
}
void Runtime::declare_mission_flag(uint32_t offset) {
    if(offset<8 || offset%4 || offset+4ull>vm_.variable_space)throw std::runtime_error("Mission flag outside LCS globals");
    on_mission_offset_=offset;
}
void Runtime::process(Script& s) {
    if (s.processing) throw std::runtime_error("LCS thread is already processing");
    struct Guard { bool& flag; ~Guard() { flag=false; } } guard{s.processing};
    s.processing=true;
    if(s.awaiting_initial_slice) {
        const auto child=std::find_if(scripts_.begin(),scripts_.end(),[&](const Script& c){return c.id==s.awaiting_initial_slice;});
        if(child!=scripts_.end() && child->thread.active && (child->blocked || child->awaiting_initial_slice || !child->thread.waiting)) {
            s.result=StepResult::yielded;
            s.thread.diagnostic="Waiting for loaded mission's initial execution to reach WAIT or termination";
            executions_.push_back({s.id,s.result,s.thread.last_ip,s.thread.last_opcode,s.thread.diagnostic,s.thread.name});
            return;
        }
        s.awaiting_initial_slice=0;
    }
    s.result=vm_.tick(s.thread,now_,budget_);
    s.blocked=s.result==StepResult::unsupported || s.result==StepResult::failed;
    // The standalone VM marks decode/adapter exceptions inactive. The scheduler
    // keeps that slot quarantined so its code lease, locals and dependencies
    // cannot disappear or be reused. Failed commands are never auto-replayed.
    if(s.blocked)s.thread.active=true;
    const auto id=s.id;
    executions_.push_back({id,s.result,s.thread.last_ip,s.thread.last_opcode,s.thread.diagnostic,s.thread.name});
    if (!s.thread.active) active_.erase(std::remove(active_.begin(),active_.end(),id),active_.end());
}
StepResult Runtime::dispatch(Thread& t,uint16_t opcode) {
    switch (opcode) {
    case COMMAND_DECLARE_MISSION_FLAG:
        on_mission_offset_=vm_.global_reference(t);return StepResult::continued;
    case COMMAND_START_NEW_SCRIPT: {
        const auto label=vm_.parameter(t);
        if (label<0) throw std::runtime_error("START_NEW_SCRIPT requires a nonnegative LCS label");
        const auto args=vm_.arguments(t,96);
        (void)owner(t);
        // Persistent main-script workers launched by initialization missions
        // do not reference their parent's mission code. Their own IP/stack
        // determine residency; inheritance prevented the next init block.
        start(uint32_t(label),args,false,false);
        return StepResult::continued;
    }
    case COMMAND_LAUNCH_MISSION: {
        const auto label=vm_.parameter(t);
        if (label<0) throw std::runtime_error("LAUNCH_MISSION requires a nonnegative LCS label");
        (void)owner(t);
        start(uint32_t(label),{},true,false);
        return StepResult::continued;
    }
    case COMMAND_LOAD_AND_LAUNCH_MISSION_INTERNAL: {
        const auto index=vm_.parameter(t);
        if (index<0) throw std::runtime_error("Negative LCS mission index");
        const auto id=load_mission(size_t(index));
        process(find(id));
        const auto& child=find(id);
        if(child.thread.active && (child.blocked || child.awaiting_initial_slice || !child.thread.waiting)) {
            owner(t).awaiting_initial_slice=id;
            return StepResult::yielded;
        }
        return StepResult::continued;
    }
    case COMMAND_TERMINATE_ALL_SCRIPTS_WITH_THIS_NAME: {
        const auto name=vm_.text_label(t);
        const auto targets=active_;
        for (const auto id:targets) if (find(id).thread.name==name) terminate(id);
        return t.active ? StepResult::continued : StepResult::terminated;
    }
    default: return engine ? engine(vm_,t,opcode) : StepResult::unsupported;
    }
}
std::vector<Execution> Runtime::tick(uint32_t now,uint32_t elapsed_ms,size_t budget) {
    if (ticking_) throw std::runtime_error("LCS scheduler cannot tick recursively");
    struct Guard { bool& flag; ~Guard() { flag=false; } } guard{ticking_};
    ticking_=true; now_=now; budget_=budget; executions_.clear();
    const auto timestep=frame_time_step ? frame_time_step() : std::optional<float>(std::min(float(elapsed_ms)/20.0f,3.0f));
    if(!timestep || !std::isfinite(*timestep) || *timestep<0)throw std::runtime_error("Invalid original script timestep");
    vm_.time_step=*timestep;
    const auto ready=active_;
    for (const auto id:ready) {
        // A preceding thread may kill and replace a ready slot. Its replacement
        // has a new ID and starts next tick; never run it under the old ID.
        const auto slot=std::find_if(scripts_.begin(),scripts_.end(),[id](const Script& s) { return s.id==id; });
        if (slot==scripts_.end()) continue;
        auto& s=*slot;
        if (!s.thread.active || s.blocked) continue;
        for (size_t i=104;i<106;++i)
            s.thread.locals[i]=int32_t(uint32_t(s.thread.locals[i])+elapsed_ms);
        process(s);
    }
    return executions_;
}
}
