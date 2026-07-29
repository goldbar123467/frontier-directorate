#include "frontier_directorate/fd.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

_Static_assert(sizeof(uint32_t) == 4U, "ABI requires 32-bit uint32_t");
_Static_assert(sizeof(uint64_t) == 8U, "ABI requires 64-bit uint64_t");
_Static_assert(offsetof(fd_context_config, struct_size) == 0U,
               "struct header must begin at offset zero");
_Static_assert(offsetof(fd_context_config, abi_version) == 4U,
               "ABI tag must follow struct size");
_Static_assert(offsetof(fd_world_config, abi_version) == 4U,
               "world ABI tag offset is frozen");
_Static_assert(offsetof(fd_joint_decision, abi_version) == 4U,
               "decision ABI tag offset is frozen");
_Static_assert(offsetof(fd_step_result, abi_version) == 4U,
               "step ABI tag offset is frozen");

static uint32_t failures = UINT32_C(0);

static void expect(int condition, const char *message)
{
    if (!condition) {
        ++failures;
        (void)fprintf(stderr, "FAIL: %s\n", message);
    }
}

static int all_bytes_equal(const uint8_t *bytes, uint64_t size, uint8_t value)
{
    uint64_t index;
    for (index = UINT64_C(0); index < size; ++index) {
        if (bytes[index] != value) {
            return 0;
        }
    }
    return 1;
}

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

typedef struct failing_allocator_state {
    uint64_t calls;
    uint64_t frees;
    uint64_t live_bytes;
    uint64_t fail_at;
} failing_allocator_state;

static void *failing_allocate(void *user, uint64_t size)
{
    failing_allocator_state *state = (failing_allocator_state *)user;
    void *memory;
    uint64_t call = state->calls;
    ++state->calls;
    if (call == state->fail_at || size > (uint64_t)SIZE_MAX) {
        return NULL;
    }
    memory = malloc((size_t)size);
    if (memory != NULL) {
        state->live_bytes += size;
    }
    return memory;
}

static void failing_deallocate(void *user, void *memory, uint64_t size)
{
    failing_allocator_state *state = (failing_allocator_state *)user;
    if (memory != NULL) {
        ++state->frees;
        state->live_bytes -= size;
        free(memory);
    }
}

