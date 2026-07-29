#include "../src/cpp/fd.hpp"
#include "../src/terminal/ascii_renderer.hpp"
#include "../src/terminal/control_model.hpp"

#include <algorithm>
#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <new>
#include <span>
#include <sstream>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include <vector>

namespace {

std::atomic<bool> fail_next_cpp_allocation{false};

void arm_cpp_allocation_failure() noexcept {
    fail_next_cpp_allocation.store(true, std::memory_order_release);
}

void* test_cpp_allocate(const std::size_t requested) {
    if (fail_next_cpp_allocation.exchange(false, std::memory_order_acq_rel)) {
        throw std::bad_alloc();
    }
    const std::size_t size = requested == 0U ? 1U : requested;
    void* const memory = std::malloc(size);
    if (memory == nullptr) {
        throw std::bad_alloc();
    }
    return memory;
}

}  // namespace

void* operator new(const std::size_t size) {
    return test_cpp_allocate(size);
}

void* operator new[](const std::size_t size) {
    return test_cpp_allocate(size);
}

void operator delete(void* const memory) noexcept {
    std::free(memory);
}

void operator delete[](void* const memory) noexcept {
    std::free(memory);
}

void operator delete(void* const memory, const std::size_t) noexcept {
    std::free(memory);
}

void operator delete[](void* const memory, const std::size_t) noexcept {
    std::free(memory);
}

namespace fd = frontier_directorate;
namespace term = frontier_directorate::terminal;

using ThrowingAllocatePointer = void* (*)(void*, std::uint64_t);
using NoexceptAllocatePointer = void* (*)(void*, std::uint64_t) noexcept;
using ThrowingDeallocatePointer = void (*)(void*, void*, std::uint64_t);
using NoexceptDeallocatePointer =
    void (*)(void*, void*, std::uint64_t) noexcept;

static_assert(std::is_same_v<fd_allocate_fn, NoexceptAllocatePointer>);
static_assert(std::is_same_v<fd_deallocate_fn, NoexceptDeallocatePointer>);
static_assert(!std::is_convertible_v<ThrowingAllocatePointer, fd_allocate_fn>);
static_assert(!std::is_convertible_v<ThrowingDeallocatePointer,
                                     fd_deallocate_fn>);

static_assert(!std::is_copy_constructible_v<fd::Context>);
static_assert(!std::is_copy_assignable_v<fd::Context>);
static_assert(std::is_nothrow_move_constructible_v<fd::Context>);
static_assert(!std::is_copy_constructible_v<fd::World>);
static_assert(!std::is_copy_assignable_v<fd::World>);
static_assert(std::is_nothrow_move_constructible_v<fd::World>);
static_assert(!std::is_copy_constructible_v<fd::Snapshot>);
static_assert(std::is_nothrow_move_constructible_v<fd::Snapshot>);
static_assert(!std::is_copy_constructible_v<fd::Replay>);
static_assert(std::is_nothrow_move_constructible_v<fd::Replay>);

namespace {

class Tests final {
public:
    void expect(const bool condition,
                const std::string_view expression,
                const std::string_view context = {}) {
        ++checks_;
        if (!condition) {
            ++failures_;
            std::cerr << "FAIL: " << expression;
            if (!context.empty()) {
                std::cerr << " (" << context << ')';
            }
            std::cerr << '\n';
        }
    }

    [[nodiscard]] int finish() const {
        if (failures_ == 0U) {
            std::cout << "PASS: " << checks_ << " C++/terminal checks\n";
            return 0;
        }
        std::cerr << "FAILED: " << failures_ << " of " << checks_
                  << " checks\n";
        return 1;
    }

private:
    std::size_t checks_{};
    std::size_t failures_{};
};

std::filesystem::path golden_directory() {
#ifdef FD_SOURCE_DIR
    return std::filesystem::path(FD_SOURCE_DIR) / "tests" / "golden";
#else
    return std::filesystem::absolute(std::filesystem::path(__FILE__))
        .parent_path() /
           "golden";
#endif
}

bool update_goldens() {
    const char* const value = std::getenv("FD_UPDATE_GOLDENS");
    return value != nullptr && std::string_view(value) == "1";
}

bool write_text(const std::filesystem::path& path, const std::string& text) {
    std::error_code error;
    std::filesystem::create_directories(path.parent_path(), error);
    if (error) {
        return false;
    }
    std::ofstream output(path, std::ios::binary | std::ios::trunc);
    output.write(text.data(), static_cast<std::streamsize>(text.size()));
    return static_cast<bool>(output);
}

bool read_text(const std::filesystem::path& path, std::string& output) {
    std::ifstream input(path, std::ios::binary);
    if (!input) {
        return false;
    }
    output.assign(std::istreambuf_iterator<char>(input),
                  std::istreambuf_iterator<char>());
    return static_cast<bool>(input) || input.eof();
}

void expect_golden(Tests& tests,
                   const std::string_view name,
                   const std::string& frame) {
    const std::filesystem::path path =
        golden_directory() / std::string(name);
    if (update_goldens()) {
        tests.expect(write_text(path, frame), "golden update write", path.string());
        return;
    }

    std::string expected;
    const bool read = read_text(path, expected);
    tests.expect(read, "golden exists/readable", path.string());
    if (read) {
        tests.expect(expected == frame, "frame equals checked-in golden",
                     path.string());
    }
}

void expect_ascii_frame(Tests& tests,
                        const std::string& frame,
                        const std::uint32_t columns,
                        const std::uint32_t rows) {
    std::uint32_t line_count = 0U;
    std::uint32_t line_length = 0U;
    bool valid_bytes = true;
    bool valid_width = true;
    for (const char character : frame) {
        const unsigned char byte = static_cast<unsigned char>(character);
        if (byte == static_cast<unsigned char>('\n')) {
            valid_width = valid_width && line_length <= columns;
            line_length = 0U;
            ++line_count;
        } else {
            valid_bytes = valid_bytes && byte >= 0x20U && byte <= 0x7eU;
            ++line_length;
        }
    }
    tests.expect(!frame.empty() && frame.back() == '\n',
                 "frame ends with newline");
    tests.expect(valid_bytes, "frame contains strict printable 7-bit ASCII");
    tests.expect(valid_width, "every frame row fits requested columns");
    tests.expect(line_count == rows, "frame has requested row count");
}

void expect_local_legend(Tests& tests, const std::string& frame) {
    static constexpr std::array<std::string_view, 10U> entries{
        "~=water",          ".=grass",  "f=forest", "^=hills",
        "v=wetland",       "r=river",  ":=road-candidate",
        "c=coastal",       "n=inland", "@=selected/inspect"};
    for (const std::string_view entry : entries) {
        tests.expect(frame.find(entry) != std::string::npos,
                     "local legend contains registered glyph", entry);
    }
}

void expect_strategic_legend(Tests& tests, const std::string& frame) {
    static constexpr std::array<std::string_view, 9U> entries{
        "[R]=region", "[P]=polity", "[C]=coastal", "[I]=inland",
        "[F]=foreign", "->=relation", "~>=river outlet", "::=road candidate",
        "@x,y=coordinates"};
    for (const std::string_view entry : entries) {
        tests.expect(frame.find(entry) != std::string::npos,
                     "strategic legend contains visible notation", entry);
    }
}

fd::Result<fd::Context> make_context() {
    auto config = fd::default_context_config();
    if (!config) {
        return fd::Result<fd::Context>::failure(config.error());
    }
    return fd::Context::create(config.value());
}

fd::Result<fd::World> make_world(const fd::Context& context,
                                 const std::uint64_t seed) {
    auto config = fd::default_world_config();
    if (!config) {
        return fd::Result<fd::World>::failure(config.error());
    }
    return context.generate(config.value(), seed);
}

bool report_error(const fd::Error& error, const std::string_view operation) {
    std::cerr << "FATAL: " << operation << " failed with " << error.code
              << ": " << error.message << '\n';
    return false;
}

struct LoopHarness final {
    std::uint32_t renders{};
    std::uint32_t passes{};
};

fd::Result<std::string> loop_render(void* const user,
                                    term::ControlState& state) {
    auto* const harness = static_cast<LoopHarness*>(user);
    if (harness == nullptr) {
        return fd::Result<std::string>::failure(fd::Error{
            FD_ERR_STATE, 0U, 0U, "missing test loop harness"});
    }
    ++harness->renders;
    return fd::Result<std::string>::success(
        "CONTROL FRAME " + std::to_string(harness->renders) + " VIEW " +
        std::to_string(static_cast<std::uint32_t>(state.view)) + "\n");
}

fd::Result<void> loop_pass(void* const user, term::ControlState& state) {
    auto* const harness = static_cast<LoopHarness*>(user);
    if (harness == nullptr) {
        return fd::Result<void>::failure(fd::Error{
            FD_ERR_STATE, 0U, 0U, "missing test loop harness"});
    }
    ++harness->passes;
    ++state.audit_count;
    state.render.message = "Test PASS submitted through binding.";
    return fd::Result<void>::success();
}

}  // namespace

