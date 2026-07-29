#ifndef FRONTIER_DIRECTORATE_TERMINAL_CONTROL_MODEL_HPP
#define FRONTIER_DIRECTORATE_TERMINAL_CONTROL_MODEL_HPP

#include <algorithm>
#include <cstdint>
#include <istream>
#include <ostream>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "ascii_renderer.hpp"

namespace frontier_directorate::terminal {

enum class TerminalView : std::uint8_t {
    local,
    strategic,
    messages,
    replay_inspector,
    debug_hash,
    help,
};

enum class ControlKind : std::uint8_t {
    move_up,
    move_down,
    move_left,
    move_right,
    cycle_pane,
    inspect,
    cancel,
    toggle_map,
    scale_down,
    scale_up,
    page_up,
    page_down,
    set_filter,
    toggle_help,
    quit,
    pass,
    show_local,
    show_strategic,
    show_messages,
    show_replay,
    show_hash,
};

struct ControlCommand final {
    ControlKind kind{ControlKind::inspect};
    std::string text{};
};

struct ControlState final {
    RenderOptions render{};
    TerminalView view{TerminalView::local};
    TerminalView before_help{TerminalView::local};
    std::uint32_t world_width{1U};
    std::uint32_t world_height{1U};
    std::uint32_t pane{};
    std::uint32_t scale{1U};
    std::uint64_t audit_count{};
    std::uint64_t first_log_record{};
    std::uint64_t replay_record{};
    std::string filter{};
    bool inspecting{};
    bool running{true};
};

inline Error control_error(const fd_result code, std::string message) {
    Error error{};
    error.code = code;
    error.message = std::move(message);
    return error;
}

inline Result<ControlCommand> parse_control(const std::string_view input) {
    ControlCommand command{};
    if (input == "up" || input == "k" || input == "\x1b[A") {
        command.kind = ControlKind::move_up;
    } else if (input == "down" || input == "j" || input == "\x1b[B") {
        command.kind = ControlKind::move_down;
    } else if (input == "left" || input == "h" || input == "\x1b[D") {
        command.kind = ControlKind::move_left;
    } else if (input == "right" || input == "l" || input == "\x1b[C") {
        command.kind = ControlKind::move_right;
    } else if (input == "tab" || input == "\t") {
        command.kind = ControlKind::cycle_pane;
    } else if (input.empty() || input == "enter") {
        command.kind = ControlKind::inspect;
    } else if (input == "esc" || input == "\x1b") {
        command.kind = ControlKind::cancel;
    } else if (input == "m") {
        command.kind = ControlKind::toggle_map;
    } else if (input == "[") {
        command.kind = ControlKind::scale_down;
    } else if (input == "]") {
        command.kind = ControlKind::scale_up;
    } else if (input == "pgup" || input == "\x1b[5~") {
        command.kind = ControlKind::page_up;
    } else if (input == "pgdn" || input == "\x1b[6~") {
        command.kind = ControlKind::page_down;
    } else if (input == "?") {
        command.kind = ControlKind::toggle_help;
    } else if (input == "q") {
        command.kind = ControlKind::quit;
    } else if (input == "pass") {
        command.kind = ControlKind::pass;
    } else if (input == "local") {
        command.kind = ControlKind::show_local;
    } else if (input == "strategic") {
        command.kind = ControlKind::show_strategic;
    } else if (input == "messages") {
        command.kind = ControlKind::show_messages;
    } else if (input == "replay") {
        command.kind = ControlKind::show_replay;
    } else if (input == "hash") {
        command.kind = ControlKind::show_hash;
    } else if (!input.empty() && input.front() == '/') {
        command.kind = ControlKind::set_filter;
        command.text.assign(input.substr(1U));
    } else {
        return Result<ControlCommand>::failure(control_error(
            FD_ERR_INVALID_ARGUMENT, "unknown terminal control token"));
    }
    return Result<ControlCommand>::success(std::move(command));
}

inline void keep_cursor_visible(ControlState& state) {
    const std::uint32_t map_width = state.render.columns > 32U
                                        ? state.render.columns - 32U
                                        : 1U;
    const std::uint32_t map_height = state.render.rows > 9U
                                         ? state.render.rows - 9U
                                         : 1U;
    if (state.render.selected_x < state.render.viewport_x) {
        state.render.viewport_x = state.render.selected_x;
    } else if (state.render.selected_x - state.render.viewport_x >= map_width) {
        state.render.viewport_x =
            state.render.selected_x - map_width + 1U;
    }
    if (state.render.selected_y < state.render.viewport_y) {
        state.render.viewport_y = state.render.selected_y;
    } else if (state.render.selected_y - state.render.viewport_y >= map_height) {
        state.render.viewport_y =
            state.render.selected_y - map_height + 1U;
    }
}

inline Result<void> apply_control(ControlState& state,
                                  const ControlCommand& command) {
    if (state.world_width == 0U || state.world_height == 0U) {
        return Result<void>::failure(control_error(
            FD_ERR_STATE, "control model requires nonzero world dimensions"));
    }
    state.render.selected_x =
        std::min(state.render.selected_x, state.world_width - 1U);
    state.render.selected_y =
        std::min(state.render.selected_y, state.world_height - 1U);
    switch (command.kind) {
        case ControlKind::move_up:
            if (state.render.selected_y != 0U) {
                --state.render.selected_y;
            }
            state.render.message = "Cursor moved up.";
            break;
        case ControlKind::move_down:
            if (state.render.selected_y + 1U < state.world_height) {
                ++state.render.selected_y;
            }
            state.render.message = "Cursor moved down.";
            break;
        case ControlKind::move_left:
            if (state.render.selected_x != 0U) {
                --state.render.selected_x;
            }
            state.render.message = "Cursor moved left.";
            break;
        case ControlKind::move_right:
            if (state.render.selected_x + 1U < state.world_width) {
                ++state.render.selected_x;
            }
            state.render.message = "Cursor moved right.";
            break;
        case ControlKind::cycle_pane:
            state.pane = (state.pane + 1U) % 3U;
            state.render.message =
                "Pane focus: " + std::to_string(state.pane + 1U) + "/3.";
            break;
        case ControlKind::inspect:
            state.inspecting = true;
            state.render.message = "Inspector confirmed at selected coordinate.";
            break;
        case ControlKind::cancel:
            state.inspecting = false;
            state.filter.clear();
            if (state.view == TerminalView::help) {
                state.view = state.before_help;
            }
            state.render.message = "Pending UI operation cancelled.";
            break;
        case ControlKind::toggle_map:
            state.view = state.view == TerminalView::local
                             ? TerminalView::strategic
                             : TerminalView::local;
            state.render.message = "Map scale toggled.";
            break;
        case ControlKind::scale_down:
        case ControlKind::scale_up:
            state.scale = 1U;
            state.render.message =
                "U01 exposes one local scale; use m for region/tile modes.";
            break;
        case ControlKind::page_up:
            if (state.view == TerminalView::replay_inspector) {
                if (state.replay_record != 0U) {
                    --state.replay_record;
                }
                state.render.message = "Replay inspector moved back one attempt.";
            } else {
                state.first_log_record = state.first_log_record > 10U
                                             ? state.first_log_record - 10U
                                             : 0U;
                state.render.message = "Message log scrolled up.";
            }
            break;
        case ControlKind::page_down:
            if (state.view == TerminalView::replay_inspector) {
                if (state.audit_count != 0U &&
                    state.replay_record < state.audit_count - 1U) {
                    ++state.replay_record;
                }
                state.render.message =
                    "Replay inspector moved forward one attempt.";
            } else {
                if (state.audit_count != 0U) {
                    const std::uint64_t maximum = state.audit_count - 1U;
                    state.first_log_record =
                        state.first_log_record >
                                maximum - std::min(maximum, UINT64_C(10))
                            ? maximum
                            : state.first_log_record + UINT64_C(10);
                }
                state.render.message = "Message log scrolled down.";
            }
            break;
        case ControlKind::set_filter:
            state.filter = command.text;
            state.render.message = state.filter.empty()
                                       ? "Message filter cleared."
                                       : "Message filter: " + state.filter;
            break;
        case ControlKind::toggle_help:
            if (state.view == TerminalView::help) {
                state.view = state.before_help;
            } else {
                state.before_help = state.view;
                state.view = TerminalView::help;
            }
            state.render.message = "Help toggled.";
            break;
        case ControlKind::quit:
            state.running = false;
            break;
        case ControlKind::pass:
            return Result<void>::failure(control_error(
                FD_ERR_STATE, "PASS must be submitted through the C action binding"));
        case ControlKind::show_local:
            state.view = TerminalView::local;
            break;
        case ControlKind::show_strategic:
            state.view = TerminalView::strategic;
            break;
        case ControlKind::show_messages:
            state.view = TerminalView::messages;
            break;
        case ControlKind::show_replay:
            state.view = TerminalView::replay_inspector;
            break;
        case ControlKind::show_hash:
            state.view = TerminalView::debug_hash;
            break;
    }
    keep_cursor_visible(state);
    return Result<void>::success();
}

inline std::string render_control_help(const ControlState& state) {
    std::vector<std::string> lines{
        "FRONTIER DIRECTORATE | TERMINAL CONTROL HELP | ASCII/NO COLOR",
        "? help/toggle    q quit          arrows or h/j/k/l move cursor",
        "Tab cycle panes  Enter inspect   Esc cancel",
        "m local/strategic map            [ / ] supported scale",
        "PgUp/PgDn message-log scroll     /text filter action audit records",
        "pass submit explicit two-seat PASS through authoritative C API",
        "local strategic messages replay hash select explicit screens",
        "Input is line-oriented; named keys and terminal escape tokens are accepted."};
    while (lines.size() + 2U < static_cast<std::size_t>(state.render.rows)) {
        lines.emplace_back();
    }
    lines.push_back("Message: " + state.render.message);
    lines.emplace_back("Help: ?=return q=quit");
    std::string frame;
    for (std::string& line : lines) {
        for (char& character : line) {
            const unsigned char byte = static_cast<unsigned char>(character);
            if (byte < 0x20U || byte > 0x7eU) {
                character = '?';
            }
        }
        if (line.size() > static_cast<std::size_t>(state.render.columns)) {
            line.resize(static_cast<std::size_t>(state.render.columns));
        }
        frame.append(line);
        frame.push_back('\n');
    }
    return frame;
}

using ControlRenderCallback =
    Result<std::string> (*)(void* user, ControlState& state);
using ControlPassCallback =
    Result<void> (*)(void* user, ControlState& state);

struct ControlBindings final {
    void* user{};
    ControlRenderCallback render{};
    ControlPassCallback pass{};
};

inline Result<void> run_control_loop(std::istream& input,
                                     std::ostream& output,
                                     ControlState& state,
                                     const ControlBindings& bindings) {
    if (bindings.render == nullptr || bindings.pass == nullptr) {
        return Result<void>::failure(control_error(
            FD_ERR_INVALID_ARGUMENT, "interactive callbacks are incomplete"));
    }
    while (state.running) {
        auto frame = bindings.render(bindings.user, state);
        if (!frame) {
            return Result<void>::failure(frame.error());
        }
        output << frame.value();
        if (!output) {
            return Result<void>::failure(control_error(
                FD_ERR_STATE, "interactive output stream failed"));
        }

        std::string token;
        if (!std::getline(input, token)) {
            break;
        }
        auto parsed = parse_control(token);
        if (!parsed) {
            state.render.message = parsed.error().message;
            continue;
        }
        if (parsed.value().kind == ControlKind::pass) {
            auto submitted = bindings.pass(bindings.user, state);
            if (!submitted) {
                return Result<void>::failure(submitted.error());
            }
        } else {
            auto applied = apply_control(state, parsed.value());
            if (!applied) {
                return applied;
            }
        }
    }
    return Result<void>::success();
}

}  // namespace frontier_directorate::terminal

#endif
