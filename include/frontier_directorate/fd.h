#ifndef FRONTIER_DIRECTORATE_FD_H
#define FRONTIER_DIRECTORATE_FD_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Update 01 public ABI.  Values are append-only within ABI major version 1. */
#define FD_ABI_VERSION UINT32_C(1)
#define FD_API_VERSION_MAJOR UINT16_C(0)
#define FD_API_VERSION_MINOR UINT16_C(1)
#define FD_API_VERSION_PATCH UINT16_C(0)
#define FD_GENERATOR_VERSION UINT32_C(1)
#define FD_RULESET_VERSION UINT32_C(1)
#define FD_RNG_REGISTRY_VERSION UINT32_C(1)
#define FD_SAVE_VERSION_MAJOR UINT16_C(1)
#define FD_SAVE_VERSION_MINOR UINT16_C(0)
#define FD_REPLAY_VERSION_MAJOR UINT16_C(1)
#define FD_REPLAY_VERSION_MINOR UINT16_C(0)

#define FD_MAX_CONTENT_PACKS UINT32_C(16)
#define FD_MAX_GENERATION_ATTEMPTS UINT32_C(64)
#define FD_MAX_NAME_BYTES UINT32_C(48)
#define FD_U01_SEAT_COUNT UINT32_C(2)
#define FD_U01_ENTITY_COUNT UINT32_C(6)
#define FD_U01_PHASE_COUNT UINT32_C(23)
#define FD_U01_PHASE_TRACE_COUNT UINT32_C(552)
#define FD_U01_ENTITY_TRACE_COUNT UINT32_C(146)
#define FD_OPERATIONAL_TICKS UINT64_C(24)

typedef struct fd_context fd_context;
typedef struct fd_world fd_world;
typedef struct fd_snapshot fd_snapshot;
typedef struct fd_replay fd_replay;

typedef uint64_t fd_entity_id;
typedef uint32_t fd_result;

#define FD_OK                       UINT32_C(0)
#define FD_ERR_INVALID_ARGUMENT     UINT32_C(1)
#define FD_ERR_INVALID_SIZE         UINT32_C(2)
#define FD_ERR_OUT_OF_RANGE         UINT32_C(3)
#define FD_ERR_CAPACITY             UINT32_C(4)
#define FD_ERR_OUT_OF_MEMORY        UINT32_C(5)
#define FD_ERR_GENERATION_EXHAUSTED UINT32_C(6)
#define FD_ERR_INVALID_ACTION       UINT32_C(7)
#define FD_ERR_BUFFER_TOO_SMALL     UINT32_C(8)
#define FD_ERR_FORMAT               UINT32_C(9)
#define FD_ERR_VERSION              UINT32_C(10)
#define FD_ERR_CHECKSUM             UINT32_C(11)
#define FD_ERR_STATE                UINT32_C(12)
#define FD_ERR_BUSY                 UINT32_C(13)
#define FD_ERR_INTERNAL             UINT32_C(14)

/* Stable diagnostic field identifiers. */
#define FD_FIELD_NONE                    UINT32_C(0)
#define FD_FIELD_STRUCT_SIZE             UINT32_C(1)
#define FD_FIELD_ABI_VERSION             UINT32_C(2)
#define FD_FIELD_WIDTH                   UINT32_C(3)
#define FD_FIELD_HEIGHT                  UINT32_C(4)
#define FD_FIELD_ATTEMPT_BUDGET          UINT32_C(5)
#define FD_FIELD_TERRAIN_DENSITY         UINT32_C(6)
#define FD_FIELD_CONTENT                 UINT32_C(7)
#define FD_FIELD_COORDINATE              UINT32_C(8)
#define FD_FIELD_ENTITY_ID               UINT32_C(9)
#define FD_FIELD_ACTION                  UINT32_C(10)
#define FD_FIELD_SEAT_COUNT              UINT32_C(11)
#define FD_FIELD_BUFFER_CAPACITY         UINT32_C(12)
#define FD_FIELD_FILE_BYTES              UINT32_C(13)
#define FD_FIELD_FORMAT_VERSION          UINT32_C(14)
#define FD_FIELD_SECTION                 UINT32_C(15)
#define FD_FIELD_TICK                    UINT32_C(16)
#define FD_FIELD_RESERVED                UINT32_C(17)
#define FD_FIELD_ASCII_SCREEN            UINT32_C(18)
#define FD_FIELD_ASCII_MESSAGE           UINT32_C(19)

