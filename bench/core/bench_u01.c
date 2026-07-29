#define _GNU_SOURCE

#include "frontier_directorate/fd.h"
#include "fd_internal.h"

#include <inttypes.h>
#include <math.h>
#include <pthread.h>
#include <sched.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <threads.h>
#include <time.h>

#define BENCH_REPETITIONS UINT32_C(30)
#define BATCH_WORLDS UINT32_C(64)
#define BATCH_THREADS UINT32_C(8)
#define BATCH_POOL UINT32_C(1024)
#define GENERATION_SEEDS UINT32_C(1000)
#define MAX_SAMPLES UINT32_C(1000)
#define SCALAR_STEPS_PER_REP UINT32_C(32)
#define IO_AGGREGATE_BYTES UINT64_C(67108864)

typedef struct sample_summary {
    double mean;
    double median;
    double stddev;
    double p95;
    double p99;
    double confidence95_half_width;
} sample_summary;

typedef struct batch_shared {
    fd_world **worlds;
    mtx_t mutex;
    cnd_t start_condition;
    cnd_t done_condition;
    uint32_t epoch;
    uint32_t completed;
    uint32_t batch_base;
    size_t cpu_ids[BATCH_THREADS];
    long package_ids[BATCH_THREADS];
    long core_ids[BATCH_THREADS];
    int stop;
} batch_shared;

typedef struct memory_measurement {
    uint64_t context_live_bytes;
    uint64_t world_live_bytes;
    uint64_t peak_live_bytes;
    uint64_t peak_above_context_bytes;
} memory_measurement;

typedef struct batch_worker {
    batch_shared *shared;
    uint32_t thread_index;
    fd_result result;
    int pin_result;
} batch_worker;

static void output_header(void *value, size_t size)
{
    const uint32_t header[2] = {(uint32_t)size, FD_ABI_VERSION};
    memset(value, 0, size);
    memcpy(value, header, sizeof(header));
}

static uint64_t now_ns(void)
{
    struct timespec value;
    if (clock_gettime(CLOCK_MONOTONIC, &value) != 0) {
        return UINT64_C(0);
    }
    return (uint64_t)value.tv_sec * UINT64_C(1000000000) +
           (uint64_t)value.tv_nsec;
}

static int read_long_file(const char *path, long *out)
{
    FILE *file = fopen(path, "r");
    int scanned;
    int close_result;
    if (file == NULL) {
        return 1;
    }
    scanned = fscanf(file, "%ld", out);
    close_result = fclose(file);
    return scanned == 1 && close_result == 0 ? 0 : 1;
}

static int read_cpu_topology(size_t cpu, long *package_id, long *core_id)
{
    char package_path[128];
    char core_path[128];
    int package_length = snprintf(package_path, sizeof(package_path),
        "/sys/devices/system/cpu/cpu%zu/topology/physical_package_id", cpu);
    int core_length = snprintf(core_path, sizeof(core_path),
        "/sys/devices/system/cpu/cpu%zu/topology/core_id", cpu);
    if (package_length < 0 || (size_t)package_length >= sizeof(package_path) ||
        core_length < 0 || (size_t)core_length >= sizeof(core_path)) {
        return 1;
    }
    return read_long_file(package_path, package_id) != 0 ||
           read_long_file(core_path, core_id) != 0 ? 1 : 0;
}

static int compare_double(const void *left, const void *right)
{
    double a = *(const double *)left;
    double b = *(const double *)right;
    return a < b ? -1 : (a > b ? 1 : 0);
}

static sample_summary summarize(const double *samples, uint32_t count)
{
    double sorted[MAX_SAMPLES];
    sample_summary result;
    double sum = 0.0;
    double variance = 0.0;
    uint32_t index;
    memcpy(sorted, samples, (size_t)count * sizeof(sorted[0]));
    qsort(sorted, count, sizeof(sorted[0]), compare_double);
    for (index = 0U; index < count; ++index) {
        sum += samples[index];
    }
    result.mean = sum / (double)count;
    result.median = count % UINT32_C(2) == UINT32_C(0) ?
        (sorted[count / UINT32_C(2) - UINT32_C(1)] +
         sorted[count / UINT32_C(2)]) / 2.0 :
        sorted[count / UINT32_C(2)];
    for (index = 0U; index < count; ++index) {
        double delta = samples[index] - result.mean;
        variance += delta * delta;
    }
    result.stddev = sqrt(variance / (double)(count - UINT32_C(1)));
    result.p95 = sorted[((uint64_t)count * UINT64_C(95) + UINT64_C(99)) /
                        UINT64_C(100) - UINT64_C(1)];
    result.p99 = sorted[((uint64_t)count * UINT64_C(99) + UINT64_C(99)) /
                        UINT64_C(100) - UINT64_C(1)];
    result.confidence95_half_width =
        1.96 * result.stddev / sqrt((double)count);
    return result;
}

