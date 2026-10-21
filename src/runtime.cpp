#include "lcs/runtime.hpp"
#include "ScriptCommands.h"
#include <algorithm>
#include <cmath>
#include <cstring>
#include <iterator>
#include <limits>
#include <stdexcept>
#include <type_traits>
#include <utility>

namespace lcs {
namespace {
constexpr uint64_t kFnvOffset = 14695981039346656037ull;
constexpr uint64_t kFnvPrime = 1099511628211ull;
constexpr size_t kMaximumRuntimeState = 112u * 1024u * 1024u;
constexpr uint8_t kRuntimeStateMagic[8] = {'L','C','S','R','U','N','0','1'};
constexpr uint32_t kRuntimeStateVersion = 1;
constexpr uint32_t kNoSavedIndex = UINT32_MAX;

uint64_t hash_byte(uint64_t hash,uint8_t byte) { return (hash^byte)*kFnvPrime; }
uint64_t state_checksum(const std::vector<uint8_t>& bytes,size_t length) {
    uint64_t hash=kFnvOffset;
    for(size_t i=0;i<length;++i)hash=hash_byte(hash,bytes[i]);
    return hash;
}
int32_t signed_bits(uint32_t bits) { int32_t value;std::memcpy(&value,&bits,sizeof(value));return value; }
class StateWriter {
public:
    std::vector<uint8_t> bytes;
    void u8(uint8_t value) { bytes.push_back(value); }
    void u16(uint16_t value) { for(unsigned i=0;i<2;++i)bytes.push_back(uint8_t(value>>(8*i))); }
    void u32(uint32_t value) { for(unsigned i=0;i<4;++i)bytes.push_back(uint8_t(value>>(8*i))); }
    void u64(uint64_t value) { for(unsigned i=0;i<8;++i)bytes.push_back(uint8_t(value>>(8*i))); }
    void raw(const uint8_t* data,size_t size) { bytes.insert(bytes.end(),data,data+size); }
    void string(const std::string& value) {
        if(value.size()>UINT16_MAX)throw std::runtime_error("LCS thread text exceeds save-state limit");
        u16(uint16_t(value.size()));raw(reinterpret_cast<const uint8_t*>(value.data()),value.size());
    }
};
class StateReader {
    const std::vector<uint8_t>& bytes_;
    size_t position_=0,end_;
    void require(size_t size) const {
        if(position_>end_ || size>end_-position_)throw std::runtime_error("Truncated LCS runtime save state");
    }
public:
    StateReader(const std::vector<uint8_t>& bytes,size_t begin,size_t end):bytes_(bytes),position_(begin),end_(end){}
    uint8_t u8() { require(1);return bytes_[position_++]; }
    uint16_t u16() { const auto low=u8();return uint16_t(low)|uint16_t(u8())<<8; }
    uint32_t u32() {
        const auto b0=u8(),b1=u8(),b2=u8(),b3=u8();
        return uint32_t(b0)|uint32_t(b1)<<8|uint32_t(b2)<<16|uint32_t(b3)<<24;
    }
    uint64_t u64() { const auto low=u32(),high=u32();return uint64_t(low)|(uint64_t(high)<<32); }
    std::vector<uint8_t> bytes(size_t size) {
        require(size);std::vector<uint8_t> result(bytes_.begin()+position_,bytes_.begin()+position_+size);position_+=size;return result;
    }
    std::string string(size_t maximum) {
        const auto size=u16();if(size>maximum)throw std::runtime_error("LCS thread text exceeds save-state limit");
        require(size);std::string result(reinterpret_cast<const char*>(bytes_.data()+position_),size);position_+=size;return result;
    }
    size_t remaining() const { return end_-position_; }
};
uint32_t remaining_wait(uint32_t wake,uint32_t now) {
    const auto delta=wake-now;
    return delta && delta<=uint32_t(std::numeric_limits<int32_t>::max()) ? delta : 0;
}
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
bool valid_runtime_thread(const Vm& vm,const Thread& thread) {
    if(thread.stack_size>thread.stack.size() || thread.locals_base>96 ||
       !(thread.and_or==0 || (thread.and_or>=1 && thread.and_or<=8) ||
         (thread.and_or>=21 && thread.and_or<=28)))return false;
    if(!thread.active)return true;
    if(!active_code_address(vm,thread.ip))return false;
    uint16_t base=thread.locals_base;
    for(size_t i=thread.stack_size;i>0;--i) {
        const auto frame=thread.stack[i-1];
        if(frame&(1u<<23)) {
            Thread caller=thread;caller.locals_base=base;
            if(!valid_function_recovery_frame(vm,caller,frame))return false;
            const auto metadata=frame&((1u<<22)-1);
            if(metadata+3ull>vm.memory.size())return false;
            const auto offset=vm.memory[metadata+2];
            if(base<offset)return false;
            base=uint16_t(base-offset);
        } else if(!valid_plain_recovery_frame(vm,frame))return false;
    }
    return base==0;
}
uint8_t thread_flags(const Thread& thread) {
    return uint8_t(thread.condition) | uint8_t(thread.inverted)<<1 | uint8_t(thread.active)<<2 |
        uint8_t(thread.waiting)<<3 | uint8_t(thread.message_wait)<<4 |
        uint8_t(thread.death_arrest_enabled)<<5 | uint8_t(thread.death_arrest_executed)<<6;
}
void restore_thread_flags(Thread& thread,uint8_t flags) {
    if(flags&0x80)throw std::runtime_error("Invalid LCS thread flags in save state");
    thread.condition=bool(flags&1);thread.inverted=bool(flags&2);thread.active=bool(flags&4);
    thread.waiting=bool(flags&8);thread.message_wait=bool(flags&16);
    thread.death_arrest_enabled=bool(flags&32);thread.death_arrest_executed=bool(flags&64);
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
std::vector<uint8_t> Runtime::save_state() const {
    if(ticking_)throw std::runtime_error("Cannot save LCS runtime during a scheduler tick");
    std::array<bool,128> scheduled{};
    for(const auto id:active_) {
        const auto slot=std::find_if(scripts_.begin(),scripts_.end(),[id](const Script& s){return id && s.id==id;});
        if(slot==scripts_.end() || !slot->thread.active)
            throw std::runtime_error("Invalid LCS active-thread queue cannot be saved");
        const auto index=size_t(slot-scripts_.begin());
        if(scheduled[index])throw std::runtime_error("Duplicate LCS active-thread ID cannot be saved");
        scheduled[index]=true;
    }
    uint64_t maximum_id=0;
    for(size_t i=0;i<scripts_.size();++i) {
        const auto& script=scripts_[i];
        if(script.processing)throw std::runtime_error("Cannot save LCS runtime during script processing");
        if(!script.id)continue;
        for(size_t prior=0;prior<i;++prior)if(scripts_[prior].id==script.id)
            throw std::runtime_error("Duplicate LCS script ID cannot be saved");
        maximum_id=std::max(maximum_id,script.id);
        const auto result=static_cast<uint8_t>(script.result);
        if(result>static_cast<uint8_t>(StepResult::failed) ||
           !valid_runtime_thread(vm_,script.thread) || script.thread.last_opcode>0x7fff ||
           script.thread.name.size()>8 || script.thread.diagnostic.size()>4096 ||
           (script.blocked && (!script.thread.active ||
              (script.result!=StepResult::unsupported && script.result!=StepResult::failed))) ||
           (script.thread.active && (script.result==StepResult::terminated ||
              (!script.blocked && (script.result==StepResult::unsupported || script.result==StepResult::failed)))) ||
           (!script.thread.active && (script.blocked || script.result!=StepResult::terminated)) ||
           (script.thread.active!=scheduled[i]))
            throw std::runtime_error("Invalid LCS script state cannot be saved");
    }
    if((next_id_ && next_id_<=maximum_id) || (!next_id_ && maximum_id!=UINT64_MAX))
        throw std::runtime_error("Invalid LCS next-script ID cannot be saved");
    if(mission_index_) {
        if(image_.exclusive_missions)throw std::runtime_error("Exclusive LCS mission state cannot be saved by this runtime");
        if(*mission_index_>=image_.mission_offsets.size())throw std::runtime_error("Invalid loaded LCS mission index");
        const auto expected=image_.mission(*mission_index_);
        if(expected.size()!=vm_.mission_size() ||
           !std::equal(expected.begin(),expected.end(),vm_.memory.begin()+vm_.main_size))
            throw std::runtime_error("Loaded LCS mission code differs from the source image");
    } else if(vm_.mission_size())throw std::runtime_error("Loaded LCS mission has no source index");
    if(on_mission_offset_ && (*on_mission_offset_<8 || (*on_mission_offset_%4) ||
       uint64_t(*on_mission_offset_)+4>vm_.variable_space))
        throw std::runtime_error("Invalid LCS mission flag cannot be saved");

    const auto vm_state=vm_.save_state();
    StateWriter writer;writer.raw(kRuntimeStateMagic,sizeof(kRuntimeStateMagic));
    writer.u32(kRuntimeStateVersion);writer.u32(uint32_t(vm_state.size()));
    writer.raw(vm_state.data(),vm_state.size());
    writer.u32(mission_index_ ? uint32_t(*mission_index_) : kNoSavedIndex);
    writer.u32(on_mission_offset_.value_or(kNoSavedIndex));writer.u64(next_id_);
    writer.u16(uint16_t(scripts_.size()));
    for(const auto& script:scripts_) {
        writer.u64(script.id);
        if(!script.id)continue;
        const auto& thread=script.thread;
        writer.u32(thread.ip);
        writer.u32(thread.waiting ? remaining_wait(thread.wake_time,now_) : 0);
        for(const auto value:thread.stack)writer.u32(value);
        for(const auto value:thread.locals)writer.u32(uint32_t(value));
        writer.u16(thread.stack_size);writer.u16(thread.locals_base);writer.u16(thread.and_or);
        writer.u8(thread_flags(thread));writer.u32(thread.last_ip);writer.u16(thread.last_opcode);
        const uint8_t flags=uint8_t(script.mission)|uint8_t(script.resident)<<1|
            uint8_t(script.blocked)<<2|uint8_t(script.bootstrap)<<3;
        writer.u8(flags);writer.u64(script.awaiting_initial_slice);
        writer.u8(static_cast<uint8_t>(script.result));writer.string(thread.name);writer.string(thread.diagnostic);
    }
    writer.u16(uint16_t(active_.size()));for(const auto id:active_)writer.u64(id);
    if(writer.bytes.size()+8>kMaximumRuntimeState)throw std::runtime_error("LCS runtime save state exceeds size limit");
    const auto checksum=state_checksum(writer.bytes,writer.bytes.size());writer.u64(checksum);
    return std::move(writer.bytes);
}
void Runtime::restore_state(const std::vector<uint8_t>& state,uint32_t now) {
    if(ticking_)throw std::runtime_error("Cannot restore LCS runtime during a scheduler tick");
    if(state.size()<32 || state.size()>kMaximumRuntimeState)
        throw std::runtime_error("Invalid LCS runtime save-state size");
    StateReader checksum_reader(state,state.size()-8,state.size());
    if(state_checksum(state,state.size()-8)!=checksum_reader.u64())
        throw std::runtime_error("LCS runtime save-state checksum mismatch");
    StateReader reader(state,0,state.size()-8);
    const auto magic=reader.bytes(sizeof(kRuntimeStateMagic));
    if(!std::equal(std::begin(kRuntimeStateMagic),std::end(kRuntimeStateMagic),std::begin(magic)) ||
       reader.u32()!=kRuntimeStateVersion)
        throw std::runtime_error("Unsupported LCS runtime save-state format");
    const auto vm_size=reader.u32();
    if(vm_size>96u*1024u*1024u+48u)throw std::runtime_error("Embedded LCS VM state exceeds size limit");
    const auto vm_state=reader.bytes(vm_size);
    const auto saved_mission=reader.u32(),saved_mission_flag=reader.u32();
    const auto saved_next_id=reader.u64();
    if(reader.u16()!=scripts_.size())throw std::runtime_error("LCS runtime thread-pool layout mismatch");
    std::optional<size_t> staged_mission;
    if(saved_mission!=kNoSavedIndex) {
        if(image_.exclusive_missions)throw std::runtime_error("Exclusive LCS missions are unsupported by this runtime");
        if(saved_mission>=image_.mission_offsets.size())throw std::runtime_error("Saved LCS mission index is invalid");
        staged_mission=size_t(saved_mission);
    }
    std::optional<uint32_t> staged_mission_flag;
    if(saved_mission_flag!=kNoSavedIndex) {
        if(saved_mission_flag<8 || saved_mission_flag%4 || uint64_t(saved_mission_flag)+4>vm_.variable_space)
            throw std::runtime_error("Saved LCS mission flag is outside global storage");
        staged_mission_flag=saved_mission_flag;
    }
    std::array<Script,128> staged_scripts{};
    uint64_t maximum_id=0;
    for(size_t i=0;i<staged_scripts.size();++i) {
        auto& script=staged_scripts[i];script.id=reader.u64();
        if(!script.id)continue;
        for(size_t prior=0;prior<i;++prior)if(staged_scripts[prior].id==script.id)
            throw std::runtime_error("Duplicate LCS script ID in save file");
        maximum_id=std::max(maximum_id,script.id);
        auto& thread=script.thread;thread.ip=reader.u32();
        const auto remaining=reader.u32();
        if(remaining>uint32_t(std::numeric_limits<int32_t>::max()))
            throw std::runtime_error("Saved LCS WAIT exceeds timer range");
        for(auto& value:thread.stack)value=reader.u32();
        for(auto& value:thread.locals)value=signed_bits(reader.u32());
        thread.stack_size=reader.u16();thread.locals_base=reader.u16();thread.and_or=reader.u16();
        restore_thread_flags(thread,reader.u8());thread.wake_time=now+remaining;
        if(!thread.waiting && remaining)throw std::runtime_error("Nonwaiting LCS thread has a saved WAIT deadline");
        thread.last_ip=reader.u32();thread.last_opcode=reader.u16();
        const auto flags=reader.u8();
        if(flags&0xf0)throw std::runtime_error("Invalid LCS script flags in save state");
        script.mission=bool(flags&1);script.resident=bool(flags&2);script.blocked=bool(flags&4);
        script.bootstrap=bool(flags&8);script.processing=false;
        script.awaiting_initial_slice=reader.u64();
        if(script.awaiting_initial_slice==script.id)
            throw std::runtime_error("LCS script cannot await its own initial slice");
        const auto result=reader.u8();if(result>static_cast<uint8_t>(StepResult::failed))
            throw std::runtime_error("Invalid LCS script result in save state");
        script.result=static_cast<StepResult>(result);
        thread.name=reader.string(8);thread.diagnostic=reader.string(4096);
        if(thread.last_opcode>0x7fff || thread.stack_size>thread.stack.size() || thread.locals_base>96 ||
           !(thread.and_or==0 || (thread.and_or>=1 && thread.and_or<=8) ||
             (thread.and_or>=21 && thread.and_or<=28)) ||
           (script.blocked && (!thread.active ||
              (script.result!=StepResult::unsupported && script.result!=StepResult::failed))) ||
           (thread.active && (script.result==StepResult::terminated ||
              (!script.blocked && (script.result==StepResult::unsupported || script.result==StepResult::failed)))) ||
           (!thread.active && (script.blocked || script.result!=StepResult::terminated)))
            throw std::runtime_error("Invalid LCS script state in save file");
    }
    if((saved_next_id && saved_next_id<=maximum_id) || (!saved_next_id && maximum_id!=UINT64_MAX))
        throw std::runtime_error("Invalid saved LCS next-script ID");
    const auto active_count=reader.u16();if(active_count>staged_scripts.size())
        throw std::runtime_error("Saved LCS active-thread count exceeds the pool");
    std::vector<ScriptId> staged_active;staged_active.reserve(active_count);
    std::array<bool,128> scheduled{};
    for(size_t i=0;i<active_count;++i) {
        const auto id=reader.u64();
        const auto slot=std::find_if(staged_scripts.begin(),staged_scripts.end(),[id](const Script& s){return id && s.id==id;});
        if(slot==staged_scripts.end() || !slot->thread.active)
            throw std::runtime_error("Saved LCS active queue references a missing thread");
        const auto index=size_t(slot-staged_scripts.begin());
        if(scheduled[index])throw std::runtime_error("Saved LCS active queue contains a duplicate thread");
        scheduled[index]=true;staged_active.push_back(id);
    }
    if(reader.remaining())throw std::runtime_error("Trailing bytes in LCS runtime save state");
    for(size_t i=0;i<staged_scripts.size();++i)
        if(staged_scripts[i].id && (staged_scripts[i].thread.active!=scheduled[i]))
            throw std::runtime_error("Saved LCS active queue is incomplete");

    Vm staged_vm=vm_;staged_vm.restore_state(vm_state);
    if(staged_mission) {
        const auto expected=image_.mission(*staged_mission);
        if(expected.size()!=staged_vm.mission_size() ||
           !std::equal(expected.begin(),expected.end(),staged_vm.memory.begin()+staged_vm.main_size))
            throw std::runtime_error("Saved LCS mission code does not match this SCM image");
    } else if(staged_vm.mission_size())throw std::runtime_error("Save state has mission code without a mission index");
    if(staged_mission_flag && uint64_t(*staged_mission_flag)+4>staged_vm.variable_space)
        throw std::runtime_error("Saved LCS mission flag does not fit this SCM image");
    for(const auto& script:staged_scripts)if(script.id && script.thread.active &&
       !valid_runtime_thread(staged_vm,script.thread))
        throw std::runtime_error("Saved LCS thread points outside loaded code");

    static_assert(std::is_nothrow_move_assignable<Script>::value,"Runtime restore commit must not throw");
    vm_.restore_state(vm_state);
    scripts_=std::move(staged_scripts);active_=std::move(staged_active);
    mission_index_=staged_mission;on_mission_offset_=staged_mission_flag;next_id_=saved_next_id;
    now_=now;budget_=10000;executions_.clear();
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
