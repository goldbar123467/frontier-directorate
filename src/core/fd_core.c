#include "fd_internal.h"

#include <stdlib.h>
#include <string.h>

static void *fd_default_allocate(void *user, uint64_t size)
{
    (void)user;
    if (size > (uint64_t)SIZE_MAX) {
        return NULL;
    }
    return malloc((size_t)size);
}

static void fd_default_deallocate(void *user, void *memory, uint64_t size)
{
    (void)user;
    (void)size;
    free(memory);
}

static bool fd_reserved_zero(const uint32_t *values, uint32_t count)
{
    uint32_t index;
    for (index = 0U; index < count; ++index) {
        if (values[index] != UINT32_C(0)) {
            return false;
        }
    }
    return true;
}

fd_result fd_diag_set(fd_diagnostic *diag,
                      fd_result code,
                      uint32_t field_id,
                      uint32_t item_index,
                      const char *message)
{
    if (diag != NULL && diag->struct_size >= (uint32_t)sizeof(*diag) &&
        diag->abi_version == FD_ABI_VERSION) {
        size_t length = message == NULL ? 0U : strlen(message);
        if (length >= sizeof(diag->message)) {
            length = sizeof(diag->message) - 1U;
        }
        diag->code = code;
        diag->field_id = field_id;
        diag->item_index = item_index;
        memset(diag->message, 0, sizeof(diag->message));
        if (length > 0U) {
            memcpy(diag->message, message, length);
        }
    }
    return code;
}

fd_result fd_diag_ok(fd_diagnostic *diag)
{
    return fd_diag_set(diag, FD_OK, FD_FIELD_NONE, UINT32_C(0), "ok");
}

bool fd_output_struct_valid(const void *out, uint32_t struct_size)
{
    uint32_t header[2];
    if (out == NULL) {
        return false;
    }
    memcpy(header, out, sizeof(header));
    return header[0] >= struct_size && header[1] == FD_ABI_VERSION;
}

bool fd_input_struct_valid(const void *input, uint32_t struct_size)
{
    return fd_output_struct_valid(input, struct_size);
}

bool fd_u64_add(uint64_t a, uint64_t b, uint64_t *out)
{
    if (out == NULL || UINT64_MAX - a < b) {
        return false;
    }
    *out = a + b;
    return true;
}

bool fd_u64_mul(uint64_t a, uint64_t b, uint64_t *out)
{
    if (out == NULL || (b != UINT64_C(0) && a > UINT64_MAX / b)) {
        return false;
    }
    *out = a * b;
    return true;
}

uint32_t fd_load_u32_le(const uint8_t *bytes)
{
    return (uint32_t)bytes[0] | ((uint32_t)bytes[1] << 8U) |
           ((uint32_t)bytes[2] << 16U) | ((uint32_t)bytes[3] << 24U);
}

uint64_t fd_load_u64_le(const uint8_t *bytes)
{
    return (uint64_t)fd_load_u32_le(bytes) |
           ((uint64_t)fd_load_u32_le(bytes + 4U) << 32U);
}

void fd_store_u16_le(uint8_t *bytes, uint16_t value)
{
    bytes[0] = (uint8_t)value;
    bytes[1] = (uint8_t)(value >> 8U);
}

void fd_store_u32_le(uint8_t *bytes, uint32_t value)
{
    bytes[0] = (uint8_t)value;
    bytes[1] = (uint8_t)(value >> 8U);
    bytes[2] = (uint8_t)(value >> 16U);
    bytes[3] = (uint8_t)(value >> 24U);
}

void fd_store_u64_le(uint8_t *bytes, uint64_t value)
{
    fd_store_u32_le(bytes, (uint32_t)value);
    fd_store_u32_le(bytes + 4U, (uint32_t)(value >> 32U));
}

void *fd_context_alloc(fd_context *context, uint64_t size)
{
    void *memory;
    uint64_t live;
    uint64_t peak;
    if (context == NULL || size == UINT64_C(0)) {
        return NULL;
    }
    memory = context->allocate(context->allocator_user, size);
    if (memory == NULL) {
        return NULL;
    }
    (void)atomic_fetch_add_explicit(&context->allocation_calls, UINT64_C(1),
                                    memory_order_relaxed);
    live = atomic_fetch_add_explicit(&context->live_bytes, size,
                                     memory_order_relaxed) + size;
    peak = atomic_load_explicit(&context->peak_live_bytes, memory_order_relaxed);
    while (live > peak &&
           !atomic_compare_exchange_weak_explicit(&context->peak_live_bytes,
                                                  &peak, live,
                                                  memory_order_relaxed,
                                                  memory_order_relaxed)) {
    }
    return memory;
}

void fd_context_free(fd_context *context, void *memory, uint64_t size)
{
    if (context == NULL || memory == NULL) {
        return;
    }
    context->deallocate(context->allocator_user, memory, size);
    (void)atomic_fetch_add_explicit(&context->deallocation_calls, UINT64_C(1),
                                    memory_order_relaxed);
    (void)atomic_fetch_sub_explicit(&context->live_bytes, size,
                                    memory_order_relaxed);
}

fd_entity_id fd_make_entity_id(uint32_t slot)
{
    return (UINT64_C(1) << 32U) | (uint64_t)slot;
}

uint64_t fd_tile_id_from_index(uint32_t index)
{
    return (uint64_t)index + UINT64_C(1);
}

bool fd_entity_index_from_id(fd_entity_id id, uint32_t *out_index)
{
    uint32_t generation = (uint32_t)(id >> 32U);
    uint32_t slot = (uint32_t)id;
    if (generation != UINT32_C(1) || slot == UINT32_C(0) ||
        slot > FD_U01_ENTITY_COUNT || out_index == NULL) {
        return false;
    }
    *out_index = slot - UINT32_C(1);
    return true;
}

fd_result fd_diagnostic_init(fd_diagnostic *out)
{
    if (out == NULL) {
        return FD_ERR_INVALID_ARGUMENT;
    }
    memset(out, 0, sizeof(*out));
    out->struct_size = (uint32_t)sizeof(*out);
    out->abi_version = FD_ABI_VERSION;
    out->code = FD_OK;
    memcpy(out->message, "ok", 3U);
    return FD_OK;
}

