#pragma once
#include <cstdint>
#include <functional>
#include <map>
#include <new>
#include <string>
#include <exception>

namespace lcs {
enum class UpdateScope { auxiliary, dependency };
struct UpdateFailure {
    UpdateScope scope;
    uint32_t attempted_at,reported_at;
    std::string diagnostic;
};
// Only fallible observations/idempotent maintenance belong here. Original VM
// commands use scheduler quarantine, never automatic retries of side effects.
class UpdateSupervisor {
    std::map<std::string,UpdateFailure> failures_;
public:
    std::function<void(const std::string&)> diagnostic_event;
    const std::map<std::string,UpdateFailure>& failures()const {return failures_;}
    template<class Action> bool run(const std::string& name,UpdateScope scope,uint32_t now,
                                   const std::string& diagnostic,Action action) {
        auto previous=failures_.find(name);
        if(previous!=failures_.end() && now-previous->second.attempted_at<250)return false;
        std::string reason=diagnostic;bool accepted=false;
        try {accepted=action();}
        catch(const std::bad_alloc&){throw;} // Allocation failure is shared infrastructure.
        catch(const std::exception& error){reason+="; ";reason+=error.what();}
        if(accepted) {
            if(previous!=failures_.end()) {
                if(diagnostic_event)diagnostic_event("Frame update recovered: service="+name);
                failures_.erase(previous);
            }
            return true;
        }
        if(previous==failures_.end()) {
            previous=failures_.emplace(name,UpdateFailure{scope,now,now,reason}).first;
            if(diagnostic_event)diagnostic_event("Frame update deferred: service="+name+" scope="+(scope==UpdateScope::auxiliary?"auxiliary":"dependency")+" diagnostic="+reason);
        } else {
            auto& fault=previous->second;
            if(fault.diagnostic!=reason || now-fault.reported_at>=5000) {
                if(diagnostic_event)diagnostic_event("Frame update still deferred: service="+name+" diagnostic="+reason);
                fault.reported_at=now;
            }
            fault.scope=scope;fault.attempted_at=now;fault.diagnostic=reason;
        }
        return false;
    }
};
}