typedef struct fd_diagnostic {
    uint32_t struct_size;
    uint32_t abi_version;
    fd_result code;
    uint32_t field_id;
    uint32_t item_index;
    char message[160];
} fd_diagnostic;

typedef struct fd_version {
    uint32_t struct_size;
    uint16_t api_major;
    uint16_t api_minor;
    uint16_t api_patch;
    uint16_t generator_major;
    uint16_t generator_minor;
    uint16_t save_major;
    uint16_t save_minor;
    uint16_t replay_major;
    uint16_t replay_minor;
} fd_version;

/* Allocator callbacks cross the C ABI and must never unwind or longjmp into the
 * core.  C++ callback types encode the non-throwing contract explicitly. */
#ifdef __cplusplus
typedef void *(*fd_allocate_fn)(void *user, uint64_t size) noexcept;
typedef void (*fd_deallocate_fn)(void *user, void *memory,
                                 uint64_t size) noexcept;
#else
typedef void *(*fd_allocate_fn)(void *user, uint64_t size);
typedef void (*fd_deallocate_fn)(void *user, void *memory, uint64_t size);
#endif

typedef struct fd_allocator {
    uint32_t struct_size;
    uint32_t abi_version;
    fd_allocate_fn allocate;
    fd_deallocate_fn deallocate;
    void *user;
} fd_allocator;

typedef struct fd_context_config {
    uint32_t struct_size;
    uint32_t abi_version;
    fd_allocator allocator;
    uint64_t max_file_bytes;
    uint64_t max_live_world_bytes;
    uint32_t reserved[8];
} fd_context_config;

typedef struct fd_allocation_stats {
    uint32_t struct_size;
    uint32_t abi_version;
    uint64_t allocation_calls;
    uint64_t deallocation_calls;
    uint64_t live_bytes;
    uint64_t peak_live_bytes;
} fd_allocation_stats;

typedef struct fd_world_config {
    uint32_t struct_size;
    uint32_t abi_version;
    uint32_t width;
    uint32_t height;
    uint32_t attempt_budget;
    uint32_t generator_version;
    uint32_t ruleset_version;
    uint32_t forest_density_ppm;
    uint32_t hill_density_ppm;
    uint32_t wetland_density_ppm;
    uint32_t reference_mode;
    uint32_t reserved[8];
} fd_world_config;

typedef struct fd_content_pack {
    uint64_t stable_id;
    uint8_t sha256[32];
} fd_content_pack;

typedef struct fd_content_manifest {
    uint32_t struct_size;
    uint32_t abi_version;
    uint32_t pack_count;
    uint32_t reserved;
    const fd_content_pack *packs;
} fd_content_manifest;

/* Stable complete-attempt generator failure reasons. */
#define FD_GEN_REJECT_NONE                  UINT32_C(0)
#define FD_GEN_REJECT_REQUIRED_DENSITY_ZERO UINT32_C(1)
#define FD_GEN_REJECT_TERRAIN_COVERAGE      UINT32_C(2)
#define FD_GEN_REJECT_HYDROLOGY             UINT32_C(3)
#define FD_GEN_REJECT_SETTLEMENT_PLACEMENT  UINT32_C(4)
#define FD_GEN_REJECT_ROAD_CORRIDOR         UINT32_C(5)
#define FD_GEN_REJECT_INVARIANT             UINT32_C(6)

typedef struct fd_generation_attempt {
    uint32_t attempt_index;
    uint32_t reason_code;
} fd_generation_attempt;

typedef struct fd_generation_report {
    uint32_t struct_size;
    uint32_t abi_version;
    uint32_t accepted_attempt;
    uint32_t rejected_count;
    uint32_t recorded_count;
    uint32_t land_tile_count;
    uint32_t river_tile_count;
    uint32_t road_tile_count;
    uint32_t river_raised_tile_count;
    uint32_t river_min_carve_cm;
    uint8_t root_seed_le[16];
    uint8_t configuration_sha256[32];
    uint8_t content_sha256[32];
    uint8_t initial_state_sha256[32];
    fd_generation_attempt attempts[FD_MAX_GENERATION_ATTEMPTS];
} fd_generation_report;

/* Terrain IDs are wire values.  River and road are overlays, not terrain. */
typedef uint32_t fd_terrain;
#define FD_TERRAIN_WATER      UINT32_C(1)
#define FD_TERRAIN_GRASSLAND  UINT32_C(2)
#define FD_TERRAIN_FOREST     UINT32_C(3)
#define FD_TERRAIN_HILLS      UINT32_C(4)
#define FD_TERRAIN_WETLANDS   UINT32_C(5)
#define FD_TERRAIN_SETTLEMENT UINT32_C(6)

