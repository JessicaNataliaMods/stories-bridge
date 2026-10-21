#pragma once
#include "vm.hpp"
#include <optional>
#include <type_traits>
#include <utility>

namespace lcs {
template<class Adapter,class=void>struct HasOpcodeOwnership:std::false_type{};
template<class Adapter>struct HasOpcodeOwnership<Adapter,std::void_t<decltype(std::declval<const Adapter&>().handles(uint16_t{}))>>:std::true_type{};
// Unsupported has two meanings in the existing adapters: not my opcode, or
// my opcode failed. Only the former may fall through. A claimed instruction
// retains its original diagnostic/IP until Vm::step restores last_ip; no
// other adapter may reinterpret the remaining operands or repeat side effects.
template<class Adapter>
std::optional<StepResult> try_engine_adapter(Adapter& adapter,Vm& vm,Thread& thread,uint16_t opcode) {
    if constexpr(HasOpcodeOwnership<Adapter>::value) {
        if(!adapter.handles(opcode))return {};
        return adapter.dispatch(vm,thread,opcode);
    }
    // Legacy optional profiles have conditional ownership; retain their
    // consumption/diagnostic boundary until their dispatch API is migrated.
    const auto input_ip=thread.ip;
    const auto input_diagnostic=thread.diagnostic;
    const auto result=adapter.dispatch(vm,thread,opcode);
    if(result==StepResult::unsupported && thread.ip==input_ip && thread.diagnostic==input_diagnostic)
        return {};
    return result;
}
}
