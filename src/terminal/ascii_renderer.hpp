#ifndef FRONTIER_DIRECTORATE_TERMINAL_ASCII_RENDERER_HPP
#define FRONTIER_DIRECTORATE_TERMINAL_ASCII_RENDERER_HPP

#include <cstdint>
#include <string>
#include <string_view>

#include "../cpp/fd.hpp"
#include "../cpp/result.hpp"

namespace frontier_directorate::terminal {

enum class ScreenMode : std::uint8_t {
    local,
    strategic,
};

struct RenderOptions final {
    std::uint32_t columns{80U};
    std::uint32_t rows{24U};
    std::uint32_t viewport_x{};
    std::uint32_t viewport_y{};
    std::uint32_t selected_x{};
    std::uint32_t selected_y{};
    ScreenMode mode{ScreenMode::local};
    std::string message{"Ready. Simulation is paused; no action submitted."};
};

// Consumes only an immutable, reference-scope C snapshot.  UI state is supplied
// separately in RenderOptions and is neither saved nor hashed.
[[nodiscard]] Result<std::string> render_ascii(const Snapshot& snapshot,
                                                const RenderOptions& options);

// U01 allocated audit screens. They display only public snapshot/replay values;
// message rows merge accepted decisions and rejected-attempt audit entries in
// original attempt order; they are authoritative replay data, not fabricated
// events.
[[nodiscard]] Result<std::string> render_debug_hash_ascii(
    const Snapshot& snapshot,
    const RenderOptions& options);
[[nodiscard]] Result<std::string> render_replay_inspector_ascii(
    const Replay& replay,
    std::uint64_t attempt_index,
    const RenderOptions& options);
[[nodiscard]] Result<std::string> render_message_log_ascii(
    const Replay& replay,
    std::uint64_t first_record,
    const RenderOptions& options,
    std::string_view filter = {});

}  // namespace frontier_directorate::terminal

#endif
