#include "lcs/vm.hpp"
#include "ScriptCommands.h"
#include <iostream>
#include <stdexcept>

namespace {
void check(bool ok,const char* name) { if (!ok) throw std::runtime_error(name); }
struct Code {
    std::vector<uint8_t> bytes;
    uint32_t label() const { return uint32_t(64+bytes.size()); }
    void op(uint16_t value) { bytes.push_back(uint8_t(value)); bytes.push_back(uint8_t(value>>8)); }
    void integer(int32_t value) {
        bytes.push_back(6);
        for (int i=0;i<4;++i) bytes.push_back(uint8_t(uint32_t(value)>>(8*i)));
    }
    size_t target() { bytes.push_back(6); const auto p=bytes.size(); bytes.resize(p+4); return p; }
    void patch(size_t p,int32_t value) {
        for (int i=0;i<4;++i) bytes.at(p+i)=uint8_t(uint32_t(value)>>(8*i));
    }
    void local(uint8_t index) { bytes.push_back(uint8_t(12+index)); }
    void global(uint8_t index) { bytes.push_back(204); bytes.push_back(index); }
    size_t call(uint16_t command,uint8_t inputs,uint8_t outputs,uint8_t offset) {
        op(command); bytes.push_back(inputs); bytes.push_back(outputs); bytes.push_back(offset); return target();
    }
    void assign(uint8_t index,int32_t value) { op(COMMAND_SET_LVAR_INT); local(index); integer(value); }
    lcs::Vm vm() const {
        std::vector<uint8_t> memory(64);
        memory.insert(memory.end(),bytes.begin(),bytes.end());
        return lcs::Vm(std::move(memory),64,64);
    }
};
lcs::Thread start() { lcs::Thread t; t.ip=64; return t; }

void inputs_outputs_and_wait() {
    Code code;
    const auto target=code.call(COMMAND_CALL,2,2,4);
    code.global(2); code.local(0);
    code.local(0); code.global(3);
    code.op(COMMAND_TERMINATE_THIS_SCRIPT);
    const auto function=code.label(); code.patch(target,int32_t(function));
    code.op(COMMAND_WAIT); code.integer(5);
    code.assign(2,50); code.assign(3,70);
    code.op(COMMAND_RETURN_TRUE);
    auto vm=code.vm(); auto t=start(); t.locals[0]=5;
    vm.memory[8]=10;
    check(vm.step(t,100)==lcs::StepResult::continued,"CALL executes");
    check(t.ip==function && t.locals_base==4 && t.stack_size==1,"CALL enters frame");
    check(t.locals[4]==10 && t.locals[5]==5,"CALL decodes LCS input references");
    check(t.stack[0]==(66u | (1u<<23) | (8u<<24)),"CALL preserves packed LCS frame");
    check(vm.tick(t,100)==lcs::StepResult::yielded && t.stack_size==1,"function can yield");
    check(vm.tick(t,104)==lcs::StepResult::yielded,"function wake time holds");
    check(vm.tick(t,105)==lcs::StepResult::terminated,"function resumes and returns");
    check(t.locals_base==0 && t.stack_size==0 && t.condition,"RETURN_TRUE restores caller");
    check(t.locals[0]==50 && lcs::read32(vm.memory,12)==70,"function copies local and global outputs");
}

void nested_calls_and_gosub() {
    Code code;
    const auto outer=code.call(COMMAND_CALLNOT,1,1,2);
    code.integer(10); code.local(0);
    code.op(COMMAND_TERMINATE_THIS_SCRIPT);
    code.patch(outer,int32_t(code.label()));
    const auto inner=code.call(COMMAND_CALLNOT,1,1,3);
    code.local(0); code.local(1);
    code.op(COMMAND_RETURN);
    // Negative labels use MainScriptSize, including inside a function.
    code.patch(inner,-int32_t(code.label()-64));
    code.assign(1,99);
    code.op(COMMAND_GOSUB); const auto sub=code.target();
    code.op(COMMAND_RETURN_FALSE);
    code.patch(sub,int32_t(code.label()));
    code.op(COMMAND_ADD_VAL_TO_INT_LVAR); code.local(1); code.integer(1);
    code.op(COMMAND_RETURN);
    auto vm=code.vm(); auto t=start();
    check(vm.tick(t,0)==lcs::StepResult::terminated,"nested CALLNOT and GOSUB terminate");
    check(t.locals[0]==100 && t.locals_base==0 && t.stack_size==0,"nested frames restore output destinations");
    check(!t.condition,"each CALLNOT inverts its own result");
}

void aliasing() {
    Code inputs;
    const auto input_target=inputs.call(COMMAND_CALL,2,0,0);
    inputs.local(1); inputs.local(0);
    inputs.op(COMMAND_TERMINATE_THIS_SCRIPT);
    inputs.patch(input_target,int32_t(inputs.label())); inputs.op(COMMAND_RETURN);
    auto input_vm=inputs.vm(); auto input_thread=start();
    input_thread.locals[0]=11; input_thread.locals[1]=22;
    check(input_vm.tick(input_thread,0)==lcs::StepResult::terminated,"aliased inputs terminate");
    check(input_thread.locals[0]==22 && input_thread.locals[1]==22,"inputs are written as collected, matching reLCS");

    Code outputs;
    const auto output_target=outputs.call(COMMAND_CALL,0,2,0);
    outputs.local(1); outputs.local(0);
    outputs.op(COMMAND_TERMINATE_THIS_SCRIPT);
    outputs.patch(output_target,int32_t(outputs.label())); outputs.op(COMMAND_RETURN);
    auto output_vm=outputs.vm(); auto output_thread=start();
    output_thread.locals[0]=11; output_thread.locals[1]=22;
    check(output_vm.tick(output_thread,0)==lcs::StepResult::terminated,"aliased outputs terminate");
    check(output_thread.locals[0]==22 && output_thread.locals[1]==11,"return snapshots values before writing outputs");

    Code array;
    const auto array_target=array.call(COMMAND_CALL,0,2,4);
    array.local(0);
    array.bytes.insert(array.bytes.end(),{109,0,3}); // Local array: base 1, index in local 0, size 3.
    array.op(COMMAND_TERMINATE_THIS_SCRIPT);
    array.patch(array_target,int32_t(array.label()));
    array.assign(0,1); array.assign(1,777); array.op(COMMAND_RETURN);
    auto array_vm=array.vm(); auto array_thread=start();
    check(array_vm.tick(array_thread,0)==lcs::StepResult::terminated,"array output terminates");
    check(array_thread.locals[0]==1 && array_thread.locals[2]==777 && array_thread.locals[1]==0,
          "output array uses restored caller locals and the preceding output's index");
}

void conditional_returns() {
    Code code;
    const auto target=code.call(COMMAND_CALL,0,0,0);
    code.op(COMMAND_TERMINATE_THIS_SCRIPT);
    code.patch(target,int32_t(code.label()));
    code.op(COMMAND_IS_INT_LVAR_EQUAL_TO_NUMBER); code.local(0); code.integer(1);
    code.op(COMMAND_RETURN_IF_TRUE);
    code.assign(0,42);
    code.op(COMMAND_IS_INT_LVAR_EQUAL_TO_NUMBER); code.local(0); code.integer(42);
    code.op(COMMAND_RETURN_IF_TRUE);
    code.op(COMMAND_CREATE_CHAR); // Reaching this would fail instead of hiding a broken return.
    auto vm=code.vm(); auto t=start();
    check(vm.tick(t,0)==lcs::StepResult::terminated && t.locals[0]==42 && t.condition,
          "conditional return tests both false and true paths");

    Code legacy;
    const auto legacy_target=legacy.call(COMMAND_CALLNOT,0,0,0);
    legacy.op(COMMAND_TERMINATE_THIS_SCRIPT);
    legacy.patch(legacy_target,int32_t(legacy.label()));
    legacy.op(COMMAND_IS_INT_LVAR_EQUAL_TO_NUMBER); legacy.local(0); legacy.integer(0);
    legacy.op(COMMAND_RETURN_TRUE_IF_TRUE);
    auto legacy_vm=legacy.vm(); auto legacy_thread=start();
    check(legacy_vm.tick(legacy_thread,0)==lcs::StepResult::terminated && legacy_thread.condition,
          "RETURN_TRUE_IF_TRUE preserves Script8.cpp's false result before CALLNOT inversion");
}

void bounds() {
    Code locals;
    locals.call(COMMAND_CALL,96,1,1);
    auto locals_vm=locals.vm(); auto locals_thread=start();
    check(locals_vm.step(locals_thread,0)==lcs::StepResult::failed && locals_thread.stack_size==0,
          "function locals overflow is rejected before pushing a frame");

    Code recursion;
    const auto recursive_target=recursion.call(COMMAND_CALL,0,0,0);
    recursion.patch(recursive_target,64);
    auto recursive_vm=recursion.vm(); auto recursive_thread=start();
    check(recursive_vm.tick(recursive_thread,0)==lcs::StepResult::failed && recursive_thread.stack_size==16,
          "recursive function cannot overrun the 16-frame LCS stack");
    check(recursive_thread.diagnostic.find("overflow")!=std::string::npos,"recursion reports its failure");

    Code length;
    const auto length_target=length.call(COMMAND_CALL,51,0,0);
    for (int i=0;i<51;++i) length.integer(i);
    length.op(COMMAND_TERMINATE_THIS_SCRIPT);
    length.patch(length_target,int32_t(length.label())); length.op(COMMAND_RETURN);
    auto length_vm=length.vm(); auto length_thread=start();
    check(length_vm.step(length_thread,0)==lcs::StepResult::failed && length_thread.stack_size==0,
          "parameter bytes cannot overflow the packed LCS frame");

    Code truncated;
    truncated.op(COMMAND_CALL); truncated.bytes.insert(truncated.bytes.end(),{1,0});
    auto truncated_vm=truncated.vm(); auto truncated_thread=start();
    check(truncated_vm.step(truncated_thread,0)==lcs::StepResult::failed && truncated_thread.ip==64,
          "truncated descriptor fails at the original opcode address");
}
}

