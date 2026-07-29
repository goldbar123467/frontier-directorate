#ifndef FRONTIER_DIRECTORATE_FD_INTERNAL_H
#define FRONTIER_DIRECTORATE_FD_INTERNAL_H

#include "frontier_directorate/fd.h"

#include <stdatomic.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define FD_MIN_WIDTH UINT32_C(32)
#define FD_MAX_WIDTH UINT32_C(256)
#define FD_MIN_HEIGHT UINT32_C(16)
#define FD_MAX_HEIGHT UINT32_C(128)
#define FD_MAX_TILES UINT32_C(32768)
#define FD_DEFAULT_MAX_FILE_BYTES UINT64_C(67108864)
#define FD_DEFAULT_MAX_LIVE_WORLD_BYTES UINT64_C(4194304)
#define FD_SAVE_SECTION_COUNT UINT32_C(11)
#define FD_SECTION_CRITICAL UINT16_C(1)

/*
 * RNG semantic registry v1.  IDs are persistent format behavior.  Retired IDs
 * remain reserved forever.  A domain separates subsystems; a draw site
 * separates logical samples within one domain.
 */
typedef enum fd_rng_domain_id_internal {
    FD_RNG_DOMAIN_COAST = 1,
    FD_RNG_DOMAIN_ELEVATION_LATTICE = 2,
    FD_RNG_DOMAIN_RIVER_SOURCE = 3,
    FD_RNG_DOMAIN_RIVER_TIE_BREAK = 4,
    FD_RNG_DOMAIN_BIOME_ASSIGNMENT = 5,
    FD_RNG_DOMAIN_SETTLEMENT_PLACEMENT = 6,
    FD_RNG_DOMAIN_ROAD_CANDIDATE = 7,
    FD_RNG_DOMAIN_NAMING = 8,
    FD_RNG_DOMAIN_FACTION_INITIALIZATION = 9
} fd_rng_domain_id_internal;

typedef enum fd_rng_draw_site_id_internal {
    FD_RNG_SITE_COAST_FACING = 0x0101,
    FD_RNG_SITE_COAST_ROW_OFFSET = 0x0102,
    FD_RNG_SITE_ELEVATION_TILE = 0x0201,
    FD_RNG_SITE_RIVER_SOURCE_ROW = 0x0301,
    FD_RNG_SITE_RIVER_TIE = 0x0401,
    FD_RNG_SITE_BIOME_TILE = 0x0501,
    FD_RNG_SITE_SETTLEMENT_ROW = 0x0601,
    FD_RNG_SITE_ROAD_TIE = 0x0701,
    FD_RNG_SITE_NAMING_SYLLABLE = 0x0801,
    FD_RNG_SITE_FACTION_APTITUDE_IDENTITY = 0x0901
} fd_rng_draw_site_id_internal;

typedef struct fd_sha256_ctx_internal {
    uint32_t state[8];
    uint64_t total_size;
    uint8_t block[64];
    uint32_t block_size;
} fd_sha256_ctx_internal;

typedef struct fd_tile_internal {
    int32_t elevation_cm;
    uint32_t rainfall_mm;
    fd_entity_id settlement_id;
    uint32_t terrain;
    uint32_t overlays;
} fd_tile_internal;

typedef struct fd_entity_internal {
    fd_entity_id id;
    uint32_t kind;
    uint32_t x;
    uint32_t y;
    fd_entity_id region_id;
    fd_entity_id owner_id;
    uint32_t name_length;
    char name[FD_MAX_NAME_BYTES];
} fd_entity_internal;

typedef struct fd_region_internal {
    fd_entity_id id;
    fd_entity_id political_owner_id;
    fd_entity_id settlement_ids[2];
    fd_entity_id foreign_faction_ids[FD_U01_SEAT_COUNT];
    uint64_t river_outlet_tile_id;
    uint64_t road_start_tile_id;
    uint64_t road_end_tile_id;
    uint32_t river_tile_count;
    uint32_t road_tile_count;
} fd_region_internal;

struct fd_context {
    fd_allocate_fn allocate;
    fd_deallocate_fn deallocate;
    void *allocator_user;
    uint64_t max_file_bytes;
    uint64_t max_live_world_bytes;
    atomic_uint_least64_t allocation_calls;
    atomic_uint_least64_t deallocation_calls;
    atomic_uint_least64_t live_bytes;
    atomic_uint_least64_t peak_live_bytes;
    atomic_uint_least32_t live_handles;
};

struct fd_world {
    fd_context *context;
    fd_world_config config;
    uint8_t root_seed_le[16];
    uint32_t content_count;
    fd_content_pack content[FD_MAX_CONTENT_PACKS];
    uint8_t configuration_sha256[32];
    uint8_t content_sha256[32];
    uint64_t tick;
    uint32_t tile_count;
    fd_tile_internal *tiles;
    fd_entity_internal entities[FD_U01_ENTITY_COUNT];
    fd_region_internal region;
    uint32_t river_tile_count;
    uint32_t road_tile_count;
    uint32_t land_tile_count;
    uint32_t river_raised_tile_count;
    uint32_t river_min_carve_cm;
    uint32_t allocation_size;
    uint8_t state_sha256[32];
};

