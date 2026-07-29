#include "frontier_directorate/fd.h"
#include "fd_internal.h"

#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct test_state {
    uint64_t checks;
    uint64_t failures;
} test_state;

static void check(test_state *state, int condition, const char *message)
{
    ++state->checks;
    if (!condition) {
        ++state->failures;
        (void)fprintf(stderr, "FAIL: %s\n", message);
    }
}

static void output_header(void *value, size_t size)
{
    const uint32_t fields[2] = {(uint32_t)size, FD_ABI_VERSION};
    memset(value, 0, size);
    memcpy(value, fields, sizeof(fields));
}

static int hash_equals_hex(const uint8_t hash[32], const char *hex)
{
    static const char digits[] = "0123456789abcdef";
    uint32_t index;
    for (index = 0U; index < 32U; ++index) {
        if (digits[hash[index] >> 4U] != hex[index * 2U] ||
            digits[hash[index] & UINT8_C(0x0f)] != hex[index * 2U + 1U]) {
            return 0;
        }
    }
    return 1;
}

static uint16_t load_u16_le_test(const uint8_t *bytes)
{
    return (uint16_t)((uint16_t)bytes[0] |
                      (uint16_t)((uint16_t)bytes[1] << 8U));
}

static uint8_t *find_section(uint8_t *bytes,
                             uint64_t size,
                             uint16_t tag,
                             uint64_t *out_length)
{
    uint64_t position = UINT64_C(8);
    uint64_t footer = size - UINT64_C(32);
    while (position + UINT64_C(48) <= footer) {
        uint16_t current = load_u16_le_test(bytes + position);
        uint64_t length = fd_load_u64_le(bytes + position + UINT64_C(8));
        if (length > footer - position - UINT64_C(48)) {
            return NULL;
        }
        if (current == tag) {
            *out_length = length;
            return bytes + position + UINT64_C(16);
        }
        position += UINT64_C(48) + length;
    }
    return NULL;
}

static int resign_container(uint8_t *bytes,
                            uint64_t size,
                            uint16_t tag,
                            const char *footer_domain)
{
    uint64_t length = UINT64_C(0);
    uint8_t *payload = find_section(bytes, size, tag, &length);
    uint8_t digest[32];
    uint8_t framed_length[4];
    fd_sha256_ctx_internal sha;
    size_t domain_length = strlen(footer_domain);
    if (payload == NULL || domain_length > (size_t)UINT32_MAX ||
        fd_sha256(payload, length, digest) != FD_OK) {
        return 0;
    }
    memcpy(payload + length, digest, 32U);
    fd_sha256_init_internal(&sha);
    fd_store_u32_le(framed_length, (uint32_t)domain_length);
    fd_sha256_update_internal(&sha, framed_length, UINT64_C(4));
    fd_sha256_update_internal(&sha, footer_domain, (uint64_t)domain_length);
    fd_sha256_update_internal(&sha, bytes, size - UINT64_C(32));
    fd_sha256_final_internal(&sha, digest);
    memcpy(bytes + size - UINT64_C(32), digest, 32U);
    return 1;
}

static uint8_t *extend_section(const uint8_t *bytes,
                               uint64_t size,
                               uint16_t tag,
                               const uint8_t *suffix,
                               uint32_t suffix_size,
                               uint64_t *out_size)
{
    uint64_t old_length = UINT64_C(0);
    uint8_t *mutable_bytes = (uint8_t *)(uintptr_t)bytes;
    uint8_t *payload = find_section(mutable_bytes, size, tag, &old_length);
    uint64_t payload_offset;
    uint64_t tail_offset;
    uint8_t *result;
    if (payload == NULL || suffix_size == UINT32_C(0) ||
        size > UINT64_MAX - (uint64_t)suffix_size) {
        return NULL;
    }
    payload_offset = (uint64_t)(payload - mutable_bytes);
    tail_offset = payload_offset + old_length;
    result = (uint8_t *)malloc((size_t)(size + (uint64_t)suffix_size));
    if (result == NULL) {
        return NULL;
    }
    memcpy(result, bytes, (size_t)tail_offset);
    memcpy(result + tail_offset, suffix, (size_t)suffix_size);
    memcpy(result + tail_offset + (uint64_t)suffix_size,
           bytes + tail_offset, (size_t)(size - tail_offset));
    fd_store_u64_le(result + payload_offset - UINT64_C(8),
                    old_length + (uint64_t)suffix_size);
    *out_size = size + (uint64_t)suffix_size;
    return result;
}

static uint8_t *append_optional_empty_section(const uint8_t *bytes,
                                              uint64_t size,
                                              uint16_t tag,
                                              uint64_t *out_size)
{
    uint64_t footer = size - UINT64_C(32);
    uint8_t empty_digest[32];
    uint8_t *result = (uint8_t *)malloc((size_t)(size + UINT64_C(48)));
    if (result == NULL) {
        return NULL;
    }
    memcpy(result, bytes, (size_t)footer);
    fd_store_u16_le(result + footer, tag);
    fd_store_u16_le(result + footer + UINT64_C(2), UINT16_C(0));
    fd_store_u32_le(result + footer + UINT64_C(4), UINT32_C(0));
    fd_store_u64_le(result + footer + UINT64_C(8), UINT64_C(0));
    (void)fd_sha256(NULL, UINT64_C(0), empty_digest);
    memcpy(result + footer + UINT64_C(16), empty_digest, 32U);
    memcpy(result + footer + UINT64_C(48), bytes + footer, 32U);
    *out_size = size + UINT64_C(48);
    return result;
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
        decision.seats[seat].action.struct_size =
            (uint32_t)sizeof(decision.seats[seat].action);
        decision.seats[seat].action.abi_version = FD_ABI_VERSION;
        decision.seats[seat].action.category = FD_ACTION_PASS;
    }
    return decision;
}