#define FD_TILE_OVERLAY_RIVER         UINT32_C(0x00000001)
#define FD_TILE_OVERLAY_ROAD_CANDIDATE UINT32_C(0x00000002)
#define FD_TILE_RIVER_OUTLET           UINT32_C(0x00000004)
#define FD_TILE_ROAD_CROSSING          UINT32_C(0x00000008)

typedef uint32_t fd_entity_kind;
#define FD_ENTITY_REGION             UINT32_C(1)
#define FD_ENTITY_SETTLEMENT_COASTAL UINT32_C(2)
#define FD_ENTITY_SETTLEMENT_INLAND  UINT32_C(3)
#define FD_ENTITY_POLITY_LOCAL       UINT32_C(4)
#define FD_ENTITY_FACTION_FOREIGN    UINT32_C(5)

typedef struct fd_world_info {
    uint32_t struct_size;
    uint32_t abi_version;
    uint32_t width;
    uint32_t height;
    uint64_t tick;
    uint32_t generator_version;
    uint32_t ruleset_version;
    uint32_t rng_registry_version;
    uint32_t terrain_type_count;
    uint32_t entity_count;
    uint32_t settlement_count;
    uint32_t foreign_faction_count;
    uint32_t region_count;
    uint32_t river_tile_count;
    uint32_t road_tile_count;
    uint32_t content_pack_count;
    uint8_t root_seed_le[16];
    uint8_t configuration_sha256[32];
    uint8_t content_sha256[32];
    uint8_t state_sha256[32];
    fd_entity_id region_id;
    fd_entity_id local_polity_id;
    fd_entity_id coastal_settlement_id;
    fd_entity_id inland_settlement_id;
    fd_entity_id foreign_faction_ids[FD_U01_SEAT_COUNT];
} fd_world_info;

typedef struct fd_tile_view {
    uint32_t struct_size;
    uint32_t abi_version;
    uint64_t tile_id;
    uint32_t x;
    uint32_t y;
    fd_terrain terrain;
    uint32_t overlays;
    int32_t elevation_cm;
    uint32_t rainfall_mm;
    fd_entity_id region_id;
    fd_entity_id settlement_id;
    fd_entity_id polity_owner_id;
} fd_tile_view;

typedef struct fd_entity_view {
    uint32_t struct_size;
    uint32_t abi_version;
    fd_entity_id id;
    fd_entity_kind kind;
    uint32_t x;
    uint32_t y;
    fd_entity_id region_id;
    fd_entity_id owner_id;
    uint32_t name_length;
    char name[FD_MAX_NAME_BYTES];
} fd_entity_view;

typedef struct fd_region_view {
    uint32_t struct_size;
    uint32_t abi_version;
    fd_entity_id id;
    fd_entity_id political_owner_id;
    uint32_t settlement_count;
    uint32_t foreign_presence_count;
    fd_entity_id settlement_ids[2];
    fd_entity_id foreign_faction_ids[FD_U01_SEAT_COUNT];
    uint64_t river_outlet_tile_id;
    uint64_t road_start_tile_id;
    uint64_t road_end_tile_id;
    uint32_t river_tile_count;
    uint32_t road_tile_count;
} fd_region_view;

/* Validation bits are stable; all are required for a valid U01 world. */
#define FD_INVARIANT_BOUNDS                UINT64_C(0x0000000000000001)
#define FD_INVARIANT_LAND_COAST            UINT64_C(0x0000000000000002)
#define FD_INVARIANT_TERRAIN_COVERAGE      UINT64_C(0x0000000000000004)
#define FD_INVARIANT_HYDROLOGY             UINT64_C(0x0000000000000008)
#define FD_INVARIANT_SETTLEMENTS           UINT64_C(0x0000000000000010)
#define FD_INVARIANT_ROAD_CORRIDOR         UINT64_C(0x0000000000000020)
#define FD_INVARIANT_OWNERSHIP             UINT64_C(0x0000000000000040)
#define FD_INVARIANT_GRAPH_TILE_AGREEMENT  UINT64_C(0x0000000000000080)
#define FD_INVARIANT_OBJECTIVE_REACHABLE   UINT64_C(0x0000000000000100)
#define FD_INVARIANT_ALL                   UINT64_C(0x00000000000001ff)

