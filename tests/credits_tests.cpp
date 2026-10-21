#include "lcs/credits.hpp"
#include "lcs/text.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

namespace {
void check(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}

bool profile = true;
bool setup_failure = false;
bool measure_failure = false;
bool line_failure = false;
bool capture_failure = false;
float scale_x = 0.0f, scale_y = 0.0f, wrap_width = 0.0f, centre_width = 0.0f;
uint8_t orientation = 0xFF;
std::array<uint8_t, 0x40> font_state{};
struct PrintedLine { float x, y; uint8_t alpha; std::string text; };
std::vector<PrintedLine> printed;

size_t occurrences(const std::string& text, const std::string& token) {
    size_t count = 0;
    for (size_t at = 0; (at = text.find(token, at)) != std::string::npos; at += token.size())
        ++count;
    return count;
}
}

bool native_credits_profile() { return profile; }
bool native_credits_setup(float sx, float sy, float wrap, float centre,
                          uint8_t direction) {
    if (setup_failure) return false;
    scale_x = sx; scale_y = sy; wrap_width = wrap; centre_width = centre;
    orientation = direction;
    font_state[0] = 0xC3;
    return true;
}
bool native_credits_measure(float, float, const char* text, int16_t* lines) {
    if (measure_failure) return false;
    *lines = static_cast<int16_t>(1 + occurrences(text, "~n~"));
    return true;
}
bool native_credits_line(uint8_t alpha, float x, float y, float, const char* text) {
    if (line_failure) return false;
    printed.push_back({x, y, alpha, text});
    font_state[1] = 0x5A;
    return true;
}
bool native_credits_capture(uint8_t* saved) {
    if (capture_failure) return false;
    std::copy(font_state.begin(), font_state.end(), saved);
    return true;
}
void native_credits_restore(const uint8_t* saved) {
    std::copy(saved, saved + font_state.size(), font_state.begin());
}

