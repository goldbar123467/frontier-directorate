#include "frontier_directorate/fd.h"

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <new>

namespace {

std::atomic<bool> fail_next_allocation{false};

void* checked_malloc(std::size_t size) {
    if (fail_next_allocation.exchange(false, std::memory_order_relaxed)) {
        throw std::bad_alloc{};
    }
    if (size == 0U) {
        size = 1U;
    }
    if (void* const memory = std::malloc(size); memory != nullptr) {
        return memory;
    }
    throw std::bad_alloc{};
}

template <typename T>
void output_header(T& value) {
    std::memset(&value, 0, sizeof(value));
    value.struct_size = static_cast<std::uint32_t>(sizeof(value));
    value.abi_version = FD_ABI_VERSION;
}

}  // namespace

void* operator new(const std::size_t size) {
    return checked_malloc(size);
}

void* operator new[](const std::size_t size) {
    return checked_malloc(size);
}

void operator delete(void* const memory) noexcept {
    std::free(memory);
}

void operator delete[](void* const memory) noexcept {
    std::free(memory);
}

void operator delete(void* const memory, const std::size_t size) noexcept {
    (void)size;
    std::free(memory);
}

void operator delete[](void* const memory, const std::size_t size) noexcept {
    (void)size;
    std::free(memory);
}

int main() {
    fd_context_config context_config{};
    fd_world_config world_config{};
    fd_content_manifest content{};
    fd_generation_report generation{};
    fd_ascii_options options{};
    fd_diagnostic diagnostic{};
    fd_context* context = nullptr;
    fd_world* world = nullptr;
    fd_snapshot* snapshot = nullptr;
    std::uint64_t measured = 0U;
    std::uint64_t written = UINT64_C(99);
    char* buffer = nullptr;
    int failures = 0;

    (void)fd_context_config_init(&context_config);
    (void)fd_world_config_init(&world_config);
    (void)fd_diagnostic_init(&diagnostic);
    content.struct_size = static_cast<std::uint32_t>(sizeof(content));
    content.abi_version = FD_ABI_VERSION;
    output_header(generation);
    if (fd_context_create(&context_config, &context, &diagnostic) != FD_OK ||
        fd_world_generate(context, &world_config, UINT64_C(42), &content,
                          &world, &generation, &diagnostic) != FD_OK ||
        fd_snapshot_create(world, FD_VISIBILITY_REFERENCE, UINT64_C(0),
                           &snapshot, &diagnostic) != FD_OK ||
        fd_ascii_options_init(&options) != FD_OK ||
        fd_ascii_measure(snapshot, &options, &measured, &diagnostic) != FD_OK ||
        measured == UINT64_C(0)) {
        std::cerr << "FAIL: ASCII exception test setup\n";
        failures = 1;
    }

    if (failures == 0) {
        buffer = static_cast<char*>(std::malloc(static_cast<std::size_t>(measured)));
        if (buffer == nullptr) {
            std::cerr << "FAIL: ASCII exception buffer allocation\n";
            failures = 1;
        }
    }
    if (failures == 0) {
        measured = UINT64_C(77);
        fail_next_allocation.store(true, std::memory_order_relaxed);
        const fd_result measure_result =
            fd_ascii_measure(snapshot, &options, &measured, &diagnostic);
        fail_next_allocation.store(false, std::memory_order_relaxed);
        if (measure_result != FD_ERR_OUT_OF_MEMORY ||
            measured != UINT64_C(0) || diagnostic.code != FD_ERR_OUT_OF_MEMORY) {
            std::cerr << "FAIL: bad_alloc escaped ASCII measure C ABI\n";
            failures = 1;
        }

        buffer[0] = 'X';
        written = UINT64_C(99);
        fail_next_allocation.store(true, std::memory_order_relaxed);
        const fd_result render_result = fd_ascii_render(
            snapshot, &options, buffer, measured, &written, &diagnostic);
        fail_next_allocation.store(false, std::memory_order_relaxed);
        if (render_result != FD_ERR_OUT_OF_MEMORY ||
            written != UINT64_C(0) || diagnostic.code != FD_ERR_OUT_OF_MEMORY ||
            buffer[0] != 'X') {
            std::cerr << "FAIL: bad_alloc escaped or partially wrote ASCII render\n";
            failures = 1;
        }
    }

    std::free(buffer);
    (void)fd_snapshot_destroy(&snapshot, nullptr);
    (void)fd_world_destroy(&world, nullptr);
    (void)fd_context_destroy(&context, nullptr);
    if (failures == 0) {
        std::cout << "PASS: renderer exceptions contained by C ABI\n";
    }
    return failures;
}