typedef struct fd_validation_report {
    uint32_t struct_size;
    uint32_t abi_version;
    uint64_t passed_invariants;
    uint64_t failed_invariants;
    uint32_t land_tile_count;
    uint32_t water_tile_count;
    uint32_t river_tile_count;
    uint32_t road_tile_count;
    uint32_t road_river_crossings;
    uint32_t first_failed_tile_index;
} fd_validation_report;

typedef uint32_t fd_visibility_scope;
#define FD_VISIBILITY_REFERENCE UINT32_C(1)
#define FD_VISIBILITY_ACTOR     UINT32_C(2)

typedef uint32_t fd_action_category;
#define FD_ACTION_PASS           UINT32_C(1)
#define FD_ACTION_EXPEDITION     UINT32_C(2)
#define FD_ACTION_CONSTRUCTION   UINT32_C(3)
#define FD_ACTION_TRANSPORT      UINT32_C(4)
#define FD_ACTION_PRODUCTION     UINT32_C(5)
#define FD_ACTION_TRADE          UINT32_C(6)
#define FD_ACTION_DIPLOMACY      UINT32_C(7)
#define FD_ACTION_ADMINISTRATION UINT32_C(8)
#define FD_ACTION_INTELLIGENCE   UINT32_C(9)
#define FD_ACTION_MILITARY       UINT32_C(10)
#define FD_ACTION_RESEARCH       UINT32_C(11)
#define FD_ACTION_POLITICS       UINT32_C(12)

typedef struct fd_action_prefix {
    uint32_t struct_size;
    uint32_t abi_version;
    uint32_t depth;
    fd_action_category category;
    uint32_t parameter_count;
    uint32_t parameters[4];
} fd_action_prefix;

typedef struct fd_action_mask {
    uint32_t struct_size;
    uint32_t abi_version;
    uint32_t schema_id;
    uint32_t first_value;
    uint32_t bit_count;
    uint32_t byte_capacity;
    uint8_t *bits;
} fd_action_mask;

typedef struct fd_action {
    uint32_t struct_size;
    uint32_t abi_version;
    fd_action_category category;
    uint32_t parameter_count;
    uint32_t parameters[4];
} fd_action;

typedef struct fd_seat_decision {
    fd_entity_id actor_id;
    fd_action action;
} fd_seat_decision;

typedef struct fd_joint_decision {
    uint32_t struct_size;
    uint32_t abi_version;
    uint32_t seat_count;
    uint32_t reserved;
    uint64_t expected_tick;
    uint8_t expected_pre_state_sha256[32];
    fd_seat_decision seats[FD_U01_SEAT_COUNT];
} fd_joint_decision;

typedef struct fd_entity_trace_entry {
    uint32_t local_tick;
    uint32_t phase_id;
    fd_entity_id entity_id;
} fd_entity_trace_entry;

typedef struct fd_step_result {
    uint32_t struct_size;
    uint32_t abi_version;
    uint64_t tick_before;
    uint64_t tick_after;
    uint32_t accepted_action_count;
    uint32_t reward_count;
    uint32_t phase_count;
    uint32_t phase_ids[FD_U01_PHASE_COUNT];
    uint32_t local_tick_count;
    uint32_t phase_trace_count;
    uint32_t phase_trace[FD_U01_PHASE_TRACE_COUNT];
    uint32_t actor_trace_count;
    fd_entity_id actor_trace[FD_U01_SEAT_COUNT];
    uint32_t entity_trace_count;
    fd_entity_trace_entry entity_trace[FD_U01_ENTITY_TRACE_COUNT];
    uint32_t transition_staged;
    uint32_t transition_stage_phase;
    uint32_t state_hash_phase;
    uint32_t rng_domain_count;
    uint32_t transition_stage_trace_index;
    uint32_t state_hash_trace_index;
    uint64_t staged_tick_before;
    uint64_t staged_tick_after;
    fd_joint_decision staged_decision;
    uint8_t staged_pre_state_sha256[32];
    uint8_t staged_decision_sha256[32];
    uint8_t staged_event_sha256[32];
    uint8_t finalized_post_state_sha256[32];
    uint8_t pre_state_sha256[32];
    uint8_t post_state_sha256[32];
    uint8_t event_sha256[32];
} fd_step_result;

typedef struct fd_replay_info {
    uint32_t struct_size;
    uint32_t abi_version;
    uint64_t decision_count;
    uint64_t cursor;
    uint64_t tick_zero_save_bytes;
    uint32_t checkpoint_count;
    uint32_t audit_count;
    uint8_t tick_zero_state_sha256[32];
} fd_replay_info;

