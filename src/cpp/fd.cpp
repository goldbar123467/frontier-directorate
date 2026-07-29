#include "fd.hpp"

#include <algorithm>
#include <array>
#include <cstring>
#include <limits>
#include <memory>
#include <new>
#include <stdexcept>
#include <string>
#include <utility>

namespace frontier_directorate {
namespace {

template <class T>
void initialize_output(T& output) noexcept {
    output = T{};
    output.struct_size = static_cast<std::uint32_t>(sizeof(T));
    output.abi_version = FD_ABI_VERSION;
}

fd_diagnostic diagnostic() noexcept {
    fd_diagnostic result{};
    const fd_result initialized = fd_diagnostic_init(&result);
    if (initialized != FD_OK) {
        initialize_output(result);
    }
    return result;
}

Error wrapper_error(const fd_result code, const std::string& message) {
    Error result{};
    result.code = code;
    result.message = message;
    return result;
}

template <class T>
Result<T> allocation_failure(const fd_result code) {
    Error error{};
    error.code = code;
    return Result<T>::failure(std::move(error));
}

struct ContextDeleter final {
    void operator()(fd_context* context) const noexcept {
        if (context != nullptr) {
            fd_context* owned = context;
            (void)fd_context_destroy(&owned, nullptr);
        }
    }
};

struct WorldDeleter final {
    void operator()(fd_world* world) const noexcept {
        if (world != nullptr) {
            fd_world* owned = world;
            (void)fd_world_destroy(&owned, nullptr);
        }
    }
};

struct SnapshotDeleter final {
    void operator()(fd_snapshot* snapshot) const noexcept {
        if (snapshot != nullptr) {
            fd_snapshot* owned = snapshot;
            (void)fd_snapshot_destroy(&owned, nullptr);
        }
    }
};

struct ReplayDeleter final {
    void operator()(fd_replay* replay) const noexcept {
        if (replay != nullptr) {
            fd_replay* owned = replay;
            (void)fd_replay_destroy(&owned, nullptr);
        }
    }
};

using ContextHandle = std::unique_ptr<fd_context, ContextDeleter>;
using WorldHandle = std::unique_ptr<fd_world, WorldDeleter>;
using SnapshotHandle = std::unique_ptr<fd_snapshot, SnapshotDeleter>;
using ReplayHandle = std::unique_ptr<fd_replay, ReplayDeleter>;

Result<std::vector<std::byte>> save_handle(const fd_world* const world) try {
    fd_diagnostic diag = diagnostic();
    std::uint64_t required = 0U;
    const fd_result measured = fd_world_save_size(world, &required, &diag);
    if (measured != FD_OK) {
        return Result<std::vector<std::byte>>::failure(
            error_from(measured, diag));
    }
    if (required > static_cast<std::uint64_t>(
                       std::numeric_limits<std::size_t>::max())) {
        return Result<std::vector<std::byte>>::failure(wrapper_error(
            FD_ERR_CAPACITY, "save does not fit the C++ address space"));
    }

    std::vector<std::byte> bytes(static_cast<std::size_t>(required));
    std::uint64_t written = 0U;
    diag = diagnostic();
    const fd_result saved = fd_world_save(
        world, bytes.empty() ? nullptr : bytes.data(), required, &written, &diag);
    if (saved != FD_OK) {
        return Result<std::vector<std::byte>>::failure(error_from(saved, diag));
    }
    if (written != required) {
        return Result<std::vector<std::byte>>::failure(wrapper_error(
            FD_ERR_INTERNAL, "save size changed between measure and write"));
    }
    return Result<std::vector<std::byte>>::success(std::move(bytes));
} catch (const std::bad_alloc&) {
    return allocation_failure<std::vector<std::byte>>(FD_ERR_OUT_OF_MEMORY);
} catch (const std::length_error&) {
    return allocation_failure<std::vector<std::byte>>(FD_ERR_CAPACITY);
}

}  // namespace

namespace detail {

class ContextState final {
public:
    explicit ContextState(ContextHandle handle) noexcept
        : handle_(std::move(handle)) {}

    [[nodiscard]] fd_context* get() const noexcept { return handle_.get(); }

private:
    ContextHandle handle_{};
};

class WorldState final {
public:
    WorldState(std::shared_ptr<ContextState> context,
               WorldHandle handle,
               std::vector<std::byte> initial_save) noexcept
        : context_(std::move(context)),
          handle_(std::move(handle)),
          initial_save_(std::move(initial_save)) {}

