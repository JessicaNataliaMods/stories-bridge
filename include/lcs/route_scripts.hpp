#pragma once
#include "route_table.hpp"
#include "vm.hpp"
#include <functional>
#include <utility>
namespace lcs {
// One original global pool, shared by every SCM thread and the live consumer.
class RouteScripts {
    OriginalRouteTable table_;
    std::function<StepResult(int32_t,int32_t,int32_t,Thread&)> follow_route_;
public:
    static bool handles(uint16_t);
    StepResult dispatch(Vm&,Thread&,uint16_t);
    void set_follow_route_handler(std::function<StepResult(int32_t,int32_t,int32_t,Thread&)> handler) {
        follow_route_=std::move(handler);
    }
    const OriginalRouteTable& table()const{return table_;}
};
}