static fd_result save_world(const fd_world *world, uint8_t **out, uint64_t *out_size)
{
    uint64_t size = UINT64_C(0);
    uint64_t written = UINT64_C(0);
    fd_result result = fd_world_save_size(world, &size, NULL);
    uint8_t *bytes;
    if (result != FD_OK || size > (uint64_t)SIZE_MAX) {
        return result == FD_OK ? FD_ERR_CAPACITY : result;
    }
    bytes = (uint8_t *)malloc((size_t)size);
    if (bytes == NULL) {
        return FD_ERR_OUT_OF_MEMORY;
    }
    result = fd_world_save(world, bytes, size, &written, NULL);
    if (result != FD_OK || written != size) {
        free(bytes);
        return result == FD_OK ? FD_ERR_INTERNAL : result;
    }
    *out = bytes;
    *out_size = size;
    return FD_OK;
}

static void test_vectors(test_state *tests)
{
    uint8_t digest[32];
    uint32_t counter[4] = {0U, 0U, 0U, 0U};
    uint32_t key[2] = {0U, 0U};
    uint32_t output[4] = {0U, 0U, 0U, 0U};
    uint8_t seed[16] = {0U};
    check(tests, fd_sha256("abc", UINT64_C(3), digest) == FD_OK,
          "SHA-256 accepts abc");
    check(tests, hash_equals_hex(digest,
          "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad"),
          "SHA-256 abc NIST vector");
    check(tests, fd_sha256(NULL, UINT64_C(0), digest) == FD_OK,
          "SHA-256 accepts empty input");
    check(tests, hash_equals_hex(digest,
          "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855"),
          "SHA-256 empty NIST vector");
    check(tests, fd_sha256(NULL, UINT64_C(1), digest) == FD_ERR_INVALID_ARGUMENT,
          "SHA-256 rejects null nonempty input");
    check(tests, fd_philox4x32_10(counter, key, output) == FD_OK,
          "Philox accepts vector input");
    check(tests, output[0] == UINT32_C(0x6627e8d5) &&
                 output[1] == UINT32_C(0xe169c58d) &&
                 output[2] == UINT32_C(0xbc57ac4c) &&
                 output[3] == UINT32_C(0x9b00dbd8),
          "Philox4x32-10 Random123 zero vector");
    check(tests, fd_philox4x32_10(NULL, key, output) == FD_ERR_INVALID_ARGUMENT,
          "Philox rejects null input");
    fd_rng_digest(seed, FD_GENERATOR_VERSION, FD_RULESET_VERSION,
                  FD_RNG_DOMAIN_COAST, UINT64_C(0), UINT64_C(0), digest);
    check(tests, hash_equals_hex(digest,
          "86472b1e1aa1f9e74a6e114cc90960afb210c6205f6c68c81db276aeadb8128c"),
          "framed RNG semantic-scope digest vector");
}