fd_result fd_context_config_init(fd_context_config *out)
{
    if (out == NULL) {
        return FD_ERR_INVALID_ARGUMENT;
    }
    memset(out, 0, sizeof(*out));
    out->struct_size = (uint32_t)sizeof(*out);
    out->abi_version = FD_ABI_VERSION;
    out->allocator.struct_size = (uint32_t)sizeof(out->allocator);
    out->allocator.abi_version = FD_ABI_VERSION;
    out->max_file_bytes = FD_DEFAULT_MAX_FILE_BYTES;
    out->max_live_world_bytes = FD_DEFAULT_MAX_LIVE_WORLD_BYTES;
    return FD_OK;
}

fd_result fd_world_config_init(fd_world_config *out)
{
    if (out == NULL) {
        return FD_ERR_INVALID_ARGUMENT;
    }
    memset(out, 0, sizeof(*out));
    out->struct_size = (uint32_t)sizeof(*out);
    out->abi_version = FD_ABI_VERSION;
    out->width = UINT32_C(48);
    out->height = UINT32_C(24);
    out->attempt_budget = FD_MAX_GENERATION_ATTEMPTS;
    out->generator_version = FD_GENERATOR_VERSION;
    out->ruleset_version = FD_RULESET_VERSION;
    out->forest_density_ppm = UINT32_C(220000);
    out->hill_density_ppm = UINT32_C(120000);
    out->wetland_density_ppm = UINT32_C(80000);
    out->reference_mode = UINT32_C(1);
    return FD_OK;
}

fd_result fd_get_version(fd_version *out)
{
    if (out == NULL || out->struct_size < (uint32_t)sizeof(*out)) {
        return FD_ERR_INVALID_SIZE;
    }
    out->api_major = FD_API_VERSION_MAJOR;
    out->api_minor = FD_API_VERSION_MINOR;
    out->api_patch = FD_API_VERSION_PATCH;
    out->generator_major = (uint16_t)FD_GENERATOR_VERSION;
    out->generator_minor = UINT16_C(0);
    out->save_major = FD_SAVE_VERSION_MAJOR;
    out->save_minor = FD_SAVE_VERSION_MINOR;
    out->replay_major = FD_REPLAY_VERSION_MAJOR;
    out->replay_minor = FD_REPLAY_VERSION_MINOR;
    return FD_OK;
}

fd_result fd_context_create(const fd_context_config *config,
                            fd_context **out,
                            fd_diagnostic *diag)
{
    fd_allocate_fn allocate;
    fd_deallocate_fn deallocate;
    void *user;
    fd_context *context;

    if (out == NULL) {
        return fd_diag_set(diag, FD_ERR_INVALID_ARGUMENT, FD_FIELD_NONE,
                           UINT32_C(0), "context output is null");
    }
    *out = NULL;
    if (!fd_input_struct_valid(config, (uint32_t)sizeof(*config))) {
        return fd_diag_set(diag, FD_ERR_INVALID_SIZE, FD_FIELD_STRUCT_SIZE,
                           UINT32_C(0), "invalid context configuration size or ABI");
    }
    if (!fd_input_struct_valid(&config->allocator,
                               (uint32_t)sizeof(config->allocator))) {
        return fd_diag_set(diag, FD_ERR_INVALID_SIZE, FD_FIELD_STRUCT_SIZE,
                           UINT32_C(0), "invalid allocator size or ABI");
    }
    if (!fd_reserved_zero(config->reserved, UINT32_C(8))) {
        return fd_diag_set(diag, FD_ERR_INVALID_ARGUMENT, FD_FIELD_RESERVED,
                           UINT32_C(0), "context reserved fields must be zero");
    }
    if ((config->allocator.allocate == NULL) !=
        (config->allocator.deallocate == NULL)) {
        return fd_diag_set(diag, FD_ERR_INVALID_ARGUMENT, FD_FIELD_NONE,
                           UINT32_C(0), "allocator callbacks must be supplied together");
    }
    if (config->max_file_bytes < UINT64_C(4096) ||
        config->max_live_world_bytes < UINT64_C(65536)) {
        return fd_diag_set(diag, FD_ERR_OUT_OF_RANGE, FD_FIELD_BUFFER_CAPACITY,
                           UINT32_C(0), "context byte limits are too small");
    }
    allocate = config->allocator.allocate != NULL ? config->allocator.allocate :
                                                    fd_default_allocate;
    deallocate = config->allocator.deallocate != NULL ? config->allocator.deallocate :
                                                        fd_default_deallocate;
    user = config->allocator.user;
    context = (fd_context *)allocate(user, (uint64_t)sizeof(*context));
    if (context == NULL) {
        return fd_diag_set(diag, FD_ERR_OUT_OF_MEMORY, FD_FIELD_NONE,
                           UINT32_C(0), "context allocation failed");
    }
    memset(context, 0, sizeof(*context));
    context->allocate = allocate;
    context->deallocate = deallocate;
    context->allocator_user = user;
    context->max_file_bytes = config->max_file_bytes;
    context->max_live_world_bytes = config->max_live_world_bytes;
    atomic_init(&context->allocation_calls, UINT64_C(0));
    atomic_init(&context->deallocation_calls, UINT64_C(0));
    atomic_init(&context->live_bytes, UINT64_C(0));
    atomic_init(&context->peak_live_bytes, UINT64_C(0));
    atomic_init(&context->live_handles, UINT32_C(0));
    *out = context;
    return fd_diag_ok(diag);
}

fd_result fd_context_destroy(fd_context **context_ptr, fd_diagnostic *diag)
{
    fd_context *context;
    fd_deallocate_fn deallocate;
    void *user;
    if (context_ptr == NULL) {
        return fd_diag_set(diag, FD_ERR_INVALID_ARGUMENT, FD_FIELD_NONE,
                           UINT32_C(0), "context pointer is null");
    }
    context = *context_ptr;
    if (context == NULL) {
        return fd_diag_ok(diag);
    }
    if (atomic_load_explicit(&context->live_handles, memory_order_acquire) !=
        UINT32_C(0)) {
        return fd_diag_set(diag, FD_ERR_BUSY, FD_FIELD_NONE, UINT32_C(0),
                           "context still owns live handles");
    }
    deallocate = context->deallocate;
    user = context->allocator_user;
    *context_ptr = NULL;
    deallocate(user, context, (uint64_t)sizeof(*context));
    return fd_diag_ok(diag);
}

