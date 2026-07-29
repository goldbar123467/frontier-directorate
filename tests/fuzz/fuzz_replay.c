#include "frontier_directorate/fd.h"
#include "fuzz_format_helpers.h"

#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

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
    decision.expected_tick = info->tick;
    memcpy(decision.expected_pre_state_sha256, info->state_sha256, 32U);
    decision.seat_count = FD_U01_SEAT_COUNT;
    for (seat = 0U; seat < FD_U01_SEAT_COUNT; ++seat) {
        decision.seats[seat].actor_id = info->foreign_faction_ids[seat];
        decision.seats[seat].action.struct_size = (uint32_t)sizeof(fd_action);
        decision.seats[seat].action.abi_version = FD_ABI_VERSION;
        decision.seats[seat].action.category = FD_ACTION_PASS;
    }
    return decision;
}

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size)
{
    fd_context_config config;
    fd_world_config world_config;
    fd_content_manifest content = empty_content();
    fd_context *context = NULL;
    fd_world *world = NULL;
    fd_replay *replay = NULL;
    fd_world_info info;
    fd_joint_decision attempts[2];
    uint8_t *canonical = NULL;
    uint64_t canonical_size = UINT64_C(0);
    uint64_t written = UINT64_C(0);
    uint8_t *payload = NULL;
    uint64_t payload_length = UINT64_C(0);
    if (size > (size_t)FD_FUZZ_FILE_LIMIT) {
        return 0;
    }
    (void)fd_context_config_init(&config);
    (void)fd_world_config_init(&world_config);
    config.max_file_bytes = FD_FUZZ_FILE_LIMIT;
    world_config.attempt_budget = UINT32_C(1);
    memset(&info, 0, sizeof(info));
    info.struct_size = (uint32_t)sizeof(info);
    info.abi_version = FD_ABI_VERSION;
    if (fd_context_create(&config, &context, NULL) != FD_OK) {
        return 0;
    }

    /* Raw framing/capacity path. */
    (void)fd_replay_open(context, data, (uint64_t)size, &replay, NULL);
    (void)fd_replay_destroy(&replay, NULL);

    /* One accepted and one rejected attempt populate transition and audit data. */
    if (fd_world_generate(context, &world_config,
                          size > 0U ? (uint64_t)data[0] : UINT64_C(0),
                          &content, &world, NULL, NULL) == FD_OK &&
        fd_world_get_info(world, &info, NULL) == FD_OK) {
        attempts[0] = pass_decision(&info);
        attempts[1] = attempts[0];
        if (fd_replay_build_size(world, attempts, UINT64_C(2),
                                 &canonical_size, NULL) == FD_OK &&
            canonical_size <= FD_FUZZ_FILE_LIMIT) {
            canonical = (uint8_t *)malloc((size_t)canonical_size);
        }
    }
    if (canonical != NULL &&
        fd_replay_build(world, attempts, UINT64_C(2), canonical,
                        canonical_size, &written, NULL) == FD_OK &&
        written == canonical_size) {
        uint16_t outer_tag = (uint16_t)(UINT16_C(1) +
            (uint16_t)(size > 1U ? data[1] % UINT8_C(5) : UINT8_C(0)));
        if (fd_fuzz_find_section(canonical, canonical_size, outer_tag,
                                 &payload, &payload_length) == 0) {
            if (outer_tag == UINT16_C(2)) {
                uint8_t *save_payload = NULL;
                uint64_t save_length = UINT64_C(0);
                uint16_t save_tag = (uint16_t)(UINT16_C(1) +
                    (uint16_t)(size > 2U ? data[2] % UINT8_C(11) :
                                           UINT8_C(0)));
                if (fd_fuzz_find_section(payload, payload_length, save_tag,
                                         &save_payload, &save_length) == 0) {
                    if (size > 3U) {
                        fd_fuzz_mutate_payload(save_payload, save_length,
                                               data + 3U, size - 3U);
                    }
                    fd_fuzz_repair_section(save_payload, save_length);
                    fd_fuzz_repair_footer(
                        payload, payload_length,
                        "FrontierDirectorate/Save/Footer/v1");
                }
            } else if (size > 2U) {
                fd_fuzz_mutate_payload(payload, payload_length,
                                       data + 2U, size - 2U);
            }
            fd_fuzz_repair_section(payload, payload_length);
            fd_fuzz_repair_footer(
                canonical, canonical_size,
                "FrontierDirectorate/Replay/Footer/v1");
            (void)fd_replay_open(context, canonical, canonical_size,
                                 &replay, NULL);
            (void)fd_replay_destroy(&replay, NULL);
        }
    }
    free(canonical);
    (void)fd_world_destroy(&world, NULL);
    (void)fd_context_destroy(&context, NULL);
    return 0;
}
