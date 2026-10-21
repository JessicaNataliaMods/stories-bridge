#include "lcs/sa_campaign_save.hpp"

#include <windows.h>
#include <cstring>

namespace lcs {
namespace {
constexpr std::uint32_t kImageBase = 0x00400000;
constexpr std::uint32_t kSaveSlotReturn = SACampaignSaveProfile::save_slot_return;
constexpr std::size_t kNativePathLimit = 0x104;

SACampaignNativeSaveWriter native_save_writer = nullptr;
SACampaignNativeGenericLoad native_generic_load = nullptr;
SACampaignSaveCallbacks callbacks{};
bool last_native_save_succeeded = false;
bool last_sidecar_commit_succeeded = false;

bool image_region(std::uint32_t address, std::size_t length, bool executable) noexcept {
    if (!address || !length || std::uint64_t(address) + length > std::uint64_t(UINT32_MAX) + 1) {
        return false;
    }

    MEMORY_BASIC_INFORMATION info{};
    const auto* const pointer = reinterpret_cast<const void*>(static_cast<std::uintptr_t>(address));
    if (!VirtualQuery(pointer, &info, sizeof(info)) || info.State != MEM_COMMIT ||
        (info.Protect & (PAGE_GUARD | PAGE_NOACCESS))) {
        return false;
    }

    const auto begin = reinterpret_cast<std::uintptr_t>(info.BaseAddress);
    const auto end = begin + info.RegionSize;
    if (address < begin || std::uint64_t(address) + length > end) {
        return false;
    }

    const auto protection = info.Protect & 0xFFu;
    if (executable) {
        return protection == PAGE_EXECUTE_READ || protection == PAGE_EXECUTE_READWRITE ||
            protection == PAGE_EXECUTE_WRITECOPY;
    }
    return protection == PAGE_READONLY || protection == PAGE_READWRITE ||
        protection == PAGE_WRITECOPY || protection == PAGE_EXECUTE_READ ||
        protection == PAGE_EXECUTE_READWRITE || protection == PAGE_EXECUTE_WRITECOPY;
}

bool matches_call(std::uint32_t address, const std::uint8_t* before, std::size_t before_size,
    const std::uint8_t* after, std::size_t after_size) noexcept {
    if (!before || !after || !image_region(address - static_cast<std::uint32_t>(before_size), before_size, false) ||
        !image_region(address, 5 + after_size, true)) {
        return false;
    }

    const auto* const site = reinterpret_cast<const std::uint8_t*>(static_cast<std::uintptr_t>(address));
    if (std::memcmp(site - before_size, before, before_size) || site[0] != 0xE8 ||
        std::memcmp(site + 5, after, after_size)) {
        return false;
    }

    std::int32_t relative = 0;
    std::memcpy(&relative, site + 1, sizeof(relative));
    const auto target = std::uint64_t(address) + 5 + relative;
    return target <= UINT32_MAX && image_region(static_cast<std::uint32_t>(target), 1, true);
}

const char* native_path(std::uint32_t address) noexcept {
    if (!image_region(address, 1, false)) {
        return nullptr;
    }

    const auto* const path = reinterpret_cast<const char*>(static_cast<std::uintptr_t>(address));
    for (std::size_t i = 0; i < kNativePathLimit; ++i) {
        if (address > UINT32_MAX - i || !image_region(address + static_cast<std::uint32_t>(i), 1, false)) {
            return nullptr;
        }
        if (path[i] == '\0') {
            return i ? path : nullptr;
        }
    }
    return nullptr;
}

bool copy_native_path(std::uint32_t address, char (&destination)[kNativePathLimit]) noexcept {
    const auto* const path = native_path(address);
    if (!path) {
        destination[0] = '\0';
        return false;
    }
    const auto length = std::strlen(path);
    if (!length || length >= kNativePathLimit) {
        destination[0] = '\0';
        return false;
    }
    std::memcpy(destination, path, length + 1);
    return true;
}

bool valid_slot(int slot) noexcept {
    return slot >= 0 && slot < 8;
}

std::uint8_t selected_slot() noexcept {
    constexpr auto address = SACampaignSaveProfile::selected_slot;
    if (!image_region(address, 1, false)) {
        return 0xFF;
    }
    const auto slot = *reinterpret_cast<const std::uint8_t*>(static_cast<std::uintptr_t>(address));
    return slot < 8 ? slot : 0xFF;
}

} // namespace

bool SACampaignSaveProfile::matches() noexcept {
    if (reinterpret_cast<std::uintptr_t>(GetModuleHandleW(nullptr)) != kImageBase) {
        return false;
    }

    // Mask only each CALL displacement so an earlier plugin target can be
    // chained. Every instruction controlling the wrapper ABI remains literal.
    constexpr std::uint8_t save_before[] = {0x6A, 0x00};
    constexpr std::uint8_t save_after[] = {
        0x83, 0xC4, 0x08, 0x84, 0xC0, 0x0F, 0x95, 0xC0,
        0xFE, 0xC8, 0x83, 0xE0, 0x02, 0xC2, 0x04, 0x00
    };
    constexpr std::uint8_t load_before[] = {0x8D, 0x44, 0x24, 0x07, 0x50};
    constexpr std::uint8_t load_after[] = {0x83, 0xC4, 0x04, 0x84, 0xC0, 0x74, 0x07};

    return matches_call(save_writer_call, save_before, sizeof(save_before), save_after, sizeof(save_after)) &&
        matches_call(generic_load_call, load_before, sizeof(load_before), load_after, sizeof(load_after));
}

void sa_campaign_save_bind_native_targets(SACampaignNativeSaveWriter save_writer,
    SACampaignNativeGenericLoad generic_load) noexcept {
    native_save_writer = save_writer;
    native_generic_load = generic_load;
}

void sa_campaign_save_bind_callbacks(const SACampaignSaveCallbacks& value) noexcept {
    callbacks = value;
}

bool sa_campaign_save_callbacks_ready() noexcept {
    return callbacks.before_save && callbacks.save_result && callbacks.before_load && callbacks.load_result;
}

bool sa_campaign_save_hooks_ready() noexcept {
    return SACampaignSaveProfile::matches() && native_save_writer && native_generic_load &&
        sa_campaign_save_callbacks_ready();
}

bool sa_campaign_save_last_native_save_succeeded() noexcept {
    return last_native_save_succeeded;
}

bool sa_campaign_save_last_sidecar_commit_succeeded() noexcept {
    return last_sidecar_commit_succeeded;
}

bool __cdecl sa_campaign_save_writer_call(int flags, int path_builder_slot,
    std::uintptr_t save_slot_return, int save_slot) {
    if (!native_save_writer || !callbacks.before_save || !callbacks.save_result || flags != 0 ||
        !valid_slot(save_slot) || path_builder_slot != save_slot || save_slot_return != kSaveSlotReturn) {
        last_native_save_succeeded = false;
        last_sidecar_commit_succeeded = false;
        return false;
    }

    char path[kNativePathLimit]{};
    if (!copy_native_path(SACampaignSaveProfile::save_path_buffer, path)) {
        last_native_save_succeeded = false;
        last_sidecar_commit_succeeded = false;
        return false;
    }

    bool captured = false;
    try {
        captured = callbacks.before_save(static_cast<std::uint8_t>(save_slot), path, callbacks.context);
    } catch (...) {
        captured = false;
    }
    if (!captured) {
        last_native_save_succeeded = false;
        last_sidecar_commit_succeeded = false;
        return false;
    }

    const bool native_saved = native_save_writer(flags);
    bool sidecar_committed = false;
    try {
        sidecar_committed = callbacks.save_result(static_cast<std::uint8_t>(save_slot), path,
            native_saved, callbacks.context);
    } catch (...) {
        sidecar_committed = false;
    }
    last_native_save_succeeded = native_saved;
    last_sidecar_commit_succeeded = native_saved && sidecar_committed;
    return last_sidecar_commit_succeeded;
}

bool __cdecl sa_campaign_generic_load_call(bool* loaded) {
    if (!native_generic_load) {
        return false;
    }

    const auto slot = selected_slot();
    char path[kNativePathLimit]{};
    const bool has_path = copy_native_path(SACampaignSaveProfile::load_path_buffer, path);
    bool sidecar_ready = false;
    if (slot != 0xFF && has_path && callbacks.before_load) {
        try {
            sidecar_ready = callbacks.before_load(slot, path, callbacks.context);
        } catch (...) {
            sidecar_ready = false;
        }
    }

    const bool native_loaded = native_generic_load(loaded);
    const bool load_flag = loaded && *loaded;
    if (callbacks.load_result) {
        try {
            callbacks.load_result(slot, has_path ? path : nullptr, sidecar_ready, native_loaded,
                load_flag, callbacks.context);
        } catch (...) {
        }
    }
    return native_loaded;
}

} // namespace lcs