fd_result fd_context_get_allocation_stats(const fd_context *context,
                                          fd_allocation_stats *out,
                                          fd_diagnostic *diag)
{
    if (context == NULL) {
        return fd_diag_set(diag, FD_ERR_INVALID_ARGUMENT, FD_FIELD_NONE,
                           UINT32_C(0), "context is null");
    }
    if (!fd_output_struct_valid(out, (uint32_t)sizeof(*out))) {
        return fd_diag_set(diag, FD_ERR_INVALID_SIZE, FD_FIELD_STRUCT_SIZE,
                           UINT32_C(0), "invalid allocation stats output");
    }
    out->allocation_calls = atomic_load_explicit(&context->allocation_calls,
                                                  memory_order_relaxed);
    out->deallocation_calls = atomic_load_explicit(&context->deallocation_calls,
                                                    memory_order_relaxed);
    out->live_bytes = atomic_load_explicit(&context->live_bytes,
                                            memory_order_relaxed);
    out->peak_live_bytes = atomic_load_explicit(&context->peak_live_bytes,
                                                 memory_order_relaxed);
    return fd_diag_ok(diag);
}

static void fd_hash_u32(fd_sha256_ctx_internal *sha, uint32_t value)
{
    uint8_t bytes[4];
    fd_store_u32_le(bytes, value);
    fd_sha256_update_internal(sha, bytes, UINT64_C(4));
}

static void fd_hash_u64(fd_sha256_ctx_internal *sha, uint64_t value)
{
    uint8_t bytes[8];
    fd_store_u64_le(bytes, value);
    fd_sha256_update_internal(sha, bytes, UINT64_C(8));
}

fd_result fd_world_compute_hash(fd_world *world)
{
    static const uint8_t domain[] = "FrontierDirectorate/State/v1";
    fd_sha256_ctx_internal sha;
    uint32_t index;
    if (world == NULL || world->tiles == NULL) {
        return FD_ERR_INVALID_ARGUMENT;
    }
    fd_sha256_init_internal(&sha);
    fd_hash_u32(&sha, (uint32_t)(sizeof(domain) - 1U));
    fd_sha256_update_internal(&sha, domain, (uint64_t)(sizeof(domain) - 1U));
    fd_hash_u32(&sha, world->config.generator_version);
    fd_hash_u32(&sha, world->config.ruleset_version);
    fd_hash_u32(&sha, FD_RNG_REGISTRY_VERSION);
    fd_hash_u32(&sha, world->config.width);
    fd_hash_u32(&sha, world->config.height);
    fd_hash_u32(&sha, world->config.attempt_budget);
    fd_hash_u32(&sha, world->config.forest_density_ppm);
    fd_hash_u32(&sha, world->config.hill_density_ppm);
    fd_hash_u32(&sha, world->config.wetland_density_ppm);
    fd_hash_u32(&sha, world->config.reference_mode);
    fd_sha256_update_internal(&sha, world->root_seed_le, UINT64_C(16));
    fd_hash_u32(&sha, world->content_count);
    for (index = 0U; index < world->content_count; ++index) {
        fd_hash_u64(&sha, world->content[index].stable_id);
        fd_sha256_update_internal(&sha, world->content[index].sha256,
                                  UINT64_C(32));
    }
    fd_hash_u64(&sha, world->tick);
    fd_hash_u32(&sha, world->tile_count);
    for (index = 0U; index < world->tile_count; ++index) {
        const fd_tile_internal *tile = &world->tiles[index];
        fd_hash_u32(&sha, tile->terrain);
        fd_hash_u32(&sha, tile->overlays);
        fd_hash_u32(&sha, (uint32_t)tile->elevation_cm);
        fd_hash_u32(&sha, tile->rainfall_mm);
        fd_hash_u64(&sha, tile->settlement_id);
    }
    fd_hash_u32(&sha, FD_U01_ENTITY_COUNT);
    for (index = 0U; index < FD_U01_ENTITY_COUNT; ++index) {
        const fd_entity_internal *entity = &world->entities[index];
        fd_hash_u64(&sha, entity->id);
        fd_hash_u32(&sha, entity->kind);
        fd_hash_u32(&sha, entity->x);
        fd_hash_u32(&sha, entity->y);
        fd_hash_u64(&sha, entity->region_id);
        fd_hash_u64(&sha, entity->owner_id);
        fd_hash_u32(&sha, entity->name_length);
        fd_sha256_update_internal(&sha, entity->name,
                                  (uint64_t)entity->name_length);
    }
    fd_hash_u64(&sha, world->region.id);
    fd_hash_u64(&sha, world->region.political_owner_id);
    for (index = 0U; index < 2U; ++index) {
        fd_hash_u64(&sha, world->region.settlement_ids[index]);
    }
    for (index = 0U; index < FD_U01_SEAT_COUNT; ++index) {
        fd_hash_u64(&sha, world->region.foreign_faction_ids[index]);
    }
    fd_hash_u64(&sha, world->region.river_outlet_tile_id);
    fd_hash_u64(&sha, world->region.road_start_tile_id);
    fd_hash_u64(&sha, world->region.road_end_tile_id);
    fd_hash_u32(&sha, world->region.river_tile_count);
    fd_hash_u32(&sha, world->region.road_tile_count);
    fd_sha256_final_internal(&sha, world->state_sha256);
    return FD_OK;
}

