#pragma once
#include "lcs/script_coronas.hpp"
namespace lcs {
bool sa_script_coronas_available();
bool sa_register_script_corona(uintptr_t,const ScriptCorona&);
std::optional<float> sa_script_corona_ground(float,float);
}