    [[nodiscard]] fd_world* get() const noexcept { return handle_.get(); }
    [[nodiscard]] const std::shared_ptr<ContextState>& context() const noexcept {
        return context_;
    }
    [[nodiscard]] const std::vector<std::byte>& initial_save() const noexcept {
        return initial_save_;
    }
    [[nodiscard]] const std::vector<fd_joint_decision>& decisions() const
        noexcept {
        return decisions_;
    }
    [[nodiscard]] bool prepare_decisions(const std::size_t additional) noexcept {
        if (additional > decisions_.max_size() - decisions_.size()) {
            return false;
        }
        const std::size_t required = decisions_.size() + additional;
        if (required > decisions_.capacity()) {
            try {
                std::size_t next = decisions_.empty() ? 8U
                                                       : decisions_.capacity();
                while (next < required) {
                    if (next > decisions_.max_size() / 2U) {
                        next = required;
                        break;
                    }
                    next *= 2U;
                }
                decisions_.reserve(next);
            } catch (const std::bad_alloc&) {
                return false;
            } catch (const std::length_error&) {
                return false;
            }
        }
        return true;
    }
    void append_decision(const fd_joint_decision& decision) noexcept {
        decisions_.push_back(decision);
    }

private:
    // Declaration order ensures the world is destroyed before its context.
    std::shared_ptr<ContextState> context_{};
    WorldHandle handle_{};
    std::vector<std::byte> initial_save_{};
    std::vector<fd_joint_decision> decisions_{};
};

class SnapshotState final {
public:
    SnapshotState(std::shared_ptr<ContextState> context,
                  SnapshotHandle handle) noexcept
        : context_(std::move(context)), handle_(std::move(handle)) {}

    [[nodiscard]] fd_snapshot* get() const noexcept { return handle_.get(); }

private:
    std::shared_ptr<ContextState> context_{};
    SnapshotHandle handle_{};
};

class ReplayState final {
public:
    ReplayState(std::shared_ptr<ContextState> context,
                ReplayHandle handle) noexcept
        : context_(std::move(context)), handle_(std::move(handle)) {}