int main() {
    Tests tests;

    auto allocation_context_config = fd::default_context_config();
    tests.expect(static_cast<bool>(allocation_context_config),
                 "allocation test context config initializes");
    if (!allocation_context_config) {
        return 1;
    }
    arm_cpp_allocation_failure();
    auto failed_context =
        fd::Context::create(allocation_context_config.value());
    tests.expect(!failed_context &&
                     failed_context.error().code == FD_ERR_OUT_OF_MEMORY,
                 "Context::create maps C++ control-block allocation failure");

    fd::Context empty_context;
    auto invalid_generate = fd::default_world_config();
    tests.expect(static_cast<bool>(invalid_generate),
                 "default world config initializes");
    if (invalid_generate) {
        auto invalid_world = empty_context.generate(invalid_generate.value(), 1U);
        tests.expect(!invalid_world, "moved/empty context returns explicit error");
        if (!invalid_world) {
            tests.expect(invalid_world.error().code == FD_ERR_STATE,
                         "empty-context error has stable code");
        }
        arm_cpp_allocation_failure();
        auto failed_error_copy =
            empty_context.generate(invalid_generate.value(), 1U);
        tests.expect(!failed_error_copy &&
                         failed_error_copy.error().code ==
                             FD_ERR_OUT_OF_MEMORY,
                     "public wrapper catches allocation while building an error");
    }

    static constexpr std::array<std::string_view, 25U> control_tokens{
        "?",       "q",       "up",      "down",    "left",
        "right",   "h",       "j",       "k",       "l",
        "\x1b[A", "\x1b[B", "\x1b[C", "\x1b[D", "tab",
        "\t",     "",        "enter",   "esc",     "m",
        "[",       "]",       "pgup",    "pgdn",    "/PASS"};
    for (const std::string_view token : control_tokens) {
        auto parsed = term::parse_control(token);
        tests.expect(static_cast<bool>(parsed),
                     "normative terminal control parses", token);
    }
    auto invalid_control = term::parse_control("not-a-control");
    tests.expect(!invalid_control &&
                     invalid_control.error().code == FD_ERR_INVALID_ARGUMENT,
                 "unknown terminal control returns explicit error");

    term::ControlState model{};
    model.world_width = 2U;
    model.world_height = 2U;
    model.audit_count = 25U;
    auto apply_token = [&tests, &model](const std::string_view token) {
        auto parsed = term::parse_control(token);
        tests.expect(static_cast<bool>(parsed), "control parses before apply",
                     token);
        if (parsed && parsed.value().kind != term::ControlKind::pass) {
            auto applied = term::apply_control(model, parsed.value());
            tests.expect(static_cast<bool>(applied), "control applies", token);
        }
    };
    apply_token("l");
    apply_token("j");
    apply_token("l");
    apply_token("j");
    tests.expect(model.render.selected_x == 1U &&
                     model.render.selected_y == 1U,
                 "cursor controls clamp at authoritative map bounds");
    apply_token("tab");
    tests.expect(model.pane == 1U, "Tab cycles pane focus");
    apply_token("enter");
    tests.expect(model.inspecting, "Enter confirms inspector");
    apply_token("esc");
    tests.expect(!model.inspecting, "Esc cancels inspector");
    apply_token("m");
    tests.expect(model.view == term::TerminalView::strategic,
                 "m switches local to strategic mode");
    apply_token("]");
    apply_token("[");
    tests.expect(model.scale == 1U,
                 "scale controls retain the one supported U01 local scale");
    apply_token("pgdn");
    tests.expect(model.first_log_record == 10U,
                 "PgDn scrolls action-attempt audit log");
    apply_token("pgup");
    tests.expect(model.first_log_record == 0U,
                 "PgUp scrolls action-attempt audit log");
    model.view = term::TerminalView::replay_inspector;
    apply_token("pgdn");
    tests.expect(model.replay_record == 1U,
                 "PgDn advances replay inspector by one attempt");
    apply_token("pgup");
    tests.expect(model.replay_record == 0U,
                 "PgUp moves replay inspector back by one attempt");
    model.view = term::TerminalView::strategic;
    apply_token("/PASS");
    tests.expect(model.filter == "PASS", "/ sets deterministic log filter");
    apply_token("?");
    tests.expect(model.view == term::TerminalView::help,
                 "? opens terminal help");
    apply_token("?");
    tests.expect(model.view == term::TerminalView::strategic,
                 "? returns from terminal help");
    const std::string help_frame = term::render_control_help(model);
    expect_ascii_frame(tests, help_frame, 80U, 24U);
    tests.expect(help_frame.find("arrows or h/j/k/l") != std::string::npos &&
                     help_frame.find("PgUp/PgDn") != std::string::npos,
                 "help screen documents the full normative control families");

    LoopHarness harness{};
    term::ControlState loop_state{};
    loop_state.world_width = 48U;
    loop_state.world_height = 24U;
    term::ControlBindings bindings{};
    bindings.user = &harness;
    bindings.render = &loop_render;
    bindings.pass = &loop_pass;
    std::istringstream scripted_input(
        "l\nm\npass\nmessages\n/PASS\npgdn\n?\n?\nq\n");
    std::ostringstream scripted_output;
    auto loop_result = term::run_control_loop(
        scripted_input, scripted_output, loop_state, bindings);
    tests.expect(static_cast<bool>(loop_result),
                 "interactive loop runs deterministically without a TTY");
    tests.expect(harness.passes == 1U,
                 "interactive PASS uses explicit authoritative binding once");
    tests.expect(harness.renders == 9U,
                 "interactive loop renders once before each scripted token");
    tests.expect(!loop_state.running && loop_state.filter == "PASS" &&
                     loop_state.audit_count == 1U,
                 "interactive controls preserve deterministic UI-only state");
    tests.expect(scripted_output.str().find("CONTROL FRAME 9") !=
                     std::string::npos,
                 "non-TTY loop output contains every deterministic frame");

    auto context_result = make_context();
    if (!context_result) {
        (void)report_error(context_result.error(), "context creation");
        return 1;
    }
    fd::Context context = std::move(context_result).value();
    tests.expect(context.valid(), "created context is valid");

    auto world_result = make_world(context, UINT64_C(42));
    if (!world_result) {
        (void)report_error(world_result.error(), "world generation");
        return 1;
    }
    fd::World world = std::move(world_result).value();
    tests.expect(world.valid(), "generated world is valid");

    // Moving the public Context does not invalidate the world's retained
    // authority lifetime.
    fd::Context moved_context = std::move(context);
    tests.expect(moved_context.valid(), "moved-to context is valid");
    auto world_info = world.info();
    tests.expect(static_cast<bool>(world_info),
                 "world remains valid after context move");
    if (!world_info) {
        (void)report_error(world_info.error(), "world info");
        return 1;
    }
    tests.expect(world_info.value().tick == 0U, "generated world begins at tick 0");

    auto live_before_generate_failure = moved_context.allocation_stats();
    tests.expect(static_cast<bool>(live_before_generate_failure),
                 "allocation stats available before failed generate");
    arm_cpp_allocation_failure();
    auto failed_generate = moved_context.generate(
        invalid_generate.value(), UINT64_C(777));
    tests.expect(!failed_generate &&
                     failed_generate.error().code == FD_ERR_OUT_OF_MEMORY,
                 "Context::generate maps C++ save-buffer allocation failure");
    auto live_after_generate_failure = moved_context.allocation_stats();
    tests.expect(static_cast<bool>(live_after_generate_failure) &&
                     live_before_generate_failure &&
                     live_after_generate_failure.value().live_bytes ==
                         live_before_generate_failure.value().live_bytes,
                 "failed Context::generate releases its C world handle");

    auto hash_before = world.state_hash();
    auto save_before = world.save();
    auto snapshot_result = world.reference_snapshot();
    if (!hash_before || !save_before || !snapshot_result) {
        const fd::Error& error = !hash_before
                                     ? hash_before.error()
                                     : (!save_before ? save_before.error()
                                                     : snapshot_result.error());
        (void)report_error(error, "pre-render capture");
        return 1;
    }
    fd::Snapshot snapshot = std::move(snapshot_result).value();

    arm_cpp_allocation_failure();
    auto failed_save = world.save();
    tests.expect(!failed_save &&
                     failed_save.error().code == FD_ERR_OUT_OF_MEMORY,
                 "World::save maps C++ byte-buffer allocation failure");

    auto live_before_snapshot_failure = moved_context.allocation_stats();
    arm_cpp_allocation_failure();
    auto failed_snapshot = world.reference_snapshot();
    tests.expect(!failed_snapshot &&
                     failed_snapshot.error().code == FD_ERR_OUT_OF_MEMORY,
                 "World::reference_snapshot maps control-block allocation failure");
    auto live_after_snapshot_failure = moved_context.allocation_stats();
    tests.expect(static_cast<bool>(live_before_snapshot_failure) &&
                     static_cast<bool>(live_after_snapshot_failure) &&
                     live_before_snapshot_failure.value().live_bytes ==
                         live_after_snapshot_failure.value().live_bytes,
                 "failed snapshot wrapper releases its C snapshot handle");

    auto live_before_load_failure = moved_context.allocation_stats();
    arm_cpp_allocation_failure();
    auto failed_load = moved_context.load(save_before.value());
    tests.expect(!failed_load &&
                     failed_load.error().code == FD_ERR_OUT_OF_MEMORY,
                 "Context::load maps C++ baseline-copy allocation failure");
    auto live_after_load_failure = moved_context.allocation_stats();
    tests.expect(static_cast<bool>(live_before_load_failure) &&
                     static_cast<bool>(live_after_load_failure) &&
                     live_before_load_failure.value().live_bytes ==
                         live_after_load_failure.value().live_bytes,
                 "failed load wrapper releases its C world handle");

    arm_cpp_allocation_failure();
    auto diagnostic_without_message = snapshot.tile(UINT32_MAX, UINT32_MAX);
    tests.expect(!diagnostic_without_message &&
                     diagnostic_without_message.error().code ==
                         FD_ERR_OUT_OF_RANGE &&
                     diagnostic_without_message.error().message.empty(),
                 "diagnostic message allocation failure preserves C error code");

    auto snapshot_hash = snapshot.state_hash();
    tests.expect(static_cast<bool>(snapshot_hash), "snapshot exposes state hash");
    if (snapshot_hash) {
        tests.expect(snapshot_hash.value() == hash_before.value(),
                     "snapshot hash equals source world hash");
    }

    term::RenderOptions local80_options{};
    local80_options.columns = 80U;
    local80_options.rows = 24U;
    local80_options.mode = term::ScreenMode::local;
    local80_options.selected_x = 0U;
    local80_options.selected_y = 0U;
    local80_options.message = "Golden seed 42; simulation paused.";
    auto local80 = term::render_ascii(snapshot, local80_options);

    term::RenderOptions local120_options = local80_options;
    local120_options.columns = 120U;
    local120_options.rows = 40U;
    local120_options.selected_x = 17U;
    local120_options.selected_y = 11U;
    auto local120 = term::render_ascii(snapshot, local120_options);

    term::RenderOptions strategic80_options = local80_options;
    strategic80_options.mode = term::ScreenMode::strategic;
    auto strategic80 = term::render_ascii(snapshot, strategic80_options);

    term::RenderOptions strategic120_options = strategic80_options;
    strategic120_options.columns = 120U;
    strategic120_options.rows = 40U;
    auto strategic120 = term::render_ascii(snapshot, strategic120_options);

    tests.expect(static_cast<bool>(local80), "80x24 local render succeeds");
    tests.expect(static_cast<bool>(local120), "120x40 local render succeeds");
    tests.expect(static_cast<bool>(strategic80),
                 "80x24 strategic render succeeds");
    tests.expect(static_cast<bool>(strategic120),
                 "120x40 strategic render succeeds");
    if (!local80 || !local120 || !strategic80 || !strategic120) {
        const fd::Error& error = !local80
                                     ? local80.error()
                                     : (!local120
                                            ? local120.error()
                                            : (!strategic80
                                                   ? strategic80.error()
                                                   : strategic120.error()));
        (void)report_error(error, "ASCII rendering");
        return 1;
    }

    expect_ascii_frame(tests, local80.value(), 80U, 24U);
    expect_ascii_frame(tests, local120.value(), 120U, 40U);
    expect_ascii_frame(tests, strategic80.value(), 80U, 24U);
    expect_ascii_frame(tests, strategic120.value(), 120U, 40U);
    tests.expect(local80.value().find("LOCAL TILE MAP") != std::string::npos,
                 "local frame has distinct title");
    tests.expect(strategic80.value().find("STRATEGIC REGION GRAPH") !=
                     std::string::npos,
                 "strategic frame has distinct title");
    tests.expect(local80.value().find("ADMIN/OMNISCIENT REFERENCE") !=
                     std::string::npos,
                 "local scope is explicit");
    tests.expect(strategic80.value().find("ADMIN/OMNISCIENT REFERENCE") !=
                     std::string::npos,
                 "strategic scope is explicit");
    tests.expect(local80.value() != strategic80.value(),
                 "local and strategic frames are distinct");
    tests.expect(local80.value().find("Tile owner:") != std::string::npos &&
                     local80.value().find("Local polity:") != std::string::npos &&
                     local80.value().find("Foreign A:") != std::string::npos &&
                     local80.value().find("Foreign B:") != std::string::npos,
                 "local inspector identifies polity ownership and both factions");
    tests.expect(local80.value().find("Manifest:") != std::string::npos &&
                     local80.value().find("Seed:") != std::string::npos &&
                     local80.value().find("State:") != std::string::npos,
                 "local inspector exposes manifest, seed, and state identity");
    tests.expect(strategic80.value().find("owner -> [P]") !=
                         std::string::npos &&
                     strategic80.value().find("presence -> [F]") !=
                         std::string::npos,
                 "strategic graph identifies polity owner and foreign presences");
    tests.expect(strategic80.value().find("river ~> outlet") !=
                         std::string::npos &&
                     strategic80.value().find("road  [C] :: [I]") !=
                         std::string::npos,
                 "strategic graph exposes physical river and road references");
    expect_local_legend(tests, local80.value());
    expect_local_legend(tests, local120.value());
    expect_strategic_legend(tests, strategic80.value());
    expect_strategic_legend(tests, strategic120.value());

    auto debug_hash = term::render_debug_hash_ascii(snapshot, local80_options);
    tests.expect(static_cast<bool>(debug_hash),
                 "U01 debug state-hash screen renders");
    if (debug_hash) {
        expect_ascii_frame(tests, debug_hash.value(), 80U, 24U);
        tests.expect(debug_hash.value().find("DEBUG STATE HASH") !=
                         std::string::npos &&
                         debug_hash.value().find("State SHA-256 [00..15]") !=
                             std::string::npos &&
                         debug_hash.value().find("Root seed:") !=
                             std::string::npos,
                     "debug screen exposes canonical hash and seed identity");
        auto debug_again =
            term::render_debug_hash_ascii(snapshot, local80_options);
        tests.expect(static_cast<bool>(debug_again) &&
                         debug_again.value() == debug_hash.value(),
                     "debug hash screen is byte deterministic");
    }

    fd_ascii_options c_ascii{};
    tests.expect(fd_ascii_options_init(nullptr) == FD_ERR_INVALID_ARGUMENT,
                 "C ASCII options initializer rejects null");
    tests.expect(fd_ascii_options_init(&c_ascii) == FD_OK,
                 "C ASCII options initializer succeeds");
    c_ascii.columns = local80_options.columns;
    c_ascii.rows = local80_options.rows;
    c_ascii.screen = FD_ASCII_SCREEN_LOCAL;
    c_ascii.selected_x = local80_options.selected_x;
    c_ascii.selected_y = local80_options.selected_y;
    c_ascii.message_length =
        static_cast<std::uint32_t>(local80_options.message.size());
    std::memcpy(c_ascii.message, local80_options.message.data(),
                local80_options.message.size());
    fd_diagnostic c_diag{};
    tests.expect(fd_diagnostic_init(&c_diag) == FD_OK,
                 "C diagnostic initializer succeeds for renderer tests");
    std::uint64_t c_required = 0U;
    tests.expect(fd_ascii_measure(snapshot.native_handle(), &c_ascii,
                                  &c_required, &c_diag) == FD_OK,
                 "C ASCII measure succeeds");
    tests.expect(c_required == local80.value().size(),
                 "C measure equals C++ frame byte count");
    std::vector<char> c_frame(static_cast<std::size_t>(c_required));
    std::uint64_t c_written = 0U;
    tests.expect(fd_ascii_render(snapshot.native_handle(), &c_ascii,
                                 c_frame.data(), c_required, &c_written,
                                 &c_diag) == FD_OK,
                 "C ASCII render succeeds");
    tests.expect(c_written == c_required,
                 "C ASCII render reports exact bytes written");
    tests.expect(std::string(c_frame.begin(), c_frame.end()) == local80.value(),
                 "C and C++ local renderer bytes are identical");

    c_ascii.columns = strategic80_options.columns;
    c_ascii.rows = strategic80_options.rows;
    c_ascii.screen = FD_ASCII_SCREEN_STRATEGIC;
    c_required = 0U;
    tests.expect(fd_ascii_measure(snapshot.native_handle(), &c_ascii,
                                  &c_required, &c_diag) == FD_OK,
                 "C strategic ASCII measure succeeds");
    c_frame.assign(static_cast<std::size_t>(c_required), '\0');
    c_written = 0U;
    tests.expect(fd_ascii_render(snapshot.native_handle(), &c_ascii,
                                 c_frame.data(), c_required, &c_written,
                                 &c_diag) == FD_OK,
                 "C strategic ASCII render succeeds");
    tests.expect(std::string(c_frame.begin(), c_frame.end()) ==
                     strategic80.value(),
                 "C and C++ strategic renderer bytes are identical");

    std::uint64_t untouched_size = UINT64_C(123);
    tests.expect(fd_ascii_measure(nullptr, &c_ascii, &untouched_size, &c_diag) ==
                     FD_ERR_INVALID_ARGUMENT &&
                     untouched_size == 0U,
                 "C measure rejects null snapshot and clears size");
    untouched_size = UINT64_C(123);
    tests.expect(fd_ascii_measure(snapshot.native_handle(), nullptr,
                                  &untouched_size, &c_diag) ==
                     FD_ERR_INVALID_ARGUMENT &&
                     untouched_size == 0U,
                 "C measure rejects null options and clears size");
    tests.expect(fd_ascii_measure(snapshot.native_handle(), &c_ascii, nullptr,
                                  &c_diag) == FD_ERR_INVALID_ARGUMENT,
                 "C measure rejects null size output");
    fd_ascii_options malformed_ascii = c_ascii;
    malformed_ascii.struct_size = 0U;
    untouched_size = UINT64_C(123);
    tests.expect(fd_ascii_measure(snapshot.native_handle(), &malformed_ascii,
                                  &untouched_size, &c_diag) ==
                     FD_ERR_INVALID_SIZE &&
                     untouched_size == 0U,
                 "C measure rejects malformed option size transactionally");
    malformed_ascii = c_ascii;
    malformed_ascii.reserved[3] = 1U;
    tests.expect(fd_ascii_measure(snapshot.native_handle(), &malformed_ascii,
                                  &untouched_size, &c_diag) ==
                     FD_ERR_INVALID_ARGUMENT &&
                     c_diag.field_id == FD_FIELD_RESERVED &&
                     c_diag.item_index == 3U,
                 "C measure rejects nonzero reserved option with diagnostic");
    malformed_ascii = c_ascii;
    malformed_ascii.message_length = FD_ASCII_MAX_MESSAGE_BYTES + 1U;
    tests.expect(fd_ascii_measure(snapshot.native_handle(), &malformed_ascii,
                                  &untouched_size, &c_diag) ==
                     FD_ERR_OUT_OF_RANGE &&
                     c_diag.field_id == FD_FIELD_ASCII_MESSAGE,
                 "C measure rejects oversized bounded message");
    malformed_ascii = c_ascii;
    malformed_ascii.columns = 0U;
    tests.expect(fd_ascii_measure(snapshot.native_handle(), &malformed_ascii,
                                  &untouched_size, &c_diag) ==
                     FD_ERR_OUT_OF_RANGE &&
                     c_diag.field_id == FD_FIELD_WIDTH,
                 "C measure diagnoses invalid renderer dimensions");
    malformed_ascii = c_ascii;
    malformed_ascii.rows = 0U;
    tests.expect(fd_ascii_measure(snapshot.native_handle(), &malformed_ascii,
                                  &untouched_size, &c_diag) ==
                     FD_ERR_OUT_OF_RANGE &&
                     c_diag.field_id == FD_FIELD_HEIGHT,
                 "C measure diagnoses zero renderer rows");
    malformed_ascii = c_ascii;
    malformed_ascii.columns = FD_ASCII_MAX_COLUMNS + 1U;
    tests.expect(fd_ascii_measure(snapshot.native_handle(), &malformed_ascii,
                                  &untouched_size, &c_diag) == FD_ERR_CAPACITY &&
                     c_diag.field_id == FD_FIELD_WIDTH,
                 "C measure enforces maximum renderer columns");
    malformed_ascii = c_ascii;
    malformed_ascii.rows = FD_ASCII_MAX_ROWS + 1U;
    tests.expect(fd_ascii_measure(snapshot.native_handle(), &malformed_ascii,
                                  &untouched_size, &c_diag) == FD_ERR_CAPACITY &&
                     c_diag.field_id == FD_FIELD_HEIGHT,
                 "C measure enforces maximum renderer rows");
    malformed_ascii = c_ascii;
    malformed_ascii.screen = UINT32_MAX;
    tests.expect(fd_ascii_measure(snapshot.native_handle(), &malformed_ascii,
                                  &untouched_size, &c_diag) ==
                     FD_ERR_OUT_OF_RANGE &&
                     c_diag.field_id == FD_FIELD_ASCII_SCREEN,
                 "C measure rejects an unknown renderer screen");

    c_required = static_cast<std::uint64_t>(strategic80.value().size());
    std::vector<char> too_small(
        static_cast<std::size_t>(c_required - 1U), 'X');
    c_written = UINT64_C(123);
    tests.expect(fd_ascii_render(snapshot.native_handle(), &c_ascii,
                                 too_small.data(), c_required - 1U, &c_written,
                                 &c_diag) == FD_ERR_BUFFER_TOO_SMALL &&
                     c_written == 0U,
                 "C render negotiates a too-small buffer without partial write");
    tests.expect(std::all_of(too_small.begin(), too_small.end(),
                             [](const char value) { return value == 'X'; }),
                 "C render leaves too-small buffer untouched");
    c_written = UINT64_C(123);
    tests.expect(fd_ascii_render(snapshot.native_handle(), &c_ascii, nullptr,
                                 c_required, &c_written, &c_diag) ==
                     FD_ERR_INVALID_ARGUMENT &&
                     c_written == 0U,
                 "C render rejects null output buffer transactionally");
    c_written = UINT64_C(123);
    tests.expect(fd_ascii_render(snapshot.native_handle(), &c_ascii, nullptr,
                                 0U, &c_written, &c_diag) ==
                     FD_ERR_INVALID_ARGUMENT &&
                     c_written == 0U,
                 "C render classifies null+zero buffer as invalid argument");
    tests.expect(fd_ascii_render(snapshot.native_handle(), &c_ascii,
                                 c_frame.data(), c_required, nullptr,
                                 &c_diag) == FD_ERR_INVALID_ARGUMENT,
                 "C render rejects null written output");

    auto local80_again = term::render_ascii(snapshot, local80_options);
    tests.expect(static_cast<bool>(local80_again), "repeat render succeeds");
    if (local80_again) {
        tests.expect(local80_again.value() == local80.value(),
                     "repeat render is byte deterministic");
    }

    term::RenderOptions edge_options = local80_options;
    edge_options.viewport_x = UINT32_MAX;
    edge_options.viewport_y = UINT32_MAX;
    edge_options.selected_x = UINT32_MAX;
    edge_options.selected_y = UINT32_MAX;
    auto edge_frame = term::render_ascii(snapshot, edge_options);
    tests.expect(static_cast<bool>(edge_frame),
                 "viewport and cursor clamp to public map bounds");
    if (edge_frame) {
        expect_ascii_frame(tests, edge_frame.value(), 80U, 24U);
        tests.expect(edge_frame.value().find("Tile owner:") !=
                             std::string::npos &&
                         edge_frame.value().find("Local polity:") !=
                             std::string::npos &&
                         edge_frame.value().find("Foreign A:") !=
                             std::string::npos &&
                         edge_frame.value().find("Foreign B:") !=
                             std::string::npos &&
                         edge_frame.value().find("Manifest:") !=
                             std::string::npos &&
                         edge_frame.value().find("State:") !=
                             std::string::npos &&
                         edge_frame.value().find("Seed:") != std::string::npos,
                     "edge viewport retains complete immutable inspector facts");
    }

    term::RenderOptions small_options = local80_options;
    small_options.columns = 79U;
    small_options.rows = 23U;
    auto size_notice = term::render_ascii(snapshot, small_options);
    tests.expect(static_cast<bool>(size_notice),
                 "below-minimum terminal renders deterministic notice");
    if (size_notice) {
        expect_ascii_frame(tests, size_notice.value(), 79U, 23U);
        tests.expect(size_notice.value().find("TERMINAL SIZE NOTICE") !=
                         std::string::npos,
                     "below-minimum notice is explicit");
    }

    term::RenderOptions fallback_options = local80_options;
    fallback_options.message =
        std::string("unsafe ") + static_cast<char>(0x1b) + " utf8 " +
        static_cast<char>(0xc3) + static_cast<char>(0xa9);
    auto fallback_frame = term::render_ascii(snapshot, fallback_options);
    tests.expect(static_cast<bool>(fallback_frame),
                 "ASCII fallback accepts non-ASCII presentation text");
    if (fallback_frame) {
        expect_ascii_frame(tests, fallback_frame.value(), 80U, 24U);
        tests.expect(fallback_frame.value().find("unsafe ? utf8 ??") !=
                         std::string::npos,
                     "control and UTF-8 bytes use visible 7-bit fallback");
        tests.expect(fallback_frame.value().find(static_cast<char>(0x1b)) ==
                         std::string::npos,
                     "ASCII mode emits no terminal control sequence byte");
    }

    expect_golden(tests, "local_80x24.txt", local80.value());
    expect_golden(tests, "local_120x40.txt", local120.value());
    expect_golden(tests, "strategic_80x24.txt", strategic80.value());
    expect_golden(tests, "strategic_120x40.txt", strategic120.value());

    arm_cpp_allocation_failure();
    auto failed_replay_build = world.build_replay();
    tests.expect(!failed_replay_build &&
                     failed_replay_build.error().code == FD_ERR_OUT_OF_MEMORY,
                 "World::build_replay maps C++ output allocation failure");

    auto empty_replay_bytes = world.build_replay();
    tests.expect(static_cast<bool>(empty_replay_bytes),
                 "C++ replay builder emits empty tick-zero audit");
    if (!empty_replay_bytes) {
        (void)report_error(empty_replay_bytes.error(), "empty replay build");
        return 1;
    }
    auto empty_replay = moved_context.open_replay(empty_replay_bytes.value());
    tests.expect(static_cast<bool>(empty_replay),
                 "C++ wrapper opens empty tick-zero audit");
    if (!empty_replay) {
        (void)report_error(empty_replay.error(), "empty replay open");
        return 1;
    }
    auto empty_inspector = term::render_replay_inspector_ascii(
        empty_replay.value(), 0U, local80_options);
    tests.expect(static_cast<bool>(empty_inspector),
                 "replay inspector renders authoritative empty audit");
    if (empty_inspector) {
        expect_ascii_frame(tests, empty_inspector.value(), 80U, 24U);
        tests.expect(empty_inspector.value().find("EMPTY ACTION AUDIT") !=
                         std::string::npos &&
                         empty_inspector.value().find(
                             "No action attempts are recorded") !=
                             std::string::npos,
                     "empty replay inspector is explicit rather than fabricated");
    }

    fd_action pass_action{};
    pass_action.struct_size = static_cast<std::uint32_t>(sizeof(pass_action));
    pass_action.abi_version = FD_ABI_VERSION;
    pass_action.category = FD_ACTION_PASS;
    fd_joint_decision valid_attempt{};
    valid_attempt.struct_size =
        static_cast<std::uint32_t>(sizeof(valid_attempt));
    valid_attempt.abi_version = FD_ABI_VERSION;
    valid_attempt.seat_count = FD_U01_SEAT_COUNT;
    valid_attempt.expected_tick = world_info.value().tick;
    std::copy(std::begin(world_info.value().state_sha256),
              std::end(world_info.value().state_sha256),
              std::begin(valid_attempt.expected_pre_state_sha256));
    for (std::uint32_t seat = 0U; seat < FD_U01_SEAT_COUNT; ++seat) {
        valid_attempt.seats[seat].actor_id =
            world_info.value().foreign_faction_ids[seat];
        valid_attempt.seats[seat].action = pass_action;
    }
    fd_joint_decision rejected_attempt = valid_attempt;
    rejected_attempt.seats[0].action.category = UINT32_MAX;
    const std::array<fd_joint_decision, 2U> mixed_attempts{
        rejected_attempt, valid_attempt};
    auto mixed_replay_bytes = world.build_replay(mixed_attempts);
    tests.expect(static_cast<bool>(mixed_replay_bytes),
                 "C++ replay builder preserves rejected attempts as audit");
    if (!mixed_replay_bytes) {
        (void)report_error(mixed_replay_bytes.error(), "mixed replay build");
        return 1;
    }
    auto mixed_replay = moved_context.open_replay(mixed_replay_bytes.value());
    tests.expect(static_cast<bool>(mixed_replay),
                 "C++ wrapper opens accepted/rejected replay");
    if (!mixed_replay) {
        (void)report_error(mixed_replay.error(), "mixed replay open");
        return 1;
    }
    auto mixed_info = mixed_replay.value().info();
    tests.expect(static_cast<bool>(mixed_info) &&
                     mixed_info.value().decision_count == 1U &&
                     mixed_info.value().audit_count == 1U,
                 "mixed replay distinguishes accepted and rejected attempts");
    auto rejected_audit = mixed_replay.value().audit(0U);
    tests.expect(static_cast<bool>(rejected_audit) &&
                     rejected_audit.value().attempt_index == 0U &&
                     rejected_audit.value().result == FD_ERR_INVALID_ACTION &&
                     rejected_audit.value().message_length != 0U,
                 "C++ replay audit exposes structured rejected diagnostic");
    term::RenderOptions mixed_options = local80_options;
    mixed_options.message = "Structured action-attempt audit.";
    auto mixed_log = term::render_message_log_ascii(
        mixed_replay.value(), 0U, mixed_options);
    tests.expect(static_cast<bool>(mixed_log),
                 "mixed accepted/rejected action log renders");
    if (mixed_log) {
        expect_ascii_frame(tests, mixed_log.value(), 80U, 24U);
        tests.expect(mixed_log.value().find(
                         "#0 | tick 0 | REJECT code=") != std::string::npos &&
                         mixed_log.value().find(
                             "#1 | 0->24 | PASS | PASS") !=
                             std::string::npos &&
                         mixed_log.value().find("field=") !=
                             std::string::npos &&
                         mixed_log.value().find("item=") !=
                             std::string::npos,
                     "message log merges attempts and shows structured diagnostics");
    }
    auto rejected_only = term::render_message_log_ascii(
        mixed_replay.value(), 0U, mixed_options, "REJECT");
    tests.expect(static_cast<bool>(rejected_only) &&
                     rejected_only.value().find("#0 | tick 0 | REJECT") !=
                         std::string::npos &&
                     rejected_only.value().find("#1 | 0->24") ==
                         std::string::npos,
                 "message-log filter applies to accepted and rejected audit rows");
    auto rejected_inspector = term::render_replay_inspector_ascii(
        mixed_replay.value(), 0U, mixed_options);
    tests.expect(static_cast<bool>(rejected_inspector),
                 "replay inspector renders rejected attempt details");
    if (rejected_inspector) {
        expect_ascii_frame(tests, rejected_inspector.value(), 80U, 24U);
        tests.expect(rejected_inspector.value().find(
                         "REJECTED ACTION ATTEMPT") != std::string::npos &&
                         rejected_inspector.value().find("Diagnostic: field=") !=
                             std::string::npos &&
                         rejected_inspector.value().find(
                             "Diagnostic text:") != std::string::npos,
                     "replay inspector exposes structured rejection diagnostic");
    }
    auto accepted_inspector = term::render_replay_inspector_ascii(
        mixed_replay.value(), 1U, mixed_options);
    tests.expect(static_cast<bool>(accepted_inspector) &&
                     accepted_inspector.value().find(
                         "VERIFIED ACCEPTED DECISION") != std::string::npos,
                 "replay inspector indexes accepted attempts after rejections");

    auto hash_after = world.state_hash();
    auto save_after = world.save();
    tests.expect(static_cast<bool>(hash_after) &&
                     hash_after.value() == hash_before.value(),
                 "rendering leaves authoritative state hash unchanged");
    tests.expect(static_cast<bool>(save_after) &&
                     save_after.value() == save_before.value(),
                 "rendering leaves canonical save bytes unchanged");

    auto step = world.step_explicit_pass();
    tests.expect(static_cast<bool>(step), "explicit two-seat PASS step succeeds");
    if (!step) {
        (void)report_error(step.error(), "PASS step");
        return 1;
    }
    tests.expect(step.value().tick_before == 0U &&
                     step.value().tick_after == FD_OPERATIONAL_TICKS,
                 "PASS advances exactly one operational interval");
    tests.expect(step.value().accepted_action_count == FD_U01_SEAT_COUNT,
                 "both explicit seat actions are accepted");
    tests.expect(step.value().reward_count == 0U,
                 "U01 does not fabricate a reward");
    tests.expect(step.value().phase_count == FD_U01_PHASE_COUNT,
                 "all 23 authoritative phases execute");

    auto stale_snapshot_hash = snapshot.state_hash();
    tests.expect(static_cast<bool>(stale_snapshot_hash) &&
                     stale_snapshot_hash.value() == hash_before.value(),
                 "immutable snapshot remains at its captured state after step");
    auto stepped_hash = world.state_hash();
    tests.expect(static_cast<bool>(stepped_hash) &&
                     stepped_hash.value() != hash_before.value(),
                 "accepted PASS produces a new tick/hash");

    auto stepped_save = world.save();
    if (!stepped_save) {
        (void)report_error(stepped_save.error(), "stepped save");
        return 1;
    }
    auto loaded = moved_context.load(stepped_save.value());
    tests.expect(static_cast<bool>(loaded), "C++ load wrapper succeeds");
    if (loaded) {
        auto loaded_hash = loaded.value().state_hash();
        tests.expect(static_cast<bool>(loaded_hash) &&
                         loaded_hash.value() == stepped_hash.value(),
                     "save/load preserves hash through public C API");
    }

    auto replay_bytes = world.build_replay();
    tests.expect(static_cast<bool>(replay_bytes),
                 "C++ replay builder emits canonical bytes");
    if (!replay_bytes) {
        (void)report_error(replay_bytes.error(), "replay build");
        return 1;
    }
    auto live_before_replay_open_failure = moved_context.allocation_stats();
    arm_cpp_allocation_failure();
    auto failed_replay_open = moved_context.open_replay(replay_bytes.value());
    tests.expect(!failed_replay_open &&
                     failed_replay_open.error().code == FD_ERR_OUT_OF_MEMORY,
                 "Context::open_replay maps control-block allocation failure");
    auto live_after_replay_open_failure = moved_context.allocation_stats();
    tests.expect(static_cast<bool>(live_before_replay_open_failure) &&
                     static_cast<bool>(live_after_replay_open_failure) &&
                     live_before_replay_open_failure.value().live_bytes ==
                         live_after_replay_open_failure.value().live_bytes,
                 "failed replay wrapper releases its C replay handle");

    auto replay = moved_context.open_replay(replay_bytes.value());
    tests.expect(static_cast<bool>(replay), "C++ replay wrapper opens bytes");
    if (!replay) {
        (void)report_error(replay.error(), "replay open");
        return 1;
    }
    auto replay_info = replay.value().info();
    tests.expect(static_cast<bool>(replay_info) &&
                     replay_info.value().decision_count == 1U &&
                     replay_info.value().audit_count == 0U,
                 "replay reports the accepted PASS decision");
    auto record = replay.value().record(0U);
    tests.expect(static_cast<bool>(record) &&
                     record.value().decision.seat_count == FD_U01_SEAT_COUNT,
                 "replay inspector exposes explicit joint decision");
    auto absent_audit = replay.value().audit(0U);
    tests.expect(!absent_audit &&
                     absent_audit.error().code == FD_ERR_OUT_OF_RANGE,
                 "C++ replay audit wrapper reports absent rejected attempts");
    term::RenderOptions audit_options = local80_options;
    audit_options.message = "Immutable replay audit.";
    auto replay_screen = term::render_replay_inspector_ascii(
        replay.value(), 0U, audit_options);
    tests.expect(static_cast<bool>(replay_screen),
                 "U01 replay inspector screen renders");
    if (replay_screen) {
        expect_ascii_frame(tests, replay_screen.value(), 80U, 24U);
        tests.expect(replay_screen.value().find("REPLAY INSPECTOR") !=
                         std::string::npos &&
                         replay_screen.value().find("Seat 0 actor") !=
                             std::string::npos &&
                         replay_screen.value().find("Post-state [00..15]") !=
                             std::string::npos &&
                         replay_screen.value().find(
                             "RNG domain trace entries:") !=
                             std::string::npos,
                     "replay inspector exposes seats, actions, hashes, and RNG trace count");
    }
    auto invalid_replay_screen = term::render_replay_inspector_ascii(
        replay.value(), 1U, audit_options);
    tests.expect(!invalid_replay_screen &&
                     invalid_replay_screen.error().code == FD_ERR_OUT_OF_RANGE,
                 "replay inspector rejects out-of-range record explicitly");
    auto message_screen = term::render_message_log_ascii(
        replay.value(), 0U, audit_options);
    tests.expect(static_cast<bool>(message_screen),
                 "U01 message-log audit screen renders");
    if (message_screen) {
        expect_ascii_frame(tests, message_screen.value(), 80U, 24U);
        tests.expect(message_screen.value().find("MESSAGE LOG") !=
                         std::string::npos &&
                         message_screen.value().find("#0 | 0->24 | PASS | PASS") !=
                             std::string::npos,
                     "message log contains real accepted PASS audit record");
    }
    auto replay_info_after_render = replay.value().info();
    tests.expect(static_cast<bool>(replay_info_after_render) && replay_info &&
                     replay_info_after_render.value().cursor ==
                         replay_info.value().cursor,
                 "replay/message rendering does not mutate replay cursor");
    auto live_before_replay_world_failure = moved_context.allocation_stats();
    arm_cpp_allocation_failure();
    auto failed_replay_world = replay.value().create_world();
    tests.expect(!failed_replay_world &&
                     failed_replay_world.error().code == FD_ERR_OUT_OF_MEMORY,
                 "Replay::create_world maps C++ baseline allocation failure");
    auto live_after_replay_world_failure = moved_context.allocation_stats();
    tests.expect(static_cast<bool>(live_before_replay_world_failure) &&
                     static_cast<bool>(live_after_replay_world_failure) &&
                     live_before_replay_world_failure.value().live_bytes ==
                         live_after_replay_world_failure.value().live_bytes,
                 "failed replay-world wrapper releases its C world handle");

    auto replay_world = replay.value().create_world();
    tests.expect(static_cast<bool>(replay_world),
                 "replay creates its immutable tick-zero checkpoint world");
    if (replay_world) {
        auto replay_world_hash_before_failure =
            replay_world.value().state_hash();
        arm_cpp_allocation_failure();
        auto failed_advance =
            replay.value().advance(replay_world.value(), 1U);
        tests.expect(!failed_advance &&
                         failed_advance.error().code == FD_ERR_OUT_OF_MEMORY,
                     "Replay::advance maps batch allocation failure");
        auto replay_info_after_failed_advance = replay.value().info();
        auto replay_world_hash_after_failure =
            replay_world.value().state_hash();
        tests.expect(static_cast<bool>(replay_info_after_failed_advance) &&
                         replay_info_after_failed_advance.value().cursor == 0U &&
                         static_cast<bool>(replay_world_hash_before_failure) &&
                         static_cast<bool>(replay_world_hash_after_failure) &&
                         replay_world_hash_before_failure.value() ==
                             replay_world_hash_after_failure.value(),
                     "failed Replay::advance preserves cursor and world state");

        auto advanced = replay.value().advance(replay_world.value(), 1U);
        tests.expect(static_cast<bool>(advanced) &&
                         advanced.value().complete != 0U,
                     "replay verifies through final record");
        auto replay_hash = replay_world.value().state_hash();
        tests.expect(static_cast<bool>(replay_hash) &&
                         replay_hash.value() == stepped_hash.value(),
                     "replayed state equals live stepped state");
        auto rebuilt_bytes = replay_world.value().build_replay();
        tests.expect(static_cast<bool>(rebuilt_bytes),
                     "replay-advanced world retains accepted decision history");
        if (rebuilt_bytes) {
            auto rebuilt = moved_context.open_replay(rebuilt_bytes.value());
            auto rebuilt_info = rebuilt ? rebuilt.value().info()
                                        : fd::Result<fd_replay_info>::failure(
                                              rebuilt.error());
            tests.expect(static_cast<bool>(rebuilt_info) &&
                             rebuilt_info.value().decision_count == 1U,
                         "rebuilt replay contains the advanced decision");
        }
    }

    auto batch_source_result = make_world(moved_context, UINT64_C(99));
    if (!batch_source_result) {
        (void)report_error(batch_source_result.error(), "batch replay source");
        return 1;
    }
    fd::World batch_source = std::move(batch_source_result).value();
    arm_cpp_allocation_failure();
    auto failed_step = batch_source.step_explicit_pass();
    tests.expect(!failed_step &&
                     failed_step.error().code == FD_ERR_OUT_OF_MEMORY,
                 "World::step_explicit_pass maps history allocation failure");
    auto batch_info_after_failed_step = batch_source.info();
    tests.expect(static_cast<bool>(batch_info_after_failed_step) &&
                     batch_info_after_failed_step.value().tick == 0U,
                 "failed PASS history allocation preserves world tick");
    for (std::uint32_t index = 0U; index < 3U; ++index) {
        auto accepted = batch_source.step_explicit_pass();
        if (!accepted) {
            (void)report_error(accepted.error(), "batch replay source step");
            return 1;
        }
    }
    auto batch_bytes = batch_source.build_replay();
    if (!batch_bytes) {
        (void)report_error(batch_bytes.error(), "batch replay build");
        return 1;
    }
    auto batch_replay = moved_context.open_replay(batch_bytes.value());
    if (!batch_replay) {
        (void)report_error(batch_replay.error(), "batch replay open");
        return 1;
    }
    auto batch_world = batch_replay.value().create_world();
    if (!batch_world) {
        (void)report_error(batch_world.error(), "batch replay world");
        return 1;
    }
    auto first_batch = batch_replay.value().advance(batch_world.value(), 1U);
    tests.expect(static_cast<bool>(first_batch) &&
                     first_batch.value().cursor == 1U &&
                     first_batch.value().complete == 0U,
                 "replay wrapper records a first partial batch");
    auto remaining_batch = batch_replay.value().advance(batch_world.value(), 99U);
    tests.expect(static_cast<bool>(remaining_batch) &&
                     remaining_batch.value().decisions_advanced == 2U &&
                     remaining_batch.value().complete != 0U,
                 "replay wrapper clamps and records batch at nonzero cursor");
    auto rebuilt_batch_bytes = batch_world.value().build_replay();
    tests.expect(static_cast<bool>(rebuilt_batch_bytes),
                 "batch-advanced world can rebuild its replay");
    if (rebuilt_batch_bytes) {
        auto rebuilt_batch =
            moved_context.open_replay(rebuilt_batch_bytes.value());
        auto rebuilt_batch_info = rebuilt_batch
                                      ? rebuilt_batch.value().info()
                                      : fd::Result<fd_replay_info>::failure(
                                            rebuilt_batch.error());
        tests.expect(static_cast<bool>(rebuilt_batch_info) &&
                         rebuilt_batch_info.value().decision_count == 3U,
                     "rebuilt batch replay contains all three decisions once");
    }

    // A World retains the shared ContextState after all public Context wrappers
    // leave scope. This is the lifetime case that raw owning pointers miss.
    fd::World retained_world;
    {
        auto short_context_result = make_context();
        if (!short_context_result) {
            (void)report_error(short_context_result.error(),
                               "short-lived context");
            return 1;
        }
        fd::Context short_context = std::move(short_context_result).value();
        auto short_world = make_world(short_context, 7U);
        if (!short_world) {
            (void)report_error(short_world.error(), "retained world generation");
            return 1;
        }
        retained_world = std::move(short_world).value();
    }
    tests.expect(retained_world.valid(),
                 "world keeps context authority alive through RAII");
    auto retained_info = retained_world.info();
    tests.expect(static_cast<bool>(retained_info) &&
                     retained_info.value().tick == 0U,
                 "retained world remains queryable after Context destruction");

    auto allocation_stats = moved_context.allocation_stats();
    tests.expect(static_cast<bool>(allocation_stats),
                 "C++ context exposes authoritative allocation statistics");
    if (allocation_stats) {
        tests.expect(allocation_stats.value().allocation_calls >=
                         allocation_stats.value().deallocation_calls,
                     "allocation counters remain internally ordered");
    }

    return tests.finish();
}
