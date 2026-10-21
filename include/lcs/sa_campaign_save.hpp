#pragma once

#include <cstdint>

namespace lcs {

// Retail 1.0 US callsites. The CALL displacement may already point at a
// third-party wrapper; the runtime profile checks its surrounding instructions
// and leaves chaining to the existing install_call helper.
struct SACampaignSaveProfile {
    static constexpr std::uint32_t save_writer_call = 0x00619081;
    static constexpr std::uint32_t generic_load_call = 0x0053C70B;
    static constexpr std::uint32_t save_slot_return = 0x00578DFF;
    static constexpr std::uint32_t save_path_buffer = 0x00C16DB8;
    static constexpr std::uint32_t load_path_buffer = 0x00C15FC8;
    static constexpr std::uint32_t selected_slot = 0x00BA68A7;

    static bool matches() noexcept;
};

using SACampaignNativeSaveWriter = bool (__cdecl *)(int flags);
using SACampaignNativeGenericLoad = bool (__cdecl *)(bool* loaded);

using SACampaignBeforeSave = bool (__cdecl *)(std::uint8_t slot, const char* native_path, void* context);
// Return true only when pairing is committed, or when the slot intentionally
// passes through without a campaign sidecar. A false result reports save
// failure to the native UI even if the original SA writer succeeded.
using SACampaignSaveResult = bool (__cdecl *)(std::uint8_t slot, const char* native_path,
    bool native_saved, void* context);
using SACampaignBeforeLoad = bool (__cdecl *)(std::uint8_t slot, const char* native_path, void* context);
using SACampaignLoadResult = void (__cdecl *)(std::uint8_t slot, const char* native_path,
    bool sidecar_ready, bool native_loaded, bool loaded, void* context);

struct SACampaignSaveCallbacks {
    SACampaignBeforeSave before_save = nullptr;
    SACampaignSaveResult save_result = nullptr;
    SACampaignBeforeLoad before_load = nullptr;
    SACampaignLoadResult load_result = nullptr;
    void* context = nullptr;
};

// Bind the callbacks before install_call, then bind the chained targets it
// returns before the game can reach either wrapper.
void sa_campaign_save_bind_native_targets(SACampaignNativeSaveWriter save_writer,
    SACampaignNativeGenericLoad generic_load) noexcept;
void sa_campaign_save_bind_callbacks(const SACampaignSaveCallbacks& callbacks) noexcept;
bool sa_campaign_save_callbacks_ready() noexcept;
bool sa_campaign_save_hooks_ready() noexcept;
bool sa_campaign_save_last_native_save_succeeded() noexcept;
bool sa_campaign_save_last_sidecar_commit_succeeded() noexcept;

// Wrapper for the retail CALL at 0x619081. Its stack also contains the slot
// pushed for the preceding filename builder; the fourth argument is the
// original SaveSlot argument. A failed capture intentionally skips the native
// writer so the existing SA slot remains intact.
bool __cdecl sa_campaign_save_writer_call(int flags, int path_builder_slot,
    std::uintptr_t save_slot_return, int save_slot);

// Wrapper for the retail CALL at 0x53C70B. Sidecar preflight never suppresses
// the native load; the result callback receives GenericLoad's bool and its
// out-parameter after the original target returns.
bool __cdecl sa_campaign_generic_load_call(bool* loaded);

} // namespace lcs
