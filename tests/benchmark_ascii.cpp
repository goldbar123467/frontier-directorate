#include "../src/cpp/fd.hpp"
#include "../src/terminal/ascii_renderer.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <iomanip>
#include <iostream>
#include <numeric>
#include <string>
#include <utility>
#include <vector>

namespace fd = frontier_directorate;
namespace term = frontier_directorate::terminal;

namespace {

constexpr std::size_t kWarmupRepetitions = 5U;
constexpr std::size_t kMeasuredRepetitions = 50U;

std::uint64_t nearest_rank_percentile(
    const std::vector<std::uint64_t>& ordered,
    const std::size_t percentile) {
    const std::size_t rank =
        (ordered.size() * percentile + 99U) / 100U;
    return ordered[rank - 1U];
}

int fail(const char* const operation, const fd::Error& error) {
    std::cerr << "benchmark failure: " << operation << " [" << error.code
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
    auto world = context.value().generate(world_config.value(), UINT64_C(42));
    if (!world) {
        return fail("world generate", world.error());
    }
    auto hash_before = world.value().state_hash();
    if (!hash_before) {
        return fail("pre-render hash", hash_before.error());
    }
    auto save_before = world.value().save();
    if (!save_before) {
        return fail("pre-render save", save_before.error());
    }
    auto snapshot = world.value().reference_snapshot();
    if (!snapshot) {
        return fail("reference snapshot", snapshot.error());
    }

    term::RenderOptions options{};
    options.columns = 120U;
    options.rows = 40U;
    options.mode = term::ScreenMode::local;
    options.selected_x = 17U;
    options.selected_y = 11U;
    options.message = "Release benchmark; immutable render purity check.";

    std::string expected;
    for (std::size_t index = 0U; index < kWarmupRepetitions; ++index) {
        auto rendered = term::render_ascii(snapshot.value(), options);
        if (!rendered) {
            return fail("warmup render", rendered.error());
        }
        expected = std::move(rendered).value();
    }

    std::vector<std::uint64_t> raw_nanoseconds;
    raw_nanoseconds.reserve(kMeasuredRepetitions);
    for (std::size_t index = 0U; index < kMeasuredRepetitions; ++index) {
        const auto started = std::chrono::steady_clock::now();
        auto rendered = term::render_ascii(snapshot.value(), options);
        const auto finished = std::chrono::steady_clock::now();
        if (!rendered) {
            return fail("measured render", rendered.error());
        }
        if (rendered.value() != expected) {
            std::cerr << "benchmark failure: render bytes changed at repetition "
                      << index << '\n';
            return 1;
        }
        const auto elapsed = std::chrono::duration_cast<std::chrono::nanoseconds>(
            finished - started);
        raw_nanoseconds.push_back(
            static_cast<std::uint64_t>(elapsed.count()));
    }

    auto hash_after = world.value().state_hash();
    if (!hash_after) {
        return fail("post-render hash", hash_after.error());
    }
    auto save_after = world.value().save();
    if (!save_after) {
        return fail("post-render save", save_after.error());
    }
    if (hash_after.value() != hash_before.value() ||
        save_after.value() != save_before.value()) {
        std::cerr << "benchmark failure: rendering mutated authoritative state\n";
        return 1;
    }

    std::vector<std::uint64_t> ordered = raw_nanoseconds;
    std::sort(ordered.begin(), ordered.end());
    const long double sum = std::accumulate(
        raw_nanoseconds.begin(), raw_nanoseconds.end(), 0.0L);
    const long double mean =
        sum / static_cast<long double>(raw_nanoseconds.size());
    long double squared_deviation_sum = 0.0L;
    for (const std::uint64_t sample : raw_nanoseconds) {
        const long double deviation = static_cast<long double>(sample) - mean;
        squared_deviation_sum += deviation * deviation;
    }
    const long double sample_standard_deviation = std::sqrt(
        squared_deviation_sum /
        static_cast<long double>(raw_nanoseconds.size() - 1U));
    const long double standard_error = sample_standard_deviation /
        std::sqrt(static_cast<long double>(raw_nanoseconds.size()));
    const long double confidence_radius = 1.96L * standard_error;
    const long double median =
        (static_cast<long double>(ordered[(ordered.size() / 2U) - 1U]) +
         static_cast<long double>(ordered[ordered.size() / 2U])) /
        2.0L;
    const std::uint64_t p95_nanoseconds =
        nearest_rank_percentile(ordered, 95U);
    const std::uint64_t p99_nanoseconds =
        nearest_rank_percentile(ordered, 99U);
    for (std::size_t index = 0U; index < raw_nanoseconds.size(); ++index) {
        std::cout << "ascii_render_120x40_raw_ns[" << index
                  << "]=" << raw_nanoseconds[index] << '\n';
    }
    std::cout << std::fixed << std::setprecision(3)
              << "ascii_render_120x40 repetitions="
              << kMeasuredRepetitions
              << " warmup_repetitions=" << kWarmupRepetitions
              << " clock=steady_clock"
              << " mean_ns=" << mean
              << " median_ns=" << median
              << " sample_stddev_ns=" << sample_standard_deviation
              << " p95_ns=" << p95_nanoseconds
              << " p99_ns=" << p99_nanoseconds
              << " mean_ci95_low_ns=" << (mean - confidence_radius)
              << " mean_ci95_high_ns=" << (mean + confidence_radius)
              << " ci_method=normal_1.96_sample_stddev"
              << " frame_bytes=" << expected.size()
              << " hash_pure=1 save_pure=1\n";
    return 0;
}
