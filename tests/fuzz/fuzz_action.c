#include "frontier_directorate/fd.h"

#include <stddef.h>
#include <stdint.h>
#include <string.h>

static void header(void *value, size_t size)
{
    const uint32_t words[2] = {(uint32_t)size, FD_ABI_VERSION};
    memset(value, 0, size);
    memcpy(value, words, sizeof(words));
}

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size)
{
    fd_context_config context_config;
    fd_world_config world_config;
    fd_content_manifest content;
    fd_context *context = NULL;
    fd_world *world = NULL;
    fd_world_info info;
    fd_joint_decision decision;
    fd_step_result step;
    fd_action_prefix prefix;
    fd_action_mask mask;
    uint8_t mask_bits[2] = {UINT8_C(0), UINT8_C(0)};
    uint8_t before_hash[32];
    uint8_t after_hash[32];
    uint32_t seat;
    (void)fd_context_config_init(&context_config);
    (void)fd_world_config_init(&world_config);
    world_config.attempt_budget = UINT32_C(1);
    memset(&content, 0, sizeof(content));
    content.struct_size = (uint32_t)sizeof(content);
    content.abi_version = FD_ABI_VERSION;
    header(&info, sizeof(info));
    header(&step, sizeof(step));
    header(&prefix, sizeof(prefix));
    header(&mask, sizeof(mask));
    memset(&decision, 0, sizeof(decision));
    decision.struct_size = (uint32_t)sizeof(decision);
    decision.abi_version = FD_ABI_VERSION;
    decision.seat_count = FD_U01_SEAT_COUNT;
    if (fd_context_create(&context_config, &context, NULL) == FD_OK &&
        fd_world_generate(context, &world_config, UINT64_C(3), &content,
                          &world, NULL, NULL) == FD_OK &&
        fd_world_get_info(world, &info, NULL) == FD_OK) {
        decision.expected_tick = info.tick;
        memcpy(decision.expected_pre_state_sha256, info.state_sha256, 32U);
        for (seat = 0U; seat < FD_U01_SEAT_COUNT; ++seat) {
            decision.seats[seat].actor_id = info.foreign_faction_ids[seat];
            decision.seats[seat].action.struct_size = (uint32_t)sizeof(fd_action);
            decision.seats[seat].action.abi_version = FD_ABI_VERSION;
            decision.seats[seat].action.category = FD_ACTION_PASS;
        }
        if (size > 0U) {
            decision.seats[0].action.category = (uint32_t)data[0];
        }
        if (size > 1U) {
            decision.seat_count = (uint32_t)data[1];
        }
        if (size > 2U) {
            decision.seats[1].action.parameter_count = (uint32_t)data[2];
        }
        if (size > 3U) {
            decision.seats[1].action.parameters[0] = (uint32_t)data[3];
        }
        if (size > 4U) {
            decision.struct_size = (uint32_t)data[4];
        }
        if (size > 5U) {
            prefix.depth = (uint32_t)data[5];
        }
        if (size > 6U) {
            prefix.category = (uint32_t)data[6];
        }
        if (size > 7U) {
            prefix.parameters[0] = (uint32_t)data[7];
        }
        mask.bits = mask_bits;
        mask.byte_capacity = size > 8U ? (uint32_t)(data[8] % UINT8_C(3)) :
                                        UINT32_C(1);
        (void)fd_action_mask_query(world, info.foreign_faction_ids[0],
                                   &prefix, &mask, NULL);
        (void)fd_action_validate(world, info.foreign_faction_ids[0],
                                 &decision.seats[0].action, NULL);
        (void)fd_world_state_hash(world, before_hash, NULL);
        {
            fd_result result = fd_world_step(world, &decision, &step, NULL);
            (void)fd_world_state_hash(world, after_hash, NULL);
            if (result != FD_OK && memcmp(before_hash, after_hash, 32U) != 0) {
                __builtin_trap();
            }
        }
        {
            fd_joint_decision attempts[2];
            uint64_t replay_size = UINT64_C(0);
            attempts[0] = decision;
            attempts[1] = decision;
            attempts[1].expected_tick += FD_OPERATIONAL_TICKS;
            (void)fd_replay_build_size(world, attempts, UINT64_C(2),
                                       &replay_size, NULL);
        }
    }
    (void)fd_world_destroy(&world, NULL);
    (void)fd_context_destroy(&context, NULL);
    return 0;
}
