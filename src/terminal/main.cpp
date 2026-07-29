#include "ascii_renderer.hpp"
#include "control_model.hpp"

#include <charconv>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <limits>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>
#include <vector>

namespace fd = frontier_directorate;
namespace term = frontier_directorate::terminal;

namespace {

enum class CliView : std::uint8_t {
    local,
    strategic,
    messages,
    replay_inspector,
    debug_hash,
};

struct CliOptions final {
    std::uint64_t seed{};
    std::uint64_t steps{};
    std::uint32_t columns{80U};
    std::uint32_t rows{24U};
    std::uint32_t viewport_x{};
    std::uint32_t viewport_y{};
    std::uint32_t selected_x{};
    std::uint32_t selected_y{};
    CliView view{CliView::local};
    std::filesystem::path save_path{};
    std::filesystem::path replay_path{};
    std::filesystem::path verify_replay_path{};
    bool seed_supplied{};
    bool steps_supplied{};
    bool interactive{};
    bool help{};
};

fd::Error cli_error(const fd_result code, std::string message) {
    fd::Error error{};
    error.code = code;
    error.message = std::move(message);
    return error;
}

template <class Integer>
fd::Result<Integer> parse_unsigned(const std::string_view text,
                                   const std::string_view option) {
    Integer value{};
    const auto parsed =
        std::from_chars(text.data(), text.data() + text.size(), value, 10);
    if (text.empty() || parsed.ec != std::errc{} ||
        parsed.ptr != text.data() + text.size()) {
        return fd::Result<Integer>::failure(cli_error(
            FD_ERR_INVALID_ARGUMENT,
            std::string(option) + " requires an unsigned decimal integer"));
    }
    return fd::Result<Integer>::success(value);
}

fd::Result<CliOptions> parse_arguments(const int argc, char** const argv) {
    CliOptions options{};
    for (int index = 1; index < argc; ++index) {
        const std::string_view argument(argv[index]);
        if (argument == "--help" || argument == "-h") {
            options.help = true;
        } else if (argument == "--local") {
            options.view = CliView::local;
        } else if (argument == "--strategic") {
            options.view = CliView::strategic;
        } else if (argument == "--messages") {
            options.view = CliView::messages;
        } else if (argument == "--replay-inspector") {
            options.view = CliView::replay_inspector;
        } else if (argument == "--debug-hash") {
            options.view = CliView::debug_hash;
        } else if (argument == "--interactive") {
            options.interactive = true;
        } else {
            if (index + 1 >= argc) {
                return fd::Result<CliOptions>::failure(cli_error(
                    FD_ERR_INVALID_ARGUMENT,
                    std::string(argument) + " requires a value"));
            }
            const std::string_view value(argv[++index]);
            if (argument == "--seed") {
                auto parsed = parse_unsigned<std::uint64_t>(value, argument);
                if (!parsed) {
                    return fd::Result<CliOptions>::failure(parsed.error());
                }
                options.seed = parsed.value();
                options.seed_supplied = true;
            } else if (argument == "--steps") {
                auto parsed = parse_unsigned<std::uint64_t>(value, argument);
                if (!parsed) {
                    return fd::Result<CliOptions>::failure(parsed.error());
                }
                options.steps = parsed.value();
                options.steps_supplied = true;
            } else if (argument == "--columns") {
                auto parsed = parse_unsigned<std::uint32_t>(value, argument);
                if (!parsed) {
                    return fd::Result<CliOptions>::failure(parsed.error());
                }
                options.columns = parsed.value();
            } else if (argument == "--rows") {
                auto parsed = parse_unsigned<std::uint32_t>(value, argument);
                if (!parsed) {
                    return fd::Result<CliOptions>::failure(parsed.error());
                }
                options.rows = parsed.value();
            } else if (argument == "--viewport-x") {
                auto parsed = parse_unsigned<std::uint32_t>(value, argument);
                if (!parsed) {
                    return fd::Result<CliOptions>::failure(parsed.error());
                }
                options.viewport_x = parsed.value();
            } else if (argument == "--viewport-y") {
                auto parsed = parse_unsigned<std::uint32_t>(value, argument);
                if (!parsed) {
                    return fd::Result<CliOptions>::failure(parsed.error());
                }
                options.viewport_y = parsed.value();
            } else if (argument == "--select-x") {
                auto parsed = parse_unsigned<std::uint32_t>(value, argument);
                if (!parsed) {
                    return fd::Result<CliOptions>::failure(parsed.error());
                }
                options.selected_x = parsed.value();
            } else if (argument == "--select-y") {
                auto parsed = parse_unsigned<std::uint32_t>(value, argument);
                if (!parsed) {
                    return fd::Result<CliOptions>::failure(parsed.error());
                }
                options.selected_y = parsed.value();
            } else if (argument == "--save" || argument == "--save-out") {
                options.save_path = std::filesystem::path(value);
            } else if (argument == "--replay" ||
                       argument == "--replay-out") {
                options.replay_path = std::filesystem::path(value);
            } else if (argument == "--verify-replay") {
                options.verify_replay_path = std::filesystem::path(value);
            } else {
                return fd::Result<CliOptions>::failure(cli_error(
                    FD_ERR_INVALID_ARGUMENT,
                    "unknown option: " + std::string(argument)));
            }
        }
    }

    if (!options.verify_replay_path.empty() &&
        (options.seed_supplied || options.steps_supplied ||
         !options.save_path.empty() || !options.replay_path.empty())) {
        return fd::Result<CliOptions>::failure(cli_error(
            FD_ERR_INVALID_ARGUMENT,
            "--verify-replay cannot be combined with generation, steps, or output"));
    }
    return fd::Result<CliOptions>::success(std::move(options));
}

fd::Result<std::vector<std::byte>> read_file(
    const std::filesystem::path& path) {
    std::ifstream input(path, std::ios::binary | std::ios::ate);
    if (!input) {
        return fd::Result<std::vector<std::byte>>::failure(
            cli_error(FD_ERR_FORMAT, "could not open " + path.string()));
    }
    const std::streampos end = input.tellg();
    if (end < std::streampos(0)) {
        return fd::Result<std::vector<std::byte>>::failure(
            cli_error(FD_ERR_FORMAT, "could not measure " + path.string()));
    }
    const auto size = static_cast<std::uint64_t>(end);
    if (size > static_cast<std::uint64_t>(
                   std::numeric_limits<std::size_t>::max()) ||
        size > static_cast<std::uint64_t>(
                   std::numeric_limits<std::streamsize>::max())) {
        return fd::Result<std::vector<std::byte>>::failure(
            cli_error(FD_ERR_CAPACITY, "file is too large: " + path.string()));
    }

    std::vector<std::byte> bytes(static_cast<std::size_t>(size));
    input.seekg(0, std::ios::beg);
    if (!bytes.empty()) {
        input.read(reinterpret_cast<char*>(bytes.data()),
                   static_cast<std::streamsize>(bytes.size()));
    }
    if (!input) {
        return fd::Result<std::vector<std::byte>>::failure(
            cli_error(FD_ERR_FORMAT, "could not read " + path.string()));
    }
    return fd::Result<std::vector<std::byte>>::success(std::move(bytes));
}

fd::Result<void> write_atomic(const std::filesystem::path& path,
                              const std::span<const std::byte> bytes) {
    if (path.empty()) {
        return fd::Result<void>::failure(
            cli_error(FD_ERR_INVALID_ARGUMENT, "output path is empty"));
    }
    if (bytes.size() > static_cast<std::size_t>(
                           std::numeric_limits<std::streamsize>::max())) {
        return fd::Result<void>::failure(
            cli_error(FD_ERR_CAPACITY, "output is too large"));
    }

    std::filesystem::path temporary = path;
    temporary += ".tmp";
    std::error_code error;
    if (std::filesystem::exists(temporary, error)) {
        return fd::Result<void>::failure(cli_error(
            FD_ERR_BUSY, "temporary output already exists: " +
                             temporary.string()));
    }
    if (error) {
        return fd::Result<void>::failure(cli_error(
            FD_ERR_FORMAT, "could not inspect output path: " + error.message()));
    }

    {
        std::ofstream output(temporary, std::ios::binary | std::ios::trunc);
        if (!output) {
            return fd::Result<void>::failure(cli_error(
                FD_ERR_FORMAT, "could not create " + temporary.string()));
        }
        if (!bytes.empty()) {
            output.write(reinterpret_cast<const char*>(bytes.data()),
                         static_cast<std::streamsize>(bytes.size()));
        }
        output.flush();
        if (!output) {
            output.close();
            std::filesystem::remove(temporary, error);
            return fd::Result<void>::failure(cli_error(
                FD_ERR_FORMAT, "could not write " + temporary.string()));
        }
    }

    std::filesystem::rename(temporary, path, error);
    if (error) {
        std::error_code ignored;
        std::filesystem::remove(temporary, ignored);
        return fd::Result<void>::failure(cli_error(
            FD_ERR_FORMAT, "could not install output: " + error.message()));
    }
    return fd::Result<void>::success();
}

void print_error(const fd::Error& error) {
    std::cerr << "error [" << error.code << "]";
    if (!error.message.empty()) {
        std::cerr << ": " << error.message;
    }
    if (error.field_id != 0U || error.item_index != 0U) {
        std::cerr << " (field " << error.field_id << ", item "
                  << error.item_index << ')';
    }
    std::cerr << '\n';
}

void print_help() {
    std::cout
        << "Frontier Directorate Update 01 terminal client\n"
        << "Usage: frontier_terminal [options]\n"
        << "  --seed N             generate the deterministic 64-bit seed N\n"
        << "  --local              render the local tile map (default)\n"
        << "  --strategic          render the strategic region graph\n"
        << "  --messages           render accepted/rejected replay action audit\n"
        << "  --replay-inspector   inspect replay decision record zero\n"
        << "  --debug-hash         render the canonical state-hash screen\n"
        << "  --interactive        run the deterministic line-oriented control loop\n"
        << "  --steps N            submit N explicit two-seat PASS decisions\n"
        << "  --columns N --rows N render for a terminal size (minimum 80x24)\n"
        << "  --viewport-x N --viewport-y N  choose local viewport origin\n"
        << "  --select-x N --select-y N      choose inspected tile\n"
        << "  --save FILE          atomically write the canonical save\n"
        << "  --replay FILE        atomically write a verifying replay\n"
        << "  --verify-replay FILE verify all records and render the final state\n"
        << "  --help               show this help\n";
}

fd::Result<fd::World> generated_world(const fd::Context& context,
                                      const CliOptions& options) {
    auto config = fd::default_world_config();
    if (!config) {
        return fd::Result<fd::World>::failure(config.error());
    }
    return context.generate(config.value(), options.seed);
}

struct Playback final {
    fd::Replay replay{};
    fd::World world{};
};

fd::Result<Playback> replay_playback(const fd::Context& context,
                                     const std::filesystem::path& path) {
    auto bytes = read_file(path);
    if (!bytes) {
        return fd::Result<Playback>::failure(bytes.error());
    }
    auto replay = context.open_replay(bytes.value());
    if (!replay) {
        return fd::Result<Playback>::failure(replay.error());
    }
    auto replay_info = replay.value().info();
    if (!replay_info) {
        return fd::Result<Playback>::failure(replay_info.error());
    }
    auto world = replay.value().create_world();
    if (!world) {
        return fd::Result<Playback>::failure(world.error());
    }
    auto advanced = replay.value().advance(world.value(),
                                            replay_info.value().decision_count);
    if (!advanced) {
        return fd::Result<Playback>::failure(advanced.error());
    }
    if (advanced.value().complete == 0U) {
        return fd::Result<Playback>::failure(cli_error(
            FD_ERR_STATE, "replay did not reach its final verified record"));
    }
    Playback playback{};
    playback.replay = std::move(replay).value();
    playback.world = std::move(world).value();
    return fd::Result<Playback>::success(std::move(playback));
}

struct InteractiveSession final {
    fd::Context* context{};
    fd::World* world{};
    std::optional<fd::Replay>* replay{};
};

fd::Result<void> ensure_audit(InteractiveSession& session,
                              term::ControlState& state) {
    if (session.context == nullptr || session.world == nullptr ||
        session.replay == nullptr) {
        return fd::Result<void>::failure(cli_error(
            FD_ERR_STATE, "interactive session binding is incomplete"));
    }
    if (!session.replay->has_value()) {
        auto bytes = session.world->build_replay();
        if (!bytes) {
            return fd::Result<void>::failure(bytes.error());
        }
        auto opened = session.context->open_replay(bytes.value());
        if (!opened) {
            return fd::Result<void>::failure(opened.error());
        }
        session.replay->emplace(std::move(opened).value());
    }
    auto info = session.replay->value().info();
    if (!info) {
        return fd::Result<void>::failure(info.error());
    }
    state.audit_count = info.value().decision_count +
                        static_cast<std::uint64_t>(info.value().audit_count);
    if (state.audit_count == 0U) {
        state.replay_record = 0U;
        state.first_log_record = 0U;
    } else {
        state.replay_record =
            std::min(state.replay_record, state.audit_count - 1U);
        state.first_log_record =
            std::min(state.first_log_record, state.audit_count - 1U);
    }
    return fd::Result<void>::success();
}

fd::Result<std::string> interactive_render(void* const user,
                                           term::ControlState& state) {
    auto* const session = static_cast<InteractiveSession*>(user);
    if (session == nullptr || session->world == nullptr) {
        return fd::Result<std::string>::failure(cli_error(
            FD_ERR_STATE, "interactive render session is invalid"));
    }
    if (state.view == term::TerminalView::help) {
        return fd::Result<std::string>::success(
            term::render_control_help(state));
    }
    if (state.view == term::TerminalView::messages ||
        state.view == term::TerminalView::replay_inspector) {
        auto audited = ensure_audit(*session, state);
        if (!audited) {
            return fd::Result<std::string>::failure(audited.error());
        }
        if (state.view == term::TerminalView::messages) {
            return term::render_message_log_ascii(
                session->replay->value(), state.first_log_record, state.render,
                state.filter);
        }
        return term::render_replay_inspector_ascii(
            session->replay->value(), state.replay_record, state.render);
    }

    auto snapshot = session->world->reference_snapshot();
    if (!snapshot) {
        return fd::Result<std::string>::failure(snapshot.error());
    }
    if (state.view == term::TerminalView::debug_hash) {
        return term::render_debug_hash_ascii(snapshot.value(), state.render);
    }
    state.render.mode = state.view == term::TerminalView::strategic
                            ? term::ScreenMode::strategic
                            : term::ScreenMode::local;
    return term::render_ascii(snapshot.value(), state.render);
}

fd::Result<void> interactive_pass(void* const user,
                                  term::ControlState& state) {
    auto* const session = static_cast<InteractiveSession*>(user);
    if (session == nullptr || session->world == nullptr ||
        session->replay == nullptr) {
        return fd::Result<void>::failure(
            cli_error(FD_ERR_STATE, "interactive PASS session is invalid"));
    }
    auto stepped = session->world->step_explicit_pass();
    if (!stepped) {
        return fd::Result<void>::failure(stepped.error());
    }
    session->replay->reset();
    state.render.message =
        "Explicit PASS accepted: tick " +
        std::to_string(stepped.value().tick_before) + " -> " +
        std::to_string(stepped.value().tick_after) + ".";
    return ensure_audit(*session, state);
}

}  // namespace

