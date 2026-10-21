#pragma once
#include <array>
#include <cmath>
#include <cstdint>
#include <optional>

namespace lcs {
// Original global route nodes, NOT SA's shared 05D6/05D7 route buffer.
// This table is a prerequisite for the live FOLLOW_ROUTE consumer. It does
// not grant support to any SCM command on its own.
class OriginalRouteTable {
public:
    struct Node {int16_t route=-1;std::array<float,3> position{};};
    static constexpr size_t capacity=200;
private:
    std::array<Node,capacity> nodes_{};
    static int16_t route_id(int32_t value) {
        const uint16_t low=uint16_t(uint32_t(value));
        return low<=0x7fff?int16_t(low):int16_t(int32_t(low)-0x10000);
    }
public:
    const std::array<Node,capacity>& nodes() const {return nodes_;}
    void reset() {nodes_={};}
    std::optional<size_t> first(int32_t id) const {
        for(size_t i=0;i<capacity;++i)if(nodes_[i].route==route_id(id))return i;
        return {};
    }
    const Node* at(int32_t index) const {
        return index>=0 && uint32_t(index)<capacity?&nodes_[size_t(index)]:nullptr;
    }
    bool add(int32_t id,const std::array<float,3>& position) {
        for(float value:position)if(!std::isfinite(value))return false;
        const auto empty=first(-1);if(!empty)return false;
        nodes_[*empty]={route_id(id),position};return true;
    }
    bool remove(int32_t id) {
        const auto start=first(id);if(!start)return true;
        size_t end=*start;
        while(end<capacity && nodes_[end].route==route_id(id))++end;
        const auto count=end-*start;
        // Retail copies only the removed block's length, NOT the entire tail.
        // Do not reinterpret the reverse's FIX_BUGS path as retail semantics.
        // If the original would read outside its pool, reject atomically.
        if(end+count>capacity)return false;
        for(size_t i=0;i<count;++i)nodes_[*start+i]=nodes_[end+i];
        for(size_t i=capacity-count;i<capacity;++i)nodes_[i].route=-1;
        return true;
    }
};

// Point selection and copied targets from SetFollowRoute/GetNextPointOnRoute.
// The engine binding must prove ownership/control before advancing this state;
// a task disappearing is not a route arrival or script-objective completion.
class OriginalRouteCursor {
public:
    enum class Result {waiting,advanced,ended,unavailable};
private:
    int16_t route_=0,mode_=0,passed_=0,direction_=1,start_=0;
    std::array<float,3> target_{};
    bool ended_=false;
    static int16_t narrow(int32_t value) {
        const auto low=uint16_t(uint32_t(value));
        return low<=0x7fff?int16_t(low):int16_t(int32_t(low)-0x10000);
    }
    std::optional<int16_t> next(const OriginalRouteTable& table) {
        auto index=narrow(int32_t(start_)+passed_+direction_);
        const auto* point=table.at(index);
        if(point && point->route==route_)return index;
        if(mode_==1)return int16_t(-1);
        if(mode_==2)direction_=narrow(-int32_t(direction_));
        else if(mode_==3)passed_=-1;
        else return {};
        index=narrow(int32_t(start_)+passed_+direction_);
        // Source does not recheck after direction/restart. Bound its read and
        // refuse missing points instead of turning corrupt routes into success.
        point=table.at(index);
        return point && point->route==route_?std::optional<int16_t>(index):std::nullopt;
    }
public:
    static std::optional<OriginalRouteCursor> begin(const OriginalRouteTable& table,int32_t route,int32_t mode) {
        OriginalRouteCursor result;result.route_=narrow(route);result.mode_=narrow(mode);
        if(result.route_==-1 || result.mode_<1 || result.mode_>3)return {};
        const auto first=table.first(result.route_);if(!first)return {};
        result.start_=int16_t(*first);
        const auto next=result.next(table);
        if(!next || *next<0)return {}; // no valid first movement target
        result.target_=table.at(*next)->position;return result;
    }
    const std::array<float,3>& target() const {return target_;}
    int16_t passed() const {return passed_;}
    int16_t direction() const {return direction_;}
    Result arrive(const OriginalRouteTable& table,const std::array<float,3>& position) {
        if(ended_)return Result::ended;
        // A removed/replaced route is not STOP_WHEN_DONE. Its copied target
        // may still match the actor's position, but no longer authorizes a
        // normal endpoint or another native write.
        const auto* origin=table.at(start_);
        if(!origin || origin->route!=route_)return Result::unavailable;
        for(float coordinate:position)if(!std::isfinite(coordinate))return Result::unavailable;
        // Retail HaveReachedNextPointOnRoute: sqrt(X*X+Y*Y) < 1.0f.
        // Z does not gate arrival. Source float/EE corner rounding is not claimed.
        const float x=target_[0]-position[0],y=target_[1]-position[1];
        const float square=x*x+y*y;
        if(!std::isfinite(square))return Result::unavailable;
        if(!(std::sqrt(square)<1.f))return Result::waiting;
        auto candidate=*this;candidate.passed_=narrow(int32_t(candidate.passed_)+candidate.direction_);
        const auto next=candidate.next(table);if(!next)return Result::unavailable;
        if(*next<0){candidate.ended_=true;*this=candidate;return Result::ended;}
        candidate.target_=table.at(*next)->position;*this=candidate;return Result::advanced;
    }
};
}