static void fd_fill_world_info(const fd_world *world, fd_world_info *out)
{
    out->width = world->config.width;
    out->height = world->config.height;
    out->tick = world->tick;
    out->generator_version = world->config.generator_version;
    out->ruleset_version = world->config.ruleset_version;
    out->rng_registry_version = FD_RNG_REGISTRY_VERSION;
    out->terrain_type_count = UINT32_C(6);
    out->entity_count = FD_U01_ENTITY_COUNT;
    out->settlement_count = UINT32_C(2);
    out->foreign_faction_count = FD_U01_SEAT_COUNT;
    out->region_count = UINT32_C(1);
    out->river_tile_count = world->river_tile_count;
    out->road_tile_count = world->road_tile_count;
    out->content_pack_count = world->content_count;
    memcpy(out->root_seed_le, world->root_seed_le, sizeof(out->root_seed_le));
    memcpy(out->configuration_sha256, world->configuration_sha256,
           sizeof(out->configuration_sha256));
    memcpy(out->content_sha256, world->content_sha256,
           sizeof(out->content_sha256));
    memcpy(out->state_sha256, world->state_sha256, sizeof(out->state_sha256));
    out->region_id = world->region.id;
    out->local_polity_id = world->entities[3].id;
    out->coastal_settlement_id = world->entities[1].id;
    out->inland_settlement_id = world->entities[2].id;
    out->foreign_faction_ids[0] = world->entities[4].id;
    out->foreign_faction_ids[1] = world->entities[5].id;
}

static void fd_fill_entity_view(const fd_entity_internal *entity,
                                fd_entity_view *out)
{
    out->id = entity->id;
    out->kind = entity->kind;
    out->x = entity->x;
    out->y = entity->y;
    out->region_id = entity->region_id;
    out->owner_id = entity->owner_id;
    out->name_length = entity->name_length;
    memset(out->name, 0, sizeof(out->name));
    memcpy(out->name, entity->name, (size_t)entity->name_length);
}

static void fd_fill_region_view(const fd_region_internal *region,
                                fd_region_view *out)
{
    out->id = region->id;
    out->political_owner_id = region->political_owner_id;
    out->settlement_count = UINT32_C(2);
    out->foreign_presence_count = FD_U01_SEAT_COUNT;
    memcpy(out->settlement_ids, region->settlement_ids,
           sizeof(out->settlement_ids));
    memcpy(out->foreign_faction_ids, region->foreign_faction_ids,
           sizeof(out->foreign_faction_ids));
    out->river_outlet_tile_id = region->river_outlet_tile_id;
    out->road_start_tile_id = region->road_start_tile_id;
    out->road_end_tile_id = region->road_end_tile_id;
    out->river_tile_count = region->river_tile_count;
    out->road_tile_count = region->road_tile_count;
}

fd_result fd_world_destroy(fd_world **world_ptr, fd_diagnostic *diag)
{
    fd_world *world;
    fd_context *context;
    if (world_ptr == NULL) {
        return fd_diag_set(diag, FD_ERR_INVALID_ARGUMENT, FD_FIELD_NONE,
                           UINT32_C(0), "world pointer is null");
    }
    world = *world_ptr;
    if (world == NULL) {
        return fd_diag_ok(diag);
    }
    context = world->context;
    if (world->tiles != NULL) {
        fd_context_free(context, world->tiles,
                        (uint64_t)world->tile_count *
                        (uint64_t)sizeof(*world->tiles));
    }
    *world_ptr = NULL;
    fd_context_free(context, world, (uint64_t)sizeof(*world));
    (void)atomic_fetch_sub_explicit(&context->live_handles, UINT32_C(1),
                                    memory_order_release);
    return fd_diag_ok(diag);
}

fd_result fd_world_get_info(const fd_world *world,
                            fd_world_info *out,
                            fd_diagnostic *diag)
{
    if (world == NULL) {
        return fd_diag_set(diag, FD_ERR_INVALID_ARGUMENT, FD_FIELD_NONE,
                           UINT32_C(0), "world is null");
    }
    if (!fd_output_struct_valid(out, (uint32_t)sizeof(*out))) {
        return fd_diag_set(diag, FD_ERR_INVALID_SIZE, FD_FIELD_STRUCT_SIZE,
                           UINT32_C(0), "invalid world info output");
    }
    fd_fill_world_info(world, out);
    return fd_diag_ok(diag);
}

fd_result fd_world_get_tile(const fd_world *world,
                            uint32_t x,
                            uint32_t y,
                            fd_tile_view *out,
                            fd_diagnostic *diag)
{
    uint32_t index;
    const fd_tile_internal *tile;
    if (world == NULL) {
        return fd_diag_set(diag, FD_ERR_INVALID_ARGUMENT, FD_FIELD_NONE,
                           UINT32_C(0), "world is null");
    }
    if (!fd_output_struct_valid(out, (uint32_t)sizeof(*out))) {
        return fd_diag_set(diag, FD_ERR_INVALID_SIZE, FD_FIELD_STRUCT_SIZE,
                           UINT32_C(0), "invalid tile view output");
    }
    if (x >= world->config.width || y >= world->config.height) {
        return fd_diag_set(diag, FD_ERR_OUT_OF_RANGE, FD_FIELD_COORDINATE,
                           y, "tile coordinate is outside the world");
    }
    index = y * world->config.width + x;
    tile = &world->tiles[index];
    out->tile_id = fd_tile_id_from_index(index);
    out->x = x;
    out->y = y;
    out->terrain = tile->terrain;
    out->overlays = tile->overlays;
    out->elevation_cm = tile->elevation_cm;
    out->rainfall_mm = tile->rainfall_mm;
    out->region_id = world->region.id;
    out->settlement_id = tile->settlement_id;
    out->polity_owner_id = tile->terrain == FD_TERRAIN_WATER ?
                           UINT64_C(0) : world->region.political_owner_id;
    return fd_diag_ok(diag);
}

fd_result fd_world_get_entity(const fd_world *world,
                              fd_entity_id id,
                              fd_entity_view *out,
                              fd_diagnostic *diag)
{
    uint32_t index;
    if (world == NULL) {
        return fd_diag_set(diag, FD_ERR_INVALID_ARGUMENT, FD_FIELD_NONE,
                           UINT32_C(0), "world is null");
    }
    if (!fd_output_struct_valid(out, (uint32_t)sizeof(*out))) {
        return fd_diag_set(diag, FD_ERR_INVALID_SIZE, FD_FIELD_STRUCT_SIZE,
                           UINT32_C(0), "invalid entity view output");
    }
    if (!fd_entity_index_from_id(id, &index) ||
        world->entities[index].id != id) {
        return fd_diag_set(diag, FD_ERR_OUT_OF_RANGE, FD_FIELD_ENTITY_ID,
                           UINT32_C(0), "entity ID is invalid or stale");
    }
    fd_fill_entity_view(&world->entities[index], out);
    return fd_diag_ok(diag);
}

