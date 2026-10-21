#include "lcs/vm.hpp"
#include "ScriptCommands.h"
#include <cmath>
#include <cstring>
#include <iostream>
#include <stdexcept>

static void check(bool ok,const char* name) { if(!ok) throw std::runtime_error(name); }
static void word(std::vector<uint8_t>& b,uint16_t v) { b.push_back(uint8_t(v)); b.push_back(uint8_t(v>>8)); }
static void integer(std::vector<uint8_t>& b,int32_t v) {
    b.push_back(6); for(int i=0;i<4;++i) b.push_back(uint8_t(uint32_t(v)>>(8*i)));
}
static void put32(std::vector<uint8_t>& b,size_t p,uint32_t v) { for(int i=0;i<4;++i) b.at(p+i)=uint8_t(v>>(8*i)); }
static lcs::Vm program(std::vector<uint8_t> code) {
    std::vector<uint8_t> b(64); b.insert(b.end(),code.begin(),code.end());
    return lcs::Vm(std::move(b),64,64);
}
static lcs::Thread thread() { lcs::Thread t; t.ip=64; return t; }
int main() {
    try {
        // Real LCS argument encoding, including compact floats and banked references.
        auto vm=program({1,2,7,0xff,8,0x00,0x80,3,0x3f,4,0x80,0x3f,5,0x00,0x80,0x3f,9,0,0,0x80,0x3f});
        auto t=thread();
        check(vm.parameter(t)==0 && vm.parameter(t)==0,"zero constants");
        check(vm.parameter(t)==-1 && vm.parameter(t)==-32768,"signed constants");
        check(vm.parameter(t)==0x3f000000,"float high byte");
        for(int i=0;i<3;++i) check(vm.parameter(t)==0x3f800000,"float reconstruction");
        auto vars=program({204,2,12,10,108,0,3,230,2,0,3}); auto vt=thread();
        vars.store(vt,42); vars.store(vt,1); vars.store(vt,100); vt.locals[1]=77;
        check(vars.variable(vt)==77,"local array");
        put32(vars.memory,12,123); check(vars.variable(vt)==123,"global array");
        std::vector<uint8_t> code;
        word(code,COMMAND_SET_LVAR_INT); code.push_back(12); integer(code,7);
        word(code,COMMAND_ADD_VAL_TO_INT_LVAR); code.push_back(12); integer(code,5);
        word(code,COMMAND_IS_INT_LVAR_EQUAL_TO_NUMBER|0x8000); code.push_back(12); integer(code,13);
        word(code,COMMAND_WAIT); integer(code,10);
        word(code,COMMAND_TERMINATE_THIS_SCRIPT);
        auto math=program(code); auto mt=thread();
        check(math.tick(mt,100)==lcs::StepResult::yielded && mt.locals[0]==12 && mt.condition,"arithmetic and negated predicate");
        check(math.tick(mt,109)==lcs::StepResult::yielded,"WAIT holds");
        check(math.tick(mt,110)==lcs::StepResult::terminated,"WAIT resumes");
        code.clear();word(code,COMMAND_MESSAGE_WAIT);integer(code,100);integer(code,1);
        word(code,COMMAND_TERMINATE_THIS_SCRIPT);
        auto message=program(code);auto message_thread=thread();bool skip=false;
        message.message_skip=[&](){return skip;};
        check(message.tick(message_thread,100)==lcs::StepResult::yielded,"original message waits before timeout");
        check(message.tick(message_thread,150)==lcs::StepResult::yielded,"message duration is retained without input");
        skip=true;
        check(message.tick(message_thread,151)==lcs::StepResult::terminated,"skippable message resumes on actual input callback");
        code.clear();word(code,COMMAND_MESSAGE_WAIT);integer(code,100);integer(code,0);
        word(code,COMMAND_TERMINATE_THIS_SCRIPT);
        auto unskippable=program(code);auto unskippable_thread=thread();unskippable.message_skip=[](){return true;};
        check(unskippable.tick(unskippable_thread,100)==lcs::StepResult::yielded &&
              unskippable.tick(unskippable_thread,199)==lcs::StepResult::yielded &&
              unskippable.tick(unskippable_thread,200)==lcs::StepResult::terminated,"unskippable message keeps original timing");
        // Mission negative labels are relative to the end of the main script, not the disk prefix.
        check(math.resolve_label(-3)==67,"mission branch base");
        code.clear(); word(code,COMMAND_RETURN);
        auto bad=program(code); auto bt=thread();
        check(bad.step(bt,0)==lcs::StepResult::failed,"call stack underflow rejected");
        code.clear(); word(code,COMMAND_CREATE_CHAR);
        auto missing=program(code); auto ut=thread();
        check(missing.step(ut,0)==lcs::StepResult::unsupported && ut.ip==64,"unsupported opcode keeps IP");
        check(ut.diagnostic.find("CREATE_CHAR")!=std::string::npos,"unsupported opcode named");
        code.clear(); word(code,COMMAND_GOTO); integer(code,64);
        auto loop=program(code); auto lt=thread();
        check(loop.tick(lt,0,25)==lcs::StepResult::yielded && lt.active,"bounded loop");
        auto groups=program({}); auto gt=thread(); gt.and_or=2; gt.condition=true;
        groups.compare(gt,true); groups.compare(gt,false);
        check(!gt.condition && gt.and_or==0,"AND group");
        gt.and_or=22; groups.compare(gt,false); groups.compare(gt,true);
        check(gt.condition && gt.and_or==0,"OR group");
        // Preserve signed ordering, operand order and IEEE comparisons; these
        // predicates are VM logic and must not be sent to an SA opcode adapter.
        std::vector<uint8_t> signed_commands;
        word(signed_commands,COMMAND_IS_NUMBER_GREATER_THAN_INT_LVAR); integer(signed_commands,2); signed_commands.push_back(12);
        word(signed_commands,COMMAND_IS_INT_LVAR_GREATER_OR_EQUAL_TO_NUMBER|0x8000); signed_commands.push_back(12); integer(signed_commands,10);
        word(signed_commands,COMMAND_IS_INT_LVAR_GREATER_THAN_INT_VAR); signed_commands.push_back(12); signed_commands.insert(signed_commands.end(),{204,2});
        auto signed_order=program(signed_commands); auto st=thread(); st.locals[0]=-10;
        check(signed_order.step(st,0)==lcs::StepResult::continued && st.condition,"number first uses signed greater-than");
        check(signed_order.step(st,0)==lcs::StepResult::continued && st.condition,"negation applies to greater-or-equal");
        put32(signed_order.memory,8,uint32_t(-11));
        check(signed_order.step(st,0)==lcs::StepResult::continued && st.condition,"signed local/global ordering");
        std::vector<uint8_t> ordering;
        word(ordering,COMMAND_IS_FLOAT_VAR_GREATER_THAN_FLOAT_LVAR); ordering.insert(ordering.end(),{204,2,12});
        word(ordering,COMMAND_IS_FLOAT_LVAR_GREATER_OR_EQUAL_TO_NUMBER); ordering.insert(ordering.end(),{12,9,0,0,0x80,0x3f});
        word(ordering,COMMAND_IS_FLOAT_LVAR_GREATER_OR_EQUAL_TO_FLOAT_VAR|0x8000); ordering.insert(ordering.end(),{12,204,2});
        auto floats=program(ordering); auto ft=thread(); ft.locals[0]=0x3f800000;
        put32(floats.memory,8,0x40000000);
        check(floats.step(ft,0)==lcs::StepResult::continued && ft.condition,"float global/local greater-than");
        check(floats.step(ft,0)==lcs::StepResult::continued && ft.condition,"float greater-or-equal includes equality");
        ft.locals[0]=0x7fc00000;
        check(floats.step(ft,0)==lcs::StepResult::continued && ft.condition,"NaN comparison remains false before inversion");
        bool rejected=false; try { lcs::ScmImage::parse({0,0,0}); } catch(...) { rejected=true; }
        check(rejected,"truncated file rejected");
        // Every integer/float arithmetic combination uses two REFERENCES,
        // unlike ADD_VAL. Real mission camera/audio helpers use these timers.
        for(int group=0;group<4;++group)for(int variant=0;variant<8;++variant) {
            code.clear();word(code,uint16_t(COMMAND_ADD_INT_VAR_TO_INT_VAR+8*group+variant));
            const bool dest_global=variant/2==0 || variant/2==3;
            const bool source_global=variant/2==0 || variant/2==2;
            if(dest_global)code.insert(code.end(),{204,2});else code.push_back(12);
            if(source_global)code.insert(code.end(),{204,3});else code.push_back(13);
            auto calc=program(code);auto ct=thread();
            const auto lhs=variant%2 ? 0x41000000 : 8;
            const auto rhs=variant%2 ? 0x40000000 : 2;
            if(dest_global)put32(calc.memory,8,lhs);else ct.locals[0]=lhs;
            if(source_global)put32(calc.memory,12,rhs);else ct.locals[1]=rhs;
            check(calc.step(ct,0)==lcs::StepResult::continued,"all variable arithmetic opcodes execute in the LCS VM");
            const auto actual=dest_global ? int32_t(lcs::read32(calc.memory,8)) : ct.locals[0];
            const int integers[]={10,6,16,4};const int floats_expected[]={0x41200000,0x40C00000,0x41800000,0x40800000};
            check(actual==(variant%2 ? floats_expected[group] : integers[group]),"variable arithmetic preserves destination and float bits");
        }
        code.clear();word(code,COMMAND_DIV_INT_LVAR_BY_INT_LVAR);code.insert(code.end(),{12,13});
        auto zero=program(code);auto zt=thread();zt.locals[0]=10;
        check(zero.step(zt,0)==lcs::StepResult::failed && zt.locals[0]==10,"division by zero cannot alter mission variables");
        // CSET converts the SOURCE into the DESTINATION type in place. This
        // covers all global/local banks, aliases and signed fractional values.
        for(unsigned variant=0;variant<8;++variant) {
            const bool dest_global=variant/2==0 || variant/2==2;
            const bool source_global=variant/2==0 || variant/2==1;
            code.clear();word(code,uint16_t(COMMAND_CSET_VAR_INT_TO_VAR_FLOAT+variant));
            if(dest_global)code.insert(code.end(),{204,2});else code.push_back(12);
            if(source_global)code.insert(code.end(),{204,3});else code.push_back(13);
            auto cvt=program(code);auto ct=thread();const auto input=variant%2 ? -7 : int32_t(0xc0f80000); // -7.75
            if(source_global)put32(cvt.memory,12,uint32_t(input));else ct.locals[1]=input;
            check(cvt.step(ct,0)==lcs::StepResult::continued,"CSET global/local variants execute");
            check((dest_global?int32_t(lcs::read32(cvt.memory,8)):ct.locals[0])==(variant%2?int32_t(0xc0e00000):-7),"CSET is numeric, signed and truncates toward zero");
        }
        code.clear();word(code,COMMAND_CSET_LVAR_INT_TO_LVAR_FLOAT);code.insert(code.end(),{12,12});
        auto alias=program(code);auto at=thread();at.locals[0]=0x40700000; // 3.75
        check(alias.step(at,0)==lcs::StepResult::continued && at.locals[0]==3,"CSET permits an aliased source/destination");
        for(const auto invalid:{0x7fc00000u,0x7f800000u,0x4f000000u}) {
            auto cvt=program(code);auto ct=thread();ct.locals[0]=int32_t(invalid);
            check(cvt.step(ct,0)==lcs::StepResult::failed && ct.locals[0]==int32_t(invalid),"unrepresentable CSET has no variable write");
        }
        for(unsigned variant=0;variant<12;++variant)for(float timestep:{0.0f,0.5f,2.0f}) {
            code.clear();word(code,uint16_t(COMMAND_ADD_TIMED_VAL_TO_FLOAT_VAR+variant));code.push_back(12);
            if(variant%6<2)integer(code,0x40800000);else code.push_back(13); // delta 4
            auto timed=program(code);auto tt=thread();timed.time_step=timestep;tt.locals[0]=0x41200000;tt.locals[1]=0x40800000; // initial 10
            check(timed.step(tt,0)==lcs::StepResult::continued,"timed arithmetic executes in LCS variable bank");
            float actual;std::memcpy(&actual,&tt.locals[0],4);
            check(actual==(variant<6?10+4*timestep:10-4*timestep),"timed arithmetic follows fractional engine timestep including pause");
        }
        code.clear();word(code,COMMAND_ABS_LVAR_FLOAT);code.push_back(12);
        auto abs_float=program(code);auto af=thread();af.locals[0]=int32_t(0x80000000u);
        check(abs_float.step(af,0)==lcs::StepResult::continued && af.locals[0]==0,"float ABS clears negative zero");
        code.clear();word(code,COMMAND_ABS_LVAR_INT);code.push_back(12);
        auto abs_int=program(code);auto ai=thread();ai.locals[0]=-12;
        check(abs_int.step(ai,0)==lcs::StepResult::continued && ai.locals[0]==12,"integer ABS uses signed magnitude");
        code.clear();word(code,COMMAND_IS_INT_LVAR_EQUAL_TO_CONSTANT|0x8000);code.push_back(12);integer(code,-12);
        auto constant=program(code);auto ci=thread();ci.locals[0]=-12;
        check(constant.step(ci,0)==lcs::StepResult::continued && !ci.condition,"constant equality remains inside LCS and preserves inversion");
        std::cout<<"PASS: LCS parameters, references, arithmetic, branches, waits, conditions, bounds and unsupported-command handling\n";
        return 0;
    } catch(const std::exception& e) { std::cerr<<"FAIL: "<<e.what()<<'\n'; return 1; }
}