int main(void)
{
    fd_context_config context_config;
    fd_world_config world_config;
    fd_content_manifest content;
    fd_generation_report generation;
    fd_diagnostic diag;
    fd_context *context = NULL;
    fd_world *world = NULL;
    fd_snapshot *snapshot = NULL;
    fd_world_info info;
    fd_world_info snapshot_info;
    fd_tile_view tile;
    fd_tile_view snapshot_tile;
    fd_entity_view entity;
    fd_entity_view entity_by_id;
    fd_region_view region;
    fd_region_view snapshot_region;
    fd_validation_report validation;
    fd_allocation_stats stats;
    uint8_t world_hash[32];
    uint8_t snapshot_hash[32];
    uint64_t save_size = UINT64_C(0);
    uint64_t written = UINT64_C(99);
    uint8_t *save_bytes = NULL;
    fd_world *loaded = (fd_world *)(uintptr_t)UINTPTR_MAX;
    fd_version version;
    uint8_t primitive_hash[32];
    uint32_t philox_counter[4] = {0U, 0U, 0U, 0U};
    uint32_t philox_key[2] = {0U, 0U};
    uint32_t philox_output[4] = {0U, 0U, 0U, 0U};

    expect(fd_context_config_init(NULL) == FD_ERR_INVALID_ARGUMENT,
           "context initializer rejects null");
    expect(fd_world_config_init(NULL) == FD_ERR_INVALID_ARGUMENT,
           "world initializer rejects null");
    expect(fd_diagnostic_init(NULL) == FD_ERR_INVALID_ARGUMENT,
           "diagnostic initializer rejects null");
    expect(fd_context_config_init(&context_config) == FD_OK,
           "context initializer succeeds");
    expect(fd_world_config_init(&world_config) == FD_OK,
           "world initializer succeeds");
    expect(fd_diagnostic_init(&diag) == FD_OK,
           "diagnostic initializer succeeds");
    memset(&version, 0, sizeof(version));
    version.struct_size = (uint32_t)sizeof(version);
    expect(fd_get_version(&version) == FD_OK &&
               fd_get_version(NULL) == FD_ERR_INVALID_SIZE,
           "version query covers success and null output");
    version.struct_size = UINT32_C(1);
    expect(fd_get_version(&version) == FD_ERR_INVALID_SIZE,
           "version query rejects undersized output");
    expect(fd_sha256("x", UINT64_C(1), primitive_hash) == FD_OK &&
               fd_sha256(NULL, UINT64_C(1), primitive_hash) ==
                   FD_ERR_INVALID_ARGUMENT &&
               fd_sha256("x", UINT64_C(1), NULL) == FD_ERR_INVALID_ARGUMENT,
           "SHA primitive validates every pointer slot");
    expect(fd_philox4x32_10(philox_counter, philox_key, philox_output) == FD_OK &&
               fd_philox4x32_10(NULL, philox_key, philox_output) ==
                   FD_ERR_INVALID_ARGUMENT &&
               fd_philox4x32_10(philox_counter, NULL, philox_output) ==
                   FD_ERR_INVALID_ARGUMENT &&
               fd_philox4x32_10(philox_counter, philox_key, NULL) ==
                   FD_ERR_INVALID_ARGUMENT,
           "Philox primitive validates every pointer slot");
    {
        fd_context_config malformed = context_config;
        fd_context *bad_context = (fd_context *)(uintptr_t)UINTPTR_MAX;
        malformed.abi_version = FD_ABI_VERSION + UINT32_C(1);
        expect(fd_context_create(&malformed, &bad_context, &diag) ==
                   FD_ERR_INVALID_SIZE && bad_context == NULL,
               "context rejects malformed ABI transactionally");
        malformed = context_config;
        malformed.reserved[7] = UINT32_C(1);
        bad_context = (fd_context *)(uintptr_t)UINTPTR_MAX;
        expect(fd_context_create(&malformed, &bad_context, &diag) ==
                   FD_ERR_INVALID_ARGUMENT && bad_context == NULL,
               "context rejects reserved fields transactionally");
        malformed = context_config;
        malformed.allocator.allocate = failing_allocate;
        malformed.allocator.deallocate = NULL;
        bad_context = (fd_context *)(uintptr_t)UINTPTR_MAX;
        expect(fd_context_create(&malformed, &bad_context, &diag) ==
                   FD_ERR_INVALID_ARGUMENT && bad_context == NULL,
               "context rejects half-specified allocator");
        malformed = context_config;
        malformed.max_file_bytes = UINT64_C(1);
        bad_context = (fd_context *)(uintptr_t)UINTPTR_MAX;
        expect(fd_context_create(&malformed, &bad_context, &diag) ==
                   FD_ERR_OUT_OF_RANGE && bad_context == NULL,
               "context enforces byte capacity floor");
    }
    expect(fd_context_create(&context_config, NULL, &diag) ==
               FD_ERR_INVALID_ARGUMENT,
           "context creation requires output");
    expect(fd_context_create(&context_config, &context, &diag) == FD_OK,
           "context creation succeeds");
    expect(fd_context_get_allocation_stats(context, NULL, &diag) ==
               FD_ERR_INVALID_SIZE &&
               fd_context_get_allocation_stats(NULL, &stats, &diag) ==
                   FD_ERR_INVALID_ARGUMENT,
           "allocation stats validate handle and output");
    memset(&content, 0, sizeof(content));
    content.struct_size = (uint32_t)sizeof(content);
    content.abi_version = FD_ABI_VERSION;
    output_header(&generation, sizeof(generation));
    expect(fd_world_generate(context, &world_config, UINT64_C(17), &content,
                             NULL, &generation, &diag) ==
               FD_ERR_INVALID_ARGUMENT,
           "generation requires world output");
    {
        fd_world_config malformed = world_config;
        fd_world *bad_world = (fd_world *)(uintptr_t)UINTPTR_MAX;
        malformed.struct_size = UINT32_C(0);
        expect(fd_world_generate(context, &malformed, UINT64_C(17), &content,
                                 &bad_world, &generation, &diag) ==
                   FD_ERR_INVALID_SIZE && bad_world == NULL,
               "generation rejects malformed config transactionally");
        bad_world = (fd_world *)(uintptr_t)UINTPTR_MAX;
        expect(fd_world_generate(NULL, &world_config, UINT64_C(17), &content,
                                 &bad_world, &generation, &diag) ==
                   FD_ERR_INVALID_ARGUMENT && bad_world == NULL,
               "generation rejects null context transactionally");
        content.abi_version = FD_ABI_VERSION + UINT32_C(1);
        bad_world = (fd_world *)(uintptr_t)UINTPTR_MAX;
        expect(fd_world_generate(context, &world_config, UINT64_C(17), &content,
                                 &bad_world, &generation, &diag) ==
                   FD_ERR_INVALID_SIZE && bad_world == NULL,
               "generation rejects malformed content transactionally");
        content.abi_version = FD_ABI_VERSION;
        generation.struct_size = UINT32_C(0);
        bad_world = (fd_world *)(uintptr_t)UINTPTR_MAX;
        expect(fd_world_generate(context, &world_config, UINT64_C(17), &content,
                                 &bad_world, &generation, &diag) ==
                   FD_ERR_INVALID_SIZE && bad_world == NULL,
               "generation rejects malformed report transactionally");
        output_header(&generation, sizeof(generation));
    }
    expect(fd_world_generate(context, &world_config, UINT64_C(17), &content,
                             &world, &generation, &diag) == FD_OK,
           "world generation succeeds through public header only");
    expect(fd_context_destroy(&context, &diag) == FD_ERR_BUSY,
           "context refuses destruction with live world");

    output_header(&info, sizeof(info));
    output_header(&snapshot_info, sizeof(snapshot_info));
    output_header(&tile, sizeof(tile));
    expect(fd_world_get_tile(world, UINT32_C(0), UINT32_C(0), &tile, &diag) ==
               FD_OK,
           "world tile output can be reused after malformed call");
    output_header(&snapshot_tile, sizeof(snapshot_tile));
    output_header(&entity, sizeof(entity));
    output_header(&entity_by_id, sizeof(entity_by_id));
    output_header(&region, sizeof(region));
    output_header(&snapshot_region, sizeof(snapshot_region));
    output_header(&validation, sizeof(validation));
    output_header(&stats, sizeof(stats));
    expect(fd_world_get_info(world, &info, &diag) == FD_OK &&
               info.entity_count == UINT32_C(6),
           "world info is complete");
    expect(fd_world_validate(world, &validation, &diag) == FD_OK &&
               validation.passed_invariants == FD_INVARIANT_ALL,
           "world validation query succeeds");
    expect(fd_world_get_tile(world, UINT32_C(0), UINT32_C(0), &tile, &diag) ==
               FD_OK,
           "world tile query succeeds");
    expect(fd_world_get_tile(world, info.width, UINT32_C(0), &tile, &diag) ==
               FD_ERR_OUT_OF_RANGE,
           "world tile query checks coordinates");
    tile.struct_size = UINT32_C(0);
    expect(fd_world_get_tile(world, UINT32_C(0), UINT32_C(0), &tile, &diag) ==
               FD_ERR_INVALID_SIZE,
           "world tile rejects malformed output");
    output_header(&tile, sizeof(tile));
    expect(fd_world_get_tile(world, UINT32_C(0), UINT32_C(0), &tile, &diag) ==
               FD_OK,
           "world tile output can be reused after malformed call");
    expect(fd_world_get_entity_by_index(world, UINT32_C(0), &entity, &diag) ==
               FD_OK &&
               fd_world_get_entity(world, entity.id, &entity_by_id, &diag) ==
                   FD_OK &&
               entity_by_id.id == entity.id,
           "entity index and stable-ID queries agree");
    expect(fd_world_get_entity_by_index(world, UINT32_C(6), &entity, &diag) ==
               FD_ERR_OUT_OF_RANGE &&
               fd_world_get_entity(world, UINT64_C(0), &entity, &diag) ==
                   FD_ERR_OUT_OF_RANGE,
           "entity queries reject invalid index and ID");
    expect(fd_world_get_entity(world,
                               (UINT64_C(2) << UINT32_C(32)) | UINT64_C(1),
                               &entity, &diag) == FD_ERR_OUT_OF_RANGE,
           "entity query rejects valid-slot stale generation ID");
    entity.abi_version = FD_ABI_VERSION + UINT32_C(1);
    expect(fd_world_get_entity_by_index(world, UINT32_C(0), &entity, &diag) ==
               FD_ERR_INVALID_SIZE,
           "entity query rejects malformed output ABI");
    output_header(&entity, sizeof(entity));
    expect(fd_world_get_region(world, info.region_id, &region, &diag) == FD_OK &&
               region.id == info.region_id,
           "region query succeeds");
    expect(fd_world_get_region(world, UINT64_C(0), &region, &diag) ==
               FD_ERR_OUT_OF_RANGE,
           "region query rejects invalid ID");
    expect(fd_world_get_region(
               world, (UINT64_C(2) << UINT32_C(32)) | UINT64_C(1),
               &region, &diag) == FD_ERR_OUT_OF_RANGE,
           "region query rejects valid-slot stale-generation ID");
    region.struct_size = UINT32_C(0);
    expect(fd_world_get_region(world, info.region_id, &region, &diag) ==
               FD_ERR_INVALID_SIZE,
           "region query rejects malformed output");
    output_header(&region, sizeof(region));
    validation.struct_size = UINT32_C(0);
    expect(fd_world_validate(world, &validation, &diag) == FD_ERR_INVALID_SIZE,
           "validation rejects malformed report header");
    output_header(&validation, sizeof(validation));
    expect(fd_world_validate(NULL, &validation, &diag) ==
               FD_ERR_INVALID_ARGUMENT,
           "validation checks handle and report header");
    expect(fd_world_state_hash(world, world_hash, &diag) == FD_OK,
           "world hash query succeeds");

    expect(fd_snapshot_create(world, FD_VISIBILITY_ACTOR,
                              info.foreign_faction_ids[0], &snapshot, &diag) ==
               FD_ERR_OUT_OF_RANGE && snapshot == NULL,
           "unsupported actor snapshot is explicit");
    expect(fd_snapshot_create(world, FD_VISIBILITY_REFERENCE, UINT64_C(0),
                              &snapshot, &diag) == FD_OK,
           "reference snapshot creation succeeds");
    expect(fd_snapshot_get_info(snapshot, &snapshot_info, &diag) == FD_OK &&
               fd_snapshot_get_tile(snapshot, UINT32_C(0), UINT32_C(0),
                                    &snapshot_tile, &diag) == FD_OK &&
               fd_snapshot_get_entity_by_index(snapshot, UINT32_C(0), &entity,
                                               &diag) == FD_OK &&
               fd_snapshot_get_region(snapshot, &snapshot_region, &diag) ==
                   FD_OK &&
               fd_snapshot_state_hash(snapshot, snapshot_hash, &diag) == FD_OK &&
               memcmp(world_hash, snapshot_hash, 32U) == 0 &&
               memcmp(&tile.terrain, &snapshot_tile.terrain,
                      sizeof(tile.terrain)) == 0,
           "snapshot queries agree with captured world");
    snapshot_info.struct_size = UINT32_C(0);
    expect(fd_snapshot_get_info(snapshot, &snapshot_info, &diag) ==
               FD_ERR_INVALID_SIZE &&
               fd_snapshot_get_info(NULL, &info, &diag) ==
                   FD_ERR_INVALID_ARGUMENT,
           "snapshot info validates handle and output header");
    output_header(&snapshot_info, sizeof(snapshot_info));
    snapshot_tile.abi_version = FD_ABI_VERSION + UINT32_C(1);
    expect(fd_snapshot_get_tile(snapshot, UINT32_C(0), UINT32_C(0),
                                &snapshot_tile, &diag) == FD_ERR_INVALID_SIZE,
           "snapshot tile rejects malformed output");
    output_header(&snapshot_tile, sizeof(snapshot_tile));
    expect(fd_snapshot_get_tile(snapshot, info.width, UINT32_C(0),
                                &snapshot_tile, &diag) == FD_ERR_OUT_OF_RANGE,
           "snapshot tile rejects out-of-range coordinate");
    entity.struct_size = UINT32_C(0);
    expect(fd_snapshot_get_entity_by_index(snapshot, UINT32_C(0), &entity,
                                           &diag) == FD_ERR_INVALID_SIZE,
           "snapshot entity rejects malformed output");
    output_header(&entity, sizeof(entity));
    expect(fd_snapshot_get_entity_by_index(snapshot, UINT32_C(6), &entity,
                                           &diag) == FD_ERR_OUT_OF_RANGE,
           "snapshot entity rejects out-of-range index");
    snapshot_region.struct_size = UINT32_C(0);
    expect(fd_snapshot_get_region(snapshot, &snapshot_region, &diag) ==
               FD_ERR_INVALID_SIZE &&
               fd_snapshot_get_region(NULL, &region, &diag) ==
                   FD_ERR_INVALID_ARGUMENT,
           "snapshot region validates handle and output");
    output_header(&snapshot_region, sizeof(snapshot_region));
    expect(fd_snapshot_state_hash(snapshot, NULL, &diag) ==
               FD_ERR_INVALID_ARGUMENT &&
               fd_snapshot_state_hash(NULL, snapshot_hash, &diag) ==
                   FD_ERR_INVALID_ARGUMENT,
           "snapshot hash validates both pointer slots");

    {
        fd_action_prefix prefix;
        fd_action_mask mask;
        uint8_t mask_byte = UINT8_C(0);
        fd_joint_decision decision = pass_decision(&info);
        fd_step_result step;
        fd_action invalid_action = decision.seats[0].action;
        output_header(&prefix, sizeof(prefix));
        output_header(&mask, sizeof(mask));
        output_header(&step, sizeof(step));
        mask.bits = &mask_byte;
        mask.byte_capacity = UINT32_C(1);
        expect(fd_action_mask_query(world, info.foreign_faction_ids[0],
                                    &prefix, &mask, &diag) == FD_OK &&
                   mask_byte == UINT8_C(1),
               "action mask success path is exact");
        prefix.struct_size = UINT32_C(0);
        expect(fd_action_mask_query(world, info.foreign_faction_ids[0],
                                    &prefix, &mask, &diag) == FD_ERR_INVALID_SIZE,
               "action mask rejects malformed prefix");
        output_header(&prefix, sizeof(prefix));
        mask.struct_size = UINT32_C(0);
        expect(fd_action_mask_query(world, info.foreign_faction_ids[0],
                                    &prefix, &mask, &diag) == FD_ERR_INVALID_SIZE,
               "action mask rejects malformed output");
        output_header(&mask, sizeof(mask));
        mask.bits = NULL;
        mask.byte_capacity = UINT32_C(0);
        expect(fd_action_mask_query(world, info.foreign_faction_ids[0],
                                    &prefix, &mask, &diag) ==
                   FD_ERR_BUFFER_TOO_SMALL && mask.byte_capacity == UINT32_C(1),
               "action mask capacity negotiation is transactional");
        expect(fd_action_mask_query(NULL, info.foreign_faction_ids[0],
                                    &prefix, &mask, &diag) ==
                   FD_ERR_INVALID_ARGUMENT &&
                   fd_action_mask_query(world, UINT64_C(0), &prefix, &mask,
                                        &diag) == FD_ERR_OUT_OF_RANGE,
               "action mask validates world and actor");
        expect(fd_action_mask_query(
                   world, (UINT64_C(2) << UINT32_C(32)) | UINT64_C(5),
                   &prefix, &mask, &diag) == FD_ERR_OUT_OF_RANGE,
               "action mask rejects valid-slot stale-generation actor");
        expect(fd_action_validate(world, info.foreign_faction_ids[0],
                                  &decision.seats[0].action, &diag) == FD_OK,
               "action validation accepts PASS");
        invalid_action.category = FD_ACTION_TRADE;
        expect(fd_action_validate(world, info.foreign_faction_ids[0],
                                  &invalid_action, &diag) ==
                   FD_ERR_INVALID_ACTION &&
                   fd_action_validate(world, UINT64_C(0),
                                      &decision.seats[0].action, &diag) ==
                       FD_ERR_OUT_OF_RANGE &&
                   fd_action_validate(NULL, info.foreign_faction_ids[0],
                                      &decision.seats[0].action, &diag) ==
                       FD_ERR_INVALID_ARGUMENT,
               "action validation covers invalid action, actor, and world");
        expect(fd_action_validate(
                   world, (UINT64_C(2) << UINT32_C(32)) | UINT64_C(5),
                   &decision.seats[0].action, &diag) == FD_ERR_OUT_OF_RANGE,
               "action validation rejects valid-slot stale-generation actor");
        invalid_action.struct_size = UINT32_C(0);
        expect(fd_action_validate(world, info.foreign_faction_ids[0],
                                  &invalid_action, &diag) == FD_ERR_INVALID_SIZE,
               "action validation rejects malformed action");
        step.struct_size = UINT32_C(0);
        expect(fd_world_step(world, &decision, &step, &diag) ==
                   FD_ERR_INVALID_SIZE,
               "step rejects malformed output without mutation");
        output_header(&step, sizeof(step));
        decision.struct_size = UINT32_C(0);
        expect(fd_world_step(world, &decision, &step, &diag) ==
                   FD_ERR_INVALID_SIZE,
               "step rejects malformed decision without mutation");
        expect(fd_world_step(NULL, &decision, &step, &diag) ==
                   FD_ERR_INVALID_ARGUMENT &&
                   fd_world_step(world, NULL, &step, &diag) ==
                       FD_ERR_INVALID_ARGUMENT,
               "step validates world and decision pointers");
    }

    {
        fd_joint_decision decision = pass_decision(&info);
        uint64_t replay_size = UINT64_C(0);
        uint64_t replay_written = UINT64_C(77);
        uint8_t *replay_bytes = NULL;
        fd_replay *replay = NULL;
        fd_world *replay_world = NULL;
        fd_replay_info replay_info;
        fd_replay_record_view record;
        fd_replay_audit_view audit;
        fd_replay_status status;
        expect(fd_replay_build_size(world, &decision, UINT64_C(1),
                                    &replay_size, &diag) == FD_OK &&
                   replay_size > UINT64_C(0),
               "replay size success path");
        expect(fd_replay_build_size(NULL, &decision, UINT64_C(1),
                                    &replay_size, &diag) ==
                   FD_ERR_INVALID_ARGUMENT &&
                   fd_replay_build_size(world, NULL, UINT64_C(1),
                                        &replay_size, &diag) ==
                       FD_ERR_INVALID_ARGUMENT &&
                   fd_replay_build_size(world, &decision, UINT64_C(1), NULL,
                                        &diag) == FD_ERR_INVALID_ARGUMENT,
               "replay size validates every pointer slot");
        (void)fd_replay_build_size(world, &decision, UINT64_C(1),
                                   &replay_size, &diag);
        replay_bytes = (uint8_t *)malloc((size_t)replay_size);
        expect(replay_bytes != NULL, "replay public buffer allocates");
        if (replay_bytes != NULL) {
            memset(replay_bytes, 0xa5, (size_t)replay_size);
            expect(fd_replay_build(world, &decision, UINT64_C(1), replay_bytes,
                                   replay_size - UINT64_C(1), &replay_written,
                                   &diag) == FD_ERR_BUFFER_TOO_SMALL &&
                       replay_written == UINT64_C(0) &&
                       all_bytes_equal(replay_bytes, replay_size,
                                       UINT8_C(0xa5)),
                   "replay build capacity failure is transactional");
            expect(fd_replay_build(world, &decision, UINT64_C(1), replay_bytes,
                                   replay_size, &replay_written, &diag) == FD_OK &&
                       replay_written == replay_size,
                   "replay build writes exact measured bytes");
            expect(fd_replay_open(context, replay_bytes, replay_size, &replay,
                                  &diag) == FD_OK,
                   "replay open success path");
            output_header(&replay_info, sizeof(replay_info));
            output_header(&record, sizeof(record));
            output_header(&audit, sizeof(audit));
            output_header(&status, sizeof(status));
            expect(fd_replay_get_info(replay, &replay_info, &diag) == FD_OK &&
                       replay_info.decision_count == UINT64_C(1),
                   "replay info success path");
            expect(fd_replay_get_record(replay, UINT64_C(0), &record, &diag) ==
                       FD_OK &&
                       fd_replay_get_record(replay, UINT64_C(1), &record, &diag) ==
                           FD_ERR_OUT_OF_RANGE,
                   "replay record covers success and range");
            expect(fd_replay_get_audit(replay, UINT32_C(0), &audit, &diag) ==
                       FD_ERR_OUT_OF_RANGE,
                   "empty replay audit reports range");
            replay_info.struct_size = UINT32_C(0);
            record.struct_size = UINT32_C(0);
            audit.struct_size = UINT32_C(0);
            expect(fd_replay_get_info(replay, &replay_info, &diag) ==
                       FD_ERR_INVALID_SIZE &&
                       fd_replay_get_record(replay, UINT64_C(0), &record, &diag) ==
                           FD_ERR_INVALID_SIZE &&
                       fd_replay_get_audit(replay, UINT32_C(0), &audit, &diag) ==
                           FD_ERR_INVALID_SIZE,
                   "replay queries reject malformed outputs");
            expect(fd_replay_create_world(replay, &replay_world, &diag) == FD_OK &&
                       replay_world != NULL,
                   "replay creates tick-zero world");
            {
                fd_world *mismatch_world = NULL;
                uint8_t mismatch_before[32];
                uint8_t mismatch_after[32];
                output_header(&replay_info, sizeof(replay_info));
                expect(fd_world_generate(context, &world_config, UINT64_C(18),
                                         &content, &mismatch_world, NULL,
                                         &diag) == FD_OK,
                       "mismatched replay world generates");
                (void)fd_world_state_hash(mismatch_world, mismatch_before, NULL);
                expect(fd_replay_advance(replay, mismatch_world, UINT64_C(1),
                                         &status, &diag) == FD_ERR_CHECKSUM &&
                           fd_replay_get_info(replay, &replay_info, &diag) ==
                               FD_OK && replay_info.cursor == UINT64_C(0),
                       "mismatched replay world leaves cursor unchanged");
                (void)fd_world_state_hash(mismatch_world, mismatch_after, NULL);
                expect(memcmp(mismatch_before, mismatch_after, 32U) == 0,
                       "mismatched replay advance leaves world unchanged");
                (void)fd_world_destroy(&mismatch_world, NULL);
            }
            expect(fd_replay_advance(replay, replay_world, UINT64_C(1), &status,
                                     &diag) == FD_OK &&
                       status.complete == UINT32_C(1),
                   "replay advance verifies accepted interval");
            status.struct_size = UINT32_C(0);
            expect(fd_replay_advance(replay, replay_world, UINT64_C(0), &status,
                                     &diag) == FD_ERR_INVALID_SIZE &&
                       fd_replay_advance(NULL, replay_world, UINT64_C(0), &status,
                                         &diag) == FD_ERR_INVALID_ARGUMENT,
                   "replay advance validates output and handle");
            expect(fd_replay_create_world(replay, NULL, &diag) ==
                       FD_ERR_INVALID_ARGUMENT,
                   "replay world creation requires output");
            (void)fd_world_destroy(&replay_world, NULL);
            expect(fd_replay_destroy(&replay, &diag) == FD_OK && replay == NULL &&
                       fd_replay_destroy(&replay, &diag) == FD_OK &&
                       fd_replay_destroy(NULL, &diag) == FD_ERR_INVALID_ARGUMENT,
                   "replay destroy is clearing, idempotent, and null-safe");
            replay = (fd_replay *)(uintptr_t)UINTPTR_MAX;
            expect(fd_replay_open(context, replay_bytes,
                                  replay_size - UINT64_C(1), &replay, &diag) !=
                       FD_OK && replay == NULL,
                   "truncated replay open is transactional");
            free(replay_bytes);
        }
    }

    {
        fd_joint_decision decision = pass_decision(&info);
        fd_step_result step;
        fd_allocation_stats before_step;
        fd_allocation_stats after_step;
        output_header(&step, sizeof(step));
        output_header(&before_step, sizeof(before_step));
        output_header(&after_step, sizeof(after_step));
        (void)fd_context_get_allocation_stats(context, &before_step, NULL);
        expect(fd_world_step(world, &decision, &step, &diag) == FD_OK &&
                   step.tick_after == FD_OPERATIONAL_TICKS &&
                   step.entity_trace_count == FD_U01_ENTITY_TRACE_COUNT,
               "direct public step success exposes complete trace");
        (void)fd_context_get_allocation_stats(context, &after_step, NULL);
        expect(before_step.allocation_calls == after_step.allocation_calls &&
                   before_step.deallocation_calls == after_step.deallocation_calls,
               "direct public step performs no allocation");
        expect(fd_world_step(world, &decision, &step, &diag) ==
                   FD_ERR_INVALID_ACTION,
               "stale decision is transactionally rejected");
    }

    info.struct_size = UINT32_C(0);
    expect(fd_world_get_info(world, &info, &diag) == FD_ERR_INVALID_SIZE &&
               diag.field_id == FD_FIELD_STRUCT_SIZE,
           "undersized output is rejected structurally");
    expect(fd_world_get_info(NULL, &snapshot_info, &diag) ==
               FD_ERR_INVALID_ARGUMENT,
           "null world is rejected");
    expect(fd_world_state_hash(world, NULL, &diag) == FD_ERR_INVALID_ARGUMENT,
           "null hash output is rejected");

    expect(fd_world_save_size(world, &save_size, &diag) == FD_OK &&
               save_size > UINT64_C(0),
           "save size query succeeds");
    expect(fd_world_save(world, NULL, save_size, &written, &diag) ==
               FD_ERR_BUFFER_TOO_SMALL && written == UINT64_C(0),
           "save null buffer negotiates transactionally");
    save_bytes = (uint8_t *)malloc((size_t)save_size);
    expect(save_bytes != NULL, "save test buffer allocates");
    if (save_bytes != NULL) {
        memset(save_bytes, 0xa5, (size_t)save_size);
        expect(fd_world_save(world, save_bytes, save_size - UINT64_C(1),
                             &written, &diag) == FD_ERR_BUFFER_TOO_SMALL &&
                   written == UINT64_C(0) &&
                   all_bytes_equal(save_bytes, save_size, UINT8_C(0xa5)),
               "save capacity is exact and transactional");
        expect(fd_world_save(world, save_bytes, save_size, &written, &diag) ==
                   FD_OK && written == save_size,
               "save writes exact measured bytes");
        expect(fd_world_load(context, save_bytes, save_size, &loaded, &diag) ==
                   FD_OK && loaded != NULL,
               "save loads through public API");
        (void)fd_world_destroy(&loaded, NULL);
        loaded = (fd_world *)(uintptr_t)UINTPTR_MAX;
        expect(fd_world_load(context, save_bytes, save_size, NULL, &diag) ==
                   FD_ERR_INVALID_ARGUMENT,
               "load requires output");
        expect(fd_world_load(context, NULL, save_size, &loaded, &diag) ==
                   FD_ERR_INVALID_ARGUMENT && loaded == NULL,
               "load rejects null bytes without partial output");
    }
    free(save_bytes);
    expect(fd_snapshot_destroy(&snapshot, &diag) == FD_OK && snapshot == NULL,
           "snapshot destroys and clears handle");
    expect(fd_snapshot_destroy(&snapshot, &diag) == FD_OK &&
               fd_snapshot_destroy(NULL, &diag) == FD_ERR_INVALID_ARGUMENT,
           "snapshot destroy is idempotent and validates pointer slot");
    expect(fd_world_destroy(&world, &diag) == FD_OK && world == NULL,
           "world destroys and clears handle");
    expect(fd_world_destroy(&world, &diag) == FD_OK &&
               fd_world_destroy(NULL, &diag) == FD_ERR_INVALID_ARGUMENT,
           "world destroy is idempotent and validates pointer slot");
    expect(fd_context_get_allocation_stats(context, &stats, &diag) == FD_OK &&
               stats.live_bytes == UINT64_C(0),
           "public operations release all owned allocations");
    expect(fd_context_destroy(&context, &diag) == FD_OK && context == NULL,
           "context destroys and clears handle");
    expect(fd_context_destroy(&context, &diag) == FD_OK &&
               fd_context_destroy(NULL, &diag) == FD_ERR_INVALID_ARGUMENT,
           "context destroy is idempotent and validates pointer slot");
    {
        failing_allocator_state allocator_state;
        fd_context_config custom_config;
        fd_context *custom_context = (fd_context *)(uintptr_t)UINTPTR_MAX;
        fd_world *custom_world = (fd_world *)(uintptr_t)UINTPTR_MAX;
        fd_generation_report custom_report;
        memset(&allocator_state, 0, sizeof(allocator_state));
        allocator_state.fail_at = UINT64_C(0);
        (void)fd_context_config_init(&custom_config);
        custom_config.allocator.allocate = failing_allocate;
        custom_config.allocator.deallocate = failing_deallocate;
        custom_config.allocator.user = &allocator_state;
        expect(fd_context_create(&custom_config, &custom_context, &diag) ==
                   FD_ERR_OUT_OF_MEMORY && custom_context == NULL &&
                   allocator_state.live_bytes == UINT64_C(0),
               "custom allocator context failure is transactional");
        memset(&allocator_state, 0, sizeof(allocator_state));
        allocator_state.fail_at = UINT64_MAX;
        custom_context = NULL;
        expect(fd_context_create(&custom_config, &custom_context, &diag) == FD_OK,
               "custom allocator context succeeds");
        output_header(&custom_report, sizeof(custom_report));
        allocator_state.fail_at = allocator_state.calls;
        custom_world = (fd_world *)(uintptr_t)UINTPTR_MAX;
        expect(fd_world_generate(custom_context, &world_config, UINT64_C(8),
                                 &content, &custom_world, &custom_report,
                                 &diag) == FD_ERR_OUT_OF_MEMORY &&
                   custom_world == NULL,
               "world-handle allocation failure publishes no world");
        allocator_state.fail_at = allocator_state.calls + UINT64_C(1);
        custom_world = (fd_world *)(uintptr_t)UINTPTR_MAX;
        expect(fd_world_generate(custom_context, &world_config, UINT64_C(8),
                                 &content, &custom_world, &custom_report,
                                 &diag) == FD_ERR_OUT_OF_MEMORY &&
                   custom_world == NULL && allocator_state.frees > UINT64_C(0),
               "tile allocation failure cleans temporary world");
        allocator_state.fail_at = UINT64_MAX;
        expect(fd_world_generate(custom_context, &world_config, UINT64_C(8),
                                 &content, &custom_world, &custom_report,
                                 &diag) == FD_OK && custom_world != NULL,
               "custom allocator world succeeds after failure injection");
        if (custom_world != NULL) {
            fd_snapshot *failed_snapshot =
                (fd_snapshot *)(uintptr_t)UINTPTR_MAX;
            uint64_t custom_save_size = UINT64_C(0);
            uint64_t custom_written = UINT64_C(0);
            uint8_t *custom_save = NULL;
            fd_world *failed_load = (fd_world *)(uintptr_t)UINTPTR_MAX;
            fd_world_info custom_info;
            fd_joint_decision custom_decision;
            uint64_t custom_replay_size = UINT64_C(0);
            uint8_t *custom_replay_bytes = NULL;
            fd_replay *custom_replay = NULL;
            fd_world *custom_replay_world = NULL;
            allocator_state.fail_at = allocator_state.calls;
            expect(fd_snapshot_create(custom_world, FD_VISIBILITY_REFERENCE,
                                      UINT64_C(0), &failed_snapshot, &diag) ==
                       FD_ERR_OUT_OF_MEMORY && failed_snapshot == NULL,
                   "snapshot handle allocation failure is transactional");
            allocator_state.fail_at = UINT64_MAX;
            expect(fd_world_save_size(custom_world, &custom_save_size, &diag) ==
                       FD_OK,
                   "custom world save size succeeds");
            custom_save = (uint8_t *)malloc((size_t)custom_save_size);
            if (custom_save != NULL) {
                (void)fd_world_save(custom_world, custom_save,
                                    custom_save_size, &custom_written, NULL);
                allocator_state.fail_at = allocator_state.calls;
                expect(fd_world_load(custom_context, custom_save,
                                     custom_save_size, &failed_load, &diag) ==
                           FD_ERR_OUT_OF_MEMORY && failed_load == NULL,
                       "load allocation failure publishes no world");
                allocator_state.fail_at = UINT64_MAX;
                output_header(&custom_info, sizeof(custom_info));
                (void)fd_world_get_info(custom_world, &custom_info, NULL);
                custom_decision = pass_decision(&custom_info);
                expect(fd_replay_build_size(custom_world, &custom_decision,
                                            UINT64_C(1), &custom_replay_size,
                                            &diag) == FD_OK,
                       "custom replay size succeeds");
                custom_replay_bytes =
                    (uint8_t *)malloc((size_t)custom_replay_size);
                if (custom_replay_bytes != NULL) {
                    expect(fd_replay_build(custom_world, &custom_decision,
                                           UINT64_C(1), custom_replay_bytes,
                                           custom_replay_size, &custom_written,
                                           &diag) == FD_OK,
                           "custom replay build succeeds");
                    allocator_state.fail_at = allocator_state.calls;
                    custom_replay =
                        (fd_replay *)(uintptr_t)UINTPTR_MAX;
                    expect(fd_replay_open(custom_context, custom_replay_bytes,
                                          custom_replay_size, &custom_replay,
                                          &diag) == FD_ERR_OUT_OF_MEMORY &&
                               custom_replay == NULL,
                           "replay-open allocation failure publishes no handle");
                    allocator_state.fail_at = UINT64_MAX;
                    expect(fd_replay_open(custom_context, custom_replay_bytes,
                                          custom_replay_size, &custom_replay,
                                          &diag) == FD_OK &&
                               fd_replay_create_world(custom_replay,
                                                      &custom_replay_world,
                                                      &diag) == FD_OK,
                           "custom replay opens and creates world");
                    if (custom_replay != NULL && custom_replay_world != NULL) {
                        fd_replay_status custom_status;
                        fd_replay_info custom_replay_info;
                        uint8_t before_hash[32];
                        uint8_t after_hash[32];
                        output_header(&custom_status, sizeof(custom_status));
                        output_header(&custom_replay_info,
                                      sizeof(custom_replay_info));
                        (void)fd_world_state_hash(custom_replay_world,
                                                  before_hash, NULL);
                        allocator_state.fail_at = allocator_state.calls;
                        expect(fd_replay_advance(
                                   custom_replay, custom_replay_world,
                                   UINT64_C(1), &custom_status, &diag) ==
                                   FD_ERR_OUT_OF_MEMORY &&
                               fd_replay_get_info(custom_replay,
                                                  &custom_replay_info,
                                                  &diag) == FD_OK &&
                               custom_replay_info.cursor == UINT64_C(0),
                               "replay-advance allocation failure preserves cursor");
                        (void)fd_world_state_hash(custom_replay_world,
                                                  after_hash, NULL);
                        expect(memcmp(before_hash, after_hash, 32U) == 0,
                               "replay-advance allocation failure preserves world");
                        allocator_state.fail_at = UINT64_MAX;
                        expect(fd_replay_advance(
                                   custom_replay, custom_replay_world,
                                   UINT64_C(1), &custom_status, &diag) == FD_OK,
                               "replay advance succeeds after allocator retry");
                    }
                    (void)fd_world_destroy(&custom_replay_world, NULL);
                    (void)fd_replay_destroy(&custom_replay, NULL);
                    free(custom_replay_bytes);
                }
                free(custom_save);
            }
            (void)fd_world_destroy(&custom_world, NULL);
        }
        allocator_state.fail_at = UINT64_MAX;
        expect(fd_context_destroy(&custom_context, &diag) == FD_OK &&
                   custom_context == NULL &&
                   allocator_state.live_bytes == UINT64_C(0),
               "custom allocator observes complete cleanup");
    }
    (void)printf("public_api_failures=%u\n", failures);
    return failures == UINT32_C(0) ? 0 : 1;
}