fd_result fd_world_get_entity_by_index(const fd_world *world,
                                       uint32_t index,
                                       fd_entity_view *out,
                                       fd_diagnostic *diag)
{
    if (world == NULL) {
        return fd_diag_set(diag, FD_ERR_INVALID_ARGUMENT, FD_FIELD_NONE,
                           UINT32_C(0), "world is null");
    }
    if (!fd_output_struct_valid(out, (uint32_t)sizeof(*out))) {
        return fd_diag_set(diag, FD_ERR_INVALID_SIZE, FD_FIELD_STRUCT_SIZE,
                           UINT32_C(0), "invalid entity view output");
    }
    if (index >= FD_U01_ENTITY_COUNT) {
        return fd_diag_set(diag, FD_ERR_OUT_OF_RANGE, FD_FIELD_ENTITY_ID,
                           index, "entity index is outside the table");
    }
    fd_fill_entity_view(&world->entities[index], out);
    return fd_diag_ok(diag);
}

fd_result fd_world_get_region(const fd_world *world,
                              fd_entity_id id,
                              fd_region_view *out,
                              fd_diagnostic *diag)
{
    if (world == NULL) {
        return fd_diag_set(diag, FD_ERR_INVALID_ARGUMENT, FD_FIELD_NONE,
                           UINT32_C(0), "world is null");
    }
    if (!fd_output_struct_valid(out, (uint32_t)sizeof(*out))) {
        return fd_diag_set(diag, FD_ERR_INVALID_SIZE, FD_FIELD_STRUCT_SIZE,
                           UINT32_C(0), "invalid region view output");
    }
    if (id != world->region.id) {
        return fd_diag_set(diag, FD_ERR_OUT_OF_RANGE, FD_FIELD_ENTITY_ID,
                           UINT32_C(0), "region ID is invalid or stale");
    }
    fd_fill_region_view(&world->region, out);
    return fd_diag_ok(diag);
}

fd_result fd_world_state_hash(const fd_world *world,
                              uint8_t out_sha256[32],
                              fd_diagnostic *diag)
{
    if (world == NULL || out_sha256 == NULL) {
        return fd_diag_set(diag, FD_ERR_INVALID_ARGUMENT, FD_FIELD_NONE,
                           UINT32_C(0), "world or hash output is null");
    }
    memcpy(out_sha256, world->state_sha256, 32U);
    return fd_diag_ok(diag);
}

fd_result fd_snapshot_create(const fd_world *world,
                             fd_visibility_scope scope,
                             fd_entity_id viewer,
                             fd_snapshot **out,
                             fd_diagnostic *diag)
{
    fd_snapshot *snapshot;
    uint64_t tile_bytes;
    if (out == NULL) {
        return fd_diag_set(diag, FD_ERR_INVALID_ARGUMENT, FD_FIELD_NONE,
                           UINT32_C(0), "snapshot output is null");
    }
    *out = NULL;
    if (world == NULL) {
        return fd_diag_set(diag, FD_ERR_INVALID_ARGUMENT, FD_FIELD_NONE,
                           UINT32_C(0), "world is null");
    }
    if (scope != FD_VISIBILITY_REFERENCE || viewer != UINT64_C(0)) {
        return fd_diag_set(diag, FD_ERR_OUT_OF_RANGE, FD_FIELD_ENTITY_ID,
                           UINT32_C(0), "U01 supports only viewer-zero reference snapshots");
    }
    tile_bytes = (uint64_t)world->tile_count * (uint64_t)sizeof(*world->tiles);
    snapshot = (fd_snapshot *)fd_context_alloc(world->context,
                                               (uint64_t)sizeof(*snapshot));
    if (snapshot == NULL) {
        return fd_diag_set(diag, FD_ERR_OUT_OF_MEMORY, FD_FIELD_NONE,
                           UINT32_C(0), "snapshot allocation failed");
    }
    memset(snapshot, 0, sizeof(*snapshot));
    snapshot->tiles = (fd_tile_internal *)fd_context_alloc(world->context,
                                                            tile_bytes);
    if (snapshot->tiles == NULL) {
        fd_context_free(world->context, snapshot, (uint64_t)sizeof(*snapshot));
        return fd_diag_set(diag, FD_ERR_OUT_OF_MEMORY, FD_FIELD_NONE,
                           UINT32_C(0), "snapshot tile allocation failed");
    }
    snapshot->context = world->context;
    snapshot->tile_allocation_size = (uint32_t)tile_bytes;
    snapshot->info.struct_size = (uint32_t)sizeof(snapshot->info);
    snapshot->info.abi_version = FD_ABI_VERSION;
    fd_fill_world_info(world, &snapshot->info);
    memcpy(snapshot->tiles, world->tiles, (size_t)tile_bytes);
    memcpy(snapshot->entities, world->entities, sizeof(snapshot->entities));
    snapshot->region = world->region;
    (void)atomic_fetch_add_explicit(&world->context->live_handles, UINT32_C(1),
                                    memory_order_release);
    *out = snapshot;
    return fd_diag_ok(diag);
}

fd_result fd_snapshot_destroy(fd_snapshot **snapshot_ptr, fd_diagnostic *diag)
{
    fd_snapshot *snapshot;
    fd_context *context;
    if (snapshot_ptr == NULL) {
        return fd_diag_set(diag, FD_ERR_INVALID_ARGUMENT, FD_FIELD_NONE,
                           UINT32_C(0), "snapshot pointer is null");
    }
    snapshot = *snapshot_ptr;
    if (snapshot == NULL) {
        return fd_diag_ok(diag);
    }
    context = snapshot->context;
    fd_context_free(context, snapshot->tiles,
                    (uint64_t)snapshot->tile_allocation_size);
    *snapshot_ptr = NULL;
    fd_context_free(context, snapshot, (uint64_t)sizeof(*snapshot));
    (void)atomic_fetch_sub_explicit(&context->live_handles, UINT32_C(1),
                                    memory_order_release);
    return fd_diag_ok(diag);
}

