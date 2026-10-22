#include "lcs/equivalents.hpp"
#include <sstream>
#include <set>
#include <stdexcept>
#include <algorithm>
namespace lcs {
std::vector<EquivalentEntity> read_equivalents(std::istream& in) {
    std::vector<EquivalentEntity> result;std::set<int32_t> ids;std::string line;
    auto valid=[](const std::string& name){return !name.empty() && name.size()<=23 && std::all_of(name.begin(),name.end(),[](unsigned char c){return std::isalnum(c)||c=='_';});};
    while(std::getline(in,line)) {
        if(!line.empty() && line.back()=='\r')line.pop_back();
        if(line.empty())continue;
        std::istringstream row(line);EquivalentEntity e;std::string kind,extra;
        if(!(row>>e.original>>e.native>>e.name>>e.native_name>>kind) || row>>extra ||
           e.original<0 || e.original>294 || e.native<0 || e.native>=20000 ||
           !valid(e.name)||!valid(e.native_name)||!ids.insert(e.original).second)throw std::runtime_error("Invalid native equivalent entity profile");
        if(kind=="ped")e.kind=ModelKind::ped;
        else if(kind=="vehicle")e.kind=ModelKind::vehicle;
        else if(kind=="object")e.kind=ModelKind::object;
        else throw std::runtime_error("Invalid native equivalent entity class");
        if((e.kind==ModelKind::vehicle && (e.native<400 || e.native>611)) ||
           (e.kind==ModelKind::ped && e.native>=290) ||
           // SA weapon models are 321..373 (default.ide weap); the gift models
           // 321..326 carry original melee weapons (docs/weapons.md).
           (e.kind==ModelKind::object && (e.original<258 || e.native<321 || e.native>373)))throw std::runtime_error("Equivalent must be a regular native entity, not a special streaming slot");
        result.push_back(e);
    }
    if(result.empty())throw std::runtime_error("Empty native equivalent entity profile");
    return result;
}
void bind_equivalents(NativeAdapter& native,const std::vector<EquivalentEntity>& entities) {
    for(const auto& e:entities)native.bind_model(e.original,e.native,e.name,e.kind);
    native.equivalent_entities=true;
    // Preserve nine distinct original gangs using SA's corresponding ped types.
    for(int32_t i=7;i<=15;++i)native.bind_ped_type(i,i);
    native.bind_ped_type(19,21);native.bind_ped_type(22,24);
}
}