int main(int argc, char** argv) try {
    check(argc == 2, "pass the converted American GXT asset");
    const auto english = lcs::TextArchive::load(argv[1], false);
    check(english.banks.at("CRED01").size() == 299,
          "converted CRED01 source contains all 299 retail text rows");
    check(english.get("CRED01", "CR01_01").size() != 0,
          "first retail credits key is present in the source archive");

    lcs::SaCredits credits;
    profile = false;
    check(!credits.available() && !credits.start(0, 1, english) && credits.finished(),
          "profile failure rejects start without publishing running credits");
    profile = true;

    auto incomplete = english;
    incomplete.banks.at("CRED01").erase(lcs::text_key("CR01_02"));
    check(!credits.start(0, 1, incomplete) && credits.finished(),
          "a missing interior CR key makes the selected bank incomplete");
    check(!credits.start(1, 1, english) && credits.finished(),
          "selector one requires CRED02 instead of silently using CRED01");

    font_state.fill(0x37);
    const auto original_font = font_state;
    check(credits.start(0, 100, english) && !credits.finished(),
          "selector zero starts the validated CRED01 bank");
    check(!credits.start(1, 100, english) && !credits.finished(),
          "a failed restart leaves a previously running credit roll intact");

    printed.clear();
    check(credits.draw(english, 10, 640, 448, true, true, false) &&
              !credits.finished() && printed.empty(),
          "active menu pauses the roll and suppresses skip input");
    check(credits.draw(english, 10, 640, 448, false, false, false) &&
              !printed.empty(),
          "visible frame renders current-language source text");
    check(std::abs(scale_x - 0.8f) < 0.001f && std::abs(scale_y - 0.7f) < 0.001f &&
              std::abs(wrap_width - (640.0f * 400.0f / 480.0f)) < 0.01f &&
              std::abs(centre_width - wrap_width) < 0.01f && orientation == 0,
          "font setup scales the retail 480-wide text layout to the current viewport");
    const auto first_key = english.get("CRED01", "CR01_01");
    const auto first = std::find_if(printed.begin(), printed.end(), [&](const PrintedLine& item) {
        return item.text == first_key;
    });
    check(first != printed.end() && std::abs(first->x - 320.0f) < 0.01f &&
              std::abs(first->y - 409.0f) < 0.01f && first->alpha == 255,
          "first CR row starts at the retail 458 minus 48-pixel initial scroll");
    check(font_state == original_font,
          "credits drawing restores the saved CFont globals and preserves render queue state");

    const auto calls_before_duplicate = printed.size();
    check(credits.draw(english, 10, 640, 448, false, false, true) &&
              printed.size() == calls_before_duplicate,
          "duplicate HUD callbacks in one native frame do not advance or enqueue twice");

    auto french = english;
    french.banks.at("CRED01")[lcs::text_key("CR01_01")] = "LANGUAGE SWITCH~n~SECOND LINE";
    printed.clear();
    check(credits.draw(french, 11, 640, 448, false, false, true),
          "active roll accepts the newly selected language archive");
    const auto translated = std::find_if(printed.begin(), printed.end(), [](const PrintedLine& item) {
        return item.text == "LANGUAGE SWITCH~n~SECOND LINE";
    });
    check(translated != printed.end() && std::abs(translated->y - 389.0f) < 0.01f,
          "language lookup is live and accelerated scrolling advances twenty pixels once per frame");

    profile = false;
    check(!credits.draw(english, 12, 640, 448, false, false, false) && !credits.finished(),
          "renderer profile loss quarantines drawing without reporting completion");
    profile = true;
    check(credits.draw(english, 12, 640, 448, false, true, false) && credits.finished(),
          "skip input stops the roll and exposes completion");

    lcs::TextArchive short_archive;
    short_archive.banks["CRED01"][lcs::text_key("CR01_01")] = "ONE LINE";
    check(credits.start(0, 0, short_archive), "small complete fixture can start independently");
    for (uint32_t frame = 1; frame < 40 && !credits.finished(); ++frame)
        check(credits.draw(short_archive, frame, 640, 448, false, false, true),
              "accelerated roll renders each valid frame");
    check(credits.finished(), "retail terminal scroll threshold ends the complete roll");

    check(credits.start(0, 0, short_archive), "fixture restarts after natural completion");
    setup_failure = true;
    check(!credits.draw(short_archive, 100, 640, 448, false, false, false) &&
              !credits.finished(),
          "font setup fault leaves query state running for safe retry");
    setup_failure = false;
    check(credits.draw(short_archive, 100, 640, 448, false, false, false),
          "font setup can retry on the next valid render frame");
    check(credits.finished() == false, "one retry frame does not prematurely finish the roll");

    lcs::TextArchive fade_archive;
    fade_archive.banks["CRED01"][lcs::text_key("CR01_01")] = "FIRST";
    fade_archive.banks["CRED01"][lcs::text_key("CR01_02")] = "SECOND";
    fade_archive.banks["CRED01"][lcs::text_key("CR01_03")] = "THIRD";
    check(credits.start(0, 0, fade_archive), "three-row fixture starts for retail fade check");
    printed.clear();
    for (uint32_t frame = 1; frame <= 26; ++frame)
        check(credits.draw(fade_archive, frame, 640, 448, false, false, false),
              "fade fixture advances by one pixel per frame");
    const auto fading_third = std::find_if(printed.rbegin(), printed.rend(), [](const PrintedLine& item) {
        return item.text == "THIRD" && item.alpha != 255;
    });
    check(fading_third != printed.rend() && std::abs(fading_third->y - 437.2f) < 0.02f &&
              fading_third->alpha == 204,
          "retail lower-edge fade uses the proven 437-to-438 pixel alpha ramp");

    std::cout << "PASS: retail credit selector, CRED01 source walk, localized lookup, scrolling, pause, skip, natural end, profile quarantine and CFont preservation; no game code/device executed\n";
    return 0;
} catch (const std::exception& error) {
    std::cerr << error.what() << '\n';
    return 1;
}
