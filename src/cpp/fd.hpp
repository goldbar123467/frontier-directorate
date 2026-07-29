#ifndef FRONTIER_DIRECTORATE_CPP_FD_HPP
#define FRONTIER_DIRECTORATE_CPP_FD_HPP

#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>
#include <vector>

#include "frontier_directorate/fd.h"

#include "result.hpp"

namespace frontier_directorate {

// Thread safety mirrors the C authority: a World must not be queried concurrently
// with mutation of that same world. Immutable Snapshots may be read independently;
// distinct worlds may be used by distinct threads even when they share a Context.
// These wrappers add lifetime ownership, not synchronization or rule logic.
// Result-returning entry points translate std::bad_alloc to
// FD_ERR_OUT_OF_MEMORY and std::length_error to FD_ERR_CAPACITY; allocation
// exceptions do not cross the wrapper boundary.

namespace detail {
class ContextState;
class WorldState;
class SnapshotState;
class ReplayState;
}  // namespace detail

namespace terminal {
struct BorrowedSnapshotRenderer;
}  // namespace terminal

class World;
class Replay;

[[nodiscard]] Result<fd_context_config> default_context_config();
[[nodiscard]] Result<fd_world_config> default_world_config();

class Context final {
public:
    Context() noexcept = default;
    ~Context() = default;

    Context(const Context&) = delete;
    Context& operator=(const Context&) = delete;
    Context(Context&&) noexcept = default;
    Context& operator=(Context&&) noexcept = default;

    [[nodiscard]] static Result<Context> create(
        const fd_context_config& config);

    [[nodiscard]] Result<World> generate(
        const fd_world_config& config,
        std::uint64_t seed,
        const fd_content_manifest* content = nullptr) const;

    [[nodiscard]] Result<World> load(
        std::span<const std::byte> bytes) const;

    [[nodiscard]] Result<Replay> open_replay(
        std::span<const std::byte> bytes) const;

    [[nodiscard]] Result<fd_allocation_stats> allocation_stats() const;

    [[nodiscard]] bool valid() const noexcept;

private:
    explicit Context(std::shared_ptr<detail::ContextState> state) noexcept;
    std::shared_ptr<detail::ContextState> state_{};
};

class Snapshot final {
public:
    Snapshot() noexcept = default;
    ~Snapshot() = default;

    Snapshot(const Snapshot&) = delete;
    Snapshot& operator=(const Snapshot&) = delete;
    Snapshot(Snapshot&& other) noexcept;
    Snapshot& operator=(Snapshot&& other) noexcept;

    // Borrowed observer accessor; ownership remains with this RAII wrapper.
    [[nodiscard]] const fd_snapshot* native_handle() const noexcept;

    [[nodiscard]] bool valid() const noexcept;
    [[nodiscard]] Result<fd_world_info> info() const;
    [[nodiscard]] Result<fd_tile_view> tile(std::uint32_t x,
                                            std::uint32_t y) const;
    [[nodiscard]] Result<fd_entity_view> entity_by_index(
        std::uint32_t index) const;
    [[nodiscard]] Result<fd_region_view> region() const;
    [[nodiscard]] Result<std::array<std::uint8_t, 32U>> state_hash() const;

private:
    friend class World;
    friend struct terminal::BorrowedSnapshotRenderer;
    // Synchronous internal adapter for the C presentation ABI. Ordinary public
    // Snapshot values always own their C handle through SnapshotState.
    [[nodiscard]] static Snapshot borrow(const fd_snapshot* snapshot) noexcept;
    explicit Snapshot(std::shared_ptr<detail::SnapshotState> state) noexcept;
    std::shared_ptr<detail::SnapshotState> state_{};
    const fd_snapshot* borrowed_{};
};

class World final {
public:
    World() noexcept = default;
    ~World() = default;

    World(const World&) = delete;
    World& operator=(const World&) = delete;
    World(World&&) noexcept = default;
    World& operator=(World&&) noexcept = default;

    [[nodiscard]] bool valid() const noexcept;
    [[nodiscard]] Result<fd_world_info> info() const;
    [[nodiscard]] Result<std::array<std::uint8_t, 32U>> state_hash() const;
    [[nodiscard]] Result<std::vector<std::byte>> save() const;
    [[nodiscard]] Result<Snapshot> reference_snapshot() const;

    // U01 has exactly two foreign seats and only PASS is legal.  This method
    // still queries the legal category mask and validates both final actions at
    // the C authority before submitting the explicit joint decision.
    [[nodiscard]] Result<fd_step_result> step_explicit_pass();

    [[nodiscard]] Result<std::vector<std::byte>> build_replay() const;
    // Re-simulates the immutable tick-zero checkpoint. Invalid action attempts
    // become structured replay audit entries; accepted attempts become replay
    // records. The live World is not mutated.
    [[nodiscard]] Result<std::vector<std::byte>> build_replay(
        std::span<const fd_joint_decision> attempts) const;

private:
    friend class Context;
    friend class Replay;
    explicit World(std::shared_ptr<detail::WorldState> state) noexcept;
    std::shared_ptr<detail::WorldState> state_{};
};

class Replay final {
public:
    Replay() noexcept = default;
    ~Replay() = default;

    Replay(const Replay&) = delete;
    Replay& operator=(const Replay&) = delete;
    Replay(Replay&&) noexcept = default;
    Replay& operator=(Replay&&) noexcept = default;

    [[nodiscard]] bool valid() const noexcept;
    [[nodiscard]] Result<fd_replay_info> info() const;
    [[nodiscard]] Result<fd_replay_record_view> record(
        std::uint64_t index) const;
    [[nodiscard]] Result<fd_replay_audit_view> audit(
        std::uint32_t index) const;
    [[nodiscard]] Result<World> create_world() const;
    [[nodiscard]] Result<fd_replay_status> advance(World& world,
                                                   std::uint64_t decisions);

private:
    friend class Context;
    explicit Replay(std::shared_ptr<detail::ReplayState> state) noexcept;
    std::shared_ptr<detail::ReplayState> state_{};
};

}  // namespace frontier_directorate

#endif
