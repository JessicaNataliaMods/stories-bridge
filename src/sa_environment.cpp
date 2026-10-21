#include <windows.h>
#include "lcs/sa_executor.hpp"
#include "lcs/native_memory.hpp"
#include <cstring>
#include <sstream>

namespace lcs {
namespace {
bool weather_profile() {
    const uint8_t interpolation[]={0xA3,0x0C,0x13,0xC8,0};
    const uint8_t rain[]={0xA3,0x24,0x13,0xC8,0};
    const uint8_t types[]={0x66,0xA3,0x18,0x13,0xC8,0,0x66,0xA3,0x20,0x13,0xC8,0,0x66,0xA3,0x1C,0x13,0xC8,0,0xC3};
    return !std::memcmp(reinterpret_cast<const void*>(native_entity_address(0x72A494)),interpolation,sizeof(interpolation)) &&
        !std::memcmp(reinterpret_cast<const void*>(native_entity_address(0x72A4B1)),rain,sizeof(rain)) &&
        !std::memcmp(reinterpret_cast<const void*>(native_entity_address(0x72A4F5)),types,sizeof(types));
}
bool clock_profile() {
    const uint8_t source[]={0xA0,0x55,0x01,0xB7,0,0x8A,0x0D,0x54,0x01,0xB7,0,0x8A,0x15,0x53,0x01,0xB7,0,0xA2,0x4D,0x01,0xB7,0,0xA0,0x52,0x01,0xB7,0,0x88,0x0D,0x4C,0x01,0xB7,0,0x66,0x8B,0x0D,0x50,0x01,0xB7,0};
    return !std::memcmp(reinterpret_cast<const void*>(native_entity_address(0x52D020)),source,sizeof(source));
}
bool valid_weather(const int32_t* values) {
    return std::isfinite(entity_float(values[0])) && std::isfinite(entity_float(values[1])) &&
        values[2]>=0 && values[2]<=22 && values[3]>=0 && values[3]<=22;
}
bool valid_clock(const int32_t* values) {
    // Source RestoreClock copies these unsigned fields without normalisation.
    return values[0]>=0 && values[0]<=255 && values[1]>=0 && values[1]<=255 && values[2]>=0 && values[2]<=65535;
}
void read_clock(int32_t* out) {
    out[0]=*reinterpret_cast<const uint8_t*>(native_entity_address(0xB70153));
    out[1]=*reinterpret_cast<const uint8_t*>(native_entity_address(0xB70152));
    out[2]=*reinterpret_cast<const uint16_t*>(native_entity_address(0xB70150));
}
struct EnvironmentReceipt {bool ok=false,entered=false;std::array<int32_t,32> values{};DWORD exception=0;};
void invoke(const NativeCall* call,EnvironmentReceipt* receipt) {
    __try {
        const auto action=*call->source_environment;
        const bool weather=action==EnvironmentCommand::read_weather || action==EnvironmentCommand::restore_weather_and_store_clock;
        if(weather && !weather_profile())return;
        if(action!=EnvironmentCommand::read_weather && !clock_profile())return;
        if(action==EnvironmentCommand::read_weather) {
            receipt->values[0]=*reinterpret_cast<const int32_t*>(native_entity_address(0xC8130C));
            receipt->values[1]=*reinterpret_cast<const int32_t*>(native_entity_address(0xC81324));
            receipt->values[2]=*reinterpret_cast<const int16_t*>(native_entity_address(0xC8131C));
            receipt->values[3]=*reinterpret_cast<const int16_t*>(native_entity_address(0xC81320));
            receipt->ok=valid_weather(receipt->values.data());return;
        }
        if(action==EnvironmentCommand::read_clock) {
            read_clock(receipt->values.data());receipt->ok=true;return;
        }
        // All profiles, shapes, values and the combined clock read are checked
        // BEFORE entry. If a write faults, the caller must not replay it.
        const auto* input=call->inputs.data();
        if(action==EnvironmentCommand::restore_weather_and_store_clock) {
            read_clock(receipt->values.data());receipt->entered=true;
            *reinterpret_cast<int32_t*>(native_entity_address(0xC8130C))=input[0];
            *reinterpret_cast<int32_t*>(native_entity_address(0xC81324))=input[1];
            *reinterpret_cast<int16_t*>(native_entity_address(0xC8131C))=int16_t(input[2]);
            *reinterpret_cast<int16_t*>(native_entity_address(0xC81320))=int16_t(input[3]);
        } else {
            receipt->entered=true;
            *reinterpret_cast<uint8_t*>(native_entity_address(0xB70153))=uint8_t(input[0]);
            *reinterpret_cast<uint8_t*>(native_entity_address(0xB70152))=uint8_t(input[1]);
            *reinterpret_cast<uint16_t*>(native_entity_address(0xB70150))=uint16_t(input[2]);
        }
        receipt->ok=true;
    } __except(EXCEPTION_EXECUTE_HANDLER){receipt->exception=GetExceptionCode();}
}
}
NativeResult sa_execute_environment_command(const NativeCall& call) {
    NativeResult result;
    if(!call.source_environment || call.opcode || call.predicate || call.text || call.replacement_text || call.text_position ||
       call.source_entity_command || call.source_statistic || call.source_frenzy_headshots || call.source_garage || call.source_traffic || call.source_wait_in_car || call.source_wander_direction ||
       call.mission_flag_reference || call.suppress_previous_brief || !sa_executor_profile_matches()) {
        result.diagnostic="Original environment mode/profile mismatch";return result;
    }
    size_t inputs=0,outputs=0;
    switch(*call.source_environment) {
    case EnvironmentCommand::read_weather:outputs=4;break;
    case EnvironmentCommand::restore_weather_and_store_clock:inputs=4;outputs=3;break;
    case EnvironmentCommand::read_clock:outputs=3;break;
    case EnvironmentCommand::restore_clock:inputs=3;break;
    default:result.diagnostic="Unknown original environment operation";return result;
    }
    if(call.inputs.size()!=inputs || call.outputs!=outputs ||
       (*call.source_environment==EnvironmentCommand::restore_weather_and_store_clock && !valid_weather(call.inputs.data())) ||
       (*call.source_environment==EnvironmentCommand::restore_clock && !valid_clock(call.inputs.data()))) {
        result.diagnostic="Original environment operand shape/value mismatch";return result;
    }
    EnvironmentReceipt receipt;invoke(&call,&receipt);result.executed=receipt.ok;result.native_entered=receipt.entered;result.values=receipt.values;
    if(!result.executed){std::ostringstream message;message<<"Original environment access refused; exception=0x"<<std::hex<<receipt.exception;result.diagnostic=message.str();}
    return result;
}
}
