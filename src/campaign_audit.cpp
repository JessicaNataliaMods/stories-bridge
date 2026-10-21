#include "lcs/scm.hpp"
#include "ScriptCommands.h"
#include <deque>
#include <set>
#include <map>
#include <optional>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <iomanip>

// Conservative control-flow inventory: both sides of conditions, every mission
// entry and MAIN. No command is executed, and no predicates/handles are simulated.
struct AuditSpec {std::vector<char> inputs;size_t outputs;};
static std::optional<AuditSpec> signature(uint16_t opcode) {
    // ScriptDebug.cpp omits or misstates these signatures. The executable
    // implementations in Script.cpp and Script2/3/4/8/10.cpp take precedence.
    switch(opcode) {
    case COMMAND_GOSUB_FILE:return AuditSpec{{'l','n'},0}; // Script3.cpp
    case COMMAND_CHAR_FOLLOW_PATH:return AuditSpec{{'n','n','n','n','n','n'},0}; // Script.cpp
    case COMMAND_INDUSTRIAL_PASSED:case COMMAND_COMMERCIAL_PASSED:case COMMAND_SUBURBAN_PASSED:
        return AuditSpec{{'n'},0}; // Script4.cpp
    case COMMAND_SET_PICKUP_VALUE:return AuditSpec{{'n','n'},0}; // Script10.cpp
    case COMMAND_ADD_PAGER_MESSAGE:return AuditSpec{{'s','n','n','n'},0}; // Script2.cpp
    case COMMAND_PRINT_HELP_NO_BRIEF:case COMMAND_PRINT_HELP_FOREVER_NO_BRIEF:
    case COMMAND_PRINT_HELP_ALWAYS_NO_BRIEF:case COMMAND_PRINT_HELP_FOREVER_ALWAYS_NO_BRIEF:
        return AuditSpec{{'s'},0}; // Script10.cpp
    case COMMAND_CREATE_SCRIPT_CORONA:return AuditSpec{{'n','n','n','n','n','n','n','n','n'},1}; // Script8.cpp
    case COMMAND_GET_RANDOM_CHAR_IN_AREA_NO_CHECKS:case COMMAND_GET_RANDOM_CAR_IN_AREA_NO_CHECKS_NO_SAVE:
        return AuditSpec{{'n','n','n','n'},1}; // Script8.cpp
    default:break;
    }
    switch(opcode) {
#include "audit_specs.inc"
    default:return {};
    }
}
static std::map<uint16_t,std::set<std::string>> handler_clauses() {
    std::map<uint16_t,std::set<std::string>> result;
#include "audit_handlers.inc"
    for(uint16_t op=COMMAND_SET_VAR_INT;op<=COMMAND_DIV_FLOAT_LVAR_BY_VAL;++op)result[op].insert("vm");
    for(uint16_t op=COMMAND_ADD_INT_VAR_TO_INT_VAR;op<=COMMAND_DIV_FLOAT_LVAR_BY_FLOAT_VAR;++op)result[op].insert("vm");
    for(uint16_t op=COMMAND_ADD_TIMED_VAL_TO_FLOAT_VAR;op<=COMMAND_SUB_TIMED_FLOAT_VAR_FROM_FLOAT_LVAR;++op)result[op].insert("vm");
    for(uint16_t op=COMMAND_CSET_VAR_INT_TO_VAR_FLOAT;op<=COMMAND_CSET_LVAR_FLOAT_TO_LVAR_INT;++op)result[op].insert("vm");
    for(uint16_t op=COMMAND_IS_INT_VAR_GREATER_THAN_NUMBER;op<=COMMAND_IS_FLOAT_LVAR_GREATER_OR_EQUAL_TO_FLOAT_VAR;++op)result[op].insert("vm");
    return result;
}
struct Number {bool constant;int32_t value;};
struct Cursor {
    const std::vector<uint8_t>& memory;size_t ip,main,end;
    void require(size_t count) {const auto limit=ip<main?main:end;if(ip>limit || count>limit-ip)throw std::runtime_error("Operand outside loaded code");}
    uint8_t byte(){require(1);return memory[ip++];}
    uint32_t bytes(unsigned n){uint32_t v=0;for(unsigned i=0;i<n;++i)v|=uint32_t(byte())<<(i*8);return v;}
    Number number(bool variable=false) {
        const auto kind=byte();
        if(variable && kind<10)throw std::runtime_error("Output is not a variable reference");
        switch(kind) {
        case 0:throw std::runtime_error("Unexpected argument terminator");
        case 1:case 2:return {true,0};
        case 3:return {true,int32_t(bytes(1)<<24)};
        case 4:return {true,int32_t(bytes(2)<<16)};
        case 5:return {true,int32_t(bytes(3)<<8)};
        case 6:case 9:return {true,int32_t(bytes(4))};
        case 7:return {true,int8_t(bytes(1))};
        case 8:return {true,int16_t(bytes(2))};
        default:
            if(kind>=230)bytes(3);
            else if(kind>=204)bytes(1);
            else if(kind>=108)bytes(2);
            return {false,0};
        }
    }
    std::string text(){require(8);std::string s;for(int i=0;i<8;++i){const auto c=memory[ip+i];if(!c)break;s+=char(c);}ip+=8;return s;}
};
static std::string quote(const std::string& value) {
    std::ostringstream s;s<<'"';
    for(unsigned char c:value) {
        if(c=='"' || c=='\\')s<<'\\'<<char(c);
        else if(c<32 || c>=127)s<<"\\u00"<<std::hex<<std::setw(2)<<std::setfill('0')<<unsigned(c);
        else s<<char(c);
    }
    return s.str()+'"';
}
struct Use {
    std::set<std::pair<int,uint32_t>> sites;
    std::set<int> contexts;
    std::set<std::string> texts;
};
int main(int argc,char** argv) {
    try {
        const bool compatibility_sites=argc==3 && std::string(argv[2])=="--compat-sites";
        if(argc!=2 && !compatibility_sites)throw std::runtime_error("Usage: lcs-campaign-audit MAIN.SCM [--compat-sites]");
        // Focused provenance for single-player scope decisions. Reuse the
        // existing decoded sites; never search raw bytes as if they were code.
        const std::set<uint16_t> focused{195,196,940,941,942,1448,1450,1492,1552,
            1570,1571,1574,1630,1498,1499,1504,1507,1508,1509,1583};
        const auto image=lcs::ScmImage::load(argv[1]);
        const auto handlers=handler_clauses();
        std::map<uint16_t,Use> uses;
        std::vector<std::set<uint16_t>> mission_commands(image.mission_offsets.size());
        std::vector<std::set<std::string>> mission_names(image.mission_offsets.size());
        std::vector<std::string> errors;
        size_t unresolved=0,contexts=0;
        for(int context=-1;context<int(image.mission_offsets.size());++context) {
            auto memory=std::vector<uint8_t>(image.bytes.begin(),image.bytes.begin()+image.main_size);
            if(context>=0){const auto mission=image.mission(size_t(context));memory.insert(memory.end(),mission.begin(),mission.end());}
            std::deque<std::pair<uint32_t,uint32_t>> pending{{image.entry_point,image.entry_point}};
            // MAIN is scanned once in context -1. Focused mission walks start
            // only at their entry, so global helper calls become real mission
            // dependencies instead of inheriting every unrelated MAIN thread.
            if(compatibility_sites && context>=0)pending.clear();
            if(context>=0)pending.push_back({image.main_size,image.main_size});
            std::set<uint32_t> decoded;
            std::vector<int32_t> owner(memory.size(),-1);
            while(!pending.empty()) {
                const auto at=pending.front().first,from=pending.front().second;pending.pop_front();
                if(!decoded.insert(at).second)continue;
                try {
                    if(at>=memory.size() || at<image.entry_point)throw std::runtime_error("Code target outside executable script region");
                    if(owner[at]>=0 && uint32_t(owner[at])!=at)throw std::runtime_error("Code target enters another instruction's operands");
                    Cursor cursor{memory,at,image.main_size,memory.size()};
                    const auto opcode=uint16_t(cursor.bytes(2)&0x7fff);
                    auto spec=signature(opcode);
                    if(!spec)throw std::runtime_error(std::string("No primary decode signature: ")+lcs::opcode_name(opcode));
                    std::set<std::string> texts;
                    std::vector<Number> labels;
                    if(opcode==COMMAND_CALL || opcode==COMMAND_CALLNOT) {
                        const auto inputs=cursor.byte(),outputs=cursor.byte(),offset=cursor.byte();
                        if(unsigned(inputs)+outputs+offset>96)throw std::runtime_error("Invalid function frame");
                        labels.push_back(cursor.number());
                        for(unsigned i=0;i<inputs;++i)cursor.number();
                        for(unsigned i=0;i<outputs;++i)cursor.number(true);
                    } else {
                        for(const auto kind:spec->inputs) {
                            if(kind=='s')texts.insert(cursor.text());
                            else {const auto value=cursor.number();if(kind=='l')labels.push_back(value);}
                        }
                        for(size_t i=0;i<spec->outputs;++i)cursor.number(true);
                        if(opcode==COMMAND_START_NEW_SCRIPT) {
                            unsigned args=0;
                            while(cursor.byte()!=0){--cursor.ip;cursor.number();if(++args>96)throw std::runtime_error("Too many thread arguments");}
                        }
                    }
                    for(size_t p=at;p<cursor.ip;++p) {
                        if(owner[p]>=0 && uint32_t(owner[p])!=at)throw std::runtime_error("Decoded instructions overlap");
                        owner[p]=int32_t(at);
                    }
                    for(const auto label:labels) {
                        if(!label.constant){++unresolved;continue;}
                        const auto target=label.value>=0?int64_t(label.value):int64_t(image.main_size)-label.value;
                        if(target<0 || uint64_t(target)>=memory.size()) {
                            // MAIN may refer to a mission-local function when no
                            // mission is loaded in the standalone MAIN context.
                            if(context<0 && label.value<0){++unresolved;continue;}
                            throw std::runtime_error("Static label outside loaded script region");
                        }
                        pending.push_back({uint32_t(target),at});
                    }
                    auto& use=uses[opcode];
                    use.sites.insert({at<image.main_size?-1:context,at});use.contexts.insert(context);
                    use.texts.insert(texts.begin(),texts.end());
                    if(context>=0 && at>=image.main_size) {
                        mission_commands[size_t(context)].insert(opcode);
                        if(opcode==COMMAND_SCRIPT_NAME)mission_names[size_t(context)].insert(texts.begin(),texts.end());
                    }
                    if(opcode!=COMMAND_GOTO && opcode!=COMMAND_TERMINATE_THIS_SCRIPT && opcode!=COMMAND_RETURN &&
                       opcode!=COMMAND_RETURN_TRUE && opcode!=COMMAND_RETURN_FALSE && cursor.ip<memory.size())
                        pending.push_back({uint32_t(cursor.ip),at});
                }catch(const std::exception& error) {
                    errors.push_back("context="+std::to_string(context)+" ip="+std::to_string(at)+" from="+std::to_string(from)+" "+error.what());
                }
            }
            ++contexts;
        }
        size_t no_handler=0,sites=0;
        for(const auto& entry:uses){sites+=entry.second.sites.size();if(!handlers.count(entry.first))++no_handler;}
        std::cout<<"{\n\"analysis\":\""<<(compatibility_sites?"mission_entry_dependency_control_flow":"conservative_control_flow")
                 <<"\",\"gameExecuted\":false,\"playabilityVerified\":false,"
                 <<"\"sourceHandlerClauseIsNotRuntimeSupport\":true,\n\"missionBlocks\":"<<image.mission_offsets.size()
                 <<",\"contextsScanned\":"<<contexts<<",\"instructionSites\":"<<sites<<",\"uniqueCommands\":"<<uses.size()
                 <<",\"commandsWithoutSourceHandlerClause\":"<<no_handler<<",\"decodeErrors\":"<<errors.size()
                 <<",\"unresolvedDynamicLabels\":"<<unresolved<<",\"inventoryComplete\":"<<(errors.empty() && !unresolved?"true":"false")
                 <<",\n\"commands\":[\n";
        bool comma=false;
        for(const auto& entry:uses) {
            if(comma)std::cout<<",\n";comma=true;
            std::cout<<"{\"opcode\":"<<entry.first<<",\"name\":"<<quote(lcs::opcode_name(entry.first))
                     <<",\"instructionSites\":"<<entry.second.sites.size()<<",\"contexts\":"<<entry.second.contexts.size()<<",\"sourceHandlerClauses\":[";
            bool sep=false;
            if(const auto h=handlers.find(entry.first);h!=handlers.end())for(const auto& name:h->second){if(sep)std::cout<<',';sep=true;std::cout<<quote(name);}
            std::cout<<"],\"textOperands\":[";sep=false;
            for(const auto& text:entry.second.texts){if(sep)std::cout<<',';sep=true;std::cout<<quote(text);}
            std::cout<<']';
            if(compatibility_sites && focused.count(entry.first)) {
                std::cout<<",\"sourceSites\":[";sep=false;
                for(const auto& site:entry.second.sites) {
                    if(sep)std::cout<<',';sep=true;
                    std::cout<<"{\"context\":"<<site.first<<",\"ip\":"<<site.second<<'}';
                }
                std::cout<<']';
                std::cout<<",\"missionEntryContexts\":[";sep=false;
                for(const auto context:entry.second.contexts)if(context>=0) {
                    if(sep)std::cout<<',';sep=true;std::cout<<context;
                }
                std::cout<<']';
            }
            std::cout<<'}';
        }
        std::cout<<"\n],\"missionDetails\":[\n";comma=false;
        for(size_t i=0;i<mission_commands.size();++i) {
            if(comma)std::cout<<",\n";comma=true;
            std::cout<<"{\"index\":"<<i<<",\"scriptNames\":[";bool sep=false;
            for(const auto& name:mission_names[i]){if(sep)std::cout<<',';sep=true;std::cout<<quote(name);}
            std::cout<<"],\"localUniqueCommands\":"<<mission_commands[i].size()<<",\"commandsWithoutSourceHandlerClause\":[";sep=false;
            for(const auto opcode:mission_commands[i])if(!handlers.count(opcode)){if(sep)std::cout<<',';sep=true;std::cout<<quote(lcs::opcode_name(opcode));}
            std::cout<<"]}";
        }
        std::cout<<"\n],\"errors\":[";comma=false;
        for(const auto& error:errors){if(comma)std::cout<<',';comma=true;std::cout<<quote(error);}
        std::cout<<"]}\n";
        return errors.empty() && !unresolved?0:2;
    }catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}
}
