#include "../src/cpp/fd.hpp"

#include <cstdint>
#include <iostream>
#include <utility>

namespace fd = frontier_directorate;

namespace {

int fail(const char* const operation, const fd::Error& error) {
    std::cerr << "headless C++ failure: " << operation << " [" << error.code
              << "]: " << error.message << '\n';
    return 1;
}

}  // namespace

int main() {
    auto context_config = fd::default_context_config();
    if (!context_config) {
        return fail("default context config", context_config.error());
    }
    auto context = fd::Context::create(context_config.value());
    if (!context) {
        return fail("context create", context.error());
    }
    auto world_config = fd::default_world_config();
    if (!world_config) {
        return fail("default world config", world_config.error());
    }
    auto world = context.value().generate(world_config.value(), UINT64_C(7));
    if (!world) {
        return fail("world generate", world.error());
    }
    auto stepped = world.value().step_explicit_pass();
    if (!stepped) {
        return fail("explicit PASS", stepped.error());
    }
    if (stepped.value().tick_after != FD_OPERATIONAL_TICKS) {
        std::cerr << "headless C++ failure: unexpected tick\n";
        return 1;
    }
    auto replay_bytes = world.value().build_replay();
    if (!replay_bytes) {
        return fail("replay build", replay_bytes.error());
    }
    auto replay = context.value().open_replay(replay_bytes.value());
    if (!replay) {
        return fail("replay open", replay.error());
    }
    auto replay_world = replay.value().create_world();
    if (!replay_world) {
        return fail("replay world", replay_world.error());
    }
    auto advanced = replay.value().advance(replay_world.value(), UINT64_C(1));
    if (!advanced) {
        return fail("replay advance", advanced.error());
    }
    if (advanced.value().complete == 0U) {
        std::cerr << "headless C++ failure: replay incomplete\n";
        return 1;
    }
    std::cout << "PASS: C++ headless wrapper path\n";
    return 0;
}