static void test_seed(test_state *tests,
                      fd_context *context,
                      const fd_world_config *config,
                      uint64_t seed,
                      int roundtrip)
{
    fd_content_manifest content = empty_content();
    fd_generation_report generation;
    fd_validation_report validation;
    fd_world_info info;
    fd_world *world = NULL;
    fd_diagnostic diag;
    fd_result result;
    output_header(&generation, sizeof(generation));
    output_header(&validation, sizeof(validation));
    output_header(&info, sizeof(info));
    (void)fd_diagnostic_init(&diag);
    result = fd_world_generate(context, config, seed, &content, &world,
                               &generation, &diag);
    if (result != FD_OK) {
        (void)fprintf(stderr, "seed=%" PRIu64 " generation result=%" PRIu32
                              " diag=%s\n", seed, result, diag.message);
    }
    check(tests, result == FD_OK && world != NULL, "seed generates a live world");
    if (world == NULL) {
        return;
    }
    check(tests, fd_world_validate(world, &validation, &diag) == FD_OK,
          "generated world passes all invariants");
    check(tests, validation.passed_invariants == FD_INVARIANT_ALL,
          "all named invariant bits pass");
    check(tests, fd_world_get_info(world, &info, &diag) == FD_OK,
          "world info query succeeds");
    check(tests, info.width == config->width && info.height == config->height &&
                 info.entity_count == UINT32_C(6) &&
                 info.foreign_faction_count == FD_U01_SEAT_COUNT,
          "world counts match frozen U01 bounds");
    if (roundtrip != 0) {
        uint8_t *save = NULL;
        uint64_t save_size = UINT64_C(0);
        fd_world *loaded = NULL;
        uint8_t *save_again = NULL;
        uint64_t save_again_size = UINT64_C(0);
        check(tests, save_world(world, &save, &save_size) == FD_OK,
              "canonical save succeeds");
        if (save != NULL) {
            check(tests, fd_world_load(context, save, save_size, &loaded, &diag) == FD_OK,
                  "canonical save loads");
            if (loaded != NULL) {
                check(tests, save_world(loaded, &save_again, &save_again_size) == FD_OK,
                      "loaded world re-saves");
                check(tests, save_again_size == save_size &&
                             memcmp(save_again, save, (size_t)save_size) == 0,
                      "save-load-save is byte identical");
                (void)fd_world_destroy(&loaded, NULL);
            }
            loaded = (fd_world *)(uintptr_t)UINTPTR_MAX;
            check(tests, fd_world_load(context, save, save_size - UINT64_C(1),
                                       &loaded, &diag) != FD_OK && loaded == NULL,
                  "truncated save is rejected without partial output");
            if (seed == UINT64_C(0)) {
                static const uint8_t suffix[4] = {
                    UINT8_C(0xde), UINT8_C(0xad), UINT8_C(0xbe), UINT8_C(0xef)
                };
                uint64_t extended_size = UINT64_C(0);
                uint64_t future_size = UINT64_C(0);
                uint8_t *extended = extend_section(
                    save, save_size, UINT16_C(2), suffix, UINT32_C(4),
                    &extended_size);
                uint8_t *future = NULL;
                check(tests, extended != NULL,
                      "future save version tuple accepts an optional field suffix");
                if (extended != NULL) {
                    future = append_optional_empty_section(
                        extended, extended_size, UINT16_C(12), &future_size);
                    check(tests, future != NULL,
                          "future save accepts an unknown optional section");
                }
                if (future != NULL) {
                    uint64_t manifest_length = UINT64_C(0);
                    uint64_t tuple_length = UINT64_C(0);
                    uint8_t *manifest = find_section(
                        future, future_size, UINT16_C(1), &manifest_length);
                    uint8_t *tuple = find_section(
                        future, future_size, UINT16_C(2), &tuple_length);
                    check(tests, manifest != NULL && tuple != NULL &&
                                 manifest_length >= UINT64_C(16) &&
                                 tuple_length >= UINT64_C(44),
                          "future save extensible prefixes remain discoverable");
                    if (manifest != NULL && tuple != NULL &&
                        manifest_length >= UINT64_C(16) &&
                        tuple_length >= UINT64_C(44)) {
                        fd_store_u16_le(manifest + UINT64_C(2), UINT16_C(1));
                        fd_store_u32_le(manifest + UINT64_C(8), UINT32_C(12));
                        fd_store_u32_le(tuple + UINT64_C(4),
                                        (uint32_t)FD_API_VERSION_MINOR +
                                            UINT32_C(1));
                        fd_store_u32_le(tuple + UINT64_C(8), UINT32_C(7));
                        fd_store_u32_le(tuple + UINT64_C(24), UINT32_C(1));
                        fd_store_u32_le(tuple + UINT64_C(32),
                                        (uint32_t)FD_REPLAY_VERSION_MINOR +
                                            UINT32_C(1));
                        check(tests, resign_container(
                                          future, future_size, UINT16_C(2),
                                          "FrontierDirectorate/Save/Footer/v1") &&
                                     resign_container(
                                          future, future_size, UINT16_C(1),
                                          "FrontierDirectorate/Save/Footer/v1"),
                              "future compatible save is correctly re-signed");
                        check(tests, fd_world_load(context, future, future_size,
                                                   &loaded, &diag) == FD_OK &&
                                     loaded != NULL,
                              "future minor save with compatible reader, optional field, and optional section loads");
                        (void)fd_world_destroy(&loaded, NULL);
                    }
                }
                free(future);
                free(extended);
            }
            save[save_size / UINT64_C(2)] ^= UINT8_C(0x01);
            check(tests, fd_world_load(context, save, save_size, &loaded, &diag) ==
                         FD_ERR_CHECKSUM && loaded == NULL,
                  "corrupted save is rejected by digest");
            free(save_again);
            free(save);
        }
    }
    (void)fd_world_destroy(&world, NULL);
}

