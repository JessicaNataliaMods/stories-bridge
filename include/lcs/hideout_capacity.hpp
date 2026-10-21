#pragma once
#include <algorithm>
namespace lcs {
// The native hideout entrance compares this value with its fixed 2/4 limit.
// Only that comparison's call site uses this projection; other count queries
// still return the actual number of vehicles.
inline int hideout_entry_count(int actual,int capacity) {
    return capacity<0 ? actual : actual>=capacity ? 4 : 0;
}
inline int hideout_storage_slots(int native_slots,int capacity) {
    return capacity<0 ? native_slots : std::min(native_slots,capacity);
}
}
