#include "frontier_directorate/fd.h"

#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

static void header(void *value, size_t size)
{
    const uint32_t words[2] = {(uint32_t)size, FD_ABI_VERSION};
    memset(value, 0, size);
    memcpy(value, words, sizeof(words));
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

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size)
{
    fd_context_config context_config;
    fd_world_config world_config;
    fd_content_manifest content;
    fd_context *context = NULL;
    fd_world *world = NULL;
    fd_snapshot *snapshot = NULL;
    fd_replay *replay = NULL;
    uint8_t *buffer = NULL;
    uint64_t buffer_size = UINT64_C(0);
    size_t instruction;
    (void)fd_context_config_init(&context_config);
    (void)fd_world_config_init(&world_config);
    world_config.attempt_budget = UINT32_C(1);
    memset(&content, 0, sizeof(content));
    content.struct_size = (uint32_t)sizeof(content);
    content.abi_version = FD_ABI_VERSION;
    (void)fd_context_create(&context_config, &context, NULL);
    for (instruction = 0U; instruction < size && instruction < 64U;
         ++instruction) {
        uint8_t opcode = (uint8_t)(data[instruction] & UINT8_C(0x0f));
        switch (opcode) {
        case 0U:
            if (context != NULL && world == NULL) {
                (void)fd_world_generate(context, &world_config,
                                        (uint64_t)data[instruction], &content,
                                        &world, NULL, NULL);
            }
            break;
        case 1U:
            if (world != NULL) {
                fd_world_info info;
                header(&info, sizeof(info));
                if ((data[instruction] & UINT8_C(0x80)) != UINT8_C(0)) {
                    info.struct_size = (uint32_t)data[instruction];
                }
                (void)fd_world_get_info(world, &info, NULL);
            }
            break;
        case 2U:
            if (world != NULL) {
                fd_tile_view tile;
                header(&tile, sizeof(tile));
                (void)fd_world_get_tile(world, (uint32_t)data[instruction],
                                        (uint32_t)(data[instruction] >> 1U),
                                        &tile, NULL);
            }
            break;
        case 3U:
            if (world != NULL && snapshot == NULL) {
                (void)fd_snapshot_create(
                    world, (data[instruction] & UINT8_C(0x80)) != UINT8_C(0) ?
                               FD_VISIBILITY_ACTOR : FD_VISIBILITY_REFERENCE,
                    UINT64_C(0), &snapshot, NULL);
            } else {
                (void)fd_snapshot_destroy(&snapshot, NULL);
            }
            break;
        case 4U:
            if (world != NULL) {
                fd_world_info info;
                fd_joint_decision decision;
                fd_step_result step;
                header(&info, sizeof(info));
                header(&step, sizeof(step));
                if (fd_world_get_info(world, &info, NULL) == FD_OK) {
                    decision = pass_decision(&info);
                    if ((data[instruction] & UINT8_C(0x80)) != UINT8_C(0)) {
                        decision.seats[0].action.category =
                            (uint32_t)data[instruction];
                    }
                    (void)fd_world_step(world, &decision, &step, NULL);
                }
            }
            break;
        case 5U:
            if (world != NULL) {
                uint64_t written = UINT64_C(0);
                free(buffer);
                buffer = NULL;
                buffer_size = UINT64_C(0);
                if (fd_world_save_size(world, &buffer_size, NULL) == FD_OK) {
                    buffer = (uint8_t *)malloc((size_t)buffer_size);
                    if (buffer != NULL) {
                        (void)fd_world_save(world, buffer, buffer_size,
                                            &written, NULL);
                    }
                }
            }
            break;
        case 6U:
            if (context != NULL && buffer != NULL) {
                fd_world *loaded = NULL;
                uint64_t input_size =
                    (data[instruction] & UINT8_C(0x80)) != UINT8_C(0) &&
                            buffer_size > UINT64_C(0) ?
                        buffer_size - UINT64_C(1) : buffer_size;
                (void)fd_world_load(context, buffer, input_size, &loaded, NULL);
                (void)fd_world_destroy(&loaded, NULL);
            }
            break;
        case 7U:
            if (world != NULL) {
                fd_validation_report report;
                header(&report, sizeof(report));
                (void)fd_world_validate(world, &report, NULL);
            }
            break;
        case 8U:
            if (world != NULL) {
                fd_world_info info;
                fd_joint_decision decision;
                uint64_t replay_size = UINT64_C(0);
                uint64_t written = UINT64_C(0);
                (void)fd_replay_destroy(&replay, NULL);
                header(&info, sizeof(info));
                if (fd_world_get_info(world, &info, NULL) == FD_OK) {
                    decision = pass_decision(&info);
                    if (fd_replay_build_size(world, &decision, UINT64_C(1),
                                             &replay_size, NULL) == FD_OK) {
                        uint8_t *replay_bytes =
                            (uint8_t *)malloc((size_t)replay_size);
                        if (replay_bytes != NULL &&
                            fd_replay_build(world, &decision, UINT64_C(1),
                                            replay_bytes, replay_size, &written,
                                            NULL) == FD_OK) {
                            (void)fd_replay_open(context, replay_bytes,
                                                 replay_size, &replay, NULL);
                        }
                        free(replay_bytes);
                    }
                }
            }
            break;
        case 9U:
            (void)fd_replay_destroy(&replay, NULL);
            break;
        case 10U:
            (void)fd_snapshot_destroy(&snapshot, NULL);
            (void)fd_world_destroy(&world, NULL);
            break;
        case 11U:
            if (context != NULL) {
                fd_allocation_stats stats;
                header(&stats, sizeof(stats));
                (void)fd_context_get_allocation_stats(context, &stats, NULL);
            }
            break;
        case 12U:
            if (replay != NULL) {
                fd_replay_info info;
                fd_replay_record_view record;
                fd_replay_audit_view audit;
                header(&info, sizeof(info));
                header(&record, sizeof(record));
                header(&audit, sizeof(audit));
                if ((data[instruction] & UINT8_C(0x80)) != UINT8_C(0)) {
                    info.struct_size = (uint32_t)data[instruction];
                }
                (void)fd_replay_get_info(replay, &info, NULL);
                (void)fd_replay_get_record(replay,
                                           (uint64_t)(data[instruction] >> 4U),
                                           &record, NULL);
                (void)fd_replay_get_audit(replay,
                                          (uint32_t)(data[instruction] >> 4U),
                                          &audit, NULL);
            }
            break;
        case 13U:
            if (replay != NULL) {
                fd_world *replay_world = NULL;
                fd_replay_status status;
                header(&status, sizeof(status));
                if ((data[instruction] & UINT8_C(0x80)) != UINT8_C(0)) {
                    status.abi_version = (uint32_t)data[instruction];
                }
                if (fd_replay_create_world(replay, &replay_world, NULL) == FD_OK) {
                    (void)fd_replay_advance(
                        replay, replay_world,
                        (uint64_t)((data[instruction] >> 4U) & UINT8_C(3)),
                        &status, NULL);
                }
                (void)fd_world_destroy(&replay_world, NULL);
            }
            break;
        case 14U:
            if (snapshot != NULL) {
                fd_world_info info;
                fd_tile_view tile;
                fd_entity_view entity;
                fd_region_view region;
                uint8_t hash[32];
                header(&info, sizeof(info));
                header(&tile, sizeof(tile));
                header(&entity, sizeof(entity));
                header(&region, sizeof(region));
                (void)fd_snapshot_get_info(snapshot, &info, NULL);
                (void)fd_snapshot_get_tile(snapshot,
                                           (uint32_t)data[instruction],
                                           (uint32_t)(data[instruction] >> 1U),
                                           &tile, NULL);
                (void)fd_snapshot_get_entity_by_index(
                    snapshot, (uint32_t)(data[instruction] >> 4U),
                    &entity, NULL);
                (void)fd_snapshot_get_region(snapshot, &region, NULL);
                (void)fd_snapshot_state_hash(snapshot, hash, NULL);
            }
            break;
        default:
            {
                uint8_t digest[32];
                (void)fd_sha256(data, (uint64_t)size, digest);
                (void)fd_context_destroy(&context, NULL);
            }
            break;
        }
    }
    free(buffer);
    (void)fd_replay_destroy(&replay, NULL);
    (void)fd_snapshot_destroy(&snapshot, NULL);
    (void)fd_world_destroy(&world, NULL);
    (void)fd_context_destroy(&context, NULL);
    return 0;
}