static void test_constructive_fallback(test_state *tests, fd_context *context)
{
    static const uint32_t dimensions[3][2] = {
        {FD_MIN_WIDTH, FD_MIN_HEIGHT},
        {UINT32_C(48), UINT32_C(24)},
        {FD_MAX_WIDTH, FD_MAX_HEIGHT}
    };
    static const uint32_t densities[3][3] = {
        {UINT32_C(220000), UINT32_C(120000), UINT32_C(80000)},
        {UINT32_C(1), UINT32_C(1), UINT32_C(1)},
        {UINT32_C(899998), UINT32_C(1), UINT32_C(1)}
    };
    static const uint64_t seeds[2] = {UINT64_C(0), UINT64_MAX};
    fd_content_manifest content = empty_content();
    uint32_t dimension;
    for (dimension = 0U; dimension < UINT32_C(3); ++dimension) {
        uint32_t density;
        for (density = 0U; density < UINT32_C(3); ++density) {
            uint32_t seed_index;
            for (seed_index = 0U; seed_index < UINT32_C(2); ++seed_index) {
                fd_world_config config;
                fd_generation_report generation;
                fd_validation_report validation;
                fd_world *world = NULL;
                fd_diagnostic diag;
                (void)fd_world_config_init(&config);
                config.width = dimensions[dimension][0];
                config.height = dimensions[dimension][1];
                config.attempt_budget = UINT32_C(1);
                config.forest_density_ppm = densities[density][0];
                config.hill_density_ppm = densities[density][1];
                config.wetland_density_ppm = densities[density][2];
                output_header(&generation, sizeof(generation));
                output_header(&validation, sizeof(validation));
                (void)fd_diagnostic_init(&diag);
                check(tests, fd_world_generate(context, &config,
                                                seeds[seed_index], &content,
                                                &world, &generation, &diag) ==
                                 FD_OK && world != NULL &&
                             generation.accepted_attempt == UINT32_C(0) &&
                             generation.river_raised_tile_count == UINT32_C(0),
                      "attempt-budget-one constructive fallback succeeds without raising river");
                if (world != NULL) {
                    check(tests, fd_world_validate(world, &validation, &diag) ==
                                     FD_OK &&
                                 validation.failed_invariants == UINT64_C(0),
                          "fallback world satisfies every frozen invariant");
                }
                (void)fd_world_destroy(&world, NULL);
            }
        }
    }
    {
        fd_world_config config;
        fd_generation_report generation;
        fd_world *world = NULL;
        fd_diagnostic diag;
        (void)fd_world_config_init(&config);
        config.attempt_budget = UINT32_C(2);
        config.forest_density_ppm = UINT32_C(1);
        config.hill_density_ppm = UINT32_C(1);
        config.wetland_density_ppm = UINT32_C(1);
        output_header(&generation, sizeof(generation));
        (void)fd_diagnostic_init(&diag);
        check(tests, fd_world_generate(context, &config, UINT64_C(0), &content,
                                       &world, &generation, &diag) == FD_OK &&
                     generation.accepted_attempt == UINT32_C(1) &&
                     generation.rejected_count == UINT32_C(1) &&
                     generation.attempts[0].reason_code ==
                         FD_GEN_REJECT_TERRAIN_COVERAGE,
              "ordinary low-density attempt rejects before final fallback");
        (void)fd_world_destroy(&world, NULL);
    }
}

static void test_save_future_continuation(test_state *tests,
                                          fd_context *context,
                                          const fd_world_config *config)
{
    fd_content_manifest content = empty_content();
    fd_world *original = NULL;
    fd_world *loaded = NULL;
    uint8_t *save = NULL;
    uint64_t save_size = UINT64_C(0);
    uint32_t interval;
    fd_diagnostic diag;
    (void)fd_diagnostic_init(&diag);
    check(tests, fd_world_generate(context, config, UINT64_C(99), &content,
                                   &original, NULL, &diag) == FD_OK &&
                 save_world(original, &save, &save_size) == FD_OK &&
                 fd_world_load(context, save, save_size, &loaded, &diag) == FD_OK,
          "continuation oracle and loaded world initialize identically");
    for (interval = 0U; interval < UINT32_C(417) && original != NULL &&
                            loaded != NULL;
         ++interval) {
        fd_world_info original_info;
        fd_world_info loaded_info;
        fd_joint_decision original_decision;
        fd_joint_decision loaded_decision;
        fd_step_result original_step;
        fd_step_result loaded_step;
        output_header(&original_info, sizeof(original_info));
        output_header(&loaded_info, sizeof(loaded_info));
        output_header(&original_step, sizeof(original_step));
        output_header(&loaded_step, sizeof(loaded_step));
        if (fd_world_get_info(original, &original_info, &diag) != FD_OK ||
            fd_world_get_info(loaded, &loaded_info, &diag) != FD_OK) {
            break;
        }
        original_decision = pass_decision(&original_info);
        loaded_decision = pass_decision(&loaded_info);
        if (fd_world_step(original, &original_decision, &original_step, &diag) !=
                FD_OK ||
            fd_world_step(loaded, &loaded_decision, &loaded_step, &diag) != FD_OK ||
            original_step.tick_after != loaded_step.tick_after ||
            memcmp(original_step.post_state_sha256,
                   loaded_step.post_state_sha256, 32U) != 0) {
            break;
        }
    }
    check(tests, interval == UINT32_C(417),
          "save/load future continuation matches for 10,008 local ticks");
    free(save);
    (void)fd_world_destroy(&loaded, NULL);
    (void)fd_world_destroy(&original, NULL);
}