    [[nodiscard]] fd_replay* get() const noexcept { return handle_.get(); }
    [[nodiscard]] const std::shared_ptr<ContextState>& context() const noexcept {
        return context_;
    }

private:
    std::shared_ptr<ContextState> context_{};
    ReplayHandle handle_{};
};

}  // namespace detail

Result<fd_context_config> default_context_config() try {
    fd_context_config config{};
    const fd_result result = fd_context_config_init(&config);
    if (result != FD_OK) {
        return Result<fd_context_config>::failure(
            wrapper_error(result, "fd_context_config_init failed"));
    }
    return Result<fd_context_config>::success(config);
} catch (const std::bad_alloc&) {
    return allocation_failure<fd_context_config>(FD_ERR_OUT_OF_MEMORY);
} catch (const std::length_error&) {
    return allocation_failure<fd_context_config>(FD_ERR_CAPACITY);
}

Result<fd_world_config> default_world_config() try {
    fd_world_config config{};
    const fd_result result = fd_world_config_init(&config);
    if (result != FD_OK) {
        return Result<fd_world_config>::failure(
            wrapper_error(result, "fd_world_config_init failed"));
    }
    return Result<fd_world_config>::success(config);
} catch (const std::bad_alloc&) {
    return allocation_failure<fd_world_config>(FD_ERR_OUT_OF_MEMORY);
} catch (const std::length_error&) {
    return allocation_failure<fd_world_config>(FD_ERR_CAPACITY);
}

Context::Context(std::shared_ptr<detail::ContextState> state) noexcept
    : state_(std::move(state)) {}

Result<Context> Context::create(const fd_context_config& config) try {
    fd_context* raw = nullptr;
    fd_diagnostic diag = diagnostic();
    const fd_result result = fd_context_create(&config, &raw, &diag);
    if (result != FD_OK) {
        return Result<Context>::failure(error_from(result, diag));
    }
    if (raw == nullptr) {
        return Result<Context>::failure(wrapper_error(
            FD_ERR_INTERNAL, "context create succeeded with a null handle"));
    }

    auto state = std::make_shared<detail::ContextState>(ContextHandle(raw));
    return Result<Context>::success(Context(std::move(state)));
} catch (const std::bad_alloc&) {
    return allocation_failure<Context>(FD_ERR_OUT_OF_MEMORY);
} catch (const std::length_error&) {
    return allocation_failure<Context>(FD_ERR_CAPACITY);
}

bool Context::valid() const noexcept {
    return state_ != nullptr && state_->get() != nullptr;
}

Result<World> Context::generate(const fd_world_config& config,
                                const std::uint64_t seed,
                                const fd_content_manifest* const content) const try {
    if (!valid()) {
        return Result<World>::failure(
            wrapper_error(FD_ERR_STATE, "context is empty or moved-from"));
    }

    fd_content_manifest empty_content{};
    initialize_output(empty_content);
    const fd_content_manifest* const selected_content =
        content == nullptr ? &empty_content : content;

    fd_generation_report report{};
    initialize_output(report);
    fd_world* raw = nullptr;
    fd_diagnostic diag = diagnostic();
    const fd_result generated = fd_world_generate(
        state_->get(), &config, seed, selected_content, &raw, &report, &diag);
    if (generated != FD_OK) {
        return Result<World>::failure(error_from(generated, diag));
    }
    WorldHandle handle(raw);
    if (handle == nullptr) {
        return Result<World>::failure(wrapper_error(
            FD_ERR_INTERNAL, "world generation succeeded with a null handle"));
    }

    auto initial_save = save_handle(handle.get());
    if (!initial_save) {
        return Result<World>::failure(initial_save.error());
    }
    auto world_state = std::make_shared<detail::WorldState>(
        state_, std::move(handle), std::move(initial_save).value());
    return Result<World>::success(World(std::move(world_state)));
} catch (const std::bad_alloc&) {
    return allocation_failure<World>(FD_ERR_OUT_OF_MEMORY);
} catch (const std::length_error&) {
    return allocation_failure<World>(FD_ERR_CAPACITY);
}

Result<World> Context::load(const std::span<const std::byte> bytes) const try {
    if (!valid()) {
        return Result<World>::failure(
            wrapper_error(FD_ERR_STATE, "context is empty or moved-from"));
    }
    if (bytes.size() > static_cast<std::size_t>(
                           std::numeric_limits<std::uint64_t>::max())) {
        return Result<World>::failure(
            wrapper_error(FD_ERR_CAPACITY, "input is too large"));
    }

    fd_world* raw = nullptr;
    fd_diagnostic diag = diagnostic();
    const fd_result loaded = fd_world_load(
        state_->get(), bytes.empty() ? nullptr : bytes.data(),
        static_cast<std::uint64_t>(bytes.size()), &raw, &diag);
    if (loaded != FD_OK) {
        return Result<World>::failure(error_from(loaded, diag));
    }
    WorldHandle handle(raw);
    if (handle == nullptr) {
        return Result<World>::failure(wrapper_error(
            FD_ERR_INTERNAL, "world load succeeded with a null handle"));
    }

    std::vector<std::byte> initial_save(bytes.begin(), bytes.end());
    auto world_state = std::make_shared<detail::WorldState>(
        state_, std::move(handle), std::move(initial_save));
    return Result<World>::success(World(std::move(world_state)));
} catch (const std::bad_alloc&) {
    return allocation_failure<World>(FD_ERR_OUT_OF_MEMORY);
} catch (const std::length_error&) {
    return allocation_failure<World>(FD_ERR_CAPACITY);
}

Result<Replay> Context::open_replay(
    const std::span<const std::byte> bytes) const try {
    if (!valid()) {
        return Result<Replay>::failure(
            wrapper_error(FD_ERR_STATE, "context is empty or moved-from"));
    }
    if (bytes.size() > static_cast<std::size_t>(
                           std::numeric_limits<std::uint64_t>::max())) {
        return Result<Replay>::failure(
            wrapper_error(FD_ERR_CAPACITY, "input is too large"));
    }

    fd_replay* raw = nullptr;
    fd_diagnostic diag = diagnostic();
    const fd_result opened = fd_replay_open(
        state_->get(), bytes.empty() ? nullptr : bytes.data(),
        static_cast<std::uint64_t>(bytes.size()), &raw, &diag);
    if (opened != FD_OK) {
        return Result<Replay>::failure(error_from(opened, diag));
    }
    ReplayHandle handle(raw);
    if (handle == nullptr) {
        return Result<Replay>::failure(wrapper_error(
            FD_ERR_INTERNAL, "replay open succeeded with a null handle"));
    }
    auto replay_state = std::make_shared<detail::ReplayState>(
        state_, std::move(handle));
    return Result<Replay>::success(Replay(std::move(replay_state)));
} catch (const std::bad_alloc&) {
    return allocation_failure<Replay>(FD_ERR_OUT_OF_MEMORY);
} catch (const std::length_error&) {
    return allocation_failure<Replay>(FD_ERR_CAPACITY);
}

Result<fd_allocation_stats> Context::allocation_stats() const try {
    if (!valid()) {
        return Result<fd_allocation_stats>::failure(
            wrapper_error(FD_ERR_STATE, "context is empty or moved-from"));
    }
    fd_allocation_stats output{};
    initialize_output(output);
    fd_diagnostic diag = diagnostic();
    const fd_result result =
        fd_context_get_allocation_stats(state_->get(), &output, &diag);
    if (result != FD_OK) {
        return Result<fd_allocation_stats>::failure(error_from(result, diag));
    }
    return Result<fd_allocation_stats>::success(output);
} catch (const std::bad_alloc&) {
    return allocation_failure<fd_allocation_stats>(FD_ERR_OUT_OF_MEMORY);
} catch (const std::length_error&) {
    return allocation_failure<fd_allocation_stats>(FD_ERR_CAPACITY);
}

Snapshot::Snapshot(std::shared_ptr<detail::SnapshotState> state) noexcept
    : state_(std::move(state)) {}

Snapshot::Snapshot(Snapshot&& other) noexcept
    : state_(std::move(other.state_)),
      borrowed_(std::exchange(other.borrowed_, nullptr)) {}

Snapshot& Snapshot::operator=(Snapshot&& other) noexcept {
    if (this != &other) {
        state_ = std::move(other.state_);
        borrowed_ = std::exchange(other.borrowed_, nullptr);
    }
    return *this;
}

Snapshot Snapshot::borrow(const fd_snapshot* const snapshot) noexcept {
    Snapshot result;
    result.borrowed_ = snapshot;
    return result;
}

const fd_snapshot* Snapshot::native_handle() const noexcept {
    return state_ != nullptr ? state_->get() : borrowed_;
}

bool Snapshot::valid() const noexcept {
    return native_handle() != nullptr;
}

Result<fd_world_info> Snapshot::info() const try {
    if (!valid()) {
        return Result<fd_world_info>::failure(
            wrapper_error(FD_ERR_STATE, "snapshot is empty or moved-from"));
    }
    fd_world_info output{};
    initialize_output(output);
    fd_diagnostic diag = diagnostic();
    const fd_result result =
        fd_snapshot_get_info(native_handle(), &output, &diag);
    if (result != FD_OK) {
        return Result<fd_world_info>::failure(error_from(result, diag));
    }
    return Result<fd_world_info>::success(output);
} catch (const std::bad_alloc&) {
    return allocation_failure<fd_world_info>(FD_ERR_OUT_OF_MEMORY);
} catch (const std::length_error&) {
    return allocation_failure<fd_world_info>(FD_ERR_CAPACITY);
}

Result<fd_tile_view> Snapshot::tile(const std::uint32_t x,
                                    const std::uint32_t y) const try {
    if (!valid()) {
        return Result<fd_tile_view>::failure(
            wrapper_error(FD_ERR_STATE, "snapshot is empty or moved-from"));
    }
    fd_tile_view output{};
    initialize_output(output);
    fd_diagnostic diag = diagnostic();
    const fd_result result =
        fd_snapshot_get_tile(native_handle(), x, y, &output, &diag);
    if (result != FD_OK) {
        return Result<fd_tile_view>::failure(error_from(result, diag));
    }
    return Result<fd_tile_view>::success(output);
} catch (const std::bad_alloc&) {
    return allocation_failure<fd_tile_view>(FD_ERR_OUT_OF_MEMORY);
} catch (const std::length_error&) {
    return allocation_failure<fd_tile_view>(FD_ERR_CAPACITY);
}

Result<fd_entity_view> Snapshot::entity_by_index(
    const std::uint32_t index) const try {
    if (!valid()) {
        return Result<fd_entity_view>::failure(
            wrapper_error(FD_ERR_STATE, "snapshot is empty or moved-from"));
    }
    fd_entity_view output{};
    initialize_output(output);
    fd_diagnostic diag = diagnostic();
    const fd_result result = fd_snapshot_get_entity_by_index(
        native_handle(), index, &output, &diag);
    if (result != FD_OK) {
        return Result<fd_entity_view>::failure(error_from(result, diag));
    }
    return Result<fd_entity_view>::success(output);
} catch (const std::bad_alloc&) {
    return allocation_failure<fd_entity_view>(FD_ERR_OUT_OF_MEMORY);
} catch (const std::length_error&) {
    return allocation_failure<fd_entity_view>(FD_ERR_CAPACITY);
}

Result<fd_region_view> Snapshot::region() const try {
    if (!valid()) {
        return Result<fd_region_view>::failure(
            wrapper_error(FD_ERR_STATE, "snapshot is empty or moved-from"));
    }
    fd_region_view output{};
    initialize_output(output);
    fd_diagnostic diag = diagnostic();
    const fd_result result =
        fd_snapshot_get_region(native_handle(), &output, &diag);
    if (result != FD_OK) {
        return Result<fd_region_view>::failure(error_from(result, diag));
    }
    return Result<fd_region_view>::success(output);
} catch (const std::bad_alloc&) {
    return allocation_failure<fd_region_view>(FD_ERR_OUT_OF_MEMORY);
} catch (const std::length_error&) {
    return allocation_failure<fd_region_view>(FD_ERR_CAPACITY);
}

Result<std::array<std::uint8_t, 32U>> Snapshot::state_hash() const try {
    if (!valid()) {
        return Result<std::array<std::uint8_t, 32U>>::failure(
            wrapper_error(FD_ERR_STATE, "snapshot is empty or moved-from"));
    }
    std::array<std::uint8_t, 32U> output{};
    fd_diagnostic diag = diagnostic();
    const fd_result result =
        fd_snapshot_state_hash(native_handle(), output.data(), &diag);
    if (result != FD_OK) {
        return Result<std::array<std::uint8_t, 32U>>::failure(
            error_from(result, diag));
    }
    return Result<std::array<std::uint8_t, 32U>>::success(output);
} catch (const std::bad_alloc&) {
    return allocation_failure<std::array<std::uint8_t, 32U>>(
        FD_ERR_OUT_OF_MEMORY);
} catch (const std::length_error&) {
    return allocation_failure<std::array<std::uint8_t, 32U>>(FD_ERR_CAPACITY);
}

World::World(std::shared_ptr<detail::WorldState> state) noexcept
    : state_(std::move(state)) {}

bool World::valid() const noexcept {
    return state_ != nullptr && state_->get() != nullptr;
}

Result<fd_world_info> World::info() const try {
    if (!valid()) {
        return Result<fd_world_info>::failure(
            wrapper_error(FD_ERR_STATE, "world is empty or moved-from"));
    }
    fd_world_info output{};
    initialize_output(output);
    fd_diagnostic diag = diagnostic();
    const fd_result result = fd_world_get_info(state_->get(), &output, &diag);
    if (result != FD_OK) {
        return Result<fd_world_info>::failure(error_from(result, diag));
    }
    return Result<fd_world_info>::success(output);
} catch (const std::bad_alloc&) {
    return allocation_failure<fd_world_info>(FD_ERR_OUT_OF_MEMORY);
} catch (const std::length_error&) {
    return allocation_failure<fd_world_info>(FD_ERR_CAPACITY);
}

Result<std::array<std::uint8_t, 32U>> World::state_hash() const try {
    if (!valid()) {
        return Result<std::array<std::uint8_t, 32U>>::failure(
            wrapper_error(FD_ERR_STATE, "world is empty or moved-from"));
    }
    std::array<std::uint8_t, 32U> output{};
    fd_diagnostic diag = diagnostic();
    const fd_result result =
        fd_world_state_hash(state_->get(), output.data(), &diag);
    if (result != FD_OK) {
        return Result<std::array<std::uint8_t, 32U>>::failure(
            error_from(result, diag));
    }
    return Result<std::array<std::uint8_t, 32U>>::success(output);
} catch (const std::bad_alloc&) {
    return allocation_failure<std::array<std::uint8_t, 32U>>(
        FD_ERR_OUT_OF_MEMORY);
} catch (const std::length_error&) {
    return allocation_failure<std::array<std::uint8_t, 32U>>(FD_ERR_CAPACITY);
}

Result<std::vector<std::byte>> World::save() const try {
    if (!valid()) {
        return Result<std::vector<std::byte>>::failure(
            wrapper_error(FD_ERR_STATE, "world is empty or moved-from"));
    }
    return save_handle(state_->get());
} catch (const std::bad_alloc&) {
    return allocation_failure<std::vector<std::byte>>(FD_ERR_OUT_OF_MEMORY);
} catch (const std::length_error&) {
    return allocation_failure<std::vector<std::byte>>(FD_ERR_CAPACITY);
}

Result<Snapshot> World::reference_snapshot() const try {
    if (!valid()) {
        return Result<Snapshot>::failure(
            wrapper_error(FD_ERR_STATE, "world is empty or moved-from"));
    }
    fd_snapshot* raw = nullptr;
    fd_diagnostic diag = diagnostic();
    const fd_result created = fd_snapshot_create(
        state_->get(), FD_VISIBILITY_REFERENCE, 0U, &raw, &diag);
    if (created != FD_OK) {
        return Result<Snapshot>::failure(error_from(created, diag));
    }
    SnapshotHandle handle(raw);
    if (handle == nullptr) {
        return Result<Snapshot>::failure(wrapper_error(
            FD_ERR_INTERNAL, "snapshot create succeeded with a null handle"));
    }
    auto snapshot_state = std::make_shared<detail::SnapshotState>(
        state_->context(), std::move(handle));
    return Result<Snapshot>::success(Snapshot(std::move(snapshot_state)));
} catch (const std::bad_alloc&) {
    return allocation_failure<Snapshot>(FD_ERR_OUT_OF_MEMORY);
} catch (const std::length_error&) {
    return allocation_failure<Snapshot>(FD_ERR_CAPACITY);
}

Result<fd_step_result> World::step_explicit_pass() try {
    if (!valid()) {
        return Result<fd_step_result>::failure(
            wrapper_error(FD_ERR_STATE, "world is empty or moved-from"));
    }
    if (!state_->prepare_decisions(1U)) {
        return Result<fd_step_result>::failure(wrapper_error(
            FD_ERR_OUT_OF_MEMORY,
            "could not reserve wrapper replay history before stepping"));
    }

    auto info_result = info();
    if (!info_result) {
        return Result<fd_step_result>::failure(info_result.error());
    }
    const fd_world_info& world_info = info_result.value();

    fd_action pass{};
    initialize_output(pass);
    pass.category = FD_ACTION_PASS;

    for (std::uint32_t seat = 0U; seat < FD_U01_SEAT_COUNT; ++seat) {
        std::array<std::uint8_t, 2U> mask_bits{};
        fd_action_prefix prefix{};
        initialize_output(prefix);
        fd_action_mask mask{};
        initialize_output(mask);
        mask.byte_capacity = static_cast<std::uint32_t>(mask_bits.size());
        mask.bits = mask_bits.data();

        fd_diagnostic diag = diagnostic();
        const fd_result queried = fd_action_mask_query(
            state_->get(), world_info.foreign_faction_ids[seat], &prefix, &mask,
            &diag);
        if (queried != FD_OK) {
            return Result<fd_step_result>::failure(error_from(queried, diag));
        }
        const bool pass_in_range =
            FD_ACTION_PASS >= mask.first_value &&
            (FD_ACTION_PASS - mask.first_value) < mask.bit_count;
        bool pass_legal = false;
        if (pass_in_range) {
            const std::uint32_t bit = FD_ACTION_PASS - mask.first_value;
            const std::uint32_t byte_index = bit / 8U;
            const std::uint32_t bit_index = bit % 8U;
            if (byte_index < mask.byte_capacity) {
                pass_legal =
                    (mask_bits[byte_index] &
                     static_cast<std::uint8_t>(UINT8_C(1) << bit_index)) != 0U;
            }
        }
        if (!pass_legal) {
            return Result<fd_step_result>::failure(wrapper_error(
                FD_ERR_STATE,
                "the authoritative legal mask did not contain PASS"));
        }

        diag = diagnostic();
        const fd_result validated = fd_action_validate(
            state_->get(), world_info.foreign_faction_ids[seat], &pass, &diag);
        if (validated != FD_OK) {
            return Result<fd_step_result>::failure(error_from(validated, diag));
        }
    }

    fd_joint_decision decision{};
    initialize_output(decision);
    decision.seat_count = FD_U01_SEAT_COUNT;
    decision.expected_tick = world_info.tick;
    std::copy(std::begin(world_info.state_sha256),
              std::end(world_info.state_sha256),
              std::begin(decision.expected_pre_state_sha256));
    for (std::uint32_t seat = 0U; seat < FD_U01_SEAT_COUNT; ++seat) {
        decision.seats[seat].actor_id = world_info.foreign_faction_ids[seat];
        decision.seats[seat].action = pass;
    }

    fd_step_result output{};
    initialize_output(output);
    fd_diagnostic diag = diagnostic();
    const fd_result stepped =
        fd_world_step(state_->get(), &decision, &output, &diag);
    if (stepped != FD_OK) {
        return Result<fd_step_result>::failure(error_from(stepped, diag));
    }
    state_->append_decision(decision);
    return Result<fd_step_result>::success(output);
} catch (const std::bad_alloc&) {
    return allocation_failure<fd_step_result>(FD_ERR_OUT_OF_MEMORY);
} catch (const std::length_error&) {
    return allocation_failure<fd_step_result>(FD_ERR_CAPACITY);
}

Result<std::vector<std::byte>> World::build_replay() const try {
    if (!valid()) {
        return Result<std::vector<std::byte>>::failure(
            wrapper_error(FD_ERR_STATE, "world is empty or moved-from"));
    }
    const auto& decisions = state_->decisions();
    return build_replay(std::span<const fd_joint_decision>(
        decisions.data(), decisions.size()));
} catch (const std::bad_alloc&) {
    return allocation_failure<std::vector<std::byte>>(FD_ERR_OUT_OF_MEMORY);
} catch (const std::length_error&) {
    return allocation_failure<std::vector<std::byte>>(FD_ERR_CAPACITY);
}

Result<std::vector<std::byte>> World::build_replay(
    const std::span<const fd_joint_decision> attempts) const try {
    if (!valid()) {
        return Result<std::vector<std::byte>>::failure(
            wrapper_error(FD_ERR_STATE, "world is empty or moved-from"));
    }

    const std::vector<std::byte>& baseline = state_->initial_save();
    fd_world* raw_tick_zero = nullptr;
    fd_diagnostic diag = diagnostic();
    const fd_result loaded = fd_world_load(
        state_->context()->get(), baseline.empty() ? nullptr : baseline.data(),
        static_cast<std::uint64_t>(baseline.size()), &raw_tick_zero, &diag);
    if (loaded != FD_OK) {
        return Result<std::vector<std::byte>>::failure(error_from(loaded, diag));
    }
    WorldHandle tick_zero(raw_tick_zero);

    const fd_joint_decision* const decision_data =
        attempts.empty() ? nullptr : attempts.data();
    const std::uint64_t decision_count =
        static_cast<std::uint64_t>(attempts.size());
    std::uint64_t required = 0U;
    diag = diagnostic();
    const fd_result measured = fd_replay_build_size(
        tick_zero.get(), decision_data, decision_count, &required, &diag);
    if (measured != FD_OK) {
        return Result<std::vector<std::byte>>::failure(
            error_from(measured, diag));
    }
    if (required > static_cast<std::uint64_t>(
                       std::numeric_limits<std::size_t>::max())) {
        return Result<std::vector<std::byte>>::failure(wrapper_error(
            FD_ERR_CAPACITY, "replay does not fit the C++ address space"));
    }

    std::vector<std::byte> bytes(static_cast<std::size_t>(required));
    std::uint64_t written = 0U;
    diag = diagnostic();
    const fd_result built = fd_replay_build(
        tick_zero.get(), decision_data, decision_count,
        bytes.empty() ? nullptr : bytes.data(), required, &written, &diag);
    if (built != FD_OK) {
        return Result<std::vector<std::byte>>::failure(error_from(built, diag));
    }
    if (written != required) {
        return Result<std::vector<std::byte>>::failure(wrapper_error(
            FD_ERR_INTERNAL, "replay size changed between measure and write"));
    }
    return Result<std::vector<std::byte>>::success(std::move(bytes));
} catch (const std::bad_alloc&) {
    return allocation_failure<std::vector<std::byte>>(FD_ERR_OUT_OF_MEMORY);
} catch (const std::length_error&) {
    return allocation_failure<std::vector<std::byte>>(FD_ERR_CAPACITY);
}

Replay::Replay(std::shared_ptr<detail::ReplayState> state) noexcept
    : state_(std::move(state)) {}

bool Replay::valid() const noexcept {
    return state_ != nullptr && state_->get() != nullptr;
}

Result<fd_replay_info> Replay::info() const try {
    if (!valid()) {
        return Result<fd_replay_info>::failure(
            wrapper_error(FD_ERR_STATE, "replay is empty or moved-from"));
    }
    fd_replay_info output{};
    initialize_output(output);
    fd_diagnostic diag = diagnostic();
    const fd_result result = fd_replay_get_info(state_->get(), &output, &diag);
    if (result != FD_OK) {
        return Result<fd_replay_info>::failure(error_from(result, diag));
    }
    return Result<fd_replay_info>::success(output);
} catch (const std::bad_alloc&) {
    return allocation_failure<fd_replay_info>(FD_ERR_OUT_OF_MEMORY);
} catch (const std::length_error&) {
    return allocation_failure<fd_replay_info>(FD_ERR_CAPACITY);
}

Result<fd_replay_record_view> Replay::record(
    const std::uint64_t index) const try {
    if (!valid()) {
        return Result<fd_replay_record_view>::failure(
            wrapper_error(FD_ERR_STATE, "replay is empty or moved-from"));
    }
    fd_replay_record_view output{};
    initialize_output(output);
    fd_diagnostic diag = diagnostic();
    const fd_result result =
        fd_replay_get_record(state_->get(), index, &output, &diag);
    if (result != FD_OK) {
        return Result<fd_replay_record_view>::failure(error_from(result, diag));
    }
    return Result<fd_replay_record_view>::success(output);
} catch (const std::bad_alloc&) {
    return allocation_failure<fd_replay_record_view>(FD_ERR_OUT_OF_MEMORY);
} catch (const std::length_error&) {
    return allocation_failure<fd_replay_record_view>(FD_ERR_CAPACITY);
}

Result<fd_replay_audit_view> Replay::audit(
    const std::uint32_t index) const try {
    if (!valid()) {
        return Result<fd_replay_audit_view>::failure(
            wrapper_error(FD_ERR_STATE, "replay is empty or moved-from"));
    }
    fd_replay_audit_view output{};
    initialize_output(output);
    fd_diagnostic diag = diagnostic();
    const fd_result result =
        fd_replay_get_audit(state_->get(), index, &output, &diag);
    if (result != FD_OK) {
        return Result<fd_replay_audit_view>::failure(error_from(result, diag));
    }
    return Result<fd_replay_audit_view>::success(output);
} catch (const std::bad_alloc&) {
    return allocation_failure<fd_replay_audit_view>(FD_ERR_OUT_OF_MEMORY);
} catch (const std::length_error&) {
    return allocation_failure<fd_replay_audit_view>(FD_ERR_CAPACITY);
}

Result<World> Replay::create_world() const try {
    if (!valid()) {
        return Result<World>::failure(
            wrapper_error(FD_ERR_STATE, "replay is empty or moved-from"));
    }
    fd_world* raw = nullptr;
    fd_diagnostic diag = diagnostic();
    const fd_result created =
        fd_replay_create_world(state_->get(), &raw, &diag);
    if (created != FD_OK) {
        return Result<World>::failure(error_from(created, diag));
    }
    WorldHandle handle(raw);
    if (handle == nullptr) {
        return Result<World>::failure(wrapper_error(
            FD_ERR_INTERNAL, "replay world creation returned a null handle"));
    }
    auto baseline = save_handle(handle.get());
    if (!baseline) {
        return Result<World>::failure(baseline.error());
    }
    auto world_state = std::make_shared<detail::WorldState>(
        state_->context(), std::move(handle), std::move(baseline).value());
    return Result<World>::success(World(std::move(world_state)));
} catch (const std::bad_alloc&) {
    return allocation_failure<World>(FD_ERR_OUT_OF_MEMORY);
} catch (const std::length_error&) {
    return allocation_failure<World>(FD_ERR_CAPACITY);
}

Result<fd_replay_status> Replay::advance(World& world,
                                         const std::uint64_t decisions) try {
    if (!valid() || !world.valid()) {
        return Result<fd_replay_status>::failure(wrapper_error(
            FD_ERR_STATE, "replay and world must both be valid"));
    }
    auto replay_info = info();
    if (!replay_info) {
        return Result<fd_replay_status>::failure(replay_info.error());
    }
    if (replay_info.value().cursor > replay_info.value().decision_count) {
        return Result<fd_replay_status>::failure(wrapper_error(
            FD_ERR_STATE, "replay cursor exceeds its decision count"));
    }
    const std::uint64_t remaining = replay_info.value().decision_count -
                                    replay_info.value().cursor;
    const std::uint64_t requested = std::min(decisions, remaining);
    if (requested > static_cast<std::uint64_t>(
                        std::numeric_limits<std::size_t>::max())) {
        return Result<fd_replay_status>::failure(wrapper_error(
            FD_ERR_CAPACITY, "replay batch does not fit C++ history storage"));
    }
    const std::size_t batch_size = static_cast<std::size_t>(requested);
    std::vector<fd_joint_decision> accepted;
    try {
        accepted.reserve(batch_size);
    } catch (const std::bad_alloc&) {
        return Result<fd_replay_status>::failure(wrapper_error(
            FD_ERR_OUT_OF_MEMORY,
            "could not reserve replay batch before advancing"));
    } catch (const std::length_error&) {
        return Result<fd_replay_status>::failure(wrapper_error(
            FD_ERR_CAPACITY, "replay batch exceeds vector bounds"));
    }
    for (std::size_t offset = 0U; offset < batch_size; ++offset) {
        const std::uint64_t record_index =
            replay_info.value().cursor + static_cast<std::uint64_t>(offset);
        auto record_result = record(record_index);
        if (!record_result) {
            return Result<fd_replay_status>::failure(record_result.error());
        }
        accepted.push_back(record_result.value().decision);
    }
    if (!world.state_->prepare_decisions(batch_size)) {
        return Result<fd_replay_status>::failure(wrapper_error(
            FD_ERR_OUT_OF_MEMORY,
            "could not reserve world replay history before advancing"));
    }

    fd_replay_status output{};
    initialize_output(output);
    fd_diagnostic diag = diagnostic();
    const fd_result result = fd_replay_advance(
        state_->get(), world.state_->get(), decisions, &output, &diag);
    if (result != FD_OK) {
        return Result<fd_replay_status>::failure(error_from(result, diag));
    }
    if (output.decisions_advanced != requested) {
        return Result<fd_replay_status>::failure(wrapper_error(
            FD_ERR_INTERNAL,
            "C replay advanced a different number of records than reported"));
    }
    for (const fd_joint_decision& decision : accepted) {
        world.state_->append_decision(decision);
    }
    return Result<fd_replay_status>::success(output);
} catch (const std::bad_alloc&) {
    return allocation_failure<fd_replay_status>(FD_ERR_OUT_OF_MEMORY);
} catch (const std::length_error&) {
    return allocation_failure<fd_replay_status>(FD_ERR_CAPACITY);
}

}  // namespace frontier_directorate
