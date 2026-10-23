#pragma once
#include "lcs/scm.hpp"
#include "ScriptCommands.h"
#include <array>
#include <algorithm>
#include <optional>
#include <set>
#include <vector>
#include <stdexcept>

namespace lcs::radar_origin_detail {
// Decode only, using the same primary signatures as the offline SCM audit.
// No script instruction, native command or branch predicate is executed here.
struct AuditSpec {std::vector<char> inputs;size_t outputs;};
inline std::optional<AuditSpec> signature(uint16_t op){
    if(op==COMMAND_GOSUB_FILE)return AuditSpec{{'l','n'},0};
    switch(op){
#include "audit_specs.inc"
    default:return {};
    }
}
struct Operand {int local=-1;bool constant=false;int32_t value=0;bool local_array=false;};
struct Cursor {
    const ScmImage& image;uint32_t ip;
    uint32_t bytes(unsigned count){
        if(uint64_t(ip)+count>image.main_size || uint64_t(ip)+count>image.bytes.size())throw std::runtime_error("Radar origin operand outside MAIN");
        uint32_t value=0;for(unsigned i=0;i<count;++i)value|=uint32_t(image.bytes[ip++])<<(8*i);return value;
    }
    Operand number(){
        const auto kind=bytes(1);
        switch(kind){
        case 0:throw std::runtime_error("Unexpected radar origin terminator");
        case 1:case 2:return {-1,true,0};
        case 3:return {-1,true,int32_t(bytes(1)<<24)};
        case 4:return {-1,true,int32_t(bytes(2)<<16)};
        case 5:return {-1,true,int32_t(bytes(3)<<8)};
        case 6:case 9:return {-1,true,int32_t(bytes(4))};
        case 7:return {-1,true,int8_t(bytes(1))};
        case 8:return {-1,true,int16_t(bytes(2))};
        default:
            if(kind>=230){bytes(3);return {};}
            if(kind>=204){bytes(1);return {};}
            if(kind>=108){bytes(2);return {-1,false,0,true};}
            return {kind>=12?int(kind)-12:104+int(kind)-10,false,0};
        }
    }
};
inline bool writes_first(uint16_t op){
    return (op>=COMMAND_SET_VAR_INT && op<=COMMAND_DIV_FLOAT_LVAR_BY_VAL) ||
        (op>=COMMAND_SET_VAR_INT_TO_VAR_INT && op<=COMMAND_SET_LVAR_INT_TO_VAR_INT) ||
        (op>=COMMAND_ADD_INT_VAR_TO_INT_VAR && op<=COMMAND_DIV_FLOAT_LVAR_BY_FLOAT_VAR) ||
        (op>=COMMAND_ADD_TIMED_VAL_TO_FLOAT_VAR && op<=COMMAND_SUB_TIMED_FLOAT_VAR_FROM_FLOAT_LVAR) ||
        (op>=COMMAND_CSET_VAR_INT_TO_VAR_FLOAT && op<=COMMAND_CSET_LVAR_FLOAT_TO_LVAR_INT) ||
        op==COMMAND_SET_VAR_INT_TO_CONSTANT || op==COMMAND_SET_LVAR_INT_TO_CONSTANT ||
        op==COMMAND_ABS_VAR_INT || op==COMMAND_ABS_LVAR_INT || op==COMMAND_ABS_VAR_FLOAT || op==COMMAND_ABS_LVAR_FLOAT;
}
// A named controller's initial local constants are useful only if every
// reachable writer (including shared-frame GOSUBs) preserves those locals.
// CALL frames above them cannot write below their own locals_base; caller
// output operands are still checked. Unknown/dynamic control flow is refused.
inline bool preserved(const ScmImage& image,uint32_t entry,uint32_t creator,
    const std::array<int,3>& protected_locals,const std::set<uint32_t>& initializers){
    std::vector<uint32_t> pending{entry};std::set<uint32_t> seen;bool found=false;
    const auto touches=[&](const Operand& a){return a.local_array || (a.local>=0 &&
        std::find(protected_locals.begin(),protected_locals.end(),a.local)!=protected_locals.end());};
    const int highest=*std::max_element(protected_locals.begin(),protected_locals.end());
    while(!pending.empty()){
        const auto at=pending.back();pending.pop_back();if(!seen.insert(at).second)continue;
        if(seen.size()>4096)return false;
        try {
            if(at<image.entry_point || at>=image.main_size)return false;
            Cursor c{image,at};const auto op=uint16_t(c.bytes(2)&0x7fff);if(at==creator)found=true;
            std::vector<Operand> inputs,outputs,labels;
            if(op==COMMAND_CALL || op==COMMAND_CALLNOT){
                const auto count=c.bytes(1),out=c.bytes(1),offset=c.bytes(1);
                if(count+out+offset>96 || int(offset)<=highest)return false;
                c.number();for(unsigned i=0;i<count;++i)c.number();
                for(unsigned i=0;i<out;++i)outputs.push_back(c.number());
            }else{
                const auto spec=signature(op);if(!spec)return false;
                for(const auto kind:spec->inputs){
                    if(kind=='s'){c.bytes(4);c.bytes(4);continue;}
                    const auto a=c.number();inputs.push_back(a);if(kind=='l')labels.push_back(a);
                }
                for(size_t i=0;i<spec->outputs;++i)outputs.push_back(c.number());
                if(op==COMMAND_START_NEW_SCRIPT){unsigned count=0;while(c.bytes(1)!=0){--c.ip;c.number();if(++count>96)return false;}}
            }
            for(const auto& a:outputs)if(touches(a))return false;
            if(writes_first(op) && !inputs.empty() && touches(inputs[0]) && !initializers.count(at))return false;
            if(op==COMMAND_GOTO || op==COMMAND_GOTO_IF_TRUE || op==COMMAND_GOTO_IF_FALSE || op==COMMAND_GOSUB || op==COMMAND_GOSUB_FILE){
                if(labels.size()!=1 || !labels[0].constant || labels[0].value<int32_t(image.entry_point) || uint32_t(labels[0].value)>=image.main_size)return false;
                pending.push_back(uint32_t(labels[0].value));
            }
            if(op!=COMMAND_GOTO && op!=COMMAND_RETURN && op!=COMMAND_RETURN_TRUE && op!=COMMAND_RETURN_FALSE && op!=COMMAND_TERMINATE_THIS_SCRIPT)
                pending.push_back(c.ip);
        }catch(...){return false;}
    }
    return found;
}
inline std::optional<std::array<int32_t,3>> controller_coordinates(const ScmImage& image,uint32_t creator,const std::array<int,3>& locals){
    // Controllers are independently launched at SCRIPT_NAME, followed by
    // literal SET_LVAR_* initializers. The launch proves an actual entry;
    // the reachability/write check proves that the creator sees those values.
    for(uint32_t at=image.entry_point;uint64_t(at)+2<image.main_size;++at){
        if(read16(image.bytes,at)!=COMMAND_START_NEW_SCRIPT)continue;
        try {
            Cursor launch{image,at+2};const auto label=launch.number();
            if(!label.constant || label.value<int32_t(image.entry_point) || uint32_t(label.value)>creator || launch.bytes(1)!=0)continue;
            Cursor c{image,uint32_t(label.value)};if(c.bytes(2)!=COMMAND_SCRIPT_NAME)continue;c.bytes(4);c.bytes(4);
            std::array<std::optional<int32_t>,96> values{};std::set<uint32_t> initializers;
            for(unsigned i=0;i<96;++i){
                const auto start=c.ip;const auto op=c.bytes(2);
                if(op!=COMMAND_SET_LVAR_FLOAT && op!=COMMAND_SET_LVAR_INT)break;
                const auto dest=c.number(),value=c.number();
                if(dest.local<0 || dest.local>=96 || !value.constant)break;
                values[size_t(dest.local)]=value.value;initializers.insert(start);
            }
            std::array<int32_t,3> result{};bool complete=true;
            for(size_t i=0;i<3;++i)if(locals[i]>=0){
                if(locals[i]>=96 || !values[size_t(locals[i])]){complete=false;break;}
                result[i]=*values[size_t(locals[i])];
            }
            if(complete && preserved(image,uint32_t(label.value),creator,locals,initializers))return result;
        }catch(...){continue;}
    }
    return {};
}
}