static void test_actions_replay(test_state *tests,
                                fd_context *context,
                                const fd_world_config *config)
{
    fd_content_manifest content = empty_content();
    fd_generation_report generation;
    fd_world *world = NULL;
    fd_world_info info;
    fd_action_prefix prefix;
    fd_action_mask mask;
    uint8_t mask_byte = UINT8_C(0);
    fd_allocation_stats before;
    fd_allocation_stats after;
    fd_joint_decision invalid;
    fd_joint_decision valid;
    fd_step_result step;
    uint8_t hash_before[32];
    uint8_t hash_after[32];
    uint8_t *tick_zero_save = NULL;
    uint64_t tick_zero_save_size = UINT64_C(0);
    fd_world *history_world = NULL;
    fd_joint_decision decisions[3];
    uint32_t index;
    fd_diagnostic diag;
    output_header(&generation, sizeof(generation));
    output_header(&info, sizeof(info));
    (void)fd_diagnostic_init(&diag);
    check(tests, fd_world_generate(context, config, UINT64_C(42), &content,
                                   &world, &generation, &diag) == FD_OK,
          "action test world generates");
    if (world == NULL) {
        return;
    }
    check(tests, fd_world_get_info(world, &info, &diag) == FD_OK,
          "action test world info");
    output_header(&prefix, sizeof(prefix));
    output_header(&mask, sizeof(mask));
    mask.bits = NULL;
    mask.byte_capacity = UINT32_C(0);
    check(tests, fd_action_mask_query(world, info.foreign_faction_ids[0],
                                      &prefix, &mask, &diag) ==
                 FD_ERR_BUFFER_TOO_SMALL && mask.bit_count == UINT32_C(1) &&
                 mask.byte_capacity == UINT32_C(1),
          "exact mask negotiates one byte and one bit");
    mask.bits = &mask_byte;
    mask.byte_capacity = UINT32_C(1);
    check(tests, fd_action_mask_query(world, info.foreign_faction_ids[0],
                                      &prefix, &mask, &diag) == FD_OK &&
                 mask_byte == UINT8_C(1) && mask.first_value == FD_ACTION_PASS,
          "PASS is the only legal mask bit");
    prefix.parameters[3] = UINT32_C(1);
    check(tests, fd_action_mask_query(world, info.foreign_faction_ids[0],
                                      &prefix, &mask, &diag) ==
                 FD_ERR_INVALID_ARGUMENT && diag.item_index == UINT32_C(3),
          "empty-prefix mask rejects nonzero unused parameters");
    prefix.parameters[3] = UINT32_C(0);
    valid = pass_decision(&info);
    check(tests, fd_action_validate(world, info.foreign_faction_ids[0],
                                    &valid.seats[0].action, &diag) == FD_OK,
          "direct PASS validation succeeds");
    invalid = valid;
    invalid.seats[1].action.category = FD_ACTION_TRADE;
    check(tests, fd_action_validate(world, info.foreign_faction_ids[1],
                                    &invalid.seats[1].action, &diag) ==
                 FD_ERR_INVALID_ACTION,
          "direct non-PASS validation is rejected");
    (void)fd_world_state_hash(world, hash_before, NULL);
    output_header(&step, sizeof(step));
    check(tests, fd_world_step(world, &invalid, &step, &diag) ==
                 FD_ERR_INVALID_ACTION,
          "invalid joint action is rejected");
    (void)fd_world_state_hash(world, hash_after, NULL);
    check(tests, memcmp(hash_before, hash_after, 32U) == 0,
          "invalid joint action is transactionally hash-identical");
    output_header(&before, sizeof(before));
    output_header(&after, sizeof(after));
    (void)fd_context_get_allocation_stats(context, &before, NULL);
    check(tests, fd_world_step(world, &valid, &step, &diag) == FD_OK,
          "two-seat explicit PASS step succeeds");
    (void)fd_context_get_allocation_stats(context, &after, NULL);
    check(tests, step.tick_after == UINT64_C(24) && step.reward_count == UINT32_C(0) &&
                 step.phase_count == FD_U01_PHASE_COUNT &&
                 step.local_tick_count == (uint32_t)FD_OPERATIONAL_TICKS &&
                 step.phase_trace_count == FD_U01_PHASE_TRACE_COUNT &&
                 step.actor_trace_count == FD_U01_SEAT_COUNT &&
                 step.actor_trace[0] == info.foreign_faction_ids[0] &&
                 step.actor_trace[1] == info.foreign_faction_ids[1] &&
                 step.entity_trace_count == FD_U01_ENTITY_TRACE_COUNT &&
                 step.transition_staged == UINT32_C(1) &&
                 step.transition_stage_phase == UINT32_C(22) &&
                 step.state_hash_phase == UINT32_C(23) &&
                 step.transition_stage_trace_index == UINT32_C(550) &&
                 step.state_hash_trace_index == UINT32_C(551) &&
                 step.staged_tick_before == step.tick_before &&
                 step.staged_tick_after == step.tick_after &&
                 memcmp(step.staged_pre_state_sha256,
                        step.pre_state_sha256, 32U) == 0 &&
                 memcmp(step.staged_event_sha256,
                        step.event_sha256, 32U) == 0 &&
                 memcmp(step.finalized_post_state_sha256,
                        step.post_state_sha256, 32U) == 0,
          "PASS advances exactly 24 ticks through 23 stages with no reward");
    {
        uint32_t trace_index;
        int ordered = 1;
        uint8_t staged_decision_hash[32];
        for (trace_index = 0U; trace_index < FD_U01_PHASE_TRACE_COUNT;
             ++trace_index) {
            if (step.phase_trace[trace_index] !=
                trace_index % FD_U01_PHASE_COUNT + UINT32_C(1)) {
                ordered = 0;
                break;
            }
        }
        check(tests, ordered, "24-by-23 phase trace is canonical and complete");
        {
            uint32_t tick_index;
            uint32_t cursor = UINT32_C(0);
            int entity_ordered = 1;
            for (tick_index = 0U;
                 tick_index < (uint32_t)FD_OPERATIONAL_TICKS;
                 ++tick_index) {
                uint32_t entity_index;
                for (entity_index = 0U; entity_index < FD_U01_ENTITY_COUNT;
                     ++entity_index) {
                    const fd_entity_trace_entry *entry =
                        &step.entity_trace[cursor++];
                    if (entry->local_tick != tick_index ||
                        entry->phase_id != UINT32_C(1) ||
                        entry->entity_id != fd_make_entity_id(
                            entity_index + UINT32_C(1))) {
                        entity_ordered = 0;
                    }
                }
                if (tick_index == UINT32_C(0)) {
                    uint32_t seat;
                    for (seat = 0U; seat < FD_U01_SEAT_COUNT; ++seat) {
                        const fd_entity_trace_entry *entry =
                            &step.entity_trace[cursor++];
                        if (entry->local_tick != UINT32_C(0) ||
                            entry->phase_id != UINT32_C(2) ||
                            entry->entity_id != info.foreign_faction_ids[seat]) {
                            entity_ordered = 0;
                        }
                    }
                }
            }
            check(tests, entity_ordered &&
                         cursor == FD_U01_ENTITY_TRACE_COUNT,
                  "phase-associated entity trace logs exact ascending traversal");
        }
        fd_hash_joint_decision_internal(&step.staged_decision,
                                        staged_decision_hash);
        check(tests, memcmp(staged_decision_hash,
                            step.staged_decision_sha256, 32U) == 0 &&
                     step.staged_decision.expected_tick == valid.expected_tick &&
                     memcmp(step.staged_decision.expected_pre_state_sha256,
                            valid.expected_pre_state_sha256, 32U) == 0,
              "phase 22 stages the canonical joint-decision payload and digest");
    }
    check(tests, before.allocation_calls == after.allocation_calls &&
                 before.deallocation_calls == after.deallocation_calls,
          "ordinary reference step performs zero heap allocations");
    check(tests, fd_world_step(world, &valid, &step, &diag) == FD_ERR_INVALID_ACTION,
          "stale expected tick/hash decision is rejected");
    (void)fd_world_destroy(&world, NULL);

    check(tests, fd_world_generate(context, config, UINT64_C(42), &content,
                                   &world, &generation, &diag) == FD_OK,
          "replay tick-zero world generates");
    if (world == NULL) {
        return;
    }
    check(tests, save_world(world, &tick_zero_save, &tick_zero_save_size) == FD_OK,
          "replay tick-zero save created");
    check(tests, fd_world_load(context, tick_zero_save, tick_zero_save_size,
                               &history_world, &diag) == FD_OK,
          "history construction world loads");
    for (index = 0U; index < 3U && history_world != NULL; ++index) {
        output_header(&info, sizeof(info));
        (void)fd_world_get_info(history_world, &info, NULL);
        decisions[index] = pass_decision(&info);
        output_header(&step, sizeof(step));
        check(tests, fd_world_step(history_world, &decisions[index], &step, &diag) == FD_OK,
              "history PASS decision advances");
    }
    (void)fd_world_destroy(&history_world, NULL);
    {
        uint64_t replay_size = UINT64_C(0);
        uint64_t replay_written = UINT64_C(0);
        uint8_t *replay_bytes;
        fd_replay *replay = NULL;
        fd_world *replay_world = NULL;
        fd_replay_status status;
        check(tests, fd_replay_build_size(world, decisions, UINT64_C(3),
                                          &replay_size, &diag) == FD_OK,
              "replay size query succeeds");
        replay_bytes = (uint8_t *)malloc((size_t)replay_size);
        check(tests, replay_bytes != NULL, "replay buffer allocates");
        if (replay_bytes != NULL) {
            check(tests, fd_replay_build(world, decisions, UINT64_C(3), replay_bytes,
                                         replay_size, &replay_written, &diag) == FD_OK &&
                         replay_written == replay_size,
                  "replay build records all interval hashes");
            check(tests, fd_replay_open(context, replay_bytes, replay_size,
                                        &replay, &diag) == FD_OK,
                  "replay parser accepts canonical replay");
            check(tests, fd_replay_create_world(replay, &replay_world, &diag) == FD_OK,
                  "replay creates tick-zero world");
            output_header(&status, sizeof(status));
            check(tests, fd_replay_advance(replay, replay_world, UINT64_C(3),
                                           &status, &diag) == FD_OK &&
                         status.complete == UINT32_C(1) &&
                         status.cursor == UINT64_C(3),
                  "replay verifies every operational boundary");
            (void)fd_world_destroy(&replay_world, NULL);
            (void)fd_replay_destroy(&replay, NULL);
            {
                static const uint8_t suffix[4] = {
                    UINT8_C(0xca), UINT8_C(0xfe), UINT8_C(0xba), UINT8_C(0xbe)
                };
                uint64_t extended_size = UINT64_C(0);
                uint64_t future_size = UINT64_C(0);
                uint8_t *extended = extend_section(
                    replay_bytes, replay_size, UINT16_C(1), suffix,
                    UINT32_C(4), &extended_size);
                uint8_t *future = NULL;
                check(tests, extended != NULL,
                      "future replay manifest accepts optional field suffix");
                if (extended != NULL) {
                    future = append_optional_empty_section(
                        extended, extended_size, UINT16_C(6), &future_size);
                }
                check(tests, future != NULL,
                      "future replay accepts unknown optional section");
                if (future != NULL) {
                    uint64_t manifest_length = UINT64_C(0);
                    uint8_t *manifest = find_section(
                        future, future_size, UINT16_C(1), &manifest_length);
                    check(tests, manifest != NULL &&
                                 manifest_length >= UINT64_C(32),
                          "future replay manifest remains discoverable");
                    if (manifest != NULL && manifest_length >= UINT64_C(32)) {
                        fd_store_u16_le(manifest + UINT64_C(2), UINT16_C(1));
                        fd_store_u32_le(manifest + UINT64_C(8), UINT32_C(6));
                        check(tests, resign_container(
                                          future, future_size, UINT16_C(1),
                                          "FrontierDirectorate/Replay/Footer/v1"),
                              "future compatible replay is correctly re-signed");
                        check(tests, fd_replay_open(context, future, future_size,
                                                    &replay, &diag) == FD_OK &&
                                     replay != NULL,
                              "future minor replay with compatible reader, optional field, and optional section opens");
                        (void)fd_replay_destroy(&replay, NULL);
                    }
                }
                free(future);
                free(extended);
            }
            {
                uint8_t *forged = (uint8_t *)malloc((size_t)replay_size);
                check(tests, forged != NULL,
                      "accepted-transition forgery buffer allocates");
                if (forged != NULL) {
                    uint64_t records_length = UINT64_C(0);
                    uint8_t *records;
                    memcpy(forged, replay_bytes, (size_t)replay_size);
                    records = find_section(forged, replay_size, UINT16_C(3),
                                           &records_length);
                    if (records != NULL && records_length >= UINT64_C(236)) {
                        records[UINT64_C(8) + UINT64_C(192)] ^= UINT8_C(1);
                        (void)resign_container(
                            forged, replay_size, UINT16_C(3),
                            "FrontierDirectorate/Replay/Footer/v1");
                        diag.code = FD_ERR_BUSY;
                        diag.field_id = FD_FIELD_RESERVED;
                        diag.item_index = UINT32_MAX;
                        check(tests, fd_replay_open(context, forged, replay_size,
                                                    &replay, &diag) ==
                                         FD_ERR_CHECKSUM && replay == NULL &&
                                     diag.field_id == FD_FIELD_ACTION &&
                                     diag.item_index == UINT32_C(0),
                              "signed transition forgery reports fresh accepted-attempt provenance");
                    }
                    free(forged);
                }
            }
            {
                uint64_t records_length = UINT64_C(0);
                uint8_t *records = find_section(replay_bytes, replay_size,
                                                UINT16_C(3), &records_length);
                check(tests, records != NULL && records_length >= UINT64_C(236),
                      "replay transition section is discoverable");
                if (records != NULL && records_length >= UINT64_C(236)) {
                    fd_store_u32_le(records + UINT64_C(8) + UINT64_C(224),
                                    UINT32_C(1));
                    check(tests, resign_container(
                                      replay_bytes, replay_size, UINT16_C(3),
                                      "FrontierDirectorate/Replay/Footer/v1"),
                          "tampered replay can be correctly re-signed");
                }
            }
            check(tests, fd_replay_open(context, replay_bytes, replay_size,
                                        &replay, &diag) == FD_ERR_FORMAT &&
                         replay == NULL,
                  "nonzero U01 RNG audit count is rejected transactionally");
            free(replay_bytes);
        }
    }
    {
        fd_joint_decision attempts[4];
        uint64_t replay_size = UINT64_C(0);
        uint64_t replay_written = UINT64_C(0);
        uint8_t *replay_bytes = NULL;
        fd_replay *replay = NULL;
        fd_replay_info replay_info;
        fd_replay_record_view record;
        fd_replay_audit_view audit0;
        fd_replay_audit_view audit1;
        attempts[0] = decisions[0];
        attempts[1] = decisions[1];
        attempts[1].struct_size = UINT32_C(0);
        attempts[2] = decisions[1];
        attempts[2].seats[0].action.struct_size = UINT32_C(0);
        attempts[3] = decisions[1];
        check(tests, fd_replay_build_size(world, attempts, UINT64_C(4),
                                          &replay_size, &diag) == FD_OK,
              "replay sizes accepted and rejected submitted attempts");
        replay_bytes = (uint8_t *)malloc((size_t)replay_size);
        check(tests, replay_bytes != NULL, "audited replay buffer allocates");
        if (replay_bytes != NULL) {
            check(tests, fd_replay_build(world, attempts, UINT64_C(4),
                                         replay_bytes, replay_size,
                                         &replay_written, &diag) == FD_OK &&
                         replay_written == replay_size,
                  "replay records invalid attempts in its audit channel");
            check(tests, fd_replay_open(context, replay_bytes, replay_size,
                                        &replay, &diag) == FD_OK,
                  "semantic audit replay opens");
            output_header(&replay_info, sizeof(replay_info));
            output_header(&record, sizeof(record));
            output_header(&audit0, sizeof(audit0));
            output_header(&audit1, sizeof(audit1));
            check(tests, fd_replay_get_info(replay, &replay_info, &diag) == FD_OK &&
                         replay_info.decision_count == UINT64_C(2) &&
                         replay_info.audit_count == UINT32_C(2),
                  "replay info separates transitions from audit attempts");
            check(tests, fd_replay_get_record(replay, UINT64_C(0), &record,
                                              &diag) == FD_OK &&
                         record.rng_domain_count == UINT32_C(0),
                  "accepted transition explicitly proves zero RNG domains");
            check(tests, fd_replay_get_audit(replay, UINT32_C(0), &audit0,
                                             &diag) == FD_OK &&
                         audit0.attempt_index == UINT64_C(1) &&
                         audit0.tick == UINT64_C(24) &&
                         audit0.result == FD_ERR_INVALID_SIZE &&
                         audit0.decision.struct_size == UINT32_C(0),
                  "audit preserves malformed joint-decision header");
            check(tests, fd_replay_get_audit(replay, UINT32_C(1), &audit1,
                                             &diag) == FD_OK &&
                         audit1.attempt_index == UINT64_C(2) &&
                         audit1.result == FD_ERR_INVALID_SIZE &&
                         audit1.decision.seats[0].action.struct_size ==
                             UINT32_C(0),
                  "audit preserves malformed nested action header");
            (void)fd_replay_destroy(&replay, NULL);
            {
                uint64_t audit_length = UINT64_C(0);
                uint8_t *audit_payload = find_section(
                    replay_bytes, replay_size, UINT16_C(4), &audit_length);
                check(tests, audit_payload != NULL &&
                             audit_length >= UINT64_C(4) + UINT64_C(328),
                      "replay audit section is discoverable");
                if (audit_payload != NULL &&
                    audit_length >= UINT64_C(4) + UINT64_C(328)) {
                    fd_store_u32_le(audit_payload + UINT64_C(4) + UINT64_C(152),
                                    FD_ERR_INVALID_ACTION);
                    check(tests, resign_container(
                                      replay_bytes, replay_size, UINT16_C(4),
                                      "FrontierDirectorate/Replay/Footer/v1"),
                          "forged audit diagnostic can be correctly re-signed");
                    check(tests, fd_replay_open(context, replay_bytes,
                                                replay_size, &replay, &diag) ==
                                     FD_ERR_FORMAT && replay == NULL &&
                                 diag.field_id == FD_FIELD_ACTION &&
                                 diag.item_index == UINT32_C(1),
                          "semantic verifier rejects forged audit with exact attempt provenance");
                }
            }
            free(replay_bytes);
        }
    }
    free(tick_zero_save);
    (void)fd_world_destroy(&world, NULL);
}

