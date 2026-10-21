#include "lcs/vm.hpp"
#include "ScriptCommands.h"
#include <algorithm>
#include <cstring>
#include <iterator>
#include <limits>
#include <cmath>
#include <sstream>
#include <stdexcept>
#include <utility>

namespace lcs {
namespace {
constexpr uint64_t kFnvOffset = 14695981039346656037ull;
constexpr uint64_t kFnvPrime = 1099511628211ull;
constexpr size_t kMaximumVmState = 96u * 1024u * 1024u;
constexpr uint8_t kVmStateMagic[8] = {'L','C','S','V','M','0','0','1'};
constexpr uint32_t kVmStateVersion = 1;

uint64_t hash_byte(uint64_t hash,uint8_t byte) { return (hash^byte)*kFnvPrime; }
uint64_t hash_u32(uint64_t hash,uint32_t value) {
    for(unsigned i=0;i<4;++i)hash=hash_byte(hash,uint8_t(value>>(8*i)));
    return hash;
}
uint64_t hash_u64(uint64_t hash,uint64_t value) {
    for(unsigned i=0;i<8;++i)hash=hash_byte(hash,uint8_t(value>>(8*i)));
    return hash;
}
uint64_t image_fingerprint(const std::vector<uint8_t>& bytes,uint32_t main_size,
                           uint32_t variable_space,uint32_t mission_allocation) {
    uint64_t hash=kFnvOffset;
    hash=hash_u32(hash,main_size);hash=hash_u32(hash,variable_space);
    hash=hash_u32(hash,mission_allocation);hash=hash_u64(hash,bytes.size());
    for(const auto byte:bytes)hash=hash_byte(hash,byte);
    return hash;
}
uint64_t state_checksum(const std::vector<uint8_t>& bytes,size_t length) {
    uint64_t hash=kFnvOffset;
    for(size_t i=0;i<length;++i)hash=hash_byte(hash,bytes[i]);
    return hash;
}
class StateWriter {
public:
    std::vector<uint8_t> bytes;
    void raw(const uint8_t* data,size_t size) { bytes.insert(bytes.end(),data,data+size); }
    void u32(uint32_t value) { for(unsigned i=0;i<4;++i)bytes.push_back(uint8_t(value>>(8*i))); }
    void u64(uint64_t value) { for(unsigned i=0;i<8;++i)bytes.push_back(uint8_t(value>>(8*i))); }
};
class StateReader {
    const std::vector<uint8_t>& bytes_;
    size_t position_=0,end_;
    void require(size_t size) const {
        if(position_>end_ || size>end_-position_)throw std::runtime_error("Truncated LCS VM save state");
    }
public:
    StateReader(const std::vector<uint8_t>& bytes,size_t begin,size_t end):bytes_(bytes),position_(begin),end_(end){}
    void raw(uint8_t* output,size_t size) { require(size);std::copy_n(bytes_.begin()+position_,size,output);position_+=size; }
    uint32_t u32() {
        require(4);const auto value=uint32_t(bytes_[position_])|uint32_t(bytes_[position_+1])<<8|
            uint32_t(bytes_[position_+2])<<16|uint32_t(bytes_[position_+3])<<24;
        position_+=4;return value;
    }
    uint64_t u64() { const auto low=u32(),high=u32();return uint64_t(low)|(uint64_t(high)<<32); }
    size_t remaining() const { return end_-position_; }
};
uint32_t float_bits(float value) { uint32_t bits;std::memcpy(&bits,&value,sizeof(bits));return bits; }
float bits_float(uint32_t bits) { float value;std::memcpy(&value,&bits,sizeof(value));return value; }
}
static float as_float(int32_t bits) { float f; std::memcpy(&f, &bits, 4); return f; }
static int32_t as_bits(float f) { int32_t bits; std::memcpy(&bits, &f, 4); return bits; }
Vm::Vm(const ScmImage& i) : memory(i.bytes.begin(), i.bytes.begin()+i.main_size),
    main_size(i.main_size), variable_space(i.variable_space) {
    memory.resize(size_t(main_size) + i.largest_mission);
    image_fingerprint_=image_fingerprint(i.bytes,i.main_size,i.variable_space,i.largest_mission);
}
Vm::Vm(std::vector<uint8_t> code, uint32_t variables, uint32_t main)
    : memory(std::move(code)), main_size(main), variable_space(variables) {
    if (memory.size()<main_size || variable_space>main_size || memory.size()>UINT32_MAX)
        throw std::runtime_error("Invalid ScriptSpace layout");
    mission_size_=uint32_t(memory.size()-main_size);
    image_fingerprint_=image_fingerprint(memory,main_size,variable_space,mission_size_);
}
std::vector<uint8_t> Vm::save_state() const {
    if(variable_space>main_size || main_size>memory.size() ||
       mission_size_>memory.size()-main_size || memory.size()>kMaximumVmState ||
       !std::isfinite(time_step) || time_step<0)
        throw std::runtime_error("Invalid LCS VM state cannot be saved");
    StateWriter writer;writer.raw(kVmStateMagic,sizeof(kVmStateMagic));
    writer.u32(kVmStateVersion);writer.u64(image_fingerprint_);writer.u32(main_size);
    writer.u32(variable_space);writer.u32(uint32_t(memory.size()));writer.u32(mission_size_);
    writer.u32(float_bits(time_step));writer.raw(memory.data(),variable_space);
    writer.raw(memory.data()+main_size,mission_size_);
    const auto checksum=state_checksum(writer.bytes,writer.bytes.size());writer.u64(checksum);
    return std::move(writer.bytes);
}
void Vm::restore_state(const std::vector<uint8_t>& state) {
    if(state.size()<48 || state.size()>kMaximumVmState+48)
        throw std::runtime_error("Invalid LCS VM save-state size");
    StateReader checksum_reader(state,state.size()-8,state.size());
    const auto expected_checksum=checksum_reader.u64();
    if(state_checksum(state,state.size()-8)!=expected_checksum)
        throw std::runtime_error("LCS VM save-state checksum mismatch");
    StateReader reader(state,0,state.size()-8);
    uint8_t magic[sizeof(kVmStateMagic)]{};reader.raw(magic,sizeof(magic));
    if(!std::equal(std::begin(kVmStateMagic),std::end(kVmStateMagic),std::begin(magic)) ||
       reader.u32()!=kVmStateVersion)
        throw std::runtime_error("Unsupported LCS VM save-state format");
    const auto fingerprint=reader.u64();
    const auto saved_main=reader.u32(),saved_variables=reader.u32();
    const auto saved_capacity=reader.u32(),saved_mission=reader.u32(),saved_time=reader.u32();
    const auto timestep=bits_float(saved_time);
    if(fingerprint!=image_fingerprint_ || saved_main!=main_size || saved_variables!=variable_space ||
       saved_capacity!=memory.size() || variable_space>main_size || main_size>memory.size() ||
       saved_mission>memory.size()-main_size || !std::isfinite(timestep) || timestep<0 ||
       uint64_t(variable_space)+saved_mission!=reader.remaining())
        throw std::runtime_error("LCS VM save state does not match this SCM image or layout");
    std::vector<uint8_t> globals(variable_space),mission(saved_mission);
    reader.raw(globals.data(),globals.size());reader.raw(mission.data(),mission.size());
    // Commit only after the complete envelope and every size/layout check pass.
    std::copy(globals.begin(),globals.end(),memory.begin());
    std::fill(memory.begin()+main_size,memory.end(),uint8_t(0));
    std::copy(mission.begin(),mission.end(),memory.begin()+main_size);
    mission_size_=saved_mission;time_step=timestep;
}
void Vm::require_code(const Thread& t, size_t bytes) const {
    const uint64_t logical_end=t.ip<main_size ? main_size : uint64_t(main_size)+mission_size_;
    // Script storage is public to adapters. A shortened or replaced buffer must
    // never inherit permission to read the previous loaded mission's bytes.
    const auto end=std::min<uint64_t>(logical_end,memory.size());
    if (t.ip>end || bytes>end-t.ip) throw std::runtime_error("Instruction outside loaded script code");
}
uint8_t Vm::byte(Thread& t) {
    require_code(t,1);
    return memory[t.ip++];
}
uint16_t Vm::word(Thread& t) { require_code(t,2); const auto v=read16(memory,t.ip); t.ip+=2; return v; }
uint32_t Vm::dword(Thread& t) { require_code(t,4); const auto v=read32(memory,t.ip); t.ip+=4; return v; }
Vm::Ref Vm::reference(Thread& t) {
    const auto kind = byte(t);
    if (kind < 10) throw std::runtime_error("Expected LCS variable reference");
    if (kind >= 230) {
        const auto base = byte(t), index_id = byte(t), count = byte(t);
        const size_t local = size_t(t.locals_base) + index_id;
        if (!count || local >= 96 || t.locals[local] < 0 || t.locals[local] >= count)
            throw std::runtime_error("Invalid global-array subscript");
        return {true, uint32_t(4 * (((kind-230) << 8) + base + t.locals[local]))};
    }
    if (kind >= 204) return {true, uint32_t(4 * (((kind-204) << 8) + byte(t)))};
    if (kind >= 108) {
        const auto index_id = byte(t), count = byte(t);
        const size_t local = size_t(t.locals_base) + index_id;
        if (!count || local >= 96 || t.locals[local] < 0 || t.locals[local] >= count)
            throw std::runtime_error("Invalid local-array subscript");
        return {false, uint32_t(t.locals_base + kind-108 + t.locals[local])};
    }
    if (kind >= 12) return {false, uint32_t(t.locals_base + kind-12)};
    return {false, uint32_t(104 + kind-10)};
}
int32_t Vm::get(Thread& t, Ref r) const {
    if (r.global) {
        if (r.index < 8 || r.index + 4ull > variable_space) throw std::runtime_error("Global outside variable space");
        return int32_t(read32(memory, r.index));
    }
    if (r.index >= t.locals.size() || (r.index >= 96 && r.index < 104))
        throw std::runtime_error("Local outside LCS locals/timers");
    return t.locals[r.index];
}
void Vm::set(Thread& t, Ref r, int32_t value) {
    (void)get(t, r);
    if (r.global) {
        const auto bits = uint32_t(value);
        for (uint32_t i=0; i<4; ++i) memory[r.index+i] = uint8_t(bits >> (8*i));
    } else t.locals[r.index] = value;
}
int32_t Vm::parameter(Thread& t) {
    const auto kind = byte(t);
    switch (kind) {
    case 0: throw std::runtime_error("Unexpected argument terminator");
    case 1: case 2: return 0;
    case 3: return int32_t(uint32_t(byte(t)) << 24);
    case 4: return int32_t(uint32_t(word(t))<<16);
    case 5: { const auto a=byte(t); const auto b=word(t); return int32_t(uint32_t(a)<<8 | uint32_t(b)<<16); }
    case 6: case 9: return int32_t(dword(t));
    case 7: return int8_t(byte(t));
    case 8: return int16_t(word(t));
    default: --t.ip; return get(t, reference(t));
    }
}
int32_t Vm::variable(Thread& t) { return get(t, reference(t)); }
uintptr_t Vm::peek_variable_identity(Thread& t) {
    const auto saved=t.ip;
    struct Restore {Thread& t;uint32_t ip;~Restore(){t.ip=ip;}} restore{t,saved};
    const auto r=reference(t);(void)get(t,r);
    return r.global ? reinterpret_cast<uintptr_t>(memory.data()+r.index)
                    : reinterpret_cast<uintptr_t>(&t.locals[r.index]);
}
uint32_t Vm::global_reference(Thread& t) {
    const auto r=reference(t);(void)get(t,r);
    if(!r.global)throw std::runtime_error("Script declaration requires a global variable");
    return r.index;
}
void Vm::store(Thread& t, int32_t value) { set(t, reference(t), value); }
void Vm::compare(Thread& t, bool value) {
    if (t.inverted) value = !value;
    if (!t.and_or) t.condition = value;
    else if (t.and_or <= 8) { t.condition &= value; --t.and_or; }
    else { t.condition |= value; t.and_or = t.and_or <= 21 ? 0 : t.and_or-1; }
}
uint32_t Vm::resolve_label(int32_t label) const {
    const int64_t target = label >= 0 ? label : int64_t(main_size)-label;
    if (target < 0 || uint64_t(target) >= uint64_t(main_size)+mission_size_)
        throw std::runtime_error("Invalid script branch or unloaded mission target");
    return uint32_t(target);
}
std::vector<int32_t> Vm::arguments(Thread& t, size_t maximum) {
    std::vector<int32_t> values;
    for (size_t i=0;i<maximum;++i) {
        require_code(t,1);
        if (!memory[t.ip]) { ++t.ip; break; }
        values.push_back(parameter(t));
    }
    return values;
}
std::string Vm::text_label(Thread& t) {
    require_code(t,8);
    std::string text;
    for (size_t i=0;i<8;++i) {
        const auto c=memory[t.ip+i];
        if (!c) break;
        text.push_back(char(c>='A' && c<='Z' ? c+('a'-'A') : c));
    }
    t.ip+=8; return text;
}
void Vm::load_mission(const std::vector<uint8_t>& code) {
    if (code.empty() || code.size()>memory.size()-main_size)
        throw std::runtime_error("Mission exceeds allocated script buffer");
    std::fill(memory.begin()+main_size,memory.end(),uint8_t(0));
    std::copy(code.begin(),code.end(),memory.begin()+main_size);
    mission_size_=uint32_t(code.size());
}
void Vm::return_from_call(Thread& t) {
    if (!t.stack_size) throw std::runtime_error("LCS call stack underflow");
    // Keep the packed LCS frame: 22-bit metadata address, invert/function flags,
    // and an 8-bit length of the label plus input arguments. Output references
    // stay in the caller's original bytecode and are decoded only on return.
    const auto frame=t.stack[t.stack_size-1];
    if (!(frame & (1u<<23))) {
        t.ip=frame; --t.stack_size; return;
    }
    t.ip=frame & ((1u<<22)-1);
    const auto inputs=byte(t), outputs=byte(t), offset=byte(t);
    if (t.locals_base < offset || size_t(t.locals_base)+inputs+outputs > 96)
        throw std::runtime_error("Invalid LCS function return frame");
    std::array<int32_t,96> values{};
    for (size_t i=0; i<outputs; ++i) values[i]=t.locals[t.locals_base+inputs+i];
    t.ip+=frame>>24;
    t.locals_base=uint16_t(t.locals_base-offset);
    for (size_t i=0; i<outputs; ++i) store(t,values[i]);
    if (frame & (1u<<22)) t.condition=!t.condition;
    --t.stack_size;
}
void Vm::restart_after_death_arrest(Thread& t) {
    if(!t.stack_size)throw std::runtime_error("Mission death/arrest has no recovery frame");
    t.stack_size=1;return_from_call(t);t.locals_base=0;
    t.death_arrest_executed=true;t.waiting=false;t.wake_time=0;
}
StepResult Vm::step(Thread& t, uint32_t now) {
    if (!t.active) return StepResult::terminated;
    t.last_ip = t.ip;
    try {
        if(before_step && before_step(*this,t))return StepResult::continued;
        const auto instruction = word(t);
        t.inverted = (instruction & 0x8000) != 0;
        const uint16_t op = instruction & 0x7fff;
        t.last_opcode = op;
        // The first 4 assignment and 16 arithmetic commands share the same two operands.
        if (op >= COMMAND_SET_VAR_INT && op <= COMMAND_DIV_FLOAT_LVAR_BY_VAL) {
            const auto dest=reference(t); const auto value=parameter(t);
            const auto group=(op-COMMAND_SET_VAR_INT)/4;
            const bool floating=((op-COMMAND_SET_VAR_INT)%4)%2 != 0;
            const auto current=get(t,dest);
            if (!group) set(t,dest,value);
            else if (floating) {
                auto a=as_float(current), b=as_float(value);
                if (group==1) a+=b; else if (group==2) a-=b; else if (group==3) a*=b; else a/=b;
                set(t,dest,as_bits(a));
            } else {
                const auto a=uint32_t(current), b=uint32_t(value);
                if (group==1) set(t,dest,int32_t(a+b));
                else if (group==2) set(t,dest,int32_t(a-b));
                else if (group==3) set(t,dest,int32_t(a*b));
                else {
                    if (!value) throw std::runtime_error("Integer division by zero");
                    set(t,dest,int32_t(int64_t(current)/value));
                }
            }
            return StepResult::continued;
        }
        // Script.cpp's variable arithmetic writes operand one; global/local
        // combinations repeat every eight commands, with integer/float pairs.
        if(op>=COMMAND_ADD_INT_VAR_TO_INT_VAR && op<=COMMAND_DIV_FLOAT_LVAR_BY_FLOAT_VAR) {
            const auto dest=reference(t);const auto value=variable(t);
            const auto group=(op-COMMAND_ADD_INT_VAR_TO_INT_VAR)/8;
            if((op-COMMAND_ADD_INT_VAR_TO_INT_VAR)%2) {
                const auto a=as_float(get(t,dest)),b=as_float(value);
                set(t,dest,as_bits(group==0 ? a+b : group==1 ? a-b : group==2 ? a*b : a/b));
            } else {
                const auto current=get(t,dest);const auto a=uint32_t(current),b=uint32_t(value);
                if(group==0)set(t,dest,int32_t(a+b));
                else if(group==1)set(t,dest,int32_t(a-b));
                else if(group==2)set(t,dest,int32_t(a*b));
                else {
                    if(!value)throw std::runtime_error("Integer division by zero");
                    set(t,dest,int32_t(int64_t(current)/value));
                }
            }
            return StepResult::continued;
        }
        if(op>=COMMAND_ADD_TIMED_VAL_TO_FLOAT_VAR && op<=COMMAND_SUB_TIMED_FLOAT_VAR_FROM_FLOAT_LVAR) {
            const auto dest=reference(t);
            const auto variant=(op-COMMAND_ADD_TIMED_VAL_TO_FLOAT_VAR)%6;
            const auto amount=as_float(variant<2 ? parameter(t) : variable(t))*time_step;
            const auto current=as_float(get(t,dest));
            set(t,dest,as_bits(op<COMMAND_SUB_TIMED_VAL_FROM_FLOAT_VAR ? current+amount : current-amount));
            return StepResult::continued;
        }
        if(op>=COMMAND_CSET_VAR_INT_TO_VAR_FLOAT && op<=COMMAND_CSET_LVAR_FLOAT_TO_LVAR_INT) {
            const auto dest=reference(t);const auto source=variable(t);
            if((op-COMMAND_CSET_VAR_INT_TO_VAR_FLOAT)%2) {
                set(t,dest,as_bits(float(source)));
            } else {
                // Script.cpp converts numerically into operand one, truncating
                // toward zero. Never reinterpret these values as native locals.
                const auto value=as_float(source);
                if(!std::isfinite(value) || double(value)<-2147483648.0 || double(value)>=2147483648.0)
                    throw std::runtime_error("Unrepresentable LCS float-to-int conversion");
                set(t,dest,int32_t(value));
            }
            return StepResult::continued;
        }
        // Script.cpp's 32 ordered comparisons keep their operands in bytecode
        // order, including NUMBER_GREATER_THAN_VAR. Integer and float groups
        // each have eight global/local/constant variants.
        if (op>=COMMAND_IS_INT_VAR_GREATER_THAN_NUMBER && op<=COMMAND_IS_FLOAT_LVAR_GREATER_OR_EQUAL_TO_FLOAT_VAR) {
            const auto a=parameter(t), b=parameter(t);
            const auto group=(op-COMMAND_IS_INT_VAR_GREATER_THAN_NUMBER)/8;
            if (group%2) compare(t,group>=2 ? as_float(a)>=as_float(b) : as_float(a)>as_float(b));
            else compare(t,group>=2 ? a>=b : a>b);
            return StepResult::continued;
        }
        switch (op) {
        case COMMAND_ABS_VAR_INT:case COMMAND_ABS_LVAR_INT: {
            const auto dest=reference(t);const auto value=get(t,dest);
            set(t,dest,value<0 ? int32_t(0u-uint32_t(value)) : value);return StepResult::continued;
        }
        case COMMAND_ABS_VAR_FLOAT:case COMMAND_ABS_LVAR_FLOAT: {
            const auto dest=reference(t);set(t,dest,int32_t(uint32_t(get(t,dest))&0x7fffffffu));return StepResult::continued;
        }
        case COMMAND_NOP: return StepResult::continued;
        case COMMAND_WAIT: {
            const auto delay=parameter(t);
            if (delay<0) throw std::runtime_error("Negative WAIT needs an explicit compatibility policy");
            t.wake_time=now+uint32_t(delay); t.waiting=true;t.message_wait=false; return StepResult::yielded;
        }
        case COMMAND_MESSAGE_WAIT: {
            const auto delay=parameter(t),skippable=parameter(t);
            if(delay<0)throw std::runtime_error("Negative MESSAGE_WAIT duration");
            t.wake_time=now+uint32_t(delay);t.waiting=true;t.message_wait=skippable!=0;
            return StepResult::yielded;
        }
        case COMMAND_GOTO: t.ip=resolve_label(parameter(t)); return StepResult::continued;
        case COMMAND_GOTO_IF_TRUE: case COMMAND_GOTO_IF_FALSE: {
            const auto target=resolve_label(parameter(t));
            if (t.condition == (op==COMMAND_GOTO_IF_TRUE)) t.ip=target;
            return StepResult::continued;
        }
        case COMMAND_GOSUB: case COMMAND_GOSUB_FILE: {
            const auto target=resolve_label(parameter(t));
            if(op==COMMAND_GOSUB_FILE)(void)parameter(t);
            if (t.stack_size==t.stack.size()) throw std::runtime_error("LCS call stack overflow");
            t.stack[t.stack_size++]=t.ip; t.ip=target; return StepResult::continued;
        }
        case COMMAND_CALL: case COMMAND_CALLNOT: {
            if (t.stack_size==t.stack.size()) throw std::runtime_error("LCS call stack overflow");
            const auto metadata=t.ip;
            if (metadata >= (1u<<22)) throw std::runtime_error("Function frame address exceeds LCS encoding");
            const auto inputs=byte(t), outputs=byte(t), offset=byte(t);
            const size_t base=size_t(t.locals_base)+offset;
            if (base+inputs+outputs > 96) throw std::runtime_error("LCS function locals overflow");
            const auto parameters=t.ip;
            const auto target=resolve_label(parameter(t));
            // reLCS writes inputs as it decodes them, while references still use
            // the caller's locals base. Preserve that order even when they alias.
            for (size_t i=0; i<inputs; ++i) t.locals[base+i]=parameter(t);
            const auto length=t.ip-parameters;
            if (length>255) throw std::runtime_error("Function parameters exceed LCS frame encoding");
            t.stack[t.stack_size++]=metadata | (1u<<23) | (length<<24) |
                (op==COMMAND_CALLNOT ? (1u<<22) : 0);
            t.locals_base=uint16_t(base); t.ip=target;
            return StepResult::continued;
        }
        case COMMAND_RETURN: case COMMAND_RETURN_TRUE: case COMMAND_RETURN_FALSE:
            if (op!=COMMAND_RETURN) compare(t,op==COMMAND_RETURN_TRUE);
            return_from_call(t); return StepResult::continued;
        case COMMAND_RETURN_IF_TRUE:
            if (t.condition) return_from_call(t);
            return StepResult::continued;
        case COMMAND_RETURN_TRUE_IF_TRUE:
            // Script8.cpp updates the result to false despite this opcode's name.
            // Preserve the original behavior until comparison with LCS proves otherwise.
            if (t.condition) { compare(t,false); return_from_call(t); }
            return StepResult::continued;
        case COMMAND_TERMINATE_THIS_SCRIPT: t.active=false; return StepResult::terminated;
        case COMMAND_ANDOR: {
            const auto mode=parameter(t);
            if (!(mode==0 || (mode>=1 && mode<=8) || (mode>=21 && mode<=28)))
                throw std::runtime_error("Invalid LCS condition group");
            t.and_or=uint16_t(mode); t.condition=mode>=1 && mode<=8; return StepResult::continued;
        }
        case COMMAND_SCRIPT_NAME: {
            t.name=text_label(t); return StepResult::continued;
        }
        case COMMAND_SET_DEATHARREST_STATE:
            t.death_arrest_enabled=parameter(t)==1;return StepResult::continued;
        case COMMAND_HAS_DEATHARREST_BEEN_EXECUTED:
            compare(t,t.death_arrest_executed);return StepResult::continued;
        case COMMAND_IS_E3_BUILD: case COMMAND_IS_MULTIPLAYER_ACTIVE:
        case COMMAND_IS_MULTIPLAYER_SERVER: case COMMAND_IS_MULTIPLAYER_TEAM_GAME:
            compare(t,false);return StepResult::continued;
        case COMMAND_IS_PC_VERSION:
            compare(t,true);return StepResult::continued;
        case COMMAND_SET_VAR_INT_TO_VAR_INT: case COMMAND_SET_LVAR_INT_TO_LVAR_INT:
        case COMMAND_SET_VAR_FLOAT_TO_VAR_FLOAT: case COMMAND_SET_LVAR_FLOAT_TO_LVAR_FLOAT:
        case COMMAND_SET_VAR_FLOAT_TO_LVAR_FLOAT: case COMMAND_SET_LVAR_FLOAT_TO_VAR_FLOAT:
        case COMMAND_SET_VAR_INT_TO_LVAR_INT: case COMMAND_SET_LVAR_INT_TO_VAR_INT: {
            const auto dest=reference(t); const auto value=variable(t); set(t,dest,value); return StepResult::continued;
        }
        case COMMAND_SET_VAR_INT_TO_CONSTANT: case COMMAND_SET_LVAR_INT_TO_CONSTANT: {
            const auto dest=reference(t);const auto value=parameter(t);set(t,dest,value);return StepResult::continued;
        }
        case COMMAND_IS_INT_VAR_EQUAL_TO_NUMBER: case COMMAND_IS_INT_LVAR_EQUAL_TO_NUMBER:
        case COMMAND_IS_INT_VAR_EQUAL_TO_CONSTANT:case COMMAND_IS_INT_LVAR_EQUAL_TO_CONSTANT:
        case COMMAND_IS_INT_VAR_EQUAL_TO_INT_VAR: case COMMAND_IS_INT_VAR_EQUAL_TO_INT_LVAR:
        case COMMAND_IS_INT_LVAR_EQUAL_TO_INT_LVAR: {
            const auto a=parameter(t), b=parameter(t); compare(t,a==b); return StepResult::continued;
        }
        case COMMAND_IS_FLOAT_VAR_EQUAL_TO_NUMBER: case COMMAND_IS_FLOAT_LVAR_EQUAL_TO_NUMBER:
        case COMMAND_IS_FLOAT_VAR_EQUAL_TO_FLOAT_VAR: case COMMAND_IS_FLOAT_VAR_EQUAL_TO_FLOAT_LVAR:
        case COMMAND_IS_FLOAT_LVAR_EQUAL_TO_FLOAT_LVAR: {
            const auto a=as_float(parameter(t)), b=as_float(parameter(t)); compare(t,a==b); return StepResult::continued;
        }
        case COMMAND_DOT_PRODUCT_2D:case COMMAND_DOT_PRODUCT_3D: {
            const size_t count=op==COMMAND_DOT_PRODUCT_2D?2:3;
            std::array<float,3> a{},b{};
            for(size_t i=0;i<count;++i)a[i]=as_float(parameter(t));
            for(size_t i=0;i<count;++i)b[i]=as_float(parameter(t));
            float result=a[0]*b[0]+a[1]*b[1];
            if(count==3)result+=a[2]*b[2];
            store(t,as_bits(result));return StepResult::continued;
        }
        default: {
            const auto result=backend ? backend(*this,t,op) : StepResult::unsupported;
            if (result!=StepResult::unsupported) return result;
            t.ip=t.last_ip;
            t.diagnostic=std::string("Implementation required: ")+opcode_name(op)+
                (t.diagnostic.empty() ? "" : "; "+t.diagnostic);
            return StepResult::unsupported;
        }
        }
    } catch (const std::exception& e) {
        t.ip=t.last_ip; t.diagnostic=e.what(); t.active=false; return StepResult::failed;
    }
}
StepResult Vm::tick(Thread& t, uint32_t now, size_t budget) {
    if (!t.active) return StepResult::terminated;
    if (t.waiting && int32_t(now-t.wake_time)<0 && !(t.message_wait && message_skip && message_skip())) return StepResult::yielded;
    t.waiting=false;t.message_wait=false; t.diagnostic.clear();
    for (size_t i=0; i<budget; ++i) {
        const auto result=step(t,now);
        if (result!=StepResult::continued) return result;
    }
    t.diagnostic="Instruction budget reached";
    return StepResult::yielded;
}
}