int main(const int argc, char** const argv) {
    auto options_result = parse_arguments(argc, argv);
    if (!options_result) {
        print_error(options_result.error());
        return 2;
    }
    const CliOptions& options = options_result.value();
    if (options.help) {
        print_help();
        return 0;
    }

    auto context_config = fd::default_context_config();
    if (!context_config) {
        print_error(context_config.error());
        return 1;
    }
    auto context = fd::Context::create(context_config.value());
    if (!context) {
        print_error(context.error());
        return 1;
    }

    fd::World world;
    std::optional<fd::Replay> audit_replay;
    if (options.verify_replay_path.empty()) {
        auto generated = generated_world(context.value(), options);
        if (!generated) {
            print_error(generated.error());
            return 1;
        }
        world = std::move(generated).value();
    } else {
        auto playback =
            replay_playback(context.value(), options.verify_replay_path);
        if (!playback) {
            print_error(playback.error());
            return 1;
        }
        audit_replay.emplace(std::move(playback.value().replay));
        world = std::move(playback.value().world);
    }

    for (std::uint64_t step = 0U; step < options.steps; ++step) {
        auto stepped = world.step_explicit_pass();
        if (!stepped) {
            print_error(stepped.error());
            return 1;
        }
    }

    if (!options.save_path.empty()) {
        auto bytes = world.save();
        if (!bytes) {
            print_error(bytes.error());
            return 1;
        }
        auto written = write_atomic(options.save_path, bytes.value());
        if (!written) {
            print_error(written.error());
            return 1;
        }
    }
    if (!options.replay_path.empty()) {
        auto bytes = world.build_replay();
        if (!bytes) {
            print_error(bytes.error());
            return 1;
        }
        auto written = write_atomic(options.replay_path, bytes.value());
        if (!written) {
            print_error(written.error());
            return 1;
        }
    }

    term::RenderOptions render_options{};
    render_options.columns = options.columns;
    render_options.rows = options.rows;
    render_options.viewport_x = options.viewport_x;
    render_options.viewport_y = options.viewport_y;
    render_options.selected_x = options.selected_x;
    render_options.selected_y = options.selected_y;
    render_options.message = options.verify_replay_path.empty()
                                 ? "Ready. Explicit PASS decisions accepted: " +
                                       std::to_string(options.steps) + "."
                                 : "Replay verified through its final record.";
    if (options.interactive) {
        auto info = world.info();
        if (!info) {
            print_error(info.error());
            return 1;
        }
        term::ControlState control{};
        control.render = render_options;
        control.world_width = info.value().width;
        control.world_height = info.value().height;
        switch (options.view) {
            case CliView::local:
                control.view = term::TerminalView::local;
                break;
            case CliView::strategic:
                control.view = term::TerminalView::strategic;
                break;
            case CliView::messages:
                control.view = term::TerminalView::messages;
                break;
            case CliView::replay_inspector:
                control.view = term::TerminalView::replay_inspector;
                break;
            case CliView::debug_hash:
                control.view = term::TerminalView::debug_hash;
                break;
        }
        InteractiveSession session{&context.value(), &world, &audit_replay};
        term::ControlBindings bindings{};
        bindings.user = &session;
        bindings.render = &interactive_render;
        bindings.pass = &interactive_pass;
        auto loop = term::run_control_loop(std::cin, std::cout, control, bindings);
        if (!loop) {
            print_error(loop.error());
            return 1;
        }
        return 0;
    }

    if ((options.view == CliView::messages ||
         options.view == CliView::replay_inspector) &&
        !audit_replay.has_value()) {
        auto bytes = world.build_replay();
        if (!bytes) {
            print_error(bytes.error());
            return 1;
        }
        auto opened = context.value().open_replay(bytes.value());
        if (!opened) {
            print_error(opened.error());
            return 1;
        }
        audit_replay.emplace(std::move(opened).value());
    }

    auto frame = [&]() -> fd::Result<std::string> {
        if (options.view == CliView::messages) {
            return term::render_message_log_ascii(*audit_replay, 0U,
                                                   render_options);
        }
        if (options.view == CliView::replay_inspector) {
            return term::render_replay_inspector_ascii(*audit_replay, 0U,
                                                        render_options);
        }
        auto snapshot = world.reference_snapshot();
        if (!snapshot) {
            return fd::Result<std::string>::failure(snapshot.error());
        }
        if (options.view == CliView::debug_hash) {
            return term::render_debug_hash_ascii(snapshot.value(),
                                                  render_options);
        }
        render_options.mode = options.view == CliView::strategic
                                  ? term::ScreenMode::strategic
                                  : term::ScreenMode::local;
        return term::render_ascii(snapshot.value(), render_options);
    }();
    if (!frame) {
        print_error(frame.error());
        return 1;
    }
    std::cout << frame.value();
    return 0;
}
