#pragma once
#include <array>
#include <filesystem>
#include <cstddef>
namespace lcs {
class SaGameplaySkins {
    std::array<bool,300> models_{};
    std::size_t count_=0;
public:
    explicit SaGameplaySkins(const std::filesystem::path& world);
    bool service();
    std::size_t count() const {return count_;}
};
}
