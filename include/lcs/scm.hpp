#pragma once
#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace lcs {
struct ScmImage {
    std::vector<uint8_t> bytes; // ScriptSpace coordinates: the disk's 8-byte prefix is removed.
    uint32_t main_size = 0, largest_mission = 0, variable_space = 0;
    uint32_t entry_point = 0;
    uint16_t true_globals = 0, most_globals = 0, exclusive_missions = 0;
    std::vector<uint16_t> saved_variables;
    std::vector<std::string> objects;
    std::vector<uint32_t> mission_offsets;
    static ScmImage load(const std::filesystem::path& path);
    static ScmImage parse(std::vector<uint8_t> disk);
    std::vector<uint8_t> mission(size_t index) const;
};
uint16_t read16(const std::vector<uint8_t>& data, size_t offset);
uint32_t read32(const std::vector<uint8_t>& data, size_t offset);
const char* opcode_name(uint16_t opcode);
}