fd_result fd_snapshot_get_info(const fd_snapshot *snapshot,
                               fd_world_info *out,
                               fd_diagnostic *diag)
{
    if (snapshot == NULL) {
        return fd_diag_set(diag, FD_ERR_INVALID_ARGUMENT, FD_FIELD_NONE,
                           UINT32_C(0), "snapshot is null");
    }
    if (!fd_output_struct_valid(out, (uint32_t)sizeof(*out))) {
        return fd_diag_set(diag, FD_ERR_INVALID_SIZE, FD_FIELD_STRUCT_SIZE,
                           UINT32_C(0), "invalid snapshot info output");
    }
    memcpy(out, &snapshot->info, sizeof(*out));
    return fd_diag_ok(diag);
}

fd_result fd_snapshot_get_tile(const fd_snapshot *snapshot,
                               uint32_t x,
                               uint32_t y,
                               fd_tile_view *out,
                               fd_diagnostic *diag)
{
    uint32_t index;
    const fd_tile_internal *tile;
    if (snapshot == NULL) {
        return fd_diag_set(diag, FD_ERR_INVALID_ARGUMENT, FD_FIELD_NONE,
                           UINT32_C(0), "snapshot is null");
    }
    if (!fd_output_struct_valid(out, (uint32_t)sizeof(*out))) {
        return fd_diag_set(diag, FD_ERR_INVALID_SIZE, FD_FIELD_STRUCT_SIZE,
                           UINT32_C(0), "invalid tile view output");
    }
    if (x >= snapshot->info.width || y >= snapshot->info.height) {
        return fd_diag_set(diag, FD_ERR_OUT_OF_RANGE, FD_FIELD_COORDINATE,
                           y, "tile coordinate is outside the snapshot");
    }
    index = y * snapshot->info.width + x;
    tile = &snapshot->tiles[index];
    out->tile_id = fd_tile_id_from_index(index);
    out->x = x;
    out->y = y;
    out->terrain = tile->terrain;
    out->overlays = tile->overlays;
    out->elevation_cm = tile->elevation_cm;
    out->rainfall_mm = tile->rainfall_mm;
    out->region_id = snapshot->region.id;
    out->settlement_id = tile->settlement_id;
    out->polity_owner_id = tile->terrain == FD_TERRAIN_WATER ?
                           UINT64_C(0) : snapshot->region.political_owner_id;
    return fd_diag_ok(diag);
}

fd_result fd_snapshot_get_entity_by_index(const fd_snapshot *snapshot,
                                          uint32_t index,
                                          fd_entity_view *out,
                                          fd_diagnostic *diag)
{
    if (snapshot == NULL) {
        return fd_diag_set(diag, FD_ERR_INVALID_ARGUMENT, FD_FIELD_NONE,
                           UINT32_C(0), "snapshot is null");
    }
    if (!fd_output_struct_valid(out, (uint32_t)sizeof(*out))) {
        return fd_diag_set(diag, FD_ERR_INVALID_SIZE, FD_FIELD_STRUCT_SIZE,
                           UINT32_C(0), "invalid entity view output");
    }
    if (index >= FD_U01_ENTITY_COUNT) {
        return fd_diag_set(diag, FD_ERR_OUT_OF_RANGE, FD_FIELD_ENTITY_ID,
                           index, "entity index is outside the snapshot");
    }
    fd_fill_entity_view(&snapshot->entities[index], out);
    return fd_diag_ok(diag);
}

fd_result fd_snapshot_get_region(const fd_snapshot *snapshot,
                                 fd_region_view *out,
                                 fd_diagnostic *diag)
{
    if (snapshot == NULL) {
        return fd_diag_set(diag, FD_ERR_INVALID_ARGUMENT, FD_FIELD_NONE,
                           UINT32_C(0), "snapshot is null");
    }
    if (!fd_output_struct_valid(out, (uint32_t)sizeof(*out))) {
        return fd_diag_set(diag, FD_ERR_INVALID_SIZE, FD_FIELD_STRUCT_SIZE,
                           UINT32_C(0), "invalid region view output");
    }
    fd_fill_region_view(&snapshot->region, out);
    return fd_diag_ok(diag);
}

fd_result fd_snapshot_state_hash(const fd_snapshot *snapshot,
                                 uint8_t out_sha256[32],
                                 fd_diagnostic *diag)
{
    if (snapshot == NULL || out_sha256 == NULL) {
        return fd_diag_set(diag, FD_ERR_INVALID_ARGUMENT, FD_FIELD_NONE,
                           UINT32_C(0), "snapshot or hash output is null");
    }
    memcpy(out_sha256, snapshot->info.state_sha256, 32U);
    return fd_diag_ok(diag);
}

static bool fd_is_foreign_actor(const fd_world *world, fd_entity_id actor)
{
    return actor == world->region.foreign_faction_ids[0] ||
           actor == world->region.foreign_faction_ids[1];
}

fd_result fd_action_mask_query(const fd_world *world,
                               fd_entity_id actor,
                               const fd_action_prefix *prefix,
                               fd_action_mask *out,
                               fd_diagnostic *diag)
{
    uint32_t capacity;
    uint8_t *bits;
    if (world == NULL || prefix == NULL) {
        return fd_diag_set(diag, FD_ERR_INVALID_ARGUMENT, FD_FIELD_NONE,
                           UINT32_C(0), "world or action prefix is null");
    }
    if (!fd_input_struct_valid(prefix, (uint32_t)sizeof(*prefix)) ||
        !fd_output_struct_valid(out, (uint32_t)sizeof(*out))) {
        return fd_diag_set(diag, FD_ERR_INVALID_SIZE, FD_FIELD_STRUCT_SIZE,
                           UINT32_C(0), "invalid action prefix or mask size");
    }
    if (!fd_is_foreign_actor(world, actor)) {
        return fd_diag_set(diag, FD_ERR_OUT_OF_RANGE, FD_FIELD_ENTITY_ID,
                           UINT32_C(0), "actor is not a U01 decision seat");
    }
    if (prefix->depth != UINT32_C(0) ||
        prefix->category != UINT32_C(0) ||
        prefix->parameter_count != UINT32_C(0)) {
        return fd_diag_set(diag, FD_ERR_OUT_OF_RANGE, FD_FIELD_ACTION,
                           UINT32_C(0), "U01 mask queries require an empty prefix");
    }
    {
        uint32_t parameter;
        for (parameter = 0U; parameter < 4U; ++parameter) {
            if (prefix->parameters[parameter] != UINT32_C(0)) {
                return fd_diag_set(diag, FD_ERR_INVALID_ARGUMENT,
                                   FD_FIELD_ACTION, parameter,
                                   "unused empty-prefix parameters must be zero");
            }
        }
    }
    capacity = out->byte_capacity;
    bits = out->bits;
    out->schema_id = UINT32_C(1);
    out->first_value = FD_ACTION_PASS;
    out->bit_count = UINT32_C(1);
    out->byte_capacity = UINT32_C(1);
    if (capacity < UINT32_C(1) || bits == NULL) {
        return fd_diag_set(diag, FD_ERR_BUFFER_TOO_SMALL,
                           FD_FIELD_BUFFER_CAPACITY, UINT32_C(0),
                           "action mask requires one byte");
    }
    bits[0] = UINT8_C(1);
    return fd_diag_ok(diag);
}