typedef struct fd_replay_audit_view {
    uint32_t struct_size;
    uint32_t abi_version;
    uint64_t attempt_index;
    uint64_t tick;
    fd_joint_decision decision;
    fd_result result;
    uint32_t field_id;
    uint32_t item_index;
    uint32_t message_length;
    char message[160];
} fd_replay_audit_view;

typedef struct fd_replay_record_view {
    uint32_t struct_size;
    uint32_t abi_version;
    uint64_t decision_index;
    uint64_t tick_before;
    uint64_t tick_after;
    fd_joint_decision decision;
    uint32_t rng_domain_count;
    uint32_t reserved;
    uint8_t pre_state_sha256[32];
    uint8_t post_state_sha256[32];
    uint8_t event_sha256[32];
} fd_replay_record_view;

typedef struct fd_replay_status {
    uint32_t struct_size;
    uint32_t abi_version;
    uint64_t cursor;
    uint64_t total_decisions;
    uint64_t decisions_advanced;
    uint32_t complete;
    uint32_t reserved;
    uint8_t state_sha256[32];
} fd_replay_status;

/* Normative no-color 7-bit ASCII renderer options. UI state is not authority. */
typedef uint32_t fd_ascii_screen;
#define FD_ASCII_SCREEN_LOCAL     UINT32_C(1)
#define FD_ASCII_SCREEN_STRATEGIC UINT32_C(2)
#define FD_ASCII_MAX_MESSAGE_BYTES UINT32_C(96)
#define FD_ASCII_MIN_COLUMNS UINT32_C(80)
#define FD_ASCII_MIN_ROWS    UINT32_C(24)
#define FD_ASCII_MAX_COLUMNS UINT32_C(512)
#define FD_ASCII_MAX_ROWS    UINT32_C(256)

typedef struct fd_ascii_options {
    uint32_t struct_size;
    uint32_t abi_version;
    uint32_t columns;
    uint32_t rows;
    fd_ascii_screen screen;
    uint32_t viewport_x;
    uint32_t viewport_y;
    uint32_t selected_x;
    uint32_t selected_y;
    uint32_t message_length;
    char message[FD_ASCII_MAX_MESSAGE_BYTES];
    uint32_t reserved[8];
} fd_ascii_options;

/* Initializers set ABI/version fields and documented defaults. */
fd_result fd_context_config_init(fd_context_config *out);
fd_result fd_world_config_init(fd_world_config *out);
fd_result fd_ascii_options_init(fd_ascii_options *out);
fd_result fd_diagnostic_init(fd_diagnostic *out);
fd_result fd_get_version(fd_version *out);

fd_result fd_context_create(const fd_context_config *config,
                            fd_context **out,
                            fd_diagnostic *diag);
fd_result fd_context_destroy(fd_context **context, fd_diagnostic *diag);
fd_result fd_context_get_allocation_stats(const fd_context *context,
                                          fd_allocation_stats *out,
                                          fd_diagnostic *diag);

fd_result fd_world_generate(fd_context *context,
                            const fd_world_config *config,
                            uint64_t seed,
                            const fd_content_manifest *content,
                            fd_world **out,
                            fd_generation_report *report,
                            fd_diagnostic *diag);
fd_result fd_world_destroy(fd_world **world, fd_diagnostic *diag);
fd_result fd_world_validate(const fd_world *world,
                            fd_validation_report *report,
                            fd_diagnostic *diag);
fd_result fd_world_get_info(const fd_world *world,
                            fd_world_info *out,
                            fd_diagnostic *diag);
fd_result fd_world_get_tile(const fd_world *world,
                            uint32_t x,
                            uint32_t y,
                            fd_tile_view *out,
                            fd_diagnostic *diag);
fd_result fd_world_get_entity(const fd_world *world,
                              fd_entity_id id,
                              fd_entity_view *out,
                              fd_diagnostic *diag);
fd_result fd_world_get_entity_by_index(const fd_world *world,
                                       uint32_t index,
                                       fd_entity_view *out,
                                       fd_diagnostic *diag);
fd_result fd_world_get_region(const fd_world *world,
                              fd_entity_id id,
                              fd_region_view *out,
                              fd_diagnostic *diag);
fd_result fd_world_state_hash(const fd_world *world,
                              uint8_t out_sha256[32],
                              fd_diagnostic *diag);

