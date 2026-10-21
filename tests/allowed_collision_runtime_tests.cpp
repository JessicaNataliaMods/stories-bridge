#include "../src/sa_allowed_collision.hpp"

#include <cmath>
#include <iostream>
#include <stdexcept>

namespace {
void check(bool condition, const char* message) {
    if (!condition)
        throw std::runtime_error(message);
}

void check_position(const std::optional<std::array<float, 3>>& actual,
                    float x, float y, float z, const char* message) {
    check(actual.has_value(), message);
    check(std::fabs((*actual)[0] - x) < 0.001f &&
          std::fabs((*actual)[1] - y) < 0.001f &&
          std::fabs((*actual)[2] - z) < 0.001f, message);
}
}

int main() {
    lcs::AllowedCollisionRuntime state;
    check(!state.position(), "unset state must leave native SA collision streaming alone");

    check(state.set(1, 3), "valid primary level must be accepted");
    check_position(state.position(), 1060.0f, -800.0f, 0.0f,
                   "non-generic primary level must take precedence");

    check(state.set(0, 2), "valid secondary fallback must be accepted");
    check_position(state.position(), 350.0f, -624.0f, 0.0f,
                   "generic primary must select the secondary level");

    check(!state.set(4, 2), "unsupported selected level must be rejected");
    check_position(state.position(), 350.0f, -624.0f, 0.0f,
                   "rejected update must preserve the last valid native request");

    check(state.set(1, 4), "unused fallback must not override a valid primary");
    check_position(state.position(), 1060.0f, -800.0f, 0.0f,
                   "only the selected original level controls collision streaming");

    check(state.set(0, 0), "generic pair must be accepted");
    check(!state.position(), "generic pair must issue no extra native collision request");

    check(state.set(2, 0), "valid state must be available before cleanup");
    state.clear();
    check(!state.position(), "cleanup must restore native SA collision streaming");
    std::cout << "AllowedCollisionRuntime contract passed\n";
}