fd_result fd_action_validate(const fd_world *world,
                             fd_entity_id actor,
                             const fd_action *action,
                             fd_diagnostic *diag)
{
    uint32_t index;
    if (world == NULL || action == NULL) {
        return fd_diag_set(diag, FD_ERR_INVALID_ARGUMENT, FD_FIELD_NONE,
                           UINT32_C(0), "world or action is null");
    }
    if (!fd_input_struct_valid(action, (uint32_t)sizeof(*action))) {
        return fd_diag_set(diag, FD_ERR_INVALID_SIZE, FD_FIELD_STRUCT_SIZE,
                           UINT32_C(0), "invalid action size or ABI");
    }
    if (!fd_is_foreign_actor(world, actor)) {
        return fd_diag_set(diag, FD_ERR_OUT_OF_RANGE, FD_FIELD_ENTITY_ID,
                           UINT32_C(0), "actor is not a U01 decision seat");
    }
    if (action->category != FD_ACTION_PASS ||
        action->parameter_count != UINT32_C(0)) {
        return fd_diag_set(diag, FD_ERR_INVALID_ACTION, FD_FIELD_ACTION,
                           UINT32_C(0), "PASS with no parameters is the only U01 action");
    }
    for (index = 0U; index < 4U; ++index) {
        if (action->parameters[index] != UINT32_C(0)) {
            return fd_diag_set(diag, FD_ERR_INVALID_ACTION, FD_FIELD_ACTION,
                               index, "unused PASS parameters must be zero");
        }
    }
    return fd_diag_ok(diag);
}

static fd_result fd_joint_validate(const fd_world *world,
                                   const fd_joint_decision *decision,
                                   fd_diagnostic *diag)
{
    uint32_t seat;
    if (!fd_input_struct_valid(decision, (uint32_t)sizeof(*decision))) {
        return fd_diag_set(diag, FD_ERR_INVALID_SIZE, FD_FIELD_STRUCT_SIZE,
                           UINT32_C(0), "invalid joint decision size or ABI");
    }
    if (decision->reserved != UINT32_C(0)) {
        return fd_diag_set(diag, FD_ERR_INVALID_ARGUMENT, FD_FIELD_RESERVED,
                           UINT32_C(0), "joint decision reserved field must be zero");
    }
    if (decision->seat_count != FD_U01_SEAT_COUNT) {
        return fd_diag_set(diag, FD_ERR_INVALID_ACTION, FD_FIELD_SEAT_COUNT,
                           decision->seat_count, "exactly two explicit seats are required");
    }
    if (decision->expected_tick != world->tick) {
        return fd_diag_set(diag, FD_ERR_INVALID_ACTION, FD_FIELD_TICK,
                           UINT32_C(0), "joint decision expected tick does not match");
    }
    if (memcmp(decision->expected_pre_state_sha256, world->state_sha256, 32U) != 0) {
        return fd_diag_set(diag, FD_ERR_INVALID_ACTION, FD_FIELD_ACTION,
                           UINT32_C(0), "joint decision pre-state hash does not match");
    }
    for (seat = 0U; seat < FD_U01_SEAT_COUNT; ++seat) {
        fd_result result;
        if (decision->seats[seat].actor_id !=
            world->region.foreign_faction_ids[seat]) {
            return fd_diag_set(diag, FD_ERR_INVALID_ACTION, FD_FIELD_ENTITY_ID,
                               seat, "seat actor IDs must be complete and canonical");
        }
        result = fd_action_validate(world, decision->seats[seat].actor_id,
                                    &decision->seats[seat].action, diag);
        if (result != FD_OK) {
            return result;
        }
    }
    return FD_OK;
}

void fd_hash_joint_decision_internal(const fd_joint_decision *decision,
                                     uint8_t out[32])
{
    static const uint8_t domain[] = "FrontierDirectorate/JointDecision/v1";
    fd_sha256_ctx_internal sha;
    uint32_t seat;
    fd_sha256_init_internal(&sha);
    fd_hash_u32(&sha, (uint32_t)(sizeof(domain) - 1U));
    fd_sha256_update_internal(&sha, domain, (uint64_t)(sizeof(domain) - 1U));
    fd_hash_u32(&sha, decision->seat_count);
    fd_hash_u32(&sha, decision->reserved);
    fd_hash_u64(&sha, decision->expected_tick);
    fd_sha256_update_internal(&sha, decision->expected_pre_state_sha256,
                              UINT64_C(32));
    for (seat = 0U; seat < FD_U01_SEAT_COUNT; ++seat) {
        uint32_t parameter;
        fd_hash_u64(&sha, decision->seats[seat].actor_id);
        fd_hash_u32(&sha, decision->seats[seat].action.category);
        fd_hash_u32(&sha, decision->seats[seat].action.parameter_count);
        for (parameter = 0U; parameter < 4U; ++parameter) {
            fd_hash_u32(&sha,
                        decision->seats[seat].action.parameters[parameter]);
        }
    }
    fd_sha256_final_internal(&sha, out);
}

