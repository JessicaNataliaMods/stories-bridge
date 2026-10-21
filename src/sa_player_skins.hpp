#pragma once
#include <filesystem>
#include <string>
#include <map>
#include <optional>
#include <cstdint>
namespace lcs {
class SaPlayerSkins {
    std::map<std::string,std::string> assets_;
    int32_t actor_=-1,current_=-1,pending_=-1;
    std::string worn_,requested_;
    bool recovering_=false;
    std::string recovery_diagnostic_;
public:
    explicit SaPlayerSkins(const std::filesystem::path& world);
    bool begin(int32_t actor,const std::string& label);
    bool finish(int32_t actor);
    std::optional<std::string> wearing(int32_t actor)const;
    bool initialize(const std::string& label);
    bool service();
    const std::string& recovery_diagnostic()const {return recovery_diagnostic_;}
};
}
