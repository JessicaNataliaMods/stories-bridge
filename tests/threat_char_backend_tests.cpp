#include "../src/sa_threat_char.hpp"
#include "lcs/native_memory.hpp"

#include <array>
#include <cstring>
#include <iostream>
#include <stdexcept>

namespace {
using namespace lcs;

// The native executable addresses are represented by an owned data image.
// This fixture intentionally exercises only the backend's read/identity
// contract; event and pool routines are supplied below as isolated shims.
alignas(16) std::array<uint8_t,0x900000> image{};
constexpr uintptr_t image_base=0x400000;
constexpr size_t ped_stride=0x7C4;
constexpr int32_t target_handle=1;
constexpr int32_t source_handle=(1<<8)|2;
uintptr_t target_ped=0,source_ped=0,intelligence=0,current_event=0,event_source=0;
bool profile_available=true,source_getter_available=true;
bool mutate_event_during_source_getter=false,corrupt_owner_during_source_getter=false;

uintptr_t cell(uintptr_t retail) {
    if(retail<image_base || retail-image_base>=image.size())
        throw std::out_of_range("threat-char fixture address is outside its mapped image");
    return reinterpret_cast<uintptr_t>(image.data())+(retail-image_base);
}

void check(bool condition,const char* message) {
    if(!condition)throw std::runtime_error(message);
}

template<size_t N> void bytes(uintptr_t retail,const uint8_t (&value)[N]) {
    std::memcpy(reinterpret_cast<void*>(cell(retail)),value,N);
}

void reset() {
    image.fill(0);
    profile_available=true;
    source_getter_available=true;
    mutate_event_during_source_getter=false;
    corrupt_owner_during_source_getter=false;
    current_event=cell(0xCD0000);
    event_source=0;
    target_ped=cell(0xCE5000);
    source_ped=target_ped+ped_stride;
    intelligence=cell(0xCE2000);

    const uint8_t ped_pool[]={0x8B,0x4C,0x24,0x04,0x8B,0x15,0x90,0x44,0xB7,0x00,0x53,0x56,0x8B,0x72,0x04,0x8B,0xC1,0xC1,0xF8,0x08,0x8A,0x1C,0x30,0x5E,0x3A,0xD9,0x5B,0x75,0x0B,0x8B,0x0A,0x69,0xC0,0xC4,0x07,0x00};
    const uint8_t history_getter[]={0x8B,0x41,0x08,0x85,0xC0,0x75,0x03,0x8B,0x41,0x04,0xC3};
    const uint8_t intelligence_ctor[]={0x6A,0xFF,0x68,0x76,0xEA,0x83,0x00,0x64,0xA1,0,0,0,0,0x50,0x64,0x89,0x25,0,0,0,0,0x51,0x53,0x55,0x8B,0x6C,0x24,0x1C,0x56,0x8B,0xF1,0x57,0x55,0x8D,0x4E,0x04,0x89,0x74,0x24,0x14,0x89,0x2E,0xE8,0x31,0xA5,0x07,0x00};
    const uint8_t event_source_default[]={0x33,0xC0,0xC3};
    const uint8_t event_source_slot[]={0xF0,0xE1,0x43,0x00};
    bytes(0x54FF90,ped_pool);
    bytes(0x4B8CA0,history_getter);
    bytes(0x607140,intelligence_ctor);
    bytes(0x85AB60,event_source_slot);
    bytes(0x43E1F0,event_source_default);

    const auto pool=cell(0xCE4000),objects=cell(0xCE5000),generations=cell(0xCE7000);
    *reinterpret_cast<uintptr_t*>(cell(0xB74490))=pool;
    *reinterpret_cast<uintptr_t*>(pool)=objects;
    *reinterpret_cast<uintptr_t*>(pool+4)=generations;
    *reinterpret_cast<int32_t*>(pool+8)=2;
    *reinterpret_cast<uint8_t*>(generations)=1;
    *reinterpret_cast<uint8_t*>(generations+1)=2;
    *reinterpret_cast<uint8_t*>(target_ped+0x36)=3;
    *reinterpret_cast<uint8_t*>(source_ped+0x36)=3;
    *reinterpret_cast<uintptr_t*>(target_ped+0x47C)=intelligence;
    *reinterpret_cast<uintptr_t*>(intelligence+0x34)=target_ped;
    event_source=source_ped;
}
} // namespace

namespace lcs {
uintptr_t native_entity_address(uintptr_t retail) {
    return cell(retail);
}

bool sa_executor_profile_matches() { return profile_available; }

uintptr_t native_threat_char_current_event(uintptr_t history) {
    return history==intelligence+0x38?current_event:0;
}

bool native_threat_char_source_entity(uintptr_t event,uintptr_t& source) {
    if(!source_getter_available || event!=current_event)return false;
    source=event_source;
    if(mutate_event_during_source_getter)current_event=0;
    if(corrupt_owner_during_source_getter)
        *reinterpret_cast<uintptr_t*>(intelligence+0x34)=source_ped;
    return true;
}
}

int main() {
    try {
        reset();
        auto result=lcs::sa_get_char_threat_char(target_handle);
        check(result && *result==source_handle,"Current event source should resolve to its allocated ped handle");

        reset();
        current_event=0;
        result=lcs::sa_get_char_threat_char(target_handle);
        check(result && *result==0,"No current event should return the retail zero sentinel");

        reset();
        event_source=0;
        result=lcs::sa_get_char_threat_char(target_handle);
        check(result && *result==0,"A legitimate event source of null should return the retail zero sentinel");

        reset();
        *reinterpret_cast<uint8_t*>(source_ped+0x36)=0;
        result=lcs::sa_get_char_threat_char(target_handle);
        check(result && *result==-1,"A non-null non-ped source should preserve the retail pool-helper -1 result");

        reset();
        event_source=cell(0xCF0000);
        result=lcs::sa_get_char_threat_char(target_handle);
        check(result && *result==-1,"A non-null source outside the ped pool should preserve the retail -1 helper result");

        reset();
        *reinterpret_cast<uint8_t*>(cell(0xCE7001))=0x82;
        *reinterpret_cast<uint32_t*>(source_ped+0x530)=54; // Dead state, but still allocated.
        result=lcs::sa_get_char_threat_char(target_handle);
        check(result && *result==-1,"A freed source slot must not produce a live ped handle");

        reset();
        *reinterpret_cast<uint32_t*>(source_ped+0x530)=54;
        result=lcs::sa_get_char_threat_char(target_handle);
        check(result && *result==source_handle,"A dead-state ped still allocated in the pool should retain its handle");

        reset();
        source_getter_available=false;
        result=lcs::sa_get_char_threat_char(target_handle);
        check(!result,"A failed native source getter must preserve output via nullopt");

        reset();
        mutate_event_during_source_getter=true;
        result=lcs::sa_get_char_threat_char(target_handle);
        check(!result,"An event identity change during the getter must not publish a stale handle");

        reset();
        corrupt_owner_during_source_getter=true;
        result=lcs::sa_get_char_threat_char(target_handle);
        check(!result,"A changed intelligence owner must not publish a stale handle");

        reset();
        profile_available=false;
        result=lcs::sa_get_char_threat_char(target_handle);
        check(!result,"A profile mismatch must preserve output via nullopt");

        std::cout << "threat char backend fixtures passed\n";
        return 0;
    } catch(const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