static void print_samples(const char *metric,
                          const double *samples,
                          uint32_t count,
                          const char *unit)
{
    sample_summary summary = summarize(samples, count);
    uint32_t index;
    for (index = 0U; index < count; ++index) {
        (void)printf("sample.%s.%u=%.3f\n", metric, index, samples[index]);
    }
    (void)printf("summary.%s.unit=%s\n", metric, unit);
    (void)printf("summary.%s.mean=%.3f\n", metric, summary.mean);
    (void)printf("summary.%s.median=%.3f\n", metric, summary.median);
    (void)printf("summary.%s.stddev=%.3f\n", metric, summary.stddev);
    (void)printf("summary.%s.p95=%.3f\n", metric, summary.p95);
    (void)printf("summary.%s.p99=%.3f\n", metric, summary.p99);
    (void)printf("summary.%s.confidence95_half_width=%.3f\n",
                 metric, summary.confidence95_half_width);
}

static fd_content_manifest empty_content(void)
{
    fd_content_manifest content;
    memset(&content, 0, sizeof(content));
    content.struct_size = (uint32_t)sizeof(content);
    content.abi_version = FD_ABI_VERSION;
    return content;
}

static fd_joint_decision pass_decision(const fd_world_info *info)
{
    fd_joint_decision decision;
    uint32_t seat;
    memset(&decision, 0, sizeof(decision));
    decision.struct_size = (uint32_t)sizeof(decision);
    decision.abi_version = FD_ABI_VERSION;
    decision.seat_count = FD_U01_SEAT_COUNT;
    decision.expected_tick = info->tick;
    memcpy(decision.expected_pre_state_sha256, info->state_sha256, 32U);
    for (seat = 0U; seat < FD_U01_SEAT_COUNT; ++seat) {
        decision.seats[seat].actor_id = info->foreign_faction_ids[seat];
        decision.seats[seat].action.struct_size = (uint32_t)sizeof(fd_action);
        decision.seats[seat].action.abi_version = FD_ABI_VERSION;
        decision.seats[seat].action.category = FD_ACTION_PASS;
    }
    return decision;
}

static fd_result step_world(fd_world *world)
{
    fd_world_info info;
    fd_step_result step;
    fd_joint_decision decision;
    output_header(&info, sizeof(info));
    output_header(&step, sizeof(step));
    if (fd_world_get_info(world, &info, NULL) != FD_OK) {
        return FD_ERR_INTERNAL;
    }
    decision = pass_decision(&info);
    return fd_world_step(world, &decision, &step, NULL);
}

static int batch_worker_main(void *opaque)
{
    batch_worker *worker = (batch_worker *)opaque;
    uint32_t observed_epoch = UINT32_C(0);
    cpu_set_t affinity;
    worker->result = FD_OK;
    CPU_ZERO(&affinity);
    CPU_SET_S(worker->shared->cpu_ids[worker->thread_index],
              sizeof(affinity), &affinity);
    worker->pin_result = pthread_setaffinity_np(pthread_self(),
                                                sizeof(affinity), &affinity);
    for (;;) {
        uint32_t first;
        uint32_t index;
        (void)mtx_lock(&worker->shared->mutex);
        while (worker->shared->epoch == observed_epoch &&
               worker->shared->stop == 0) {
            (void)cnd_wait(&worker->shared->start_condition,
                           &worker->shared->mutex);
        }
        if (worker->shared->stop != 0) {
            (void)mtx_unlock(&worker->shared->mutex);
            break;
        }
        observed_epoch = worker->shared->epoch;
        first = worker->shared->batch_base +
                worker->thread_index * UINT32_C(8);
        (void)mtx_unlock(&worker->shared->mutex);
        for (index = 0U; index < UINT32_C(8); ++index) {
            worker->result = step_world(worker->shared->worlds[first + index]);
            if (worker->result != FD_OK) {
                break;
            }
        }
        (void)mtx_lock(&worker->shared->mutex);
        ++worker->shared->completed;
        if (worker->shared->completed == BATCH_THREADS) {
            (void)cnd_signal(&worker->shared->done_condition);
        }
        (void)mtx_unlock(&worker->shared->mutex);
    }
    return worker->result == FD_OK ? 0 : 1;
}