void gosub_file() {
    Code code;code.op(COMMAND_GOSUB_FILE);const auto target=code.target();code.integer(123);
    code.op(COMMAND_TERMINATE_THIS_SCRIPT);code.patch(target,int32_t(code.label()));
    code.assign(0,91);code.op(COMMAND_RETURN);
    auto vm=code.vm();auto t=start();
    check(vm.tick(t,0)==lcs::StepResult::terminated && t.locals[0]==91 && t.stack_size==0,"GOSUB_FILE consumes both operands and returns after filename identifier");
    Code truncated;truncated.op(COMMAND_GOSUB_FILE);truncated.integer(64);truncated.bytes.push_back(6);
    vm=truncated.vm();t=start();
    check(vm.step(t,0)==lcs::StepResult::failed && t.stack_size==0,"truncated GOSUB_FILE filename cannot create a return frame");
}
int main() {
    try {
        inputs_outputs_and_wait(); nested_calls_and_gosub(); aliasing(); conditional_returns(); bounds();gosub_file();
        std::cout<<"PASS: LCS function frames, arguments, outputs, aliasing, nested CALLNOT/GOSUB, waits, returns and bounds\n";
        return 0;
    } catch (const std::exception& e) { std::cerr<<"FAIL: "<<e.what()<<'\n'; return 1; }
}
