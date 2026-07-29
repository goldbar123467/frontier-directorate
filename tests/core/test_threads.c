#include "frontier_directorate/fd.h"

#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <threads.h>

#define THREAD_WORLD_COUNT UINT32_C(4)
#define THREAD_STEP_COUNT UINT32_C(8)

typedef struct worker_input {
    fd_world *world;
    fd_result result;
    uint8_t final_hash[32];
} worker_input;

static void output_header(void *value, size_t size)
{
    const uint32_t header[2] = {(uint32_t)size, FD_ABI_VERSION};
    memset(value, 0, size);
    memcpy(value, header, sizeof(header));
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

static fd_result run_steps(fd_world *world, uint8_t final_hash[32])
{
    uint32_t step_index;
    for (step_index = 0U; step_index < THREAD_STEP_COUNT; ++step_index) {
        fd_world_info info;
        fd_step_result step;
        fd_joint_decision decision;
        fd_result result;
        output_header(&info, sizeof(info));
        output_header(&step, sizeof(step));
        result = fd_world_get_info(world, &info, NULL);
        if (result != FD_OK) {
            return result;
        }
        decision = pass_decision(&info);
        result = fd_world_step(world, &decision, &step, NULL);
        if (result != FD_OK) {
            return result;
        }
    }
    return fd_world_state_hash(world, final_hash, NULL);
}

static int worker_main(void *opaque)
{
    worker_input *worker = (worker_input *)opaque;
    worker->result = run_steps(worker->world, worker->final_hash);
    return worker->result == FD_OK ? 0 : 1;
}

int main(void)
{
    fd_context_config context_config;
    fd_world_config world_config;
    fd_content_manifest content;
    fd_context *context = NULL;
    worker_input workers[THREAD_WORLD_COUNT];
    thrd_t threads[THREAD_WORLD_COUNT];
    uint8_t oracle_hashes[THREAD_WORLD_COUNT][32];
    uint32_t index;
    uint32_t created = UINT32_C(0);
    int failed = 0;
    (void)fd_context_config_init(&context_config);
    (void)fd_world_config_init(&world_config);
    memset(&content, 0, sizeof(content));
    content.struct_size = (uint32_t)sizeof(content);
    content.abi_version = FD_ABI_VERSION;
    memset(workers, 0, sizeof(workers));
    memset(oracle_hashes, 0, sizeof(oracle_hashes));
    if (fd_context_create(&context_config, &context, NULL) != FD_OK) {
        return 1;
    }
    for (index = 0U; index < THREAD_WORLD_COUNT; ++index) {
        fd_world *oracle = NULL;
        uint64_t seed = UINT64_C(1000) + (uint64_t)index;
        if (fd_world_generate(context, &world_config, seed, &content,
                              &oracle, NULL, NULL) != FD_OK ||
            run_steps(oracle, oracle_hashes[index]) != FD_OK ||
            fd_world_destroy(&oracle, NULL) != FD_OK ||
            fd_world_generate(context, &world_config, seed, &content,
                              &workers[index].world, NULL, NULL) != FD_OK) {
            failed = 1;
            break;
        }
    }
    if (failed == 0) {
        for (index = 0U; index < THREAD_WORLD_COUNT; ++index) {
            if (thrd_create(&threads[index], worker_main, &workers[index]) !=
                thrd_success) {
                failed = 1;
                break;
            }
            ++created;
        }
        for (index = 0U; index < created; ++index) {
            int thread_result = 1;
            if (thrd_join(threads[index], &thread_result) != thrd_success ||
                thread_result != 0 || workers[index].result != FD_OK ||
                memcmp(workers[index].final_hash,
                       oracle_hashes[index], 32U) != 0) {
                failed = 1;
            }
        }
    }
    for (index = 0U; index < THREAD_WORLD_COUNT; ++index) {
        (void)fd_world_destroy(&workers[index].world, NULL);
    }
    if (fd_context_destroy(&context, NULL) != FD_OK) {
        failed = 1;
    }
    (void)printf("thread_worlds=%u steps_per_world=%u failures=%d\n",
                 THREAD_WORLD_COUNT, THREAD_STEP_COUNT, failed);
    return failed == 0 ? 0 : 1;
}