struct fd_snapshot {
    fd_context *context;
    fd_world_info info;
    fd_tile_internal *tiles;
    fd_entity_internal entities[FD_U01_ENTITY_COUNT];
    fd_region_internal region;
    uint32_t tile_allocation_size;
};

typedef struct fd_replay_record_internal {
    uint64_t tick_before;
    uint64_t tick_after;
    fd_joint_decision decision;
    uint32_t rng_domain_count;
    uint8_t pre_hash[32];
    uint8_t post_hash[32];
    uint8_t event_hash[32];
} fd_replay_record_internal;

typedef struct fd_replay_audit_internal {
    uint64_t attempt_index;
    uint64_t tick;
    fd_joint_decision decision;
    fd_result result;
    uint32_t field_id;
    uint32_t item_index;
    uint32_t message_length;
    char message[160];
} fd_replay_audit_internal;

struct fd_replay {
    fd_context *context;
    uint8_t *tick_zero_save;
    uint64_t tick_zero_save_size;
    fd_replay_record_internal *records;
    fd_replay_audit_internal *audits;
    uint64_t record_count;
    uint32_t audit_count;
    uint64_t cursor;
    uint32_t checkpoint_count;
    uint64_t tick_zero_allocation_size;
    uint64_t record_allocation_size;
    uint64_t audit_allocation_size;
    uint8_t tick_zero_hash[32];
};

void fd_sha256_init_internal(fd_sha256_ctx_internal *ctx);
void fd_sha256_update_internal(fd_sha256_ctx_internal *ctx,
                               const void *bytes,
                               uint64_t size);
void fd_sha256_final_internal(fd_sha256_ctx_internal *ctx, uint8_t out[32]);

fd_result fd_diag_set(fd_diagnostic *diag,
                      fd_result code,
                      uint32_t field_id,
                      uint32_t item_index,
                      const char *message);
fd_result fd_diag_ok(fd_diagnostic *diag);
bool fd_output_struct_valid(const void *out, uint32_t struct_size);
bool fd_input_struct_valid(const void *input, uint32_t struct_size);

void *fd_context_alloc(fd_context *context, uint64_t size);
void fd_context_free(fd_context *context, void *memory, uint64_t size);
bool fd_u64_add(uint64_t a, uint64_t b, uint64_t *out);
bool fd_u64_mul(uint64_t a, uint64_t b, uint64_t *out);

uint32_t fd_load_u32_le(const uint8_t *bytes);
uint64_t fd_load_u64_le(const uint8_t *bytes);
void fd_store_u16_le(uint8_t *bytes, uint16_t value);
void fd_store_u32_le(uint8_t *bytes, uint32_t value);
void fd_store_u64_le(uint8_t *bytes, uint64_t value);

void fd_rng_digest(const uint8_t root_seed_le[16],
                   uint32_t generator_version,
                   uint32_t ruleset_version,
                   uint32_t domain_id,
                   uint64_t subject_id,
                   uint64_t occurrence_id,
                   uint8_t digest[32]);
uint32_t fd_rng_u32(const uint8_t root_seed_le[16],
                    uint32_t generator_version,
                    uint32_t ruleset_version,
                    uint32_t domain_id,
                    uint64_t subject_id,
                    uint64_t occurrence_id,
                    uint64_t logical_sample_index,
                    uint32_t draw_site_id);
fd_result fd_rng_bounded(const uint8_t root_seed_le[16],
                         uint32_t generator_version,
                         uint32_t ruleset_version,
                         uint32_t domain_id,
                         uint64_t subject_id,
                         uint64_t occurrence_id,
                         uint64_t logical_sample_index,
                         uint32_t draw_site_id,
                         uint32_t upper_bound,
                         uint32_t *out);

fd_entity_id fd_make_entity_id(uint32_t slot);
uint64_t fd_tile_id_from_index(uint32_t index);
bool fd_entity_index_from_id(fd_entity_id id, uint32_t *out_index);

fd_result fd_world_compute_hash(fd_world *world);
void fd_hash_config_internal(const fd_world_config *config, uint8_t out[32]);
void fd_hash_content_internal(const fd_content_manifest *content, uint8_t out[32]);
void fd_hash_joint_decision_internal(const fd_joint_decision *decision,
                                     uint8_t out[32]);
fd_result fd_validate_generation_inputs_internal(
    const fd_world_config *config,
    const fd_content_manifest *content,
    fd_diagnostic *diag);
fd_result fd_world_validate_internal(const fd_world *world,
                                     fd_validation_report *report,
                                     fd_diagnostic *diag);
fd_result fd_world_clone_via_save(const fd_world *source,
                                  fd_world **out,
                                  fd_diagnostic *diag);

#endif /* FRONTIER_DIRECTORATE_FD_INTERNAL_H */