fd_result fd_snapshot_create(const fd_world *world,
                             fd_visibility_scope scope,
                             fd_entity_id viewer,
                             fd_snapshot **out,
                             fd_diagnostic *diag);
fd_result fd_snapshot_destroy(fd_snapshot **snapshot, fd_diagnostic *diag);
fd_result fd_snapshot_get_info(const fd_snapshot *snapshot,
                               fd_world_info *out,
                               fd_diagnostic *diag);
fd_result fd_snapshot_get_tile(const fd_snapshot *snapshot,
                               uint32_t x,
                               uint32_t y,
                               fd_tile_view *out,
                               fd_diagnostic *diag);
fd_result fd_snapshot_get_entity_by_index(const fd_snapshot *snapshot,
                                          uint32_t index,
                                          fd_entity_view *out,
                                          fd_diagnostic *diag);
fd_result fd_snapshot_get_region(const fd_snapshot *snapshot,
                                 fd_region_view *out,
                                 fd_diagnostic *diag);
fd_result fd_snapshot_state_hash(const fd_snapshot *snapshot,
                                 uint8_t out_sha256[32],
                                 fd_diagnostic *diag);

fd_result fd_action_mask_query(const fd_world *world,
                               fd_entity_id actor,
                               const fd_action_prefix *prefix,
                               fd_action_mask *out,
                               fd_diagnostic *diag);
fd_result fd_action_validate(const fd_world *world,
                             fd_entity_id actor,
                             const fd_action *action,
                             fd_diagnostic *diag);
fd_result fd_world_step(fd_world *world,
                        const fd_joint_decision *decision,
                        fd_step_result *out,
                        fd_diagnostic *diag);

fd_result fd_world_save_size(const fd_world *world,
                             uint64_t *out_size,
                             fd_diagnostic *diag);
fd_result fd_world_save(const fd_world *world,
                        void *buffer,
                        uint64_t capacity,
                        uint64_t *written,
                        fd_diagnostic *diag);
fd_result fd_world_load(fd_context *context,
                        const void *bytes,
                        uint64_t size,
                        fd_world **out,
                        fd_diagnostic *diag);

/* Replay construction re-simulates a tick-zero world and records every hash. */
fd_result fd_replay_build_size(const fd_world *tick_zero_world,
                               const fd_joint_decision *decisions,
                               uint64_t decision_count,
                               uint64_t *out_size,
                               fd_diagnostic *diag);
fd_result fd_replay_build(const fd_world *tick_zero_world,
                          const fd_joint_decision *decisions,
                          uint64_t decision_count,
                          void *buffer,
                          uint64_t capacity,
                          uint64_t *written,
                          fd_diagnostic *diag);
fd_result fd_replay_open(fd_context *context,
                         const void *bytes,
                         uint64_t size,
                         fd_replay **out,
                         fd_diagnostic *diag);
fd_result fd_replay_destroy(fd_replay **replay, fd_diagnostic *diag);
fd_result fd_replay_get_info(const fd_replay *replay,
                             fd_replay_info *out,
                             fd_diagnostic *diag);
fd_result fd_replay_get_record(const fd_replay *replay,
                               uint64_t index,
                               fd_replay_record_view *out,
                               fd_diagnostic *diag);
fd_result fd_replay_get_audit(const fd_replay *replay,
                              uint32_t index,
                              fd_replay_audit_view *out,
                              fd_diagnostic *diag);
fd_result fd_replay_create_world(const fd_replay *replay,
                                 fd_world **out,
                                 fd_diagnostic *diag);
fd_result fd_replay_advance(fd_replay *replay,
                            fd_world *world,
                            uint64_t decisions,
                            fd_replay_status *out,
                            fd_diagnostic *diag);

/* Measured/rendered sizes are text bytes, excluding any NUL terminator. */
fd_result fd_ascii_measure(const fd_snapshot *snapshot,
                           const fd_ascii_options *options,
                           uint64_t *out_size,
                           fd_diagnostic *diag);
fd_result fd_ascii_render(const fd_snapshot *snapshot,
                          const fd_ascii_options *options,
                          char *buffer,
                          uint64_t capacity,
                          uint64_t *written,
                          fd_diagnostic *diag);

/* Public primitives and pinned vectors make RNG/hash behavior directly testable. */
fd_result fd_sha256(const void *bytes,
                    uint64_t size,
                    uint8_t out_sha256[32]);
fd_result fd_philox4x32_10(const uint32_t counter[4],
                           const uint32_t key[2],
                           uint32_t out[4]);

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* FRONTIER_DIRECTORATE_FD_H */
