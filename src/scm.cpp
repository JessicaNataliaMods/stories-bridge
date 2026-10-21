#include "lcs/scm.hpp"
#include "ScriptCommands.h"
#include <algorithm>
#include <fstream>
#include <iterator>
#include <stdexcept>

namespace lcs {
static void require(bool condition, const char* error) {
    if (!condition) throw std::runtime_error(error);
}
uint16_t read16(const std::vector<uint8_t>& d, size_t p) {
    require(p <= d.size() && d.size() - p >= 2, "Truncated 16-bit field");
    return uint16_t(d[p]) | uint16_t(d[p + 1]) << 8;
}
uint32_t read32(const std::vector<uint8_t>& d, size_t p) {
    require(p <= d.size() && d.size() - p >= 4, "Truncated 32-bit field");
    return uint32_t(d[p]) | uint32_t(d[p+1]) << 8 | uint32_t(d[p+2]) << 16 | uint32_t(d[p+3]) << 24;
}
ScmImage ScmImage::load(const std::filesystem::path& path) {
    std::ifstream f(path, std::ios::binary | std::ios::ate);
    require(bool(f), "Cannot open LCS MAIN.SCM");
    const auto size = f.tellg();
    require(size >= 8 && size <= 64 * 1024 * 1024, "Invalid SCM file size");
    std::vector<uint8_t> disk(static_cast<size_t>(size));
    f.seekg(0);
    require(bool(f.read(reinterpret_cast<char*>(disk.data()), size)), "Cannot read SCM");
    return parse(std::move(disk));
}
ScmImage ScmImage::parse(std::vector<uint8_t> disk) {
    ScmImage result;
    result.main_size = read32(disk, 0);
    result.largest_mission = read32(disk, 4);
    require(disk.size() >= 8 && result.main_size >= 32 && result.main_size <= disk.size() - 8,
            "Invalid LCS main script size");
    require(result.largest_mission <= 16 * 1024 * 1024, "Unreasonable mission allocation");
    result.bytes.assign(disk.begin() + 8, disk.end());
    const auto& b = result.bytes;
    const auto section_jump = [&](uint32_t p) {
        require(p <= result.main_size && result.main_size - p >= 8, "Section header outside main script");
        require(read16(b, p) == COMMAND_GOTO && b[p+2] == 6, "Expected LCS section jump (INT32 type 6)");
        const auto target = read32(b, p+3);
        require(target > p && target < result.main_size, "Invalid section jump target");
        return target;
    };
    result.variable_space = section_jump(0);
    const auto mission_section = section_jump(result.variable_space);
    result.entry_point = section_jump(mission_section);
    size_t p = size_t(result.variable_space) + 8;
    const auto saved_count = read32(b, p); p += 4;
    require(saved_count <= 6656 && p + 2ull * saved_count + 4 <= mission_section,
            "Invalid saved-variable table");
    for (uint32_t i = 0; i < saved_count; ++i, p += 2) {
        const auto offset = read16(b, p);
        require(offset + 4u <= result.variable_space, "Saved variable outside variable space");
        result.saved_variables.push_back(offset);
    }
    const auto object_count = read16(b, p); p += 4;
    require(p + 24ull * object_count <= mission_section, "Invalid script object table");
    for (uint16_t i = 0; i < object_count; ++i, p += 24) {
        const auto end = std::find(b.begin() + p, b.begin() + p + 24, uint8_t(0));
        result.objects.emplace_back(b.begin() + p, end);
    }
    p = size_t(mission_section) + 8;
    require(p + 12 <= result.entry_point, "Truncated mission metadata");
    result.true_globals = read16(b, p); p += 2;
    result.most_globals = read16(b, p); p += 2;
    require(read32(b, p) == result.largest_mission, "Mission size headers disagree"); p += 4;
    const auto mission_count = read16(b, p); p += 2;
    result.exclusive_missions = read16(b, p); p += 2;
    require(mission_count <= 150 && result.exclusive_missions <= mission_count &&
            p + 4ull * mission_count <= result.entry_point, "Invalid mission table");
    for (uint16_t i = 0; i < mission_count; ++i, p += 4) {
        const auto offset = read32(b, p);
        require(offset >= result.main_size && offset < b.size(), "Mission offset outside SCM");
        require(i == 0 || offset > result.mission_offsets.back(), "Mission offsets not increasing");
        result.mission_offsets.push_back(offset);
    }
    for (size_t i = 0; i < result.mission_offsets.size(); ++i) {
        const auto start = result.mission_offsets[i];
        const auto end = i+1 < result.mission_offsets.size() ? result.mission_offsets[i+1] : b.size();
        require(end - start <= result.largest_mission, "Mission exceeds largest-mission buffer");
    }
    return result;
}
std::vector<uint8_t> ScmImage::mission(size_t index) const {
    if (index >= mission_offsets.size()) throw std::out_of_range("Mission index");
    const auto start = mission_offsets[index];
    const auto end = index + 1 < mission_offsets.size() ? mission_offsets[index+1] : bytes.size();
    return {bytes.begin() + start, bytes.begin() + end};
}
const char* opcode_name(uint16_t op) {
    switch (op & 0x7fff) {
#include "opcode_names.inc"
    default: return "UNKNOWN_OPCODE";
    }
}
}