static int generate_world(fd_context *context,
                          const fd_world_config *config,
                          uint64_t seed,
                          fd_world **out)
{
    fd_content_manifest content = empty_content();
    return fd_world_generate(context, config, seed, &content, out, NULL, NULL) ==
           FD_OK ? 0 : 1;
}

static int measure_memory(const fd_context_config *context_config,
                          const fd_world_config *world_config,
                          memory_measurement *out)
{
    fd_context *context = NULL;
    fd_world *world = NULL;
    fd_allocation_stats before;
    fd_allocation_stats after;
    output_header(&before, sizeof(before));
    output_header(&after, sizeof(after));
    if (fd_context_create(context_config, &context, NULL) != FD_OK ||
        fd_context_get_allocation_stats(context, &before, NULL) != FD_OK ||
        generate_world(context, world_config, UINT64_C(0), &world) != 0 ||
        fd_context_get_allocation_stats(context, &after, NULL) != FD_OK) {
        (void)fd_world_destroy(&world, NULL);
        (void)fd_context_destroy(&context, NULL);
        return 1;
    }
    out->context_live_bytes = before.live_bytes;
    out->world_live_bytes = after.live_bytes - before.live_bytes;
    out->peak_live_bytes = after.peak_live_bytes;
    out->peak_above_context_bytes = after.peak_live_bytes - before.live_bytes;
    if (fd_world_destroy(&world, NULL) != FD_OK ||
        fd_context_destroy(&context, NULL) != FD_OK) {
        return 1;
    }
    return 0;
}

static int dispatch_batch(batch_shared *shared, uint32_t batch_base)
{
    (void)mtx_lock(&shared->mutex);
    shared->batch_base = batch_base;
    shared->completed = UINT32_C(0);
    ++shared->epoch;
    (void)cnd_broadcast(&shared->start_condition);
    while (shared->completed != BATCH_THREADS) {
        (void)cnd_wait(&shared->done_condition, &shared->mutex);
    }
    (void)mtx_unlock(&shared->mutex);
    return 0;
}

static int bench_generation(fd_context *context,
                            const fd_world_config *config,
                            const char *name,
                            uint64_t seed_base,
                            uint8_t correctness[32])
{
    double samples[GENERATION_SEEDS];
    uint8_t hashes[GENERATION_SEEDS][32];
    uint32_t rep;
    for (rep = 0U; rep < GENERATION_SEEDS; ++rep) {
        fd_world *world = NULL;
        uint64_t start = now_ns();
        if (generate_world(context, config, seed_base + (uint64_t)rep, &world) != 0) {
            return 1;
        }
        samples[rep] = (double)(now_ns() - start) / 1000000.0;
        if (fd_world_state_hash(world, hashes[rep], NULL) != FD_OK) {
            return 1;
        }
        (void)fd_world_destroy(&world, NULL);
    }
    if (fd_sha256(hashes, (uint64_t)sizeof(hashes), correctness) != FD_OK) {
        return 1;
    }
    print_samples(name, samples, GENERATION_SEEDS, "milliseconds");
    return 0;
}

static void print_hash(const char *key, const uint8_t hash[32])
{
    uint32_t index;
    (void)printf("%s=", key);
    for (index = 0U; index < UINT32_C(32); ++index) {
        (void)printf("%02x", hash[index]);
    }
    (void)printf("\n");
}

