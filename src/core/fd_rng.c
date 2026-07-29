#include "fd_internal.h"

#include <string.h>

static uint64_t fd_mul32(uint32_t left, uint32_t right)
{
    return (uint64_t)left * (uint64_t)right;
}

fd_result fd_philox4x32_10(const uint32_t counter[4],
                           const uint32_t key[2],
                           uint32_t out[4])
{
    uint32_t value[4];
    uint32_t round_key[2];
    uint32_t round;

    if (counter == NULL || key == NULL || out == NULL) {
        return FD_ERR_INVALID_ARGUMENT;
    }
    memcpy(value, counter, sizeof(value));
    memcpy(round_key, key, sizeof(round_key));
    for (round = 0U; round < 10U; ++round) {
        uint64_t product0 = fd_mul32(UINT32_C(0xd2511f53), value[0]);
        uint64_t product1 = fd_mul32(UINT32_C(0xcd9e8d57), value[2]);
        uint32_t next[4];
        next[0] = (uint32_t)(product1 >> 32U) ^ value[1] ^ round_key[0];
        next[1] = (uint32_t)product1;
        next[2] = (uint32_t)(product0 >> 32U) ^ value[3] ^ round_key[1];
        next[3] = (uint32_t)product0;
        memcpy(value, next, sizeof(value));
        round_key[0] += UINT32_C(0x9e3779b9);
        round_key[1] += UINT32_C(0xbb67ae85);
    }
    memcpy(out, value, sizeof(value));
    return FD_OK;
}

void fd_rng_digest(const uint8_t root_seed_le[16],
                   uint32_t generator_version,
                   uint32_t ruleset_version,
                   uint32_t domain_id,
                   uint64_t subject_id,
                   uint64_t occurrence_id,
                   uint8_t digest[32])
{
    static const uint8_t literal[26] = "FrontierDirectorate/RNG/v1";
    fd_sha256_ctx_internal sha;
    uint8_t number[8];

    fd_sha256_init_internal(&sha);
    fd_store_u32_le(number, UINT32_C(26));
    fd_sha256_update_internal(&sha, number, UINT64_C(4));
    fd_sha256_update_internal(&sha, literal, UINT64_C(26));
    fd_store_u32_le(number, UINT32_C(16));
    fd_sha256_update_internal(&sha, number, UINT64_C(4));
    fd_sha256_update_internal(&sha, root_seed_le, UINT64_C(16));
    fd_store_u32_le(number, UINT32_C(4));
    fd_sha256_update_internal(&sha, number, UINT64_C(4));
    fd_store_u32_le(number, generator_version);
    fd_sha256_update_internal(&sha, number, UINT64_C(4));
    fd_store_u32_le(number, UINT32_C(4));
    fd_sha256_update_internal(&sha, number, UINT64_C(4));
    fd_store_u32_le(number, ruleset_version);
    fd_sha256_update_internal(&sha, number, UINT64_C(4));
    fd_store_u32_le(number, UINT32_C(4));
    fd_sha256_update_internal(&sha, number, UINT64_C(4));
    fd_store_u32_le(number, domain_id);
    fd_sha256_update_internal(&sha, number, UINT64_C(4));
    fd_store_u32_le(number, UINT32_C(8));
    fd_sha256_update_internal(&sha, number, UINT64_C(4));
    fd_store_u64_le(number, subject_id);
    fd_sha256_update_internal(&sha, number, UINT64_C(8));
    fd_store_u32_le(number, UINT32_C(8));
    fd_sha256_update_internal(&sha, number, UINT64_C(4));
    fd_store_u64_le(number, occurrence_id);
    fd_sha256_update_internal(&sha, number, UINT64_C(8));
    fd_sha256_final_internal(&sha, digest);
}

static uint32_t fd_rng_u32_retry(const uint8_t root_seed_le[16],
                                 uint32_t generator_version,
                                 uint32_t ruleset_version,
                                 uint32_t domain_id,
                                 uint64_t subject_id,
                                 uint64_t occurrence_id,
                                 uint64_t logical_sample_index,
                                 uint32_t draw_site_id,
                                 uint32_t retry_block,
                                 uint32_t lane)
{
    uint8_t digest[32];
    uint32_t key[2];
    uint32_t counter[4];
    uint32_t output[4];

    fd_rng_digest(root_seed_le, generator_version, ruleset_version, domain_id,
                  subject_id, occurrence_id, digest);
    key[0] = fd_load_u32_le(digest) ^ fd_load_u32_le(digest + 24U);
    key[1] = fd_load_u32_le(digest + 4U) ^ fd_load_u32_le(digest + 28U);
    counter[0] = fd_load_u32_le(digest + 8U) ^ (uint32_t)logical_sample_index;
    counter[1] = fd_load_u32_le(digest + 12U) ^
                 (uint32_t)(logical_sample_index >> 32U);
    counter[2] = fd_load_u32_le(digest + 16U) ^ draw_site_id;
    counter[3] = fd_load_u32_le(digest + 20U) ^ retry_block;
    (void)fd_philox4x32_10(counter, key, output);
    return output[lane];
}

uint32_t fd_rng_u32(const uint8_t root_seed_le[16],
                    uint32_t generator_version,
                    uint32_t ruleset_version,
                    uint32_t domain_id,
                    uint64_t subject_id,
                    uint64_t occurrence_id,
                    uint64_t logical_sample_index,
                    uint32_t draw_site_id)
{
    uint32_t lane = (uint32_t)(logical_sample_index & UINT64_C(3));
    return fd_rng_u32_retry(root_seed_le, generator_version, ruleset_version,
                            domain_id, subject_id, occurrence_id,
                            logical_sample_index, draw_site_id, UINT32_C(0), lane);
}

fd_result fd_rng_bounded(const uint8_t root_seed_le[16],
                         uint32_t generator_version,
                         uint32_t ruleset_version,
                         uint32_t domain_id,
                         uint64_t subject_id,
                         uint64_t occurrence_id,
                         uint64_t logical_sample_index,
                         uint32_t draw_site_id,
                         uint32_t upper_bound,
                         uint32_t *out)
{
    uint64_t range;
    uint32_t threshold;
    uint32_t block;

    if (out == NULL || upper_bound == UINT32_C(0)) {
        return FD_ERR_INVALID_ARGUMENT;
    }
    range = UINT64_C(1) << 32U;
    threshold = (uint32_t)(range % (uint64_t)upper_bound);
    for (block = 0U; block < 256U; ++block) {
        uint32_t lane;
        for (lane = 0U; lane < 4U; ++lane) {
            uint32_t value = fd_rng_u32_retry(root_seed_le,
                                              generator_version,
                                              ruleset_version,
                                              domain_id,
                                              subject_id,
                                              occurrence_id,
                                              logical_sample_index,
                                              draw_site_id,
                                              block,
                                              lane);
            if (value >= threshold) {
                *out = value % upper_bound;
                return FD_OK;
            }
        }
    }
    return FD_ERR_CAPACITY;
}
