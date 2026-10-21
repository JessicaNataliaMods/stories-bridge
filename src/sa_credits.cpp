#include "lcs/credits.hpp"
#include "lcs/text.hpp"
#include "sa_hud.hpp"

#include <array>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <utility>

#ifdef LCS_ISOLATED_CREDITS_RENDER_TEST
bool native_credits_profile();
bool native_credits_setup(float scale_x, float scale_y, float wrap_width,
                          float centre_width, uint8_t orientation);
bool native_credits_measure(float x, float y, const char* text, int16_t* lines);
bool native_credits_line(uint8_t alpha, float x, float y, float scale_y,
                         const char* text);
bool native_credits_capture(uint8_t* saved);
void native_credits_restore(const uint8_t* saved);
#else
#include <windows.h>
namespace {
bool credits_setup(float scale_x, float scale_y, float wrap_width,
                   float centre_width) {
    __try {
        reinterpret_cast<void(__cdecl*)(float, float)>(0x719380)(scale_x, scale_y);
        reinterpret_cast<void(__cdecl*)(float)>(0x7194D0)(wrap_width);
        reinterpret_cast<void(__cdecl*)(float)>(0x7194E0)(centre_width);
        reinterpret_cast<void(__cdecl*)(uint8_t)>(0x719610)(0); // CENTER
        reinterpret_cast<void(__cdecl*)(bool)>(0x7195B0)(true);
        reinterpret_cast<void(__cdecl*)(bool)>(0x719600)(false);
        reinterpret_cast<void(__cdecl*)(bool, bool)>(0x7195C0)(false, false);
        reinterpret_cast<void(__cdecl*)(int16_t)>(0x719490)(1);
        reinterpret_cast<void(__cdecl*)(int16_t)>(0x719570)(0);
        reinterpret_cast<void(__cdecl*)(int16_t)>(0x719590)(0);
        reinterpret_cast<void(__cdecl*)(float)>(0x719500)(255.0f);
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}
bool credits_measure(float x, float y, const char* text, int16_t* lines) {
    __try {
        *lines = reinterpret_cast<int16_t(__cdecl*)(float, float, const char*)>(0x71A5E0)(x, y, text);
        return *lines >= 0;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}
bool credits_line(uint8_t alpha, float x, float y, float scale_y,
                  const char* text) {
    __try {
        const auto a = static_cast<uint32_t>(alpha);
        const uint32_t foreground = (a << 24) | 0x00DCDCDCu;
        const uint32_t shadow = a << 24;
        reinterpret_cast<void(__cdecl*)(uint32_t)>(0x719430)(foreground);
        reinterpret_cast<void(__cdecl*)(uint32_t)>(0x719510)(shadow);
        reinterpret_cast<void(__cdecl*)(float, float, const char*)>(0x71A700)(x, y - scale_y, text);
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}
bool capture_font(uint8_t* saved) {
    __try {
        std::memcpy(saved, reinterpret_cast<const void*>(0xC71A60), 0x40);
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}
void restore_font(const uint8_t* saved) {
    __try {
        std::memcpy(reinterpret_cast<void*>(0xC71A60), saved, 0x40);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
    }
}
}
#endif

namespace lcs {
namespace {
constexpr float base_width = 480.0f;
constexpr float base_height = 448.0f;
constexpr float base_start_y = 458.0f;
constexpr float line_gap = 14.0f;
constexpr float glyph_height = 18.0f;
constexpr float credit_scale_x = 0.6f;
constexpr float credit_scale_y = 0.7f;
constexpr float wrap_width = 400.0f;
constexpr float centre_width = 400.0f;

std::string credit_bank_name(int32_t group) {
    const auto bank = static_cast<int64_t>(group) + 1;
    if (bank <= 0) return {};
    char name[16]{};
    std::snprintf(name, sizeof(name), "CRED%02lld", static_cast<long long>(bank));
    return name;
}

std::string credit_key(unsigned group, unsigned entry) {
    char key[16]{};
    std::snprintf(key, sizeof(key), "CR%02u_%02u", group, entry);
    return key;
}

const std::string* find_credit(const TextBank& bank, unsigned group,
                               unsigned entry) {
    const auto key = credit_key(group, entry);
    const auto found = bank.find(text_key(key));
    return found == bank.end() ? nullptr : &found->second;
}

bool credits_setup(float sx, float sy, float width) {
#ifdef LCS_ISOLATED_CREDITS_RENDER_TEST
    return native_credits_setup(credit_scale_x * sx, credit_scale_y * sy,
                                wrap_width * sx, centre_width * sx, 0);
#else
    return ::credits_setup(credit_scale_x * sx, credit_scale_y * sy,
                           width * (wrap_width / base_width),
                           width * (centre_width / base_width));
#endif
}

bool credits_measure(float x, float y, const char* value, int16_t* lines) {
#ifdef LCS_ISOLATED_CREDITS_RENDER_TEST
    return native_credits_measure(x, y, value, lines);
#else
    return ::credits_measure(x, y, value, lines);
#endif
}

bool credits_line(uint8_t alpha, float x, float y, float sy, const char* value) {
#ifdef LCS_ISOLATED_CREDITS_RENDER_TEST
    return native_credits_line(alpha, x, y, sy, value);
#else
    return ::credits_line(alpha, x, y, sy, value);
#endif
}

bool save_font(uint8_t* saved) {
#ifdef LCS_ISOLATED_CREDITS_RENDER_TEST
    return native_credits_capture(saved);
#else
    return capture_font(saved);
#endif
}

void restore_font(const uint8_t* saved) {
#ifdef LCS_ISOLATED_CREDITS_RENDER_TEST
    native_credits_restore(saved);
#else
    ::restore_font(saved);
#endif
}

}

bool SaCredits::collect_lines(int32_t selected_group, const TextArchive& archive,
                              std::vector<Line>& lines) {
    const auto name = credit_bank_name(selected_group);
    if (name.empty()) return false;
    const auto bank_it = archive.banks.find(name);
    if (bank_it == archive.banks.end() || bank_it->second.empty()) return false;

    const auto& bank = bank_it->second;
    lines.clear();
    lines.reserve(bank.size());
    // Retail visits each CRxx_yy run until the missing key, then advances the
    // group. Require a complete walk so a truncated GXT cannot start early.
    for (unsigned group = 1; group <= bank.size() && lines.size() < bank.size(); ++group) {
        for (unsigned entry = 1; entry <= bank.size(); ++entry) {
            if (!find_credit(bank, group, entry)) break;
            lines.push_back({group, entry});
            if (lines.size() == bank.size()) break;
        }
    }
    return lines.size() == bank.size();
}

bool sa_credits_render_profile() noexcept {
#ifdef LCS_ISOLATED_CREDITS_RENDER_TEST
    return native_credits_profile();
#else
    if(!sa_hud_render_profile())return false;
    __try {
        // Render2dStuff calls CHud::Draw here, then tail-flushes the font buffer.
        // Only test the CALL opcode: install_call may chain another mod target.
        const uint8_t flush[]={0xE9,0xE0,0xBC,0x1D,0};
        return *reinterpret_cast<const uint8_t*>(0x53E4FF)==0xE8 &&
            !std::memcmp(reinterpret_cast<const void*>(0x53E52B),flush,sizeof(flush));
    }__except(EXCEPTION_EXECUTE_HANDLER){return false;}
#endif
}

bool SaCredits::available() const noexcept {
    return sa_credits_render_profile();
}

bool SaCredits::start(int32_t group, uint32_t now, const TextArchive& text) {
    std::vector<Line> lines;
    if (!available() || !collect_lines(group, text, lines)) return false;
    auto bank = credit_bank_name(group);
    lines_ = std::move(lines);
    bank_name_ = std::move(bank);
    scroll_ = 48.0f;
    started_at_ = now;
    last_frame_ = 0;
    have_last_frame_ = false;
    running_ = true;
    return true;
}

void SaCredits::stop() noexcept {
    running_ = false;
    lines_.clear();
    bank_name_.clear();
    scroll_ = 48.0f;
    started_at_ = 0;
    last_frame_ = 0;
    have_last_frame_ = false;
}

bool SaCredits::finished() const noexcept {
    return !running_;
}

bool SaCredits::draw(const TextArchive& archive, uint32_t frame_id, int width,
                     int height, bool menu_active, bool skip_pressed,
                     bool accelerated) {
    if (!running_) return false;
    // A failed profile/data check quarantines drawing without clearing the
    // running bit, so the script cannot observe false completion.
    if (!available() || width <= 0 || height <= 0 || width > 32768 || height > 32768)
        return false;
    if (menu_active) return true;
    if (skip_pressed) {
        stop();
        return true;
    }
    if (have_last_frame_ && last_frame_ == frame_id) return true;

    const auto bank_it = archive.banks.find(bank_name_);
    if (bank_it == archive.banks.end() || bank_it->second.size() != lines_.size())
        return false;
    std::vector<const std::string*> localized;
    localized.reserve(lines_.size());
    for (const auto& line : lines_) {
        const auto* value = find_credit(bank_it->second, line.group, line.entry);
        if (!value) return false;
        localized.push_back(value);
    }

    const float sx = static_cast<float>(width) / base_width;
    const float sy = static_cast<float>(height) / base_height;
    const float x = static_cast<float>(width) * 0.5f;
    std::array<uint8_t, 0x40> saved{};
    if (!save_font(saved.data())) return false;
    struct RestoreFont {const uint8_t* state;~RestoreFont(){restore_font(state);}} restore{saved.data()};
    const auto fail = []() {return false;};
    if (!credits_setup(sx, sy, static_cast<float>(width))) return fail();

    float next_scroll = scroll_ + (accelerated ? 20.0f : 1.0f);
    float line_offset = 0.0f;
    for (size_t i = 0; i < lines_.size(); ++i) {
        auto& line = lines_[i];
        const auto& value = *localized[i];
        const float source_y = base_start_y + line_offset - next_scroll;
        const float y = source_y * sy;
        if (line.cached_text != value || line.cached_line_count < 0) {
            int16_t line_count = 0;
            if (!credits_measure(x, y, value.c_str(), &line_count)) return fail();
            line.cached_text = value;
            line.cached_line_count = line_count;
        }
        const float measured_height = static_cast<float>(line.cached_line_count) *
                                      glyph_height * credit_scale_y;
        if (source_y > -118.0f && source_y < 438.0f) {
            float alpha = 255.0f;
            if (source_y < -117.0f) alpha = 255.0f * (source_y + 118.0f);
            else if (source_y > 437.0f) alpha = 255.0f * (438.0f - source_y);
            // COP1 cvt.w.s uses round-to-nearest; retain the native low byte.
            const auto alpha_byte = static_cast<uint8_t>(std::nearbyint(alpha));
            if (!credits_line(alpha_byte, x, y, sy, value.c_str())) return fail();
        }
        line_offset += line_gap + measured_height;
    }
    scroll_ = next_scroll;
    last_frame_ = frame_id;
    have_last_frame_ = true;
    if (line_offset + base_height - scroll_ < -10.0f) stop();
    return true;
}

} // namespace lcs