fd_result fd_world_step(fd_world *world,
                        const fd_joint_decision *decision,
                        fd_step_result *out,
                        fd_diagnostic *diag)
{
    fd_step_result result;
    uint32_t local_tick;
    uint32_t phase;
    fd_result status;
    if (world == NULL || decision == NULL) {
        return fd_diag_set(diag, FD_ERR_INVALID_ARGUMENT, FD_FIELD_NONE,
                           UINT32_C(0), "world or joint decision is null");
    }
    if (!fd_output_struct_valid(out, (uint32_t)sizeof(*out))) {
        return fd_diag_set(diag, FD_ERR_INVALID_SIZE, FD_FIELD_STRUCT_SIZE,
                           UINT32_C(0), "invalid step result output");
    }
    status = fd_joint_validate(world, decision, diag);
    if (status != FD_OK) {
        return status;
    }
    if (world->tick > UINT64_MAX - FD_OPERATIONAL_TICKS) {
        return fd_diag_set(diag, FD_ERR_CAPACITY, FD_FIELD_TICK,
                           UINT32_C(0), "tick counter would overflow");
    }
    memset(&result, 0, sizeof(result));
    result.struct_size = (uint32_t)sizeof(result);
    result.abi_version = FD_ABI_VERSION;
    result.tick_before = world->tick;
    result.tick_after = world->tick + FD_OPERATIONAL_TICKS;
    result.accepted_action_count = FD_U01_SEAT_COUNT;
    result.reward_count = UINT32_C(0);
    result.phase_count = FD_U01_PHASE_COUNT;
    result.local_tick_count = (uint32_t)FD_OPERATIONAL_TICKS;
    result.phase_trace_count = FD_U01_PHASE_TRACE_COUNT;
    result.actor_trace_count = FD_U01_SEAT_COUNT;
    result.actor_trace[0] = world->region.foreign_faction_ids[0];
    result.actor_trace[1] = world->region.foreign_faction_ids[1];
    memcpy(result.pre_state_sha256, world->state_sha256, 32U);
    (void)fd_sha256(NULL, UINT64_C(0), result.event_sha256);
    fd_hash_joint_decision_internal(decision, result.staged_decision_sha256);
    for (phase = 0U; phase < FD_U01_PHASE_COUNT; ++phase) {
        result.phase_ids[phase] = phase + UINT32_C(1);
    }
    for (local_tick = 0U; local_tick < (uint32_t)FD_OPERATIONAL_TICKS;
         ++local_tick) {
        for (phase = 0U; phase < FD_U01_PHASE_COUNT; ++phase) {
            uint32_t trace_index = local_tick * FD_U01_PHASE_COUNT + phase;
            result.phase_trace[trace_index] = phase + UINT32_C(1);
            if (phase == UINT32_C(0)) {
                uint32_t entity_index;
                for (entity_index = 0U; entity_index < FD_U01_ENTITY_COUNT;
                     ++entity_index) {
                    fd_entity_trace_entry *entry =
                        &result.entity_trace[result.entity_trace_count];
                    entry->local_tick = local_tick;
                    entry->phase_id = UINT32_C(1);
                    entry->entity_id = world->entities[entity_index].id;
                    ++result.entity_trace_count;
                }
                if (local_tick == UINT32_C(0)) {
                    fd_validation_report validation;
                    memset(&validation, 0, sizeof(validation));
                    validation.struct_size = (uint32_t)sizeof(validation);
                    validation.abi_version = FD_ABI_VERSION;
                    status =
                        fd_world_validate_internal(world, &validation, diag);
                    if (status != FD_OK) {
                        return status;
                    }
                }
            } else if (local_tick == UINT32_C(0) && phase == UINT32_C(1)) {
                uint32_t seat;
                for (seat = 0U; seat < FD_U01_SEAT_COUNT; ++seat) {
                    fd_entity_trace_entry *entry =
                        &result.entity_trace[result.entity_trace_count];
                    entry->local_tick = UINT32_C(0);
                    entry->phase_id = UINT32_C(2);
                    entry->entity_id = decision->seats[seat].actor_id;
                    ++result.entity_trace_count;
                }
            } else if (local_tick + UINT32_C(1) ==
                           (uint32_t)FD_OPERATIONAL_TICKS &&
                       phase == UINT32_C(21)) {
                /* Phase 22 stages immutable transition metadata before the
                 * authoritative phase-23 hash exists. */
                result.transition_staged = UINT32_C(1);
                result.transition_stage_phase = UINT32_C(22);
                result.transition_stage_trace_index = trace_index;
                result.staged_tick_before = result.tick_before;
                result.staged_tick_after = result.tick_after;
                result.staged_decision = *decision;
                result.staged_decision.struct_size =
                    (uint32_t)sizeof(result.staged_decision);
                result.staged_decision.abi_version = FD_ABI_VERSION;
                {
                    uint32_t seat;
                    for (seat = 0U; seat < FD_U01_SEAT_COUNT; ++seat) {
                        result.staged_decision.seats[seat].action.struct_size =
                            (uint32_t)sizeof(fd_action);
                        result.staged_decision.seats[seat].action.abi_version =
                            FD_ABI_VERSION;
                    }
                }
                memcpy(result.staged_pre_state_sha256,
                       result.pre_state_sha256, 32U);
                memcpy(result.staged_event_sha256,
                       result.event_sha256, 32U);
            } else if (phase == UINT32_C(22)) {
                world->tick += UINT64_C(1);
                if (local_tick + UINT32_C(1) ==
                    (uint32_t)FD_OPERATIONAL_TICKS) {
                    status = fd_world_compute_hash(world);
                    if (status != FD_OK) {
                        return fd_diag_set(diag, FD_ERR_INTERNAL, FD_FIELD_NONE,
                                           local_tick,
                                           "state hash computation failed");
                    }
                    result.state_hash_phase = UINT32_C(23);
                    result.state_hash_trace_index = trace_index;
                    /* Non-authoritative finalization after phase 23. */
                    memcpy(result.finalized_post_state_sha256,
                           world->state_sha256, 32U);
                }
            }
        }
    }
    if (result.transition_staged != UINT32_C(1) ||
        result.transition_stage_trace_index >= result.state_hash_trace_index) {
        return fd_diag_set(diag, FD_ERR_INTERNAL, FD_FIELD_NONE,
                           UINT32_C(0), "transition staging order was not observed");
    }
    memcpy(result.post_state_sha256, world->state_sha256, 32U);
    memcpy(out, &result, sizeof(result));
    return fd_diag_ok(diag);
}
