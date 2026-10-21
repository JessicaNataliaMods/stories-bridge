#pragma once
#include <cstdint>
#include <map>
#include <string>
#include <vector>
#include <filesystem>

namespace lcs {
uint32_t text_key(const std::string& name);
using TextBank=std::map<uint32_t,std::string>;
struct TextArchive {
    std::map<std::string,TextBank> banks;
    static TextArchive load(const std::filesystem::path& path,bool lcs_format);
    static TextArchive parse(const std::vector<uint8_t>& bytes,bool lcs_format);
    std::vector<uint8_t> native_bytes() const;
    const std::string& get(const std::string& bank,const std::string& label) const;
    // Native frontend labels are retained; the LCS campaign owns mission banks.
    void merge_native_frontend(const TextArchive& native);
    void require_campaign(const TextArchive& lcs) const;
};
}
