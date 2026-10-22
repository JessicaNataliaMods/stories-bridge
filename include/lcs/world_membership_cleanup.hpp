#pragma once
#include <cstddef>

namespace lcs {
// Reconcile only linked owned entities; detached story entities stay detached.
// Native world teardown retains responsibility for their destruction.
template<class Snapshot,class Validate,class Remove,class Purge,class Add>
bool reconcile_world_membership(const Snapshot& snapshot,Validate validate,
                                Remove remove,Purge purge,Add add,size_t* repaired) {
    for(const auto& [entity,heads]:snapshot) {
        if(!validate(entity) || !remove(entity))return false;
        for(const auto& head:heads)if(!purge(head,entity))return false;
        if(!add(entity))return false;
        ++*repaired;
    }
    return true;
}
}