int main(void)
{
    fd_context_config context_config;
    fd_world_config default_config;
    fd_world_config max_config;
    fd_context *context = NULL;
    fd_world *default_world = NULL;
    fd_world *max_world = NULL;
    memory_measurement default_memory;
    memory_measurement max_memory;
    uint64_t default_save_size = UINT64_C(0);
    uint64_t max_save_size = UINT64_C(0);
    uint8_t default_generation_hash[32];
    uint8_t max_generation_hash[32];
    double scalar_samples[BENCH_REPETITIONS];
    double batch_samples[BENCH_REPETITIONS];
    double save_samples[BENCH_REPETITIONS];
    double load_samples[BENCH_REPETITIONS];
    double hash_samples[BENCH_REPETITIONS];
    uint32_t rep;
    uint8_t *save_bytes = NULL;
    uint64_t io_loops;
    uint64_t save_written = UINT64_C(0);
    fd_world *batch_worlds[BATCH_POOL];
    memset(batch_worlds, 0, sizeof(batch_worlds));
    (void)fd_context_config_init(&context_config);
    (void)fd_world_config_init(&default_config);
    max_config = default_config;
    max_config.width = UINT32_C(256);
    max_config.height = UINT32_C(128);
    if (measure_memory(&context_config, &default_config, &default_memory) != 0 ||
        measure_memory(&context_config, &max_config, &max_memory) != 0) {
        return 1;
    }
    if (fd_context_create(&context_config, &context, NULL) != FD_OK) {
        return 1;
    }
    if (generate_world(context, &default_config, UINT64_C(0), &default_world) != 0) {
        return 1;
    }
    if (fd_world_save_size(default_world, &default_save_size, NULL) != FD_OK) {
        return 1;
    }
    if (generate_world(context, &max_config, UINT64_C(0), &max_world) != 0) {
        return 1;
    }
    if (fd_world_save_size(max_world, &max_save_size, NULL) != FD_OK) {
        return 1;
    }
    (void)printf("config.repetitions=%u\n", BENCH_REPETITIONS);
    (void)printf("config.generation_seed_set=0..999\n");
    (void)printf("config.step_seed_pool=1000..2023\n");
    (void)printf("config.warmup=generation_then_one_scalar_step_one_batch_dispatch_one_save_one_load_one_hash\n");
    (void)printf("config.confidence_interval=normal_approximation_1.96_standard_errors\n");
    (void)printf("config.memory_method=isolated_context_allocator_counters_exclude_context_code_and_shared_libraries_include_world_and_generation_temporaries\n");
    (void)printf("metric.allocator_baseline_live_bytes.default_run=%" PRIu64
                 "\n", default_memory.context_live_bytes);
    (void)printf("metric.allocator_baseline_live_bytes.max_run=%" PRIu64
                 "\n", max_memory.context_live_bytes);
    (void)printf("metric.world_live_bytes.default_isolated=%" PRIu64 "\n",
                 default_memory.world_live_bytes);
    (void)printf("metric.world_live_bytes.max_isolated=%" PRIu64 "\n",
                 max_memory.world_live_bytes);
    (void)printf("metric.peak_above_context_bytes.default_isolated=%" PRIu64
                 "\n", default_memory.peak_above_context_bytes);
    (void)printf("metric.peak_above_context_bytes.max_isolated=%" PRIu64 "\n",
                 max_memory.peak_above_context_bytes);
    (void)printf("metric.live_bytes.default=%" PRIu64 "\n",
                 default_memory.world_live_bytes);
    (void)printf("metric.live_bytes.max=%" PRIu64 "\n",
                 max_memory.world_live_bytes);
    (void)printf("metric.save_bytes.default=%" PRIu64 "\n", default_save_size);
    (void)printf("metric.save_bytes.max=%" PRIu64 "\n", max_save_size);
    if (bench_generation(context, &default_config, "generation_default",
                         UINT64_C(0), default_generation_hash) != 0 ||
        bench_generation(context, &max_config, "generation_max",
                         UINT64_C(0), max_generation_hash) != 0) {
        return 1;
    }
    print_hash("correctness.generation_default_sha256", default_generation_hash);
    print_hash("correctness.generation_max_sha256", max_generation_hash);
    (void)printf("correctness.reference_optimized_comparison=not_applicable_single_reference_implementation\n");

    {
        cpu_set_t original_affinity;
        cpu_set_t scalar_affinity;
        size_t scalar_cpu;
        int found_cpu = 0;
        if (sched_getaffinity(0, sizeof(original_affinity),
                              &original_affinity) != 0) {
            return 1;
        }
        for (scalar_cpu = 0U; scalar_cpu < (size_t)CPU_SETSIZE; ++scalar_cpu) {
            if (CPU_ISSET_S(scalar_cpu, sizeof(original_affinity),
                            &original_affinity)) {
                found_cpu = 1;
                break;
            }
        }
        if (found_cpu == 0) {
            return 1;
        }
        CPU_ZERO(&scalar_affinity);
        CPU_SET_S(scalar_cpu, sizeof(scalar_affinity), &scalar_affinity);
        if (pthread_setaffinity_np(pthread_self(), sizeof(scalar_affinity),
                                   &scalar_affinity) != 0) {
            return 1;
        }
        {
            fd_world *warmup_world = NULL;
            if (generate_world(context, &default_config, UINT64_C(99999),
                               &warmup_world) != 0 ||
                step_world(warmup_world) != FD_OK ||
                fd_world_destroy(&warmup_world, NULL) != FD_OK) {
                return 1;
            }
        }
        (void)printf("config.scalar_cpu=%zu\n", scalar_cpu);
        (void)printf("config.scalar_warmup_seed=99999\n");
        (void)printf("config.scalar_warmup_intervals=1\n");
        for (rep = 0U; rep < BENCH_REPETITIONS; ++rep) {
            uint32_t step;
            uint64_t start = now_ns();
            for (step = 0U; step < SCALAR_STEPS_PER_REP; ++step) {
                if (step_world(default_world) != FD_OK) {
                    return 1;
                }
            }
            scalar_samples[rep] =
                (double)SCALAR_STEPS_PER_REP * 1000000000.0 /
                (double)(now_ns() - start);
        }
        if (pthread_setaffinity_np(pthread_self(), sizeof(original_affinity),
                                   &original_affinity) != 0) {
            return 1;
        }
    }
    print_samples("pass_scalar", scalar_samples, BENCH_REPETITIONS,
                  "intervals_per_second");
    {
        fd_allocation_stats before;
        fd_allocation_stats after;
        output_header(&before, sizeof(before));
        output_header(&after, sizeof(after));
        (void)fd_context_get_allocation_stats(context, &before, NULL);
        if (step_world(default_world) != FD_OK) {
            return 1;
        }
        (void)fd_context_get_allocation_stats(context, &after, NULL);
        (void)printf("metric.step_allocation_calls=%" PRIu64 "\n",
                     after.allocation_calls - before.allocation_calls);
    }
    for (rep = 0U; rep < BATCH_POOL; ++rep) {
        if (generate_world(context, &default_config, UINT64_C(1000) + rep,
                           &batch_worlds[rep]) != 0) {
            return 1;
        }
    }
    {
        thrd_t threads[BATCH_THREADS];
        batch_worker workers[BATCH_THREADS];
        batch_shared shared;
        cpu_set_t available;
        uint32_t thread_index;
        size_t cpu_index;
        uint32_t cpu_count = UINT32_C(0);
        uint64_t startup_begin = now_ns();
        memset(&shared, 0, sizeof(shared));
        shared.worlds = batch_worlds;
        if (mtx_init(&shared.mutex, mtx_plain) != thrd_success ||
            cnd_init(&shared.start_condition) != thrd_success ||
            cnd_init(&shared.done_condition) != thrd_success ||
            sched_getaffinity(0, sizeof(available), &available) != 0) {
            return 1;
        }
        for (cpu_index = 0U;
             cpu_index < (size_t)CPU_SETSIZE && cpu_count < BATCH_THREADS;
             ++cpu_index) {
            long package_id;
            long core_id;
            int distinct = 1;
            uint32_t selected;
            if (!CPU_ISSET_S(cpu_index, sizeof(available), &available) ||
                read_cpu_topology(cpu_index, &package_id, &core_id) != 0) {
                continue;
            }
            for (selected = 0U; selected < cpu_count; ++selected) {
                if (shared.package_ids[selected] == package_id &&
                    shared.core_ids[selected] == core_id) {
                    distinct = 0;
                    break;
                }
            }
            if (distinct != 0) {
                shared.cpu_ids[cpu_count] = cpu_index;
                shared.package_ids[cpu_count] = package_id;
                shared.core_ids[cpu_count] = core_id;
                ++cpu_count;
            }
        }
        if (cpu_count < BATCH_THREADS) {
            return 1;
        }
        for (thread_index = 0U; thread_index < BATCH_THREADS; ++thread_index) {
            (void)printf("config.batch_cpu.%u=%zu package=%ld core=%ld\n",
                         thread_index, shared.cpu_ids[thread_index],
                         shared.package_ids[thread_index],
                         shared.core_ids[thread_index]);
        }
        for (thread_index = 0U; thread_index < BATCH_THREADS; ++thread_index) {
            workers[thread_index].shared = &shared;
            workers[thread_index].thread_index = thread_index;
            workers[thread_index].result = FD_OK;
            workers[thread_index].pin_result = 0;
            if (thrd_create(&threads[thread_index], batch_worker_main,
                            &workers[thread_index]) != thrd_success) {
                return 1;
            }
        }
        (void)printf("metric.batch_thread_startup_ns=%" PRIu64 "\n",
                     now_ns() - startup_begin);
        if (dispatch_batch(&shared, BATCH_POOL - BATCH_WORLDS) != 0) {
            return 1;
        }
        for (thread_index = 0U; thread_index < BATCH_THREADS;
             ++thread_index) {
            if (workers[thread_index].result != FD_OK) {
                return 1;
            }
        }
        for (rep = BATCH_POOL - BATCH_WORLDS; rep < BATCH_POOL; ++rep) {
            if (fd_world_destroy(&batch_worlds[rep], NULL) != FD_OK ||
                generate_world(context, &default_config,
                               UINT64_C(1000) + (uint64_t)rep,
                               &batch_worlds[rep]) != 0) {
                return 1;
            }
        }
        (void)printf("config.batch_warmup_base=%u\n",
                     BATCH_POOL - BATCH_WORLDS);
        (void)printf("config.batch_warmup_worlds=regenerated_before_measurement\n");
        for (rep = 0U; rep < BENCH_REPETITIONS; ++rep) {
            uint64_t start;
            start = now_ns();
            if (dispatch_batch(&shared,
                               (rep * BATCH_WORLDS) % BATCH_POOL) != 0) {
                return 1;
            }
            batch_samples[rep] = (double)BATCH_WORLDS * 1000000000.0 /
                                 (double)(now_ns() - start);
            for (thread_index = 0U; thread_index < BATCH_THREADS;
                 ++thread_index) {
                if (workers[thread_index].result != FD_OK) {
                    return 1;
                }
            }
        }
        (void)mtx_lock(&shared.mutex);
        shared.stop = 1;
        (void)cnd_broadcast(&shared.start_condition);
        (void)mtx_unlock(&shared.mutex);
        for (thread_index = 0U; thread_index < BATCH_THREADS; ++thread_index) {
            int thread_result = 1;
            if (thrd_join(threads[thread_index], &thread_result) != thrd_success ||
                thread_result != 0 || workers[thread_index].result != FD_OK ||
                workers[thread_index].pin_result != 0) {
                return 1;
            }
        }
        cnd_destroy(&shared.done_condition);
        cnd_destroy(&shared.start_condition);
        mtx_destroy(&shared.mutex);
    }
    print_samples("pass_batch_64x8_persistent_pinned", batch_samples,
                  BENCH_REPETITIONS, "intervals_per_second");

    save_bytes = (uint8_t *)malloc((size_t)default_save_size);
    if (save_bytes == NULL ||
        fd_world_save(default_world, save_bytes, default_save_size,
                      &save_written, NULL) != FD_OK) {
        return 1;
    }
    {
        fd_world *warmup_loaded = NULL;
        if (save_written != default_save_size ||
            fd_world_load(context, save_bytes, default_save_size,
                          &warmup_loaded, NULL) != FD_OK ||
            fd_world_destroy(&warmup_loaded, NULL) != FD_OK) {
            return 1;
        }
    }
    (void)printf("config.io_warmup_operations=one_save_and_one_load\n");
    io_loops = (IO_AGGREGATE_BYTES +
                default_save_size * BENCH_REPETITIONS - UINT64_C(1)) /
               (default_save_size * BENCH_REPETITIONS);
    for (rep = 0U; rep < BENCH_REPETITIONS; ++rep) {
        uint64_t loop;
        uint64_t start = now_ns();
        for (loop = 0U; loop < io_loops; ++loop) {
            if (fd_world_save(default_world, save_bytes, default_save_size,
                              &save_written, NULL) != FD_OK ||
                save_written != default_save_size) {
                return 1;
            }
        }
        save_samples[rep] =
            ((double)(io_loops * default_save_size) / 1048576.0) /
            ((double)(now_ns() - start) / 1000000000.0);
        start = now_ns();
        for (loop = 0U; loop < io_loops; ++loop) {
            fd_world *loaded = NULL;
            if (fd_world_load(context, save_bytes, default_save_size,
                              &loaded, NULL) != FD_OK) {
                return 1;
            }
            (void)fd_world_destroy(&loaded, NULL);
        }
        load_samples[rep] =
            ((double)(io_loops * default_save_size) / 1048576.0) /
            ((double)(now_ns() - start) / 1000000000.0);
    }
    print_samples("save_throughput", save_samples, BENCH_REPETITIONS,
                  "mebibytes_per_second");
    print_samples("load_throughput", load_samples, BENCH_REPETITIONS,
                  "mebibytes_per_second");
    (void)printf("metric.io_aggregate_bytes_total_each_operation=%" PRIu64 "\n",
                 io_loops * default_save_size * BENCH_REPETITIONS);
    if (fd_world_compute_hash(default_world) != FD_OK) {
        return 1;
    }
    (void)printf("config.hash_warmup_operations=1\n");
    for (rep = 0U; rep < BENCH_REPETITIONS; ++rep) {
        uint32_t loop;
        uint64_t start = now_ns();
        for (loop = 0U; loop < UINT32_C(1000); ++loop) {
            if (fd_world_compute_hash(default_world) != FD_OK) {
                return 1;
            }
        }
        hash_samples[rep] = (double)(now_ns() - start) /
                            (double)UINT32_C(1000) / 1000.0;
    }
    print_samples("hash_canonical", hash_samples, BENCH_REPETITIONS,
                  "microseconds");
    {
        fd_world_info info;
        fd_joint_decision decision;
        uint64_t empty_size = UINT64_C(0);
        uint64_t one_size = UINT64_C(0);
        output_header(&info, sizeof(info));
        (void)fd_world_get_info(max_world, &info, NULL);
        decision = pass_decision(&info);
        if (fd_replay_build_size(max_world, NULL, UINT64_C(0),
                                 &empty_size, NULL) != FD_OK ||
            fd_replay_build_size(max_world, &decision, UINT64_C(1),
                                 &one_size, NULL) != FD_OK) {
            return 1;
        }
        (void)printf("metric.replay_bytes_per_record=%" PRIu64 "\n",
                     one_size - empty_size);
    }
    {
        uint8_t final_hash[32];
        if (fd_world_state_hash(default_world, final_hash, NULL) != FD_OK) {
            return 1;
        }
        print_hash("correctness.scalar_final_sha256", final_hash);
    }
    free(save_bytes);
    {
        uint8_t batch_hashes[BATCH_POOL][32];
        uint8_t batch_aggregate[32];
        for (rep = 0U; rep < BATCH_POOL; ++rep) {
            if (fd_world_state_hash(batch_worlds[rep], batch_hashes[rep],
                                    NULL) != FD_OK) {
                return 1;
            }
        }
        if (fd_sha256(batch_hashes, (uint64_t)sizeof(batch_hashes),
                      batch_aggregate) != FD_OK) {
            return 1;
        }
        print_hash("correctness.batch_pool_sha256", batch_aggregate);
    }
    for (rep = 0U; rep < BATCH_POOL; ++rep) {
        (void)fd_world_destroy(&batch_worlds[rep], NULL);
    }
    (void)fd_world_destroy(&max_world, NULL);
    (void)fd_world_destroy(&default_world, NULL);
    (void)fd_context_destroy(&context, NULL);
    return 0;
}
