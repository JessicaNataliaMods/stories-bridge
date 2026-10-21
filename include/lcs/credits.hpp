#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace lcs {

struct TextArchive;

// Runtime adapter for the LCS credits bank rendered through SA's native CFont.
// The caller supplies the currently selected language archive on every draw.
class SaCredits {
public:
    bool available() const noexcept;
    bool start(int32_t group, uint32_t now, const TextArchive& text);
    void stop() noexcept;
    bool finished() const noexcept;
    const std::string& bank() const noexcept {return bank_name_;}
    // frame_id is the native render-frame counter: the roll advances once per
    // rendered frame and remains paused when the game clock is paused.
    bool draw(const TextArchive& text, uint32_t frame_id, int width, int height,
              bool menu_active, bool skip_pressed, bool accelerated);

private:
    struct Line {
        unsigned group = 0;
        unsigned entry = 0;
        int16_t cached_line_count = -1;
        std::string cached_text;
    };

    std::vector<Line> lines_;
    std::string bank_name_;
    float scroll_ = 48.0f;
    uint32_t started_at_ = 0;
    uint32_t last_frame_ = 0;
    bool have_last_frame_ = false;
    bool running_ = false;

    static bool collect_lines(int32_t group, const TextArchive& text,
                              std::vector<Line>& lines);
};

// Reuses the HUD font profile and verifies the parent call/shared font flush.
bool sa_credits_render_profile() noexcept;

} // namespace lcs