int main(int argc, char **argv)
{
    uint64_t seeds = UINT64_C(32);
    uint64_t roundtrips = UINT64_C(8);
    int argument;
    test_state tests = {UINT64_C(0), UINT64_C(0)};
    fd_context_config context_config;
    fd_world_config world_config;
    fd_context *context = NULL;
    fd_diagnostic diag;
    fd_version version;
    uint64_t seed;
    for (argument = 1; argument + 1 < argc; argument += 2) {
        if (strcmp(argv[argument], "--seeds") == 0) {
            seeds = strtoull(argv[argument + 1], NULL, 10);
        } else if (strcmp(argv[argument], "--roundtrips") == 0) {
            roundtrips = strtoull(argv[argument + 1], NULL, 10);
        } else {
            (void)fprintf(stderr, "usage: %s [--seeds N] [--roundtrips N]\n",
                          argv[0]);
            return 2;
        }
    }
    test_vectors(&tests);
    memset(&version, 0, sizeof(version));
    version.struct_size = (uint32_t)sizeof(version);
    check(&tests, fd_get_version(&version) == FD_OK &&
                  version.api_minor == FD_API_VERSION_MINOR,
          "version query honors its non-ABI-tagged frozen struct");
    check(&tests, fd_context_config_init(&context_config) == FD_OK,
          "context config defaults initialize");
    check(&tests, fd_world_config_init(&world_config) == FD_OK,
          "world config defaults initialize");
    (void)fd_diagnostic_init(&diag);
    check(&tests, fd_context_create(&context_config, &context, &diag) == FD_OK,
          "context creates");
    if (context == NULL) {
        return 1;
    }
    {
        fd_world_config invalid = world_config;
        fd_content_manifest content = empty_content();
        fd_world *world = (fd_world *)(uintptr_t)UINTPTR_MAX;
        fd_generation_report report;
        invalid.width = FD_MIN_WIDTH - UINT32_C(1);
        check(&tests, fd_world_generate(context, &invalid, UINT64_C(1), &content,
                                        &world, NULL, &diag) == FD_ERR_OUT_OF_RANGE &&
                      world == NULL && diag.field_id == FD_FIELD_WIDTH,
              "invalid dimensions return structured error and no world");
        invalid = world_config;
        invalid.wetland_density_ppm = UINT32_C(0);
        world = (fd_world *)(uintptr_t)UINTPTR_MAX;
        output_header(&report, sizeof(report));
        check(&tests, fd_world_generate(context, &invalid, UINT64_C(1), &content,
                                        &world, &report, &diag) ==
                      FD_ERR_GENERATION_EXHAUSTED && world == NULL &&
                    report.rejected_count == invalid.attempt_budget &&
                    report.attempts[0].reason_code ==
                        FD_GEN_REJECT_REQUIRED_DENSITY_ZERO &&
                    diag.field_id == FD_FIELD_TERRAIN_DENSITY,
              "impossible valid density records its bounded failure reason");
    }
    test_constructive_fallback(&tests, context);
    test_save_future_continuation(&tests, context, &world_config);
    for (seed = 0U; seed < seeds; ++seed) {
        test_seed(&tests, context, &world_config, seed,
                  seed < roundtrips ? 1 : 0);
    }
    test_actions_replay(&tests, context, &world_config);
    {
        fd_allocation_stats stats;
        output_header(&stats, sizeof(stats));
        check(&tests, fd_context_get_allocation_stats(context, &stats, &diag) == FD_OK &&
                      stats.live_bytes == UINT64_C(0),
              "all C-core owned allocations are released");
    }
    check(&tests, fd_context_destroy(&context, &diag) == FD_OK && context == NULL,
          "context destroys and clears handle");
    (void)printf("checks=%" PRIu64 " failures=%" PRIu64
                 " seeds=%" PRIu64 " roundtrips=%" PRIu64 "\n",
                 tests.checks, tests.failures, seeds, roundtrips);
    return tests.failures == UINT64_C(0) ? 0 : 1;
}
