#include "fd_internal.h"

#include <limits.h>
#include <string.h>

typedef struct fd_heap_internal {
    uint32_t *nodes;
    uint32_t *positions;
    uint32_t *distances;
    uint32_t size;
} fd_heap_internal;

static bool fd_heap_less(const fd_heap_internal *heap,
                         uint32_t left_node,
                         uint32_t right_node)
{
    uint32_t left_distance = heap->distances[left_node];
    uint32_t right_distance = heap->distances[right_node];
    return left_distance < right_distance ||
           (left_distance == right_distance && left_node < right_node);
}

static void fd_heap_swap(fd_heap_internal *heap, uint32_t left, uint32_t right)
{
    uint32_t value = heap->nodes[left];
    heap->nodes[left] = heap->nodes[right];
    heap->nodes[right] = value;
    heap->positions[heap->nodes[left]] = left;
    heap->positions[heap->nodes[right]] = right;
}

static void fd_heap_up(fd_heap_internal *heap, uint32_t position)
{
    uint32_t current = position;
    while (current > UINT32_C(0)) {
        uint32_t parent = (current - UINT32_C(1)) / UINT32_C(2);
        if (!fd_heap_less(heap, heap->nodes[current], heap->nodes[parent])) {
            break;
        }
        fd_heap_swap(heap, current, parent);
        current = parent;
    }
}

static void fd_heap_down(fd_heap_internal *heap, uint32_t position)
{
    uint32_t current = position;
    while (true) {
        uint32_t left = current * UINT32_C(2) + UINT32_C(1);
        uint32_t right = left + UINT32_C(1);
        uint32_t smallest = current;
        if (left < heap->size &&
            fd_heap_less(heap, heap->nodes[left], heap->nodes[smallest])) {
            smallest = left;
        }
        if (right < heap->size &&
            fd_heap_less(heap, heap->nodes[right], heap->nodes[smallest])) {
            smallest = right;
        }
        if (smallest == current) {
            break;
        }
        fd_heap_swap(heap, current, smallest);
        current = smallest;
    }
}

static void fd_heap_insert(fd_heap_internal *heap, uint32_t node)
{
    uint32_t position = heap->size;
    heap->nodes[position] = node;
    heap->positions[node] = position;
    heap->size += UINT32_C(1);
    fd_heap_up(heap, position);
}

static uint32_t fd_heap_pop(fd_heap_internal *heap)
{
    uint32_t result = heap->nodes[0];
    heap->size -= UINT32_C(1);
    heap->positions[result] = UINT32_MAX;
    if (heap->size > UINT32_C(0)) {
        heap->nodes[0] = heap->nodes[heap->size];
        heap->positions[heap->nodes[0]] = UINT32_C(0);
        fd_heap_down(heap, UINT32_C(0));
    }
    return result;
}

void fd_hash_config_internal(const fd_world_config *config, uint8_t out[32])
{
    static const uint8_t domain[] = "FrontierDirectorate/Config/v1";
    fd_sha256_ctx_internal sha;
    uint8_t bytes[4];
    const uint32_t values[10] = {
        config->width, config->height, config->attempt_budget,
        config->generator_version, config->ruleset_version,
        config->forest_density_ppm, config->hill_density_ppm,
        config->wetland_density_ppm, config->reference_mode,
        FD_RNG_REGISTRY_VERSION
    };
    uint32_t index;
    fd_sha256_init_internal(&sha);
    fd_store_u32_le(bytes, (uint32_t)(sizeof(domain) - 1U));
    fd_sha256_update_internal(&sha, bytes, UINT64_C(4));
    fd_sha256_update_internal(&sha, domain, (uint64_t)(sizeof(domain) - 1U));
    for (index = 0U; index < 10U; ++index) {
        fd_store_u32_le(bytes, values[index]);
        fd_sha256_update_internal(&sha, bytes, UINT64_C(4));
    }
    fd_sha256_final_internal(&sha, out);
}

void fd_hash_content_internal(const fd_content_manifest *content, uint8_t out[32])
{
    static const uint8_t domain[] = "FrontierDirectorate/Content/v1";
    fd_sha256_ctx_internal sha;
    uint8_t bytes[8];
    uint32_t index;
    fd_sha256_init_internal(&sha);
    fd_store_u32_le(bytes, (uint32_t)(sizeof(domain) - 1U));
    fd_sha256_update_internal(&sha, bytes, UINT64_C(4));
    fd_sha256_update_internal(&sha, domain, (uint64_t)(sizeof(domain) - 1U));
    fd_store_u32_le(bytes, content->pack_count);
    fd_sha256_update_internal(&sha, bytes, UINT64_C(4));
    for (index = 0U; index < content->pack_count; ++index) {
        fd_store_u64_le(bytes, content->packs[index].stable_id);
        fd_sha256_update_internal(&sha, bytes, UINT64_C(8));
        fd_sha256_update_internal(&sha, content->packs[index].sha256,
                                  UINT64_C(32));
    }
    fd_sha256_final_internal(&sha, out);
}

static bool fd_config_reserved_zero(const fd_world_config *config)
{
    uint32_t index;
    for (index = 0U; index < 8U; ++index) {
        if (config->reserved[index] != UINT32_C(0)) {
            return false;
        }
    }
    return true;
}

fd_result fd_validate_generation_inputs_internal(
    const fd_world_config *config,
    const fd_content_manifest *content,
    fd_diagnostic *diag)
{
    uint64_t tile_count;
    uint64_t density_sum;
    uint32_t index;
    if (!fd_input_struct_valid(config, (uint32_t)sizeof(*config))) {
        return fd_diag_set(diag, FD_ERR_INVALID_SIZE, FD_FIELD_STRUCT_SIZE,
                           UINT32_C(0), "invalid world configuration size or ABI");
    }
    if (!fd_config_reserved_zero(config)) {
        return fd_diag_set(diag, FD_ERR_INVALID_ARGUMENT, FD_FIELD_RESERVED,
                           UINT32_C(0), "world configuration reserved fields must be zero");
    }
    if (config->width < FD_MIN_WIDTH || config->width > FD_MAX_WIDTH) {
        return fd_diag_set(diag, FD_ERR_OUT_OF_RANGE, FD_FIELD_WIDTH,
                           config->width, "world width is outside 32..256");
    }
    if (config->height < FD_MIN_HEIGHT || config->height > FD_MAX_HEIGHT) {
        return fd_diag_set(diag, FD_ERR_OUT_OF_RANGE, FD_FIELD_HEIGHT,
                           config->height, "world height is outside 16..128");
    }
    tile_count = (uint64_t)config->width * (uint64_t)config->height;
    if (tile_count > (uint64_t)FD_MAX_TILES) {
        return fd_diag_set(diag, FD_ERR_CAPACITY, FD_FIELD_WIDTH,
                           UINT32_C(0), "world tile count exceeds 32768");
    }
    if (config->attempt_budget == UINT32_C(0) ||
        config->attempt_budget > FD_MAX_GENERATION_ATTEMPTS) {
        return fd_diag_set(diag, FD_ERR_OUT_OF_RANGE, FD_FIELD_ATTEMPT_BUDGET,
                           config->attempt_budget, "attempt budget is outside 1..64");
    }
    if (config->generator_version != FD_GENERATOR_VERSION ||
        config->ruleset_version != FD_RULESET_VERSION) {
        return fd_diag_set(diag, FD_ERR_VERSION, FD_FIELD_FORMAT_VERSION,
                           UINT32_C(0), "unsupported generator or ruleset version");
    }
    if (config->reference_mode != UINT32_C(1)) {
        return fd_diag_set(diag, FD_ERR_OUT_OF_RANGE, FD_FIELD_RESERVED,
                           UINT32_C(0), "U01 requires reference mode 1");
    }
    density_sum = (uint64_t)config->forest_density_ppm +
                  (uint64_t)config->hill_density_ppm +
                  (uint64_t)config->wetland_density_ppm;
    if (config->forest_density_ppm > UINT32_C(1000000) ||
        config->hill_density_ppm > UINT32_C(1000000) ||
        config->wetland_density_ppm > UINT32_C(1000000) ||
        density_sum > UINT64_C(900000)) {
        return fd_diag_set(diag, FD_ERR_OUT_OF_RANGE, FD_FIELD_TERRAIN_DENSITY,
                           UINT32_C(0), "terrain densities are outside bounded ranges");
    }
    if (!fd_input_struct_valid(content, (uint32_t)sizeof(*content))) {
        return fd_diag_set(diag, FD_ERR_INVALID_SIZE, FD_FIELD_CONTENT,
                           UINT32_C(0), "invalid content manifest size or ABI");
    }
    if (content->reserved != UINT32_C(0) ||
        content->pack_count > FD_MAX_CONTENT_PACKS ||
        (content->pack_count > UINT32_C(0) && content->packs == NULL)) {
        return fd_diag_set(diag, FD_ERR_INVALID_ARGUMENT, FD_FIELD_CONTENT,
                           UINT32_C(0), "invalid bounded content manifest");
    }
    for (index = 0U; index < content->pack_count; ++index) {
        if (content->packs[index].stable_id == UINT64_C(0) ||
            (index > UINT32_C(0) &&
             content->packs[index - UINT32_C(1)].stable_id >=
             content->packs[index].stable_id)) {
            return fd_diag_set(diag, FD_ERR_INVALID_ARGUMENT, FD_FIELD_CONTENT,
                               index, "content pack IDs must be nonzero and sorted");
        }
    }
    return FD_OK;
}

static uint32_t fd_coast_x(bool west_water, uint32_t width, uint32_t water_width)
{
    return west_water ? water_width : width - water_width - UINT32_C(1);
}

static fd_result fd_apply_elevation_fields(fd_world *world,
                                           uint64_t attempt)
{
    static const uint32_t scales[3] = {UINT32_C(16), UINT32_C(8), UINT32_C(4)};
    static const uint32_t weights[3] = {UINT32_C(1), UINT32_C(2), UINT32_C(4)};
    uint32_t octave;
    for (octave = 0U; octave < 3U; ++octave) {
        uint32_t scale = scales[octave];
        uint32_t grid_width = (world->config.width + scale - UINT32_C(1)) /
                              scale + UINT32_C(1);
        uint32_t grid_height = (world->config.height + scale - UINT32_C(1)) /
                               scale + UINT32_C(1);
        uint64_t grid_count = (uint64_t)grid_width * (uint64_t)grid_height;
        uint64_t grid_bytes = grid_count * UINT64_C(4);
        uint32_t *grid = (uint32_t *)fd_context_alloc(world->context, grid_bytes);
        uint32_t gy;
        if (grid == NULL) {
            return FD_ERR_OUT_OF_MEMORY;
        }
        for (gy = 0U; gy < grid_height; ++gy) {
            uint32_t gx;
            for (gx = 0U; gx < grid_width; ++gx) {
                uint64_t subject = ((uint64_t)octave << 56U) |
                                   ((uint64_t)gy << 28U) | (uint64_t)gx;
                fd_result result = fd_rng_bounded(
                    world->root_seed_le, world->config.generator_version,
                    world->config.ruleset_version,
                    FD_RNG_DOMAIN_ELEVATION_LATTICE, subject, attempt,
                    UINT64_C(0), FD_RNG_SITE_ELEVATION_TILE,
                    UINT32_C(1024), &grid[gy * grid_width + gx]);
                if (result != FD_OK) {
                    fd_context_free(world->context, grid, grid_bytes);
                    return result;
                }
            }
        }
        for (gy = 0U; gy < world->config.height; ++gy) {
            uint32_t x;
            uint32_t y0 = gy / scale;
            uint32_t dy = gy % scale;
            for (x = 0U; x < world->config.width; ++x) {
                uint32_t index = gy * world->config.width + x;
                uint32_t x0;
                uint32_t dx;
                uint64_t top;
                uint64_t bottom;
                uint64_t interpolated;
                uint32_t addition;
                if (world->tiles[index].terrain == FD_TERRAIN_WATER) {
                    continue;
                }
                x0 = x / scale;
                dx = x % scale;
                top = (uint64_t)grid[y0 * grid_width + x0] *
                          (uint64_t)(scale - dx) +
                      (uint64_t)grid[y0 * grid_width + x0 + UINT32_C(1)] *
                          (uint64_t)dx;
                bottom =
                    (uint64_t)grid[(y0 + UINT32_C(1)) * grid_width + x0] *
                        (uint64_t)(scale - dx) +
                    (uint64_t)grid[(y0 + UINT32_C(1)) * grid_width +
                                   x0 + UINT32_C(1)] * (uint64_t)dx;
                top /= (uint64_t)scale;
                bottom /= (uint64_t)scale;
                interpolated = (top * (uint64_t)(scale - dy) +
                                bottom * (uint64_t)dy) /
                               (uint64_t)scale;
                addition = (uint32_t)((interpolated * (uint64_t)weights[octave]) /
                                      UINT64_C(7));
                world->tiles[index].elevation_cm += (int32_t)addition;
            }
        }
        fd_context_free(world->context, grid, grid_bytes);
    }
    {
        uint32_t index;
        for (index = 0U; index < world->tile_count; ++index) {
            if (world->tiles[index].terrain != FD_TERRAIN_WATER) {
                uint32_t elevation = (uint32_t)world->tiles[index].elevation_cm;
                uint32_t rainfall = UINT32_C(800) +
                    (uint32_t)(((uint64_t)elevation * UINT64_C(1600)) /
                               UINT64_C(5000));
                world->tiles[index].rainfall_mm = rainfall > UINT32_C(2400) ?
                                                   UINT32_C(2400) : rainfall;
            }
        }
    }
    return FD_OK;
}

static bool fd_wetland_eligible(const fd_world *world, uint32_t index)
{
    uint32_t width = world->config.width;
    uint32_t height = world->config.height;
    uint32_t x = index % width;
    uint32_t y = index / width;
    if ((world->tiles[index].overlays & FD_TILE_OVERLAY_RIVER) != UINT32_C(0)) {
        return true;
    }
#define FD_ELIGIBLE_NEIGHBOR(candidate_) \
    (world->tiles[(candidate_)].terrain == FD_TERRAIN_WATER || \
     (world->tiles[(candidate_)].overlays & FD_TILE_OVERLAY_RIVER) != UINT32_C(0))
    if (x > UINT32_C(0) && FD_ELIGIBLE_NEIGHBOR(index - UINT32_C(1))) {
        return true;
    }
    if (x + UINT32_C(1) < width && FD_ELIGIBLE_NEIGHBOR(index + UINT32_C(1))) {
        return true;
    }
    if (y > UINT32_C(0) && FD_ELIGIBLE_NEIGHBOR(index - width)) {
        return true;
    }
    if (y + UINT32_C(1) < height && FD_ELIGIBLE_NEIGHBOR(index + width)) {
        return true;
    }
#undef FD_ELIGIBLE_NEIGHBOR
    return false;
}

static fd_result fd_assign_name(fd_world *world,
                                fd_entity_internal *entity,
                                uint32_t domain,
                                uint64_t subject,
                                uint64_t attempt,
                                const char *suffix)
{
    static const char *const syllables[] = {
        "a", "be", "cor", "da", "el", "fi", "gan", "ha",
        "io", "jor", "ka", "lum", "mer", "na", "or", "pel",
        "qui", "ran", "sa", "tor", "ul", "ven", "wa", "yor"
    };
    uint32_t count = (uint32_t)(sizeof(syllables) / sizeof(syllables[0]));
    uint32_t part;
    uint32_t length = UINT32_C(0);
    memset(entity->name, 0, sizeof(entity->name));
    for (part = 0U; part < 3U; ++part) {
        uint32_t selected = UINT32_C(0);
        const char *syllable;
        size_t syllable_length;
        fd_result result = fd_rng_bounded(
            world->root_seed_le, world->config.generator_version,
            world->config.ruleset_version, domain, subject, attempt,
            (uint64_t)part, FD_RNG_SITE_NAMING_SYLLABLE, count, &selected);
        if (result != FD_OK) {
            return result;
        }
        syllable = syllables[selected];
        syllable_length = strlen(syllable);
        if ((uint64_t)length + (uint64_t)syllable_length <
            (uint64_t)FD_MAX_NAME_BYTES) {
            memcpy(entity->name + length, syllable, syllable_length);
            length += (uint32_t)syllable_length;
        }
    }
    if (suffix != NULL) {
        size_t suffix_length = strlen(suffix);
        if ((uint64_t)length + (uint64_t)suffix_length <
            (uint64_t)FD_MAX_NAME_BYTES) {
            memcpy(entity->name + length, suffix, suffix_length);
            length += (uint32_t)suffix_length;
        }
    }
    if (length > UINT32_C(0) && entity->name[0] >= 'a' && entity->name[0] <= 'z') {
        entity->name[0] = (char)(entity->name[0] - ('a' - 'A'));
    }
    entity->name_length = length;
    return FD_OK;
}

static fd_result fd_initialize_entities(fd_world *world,
                                        uint32_t coastal_x,
                                        uint32_t coastal_y,
                                        uint32_t inland_x,
                                        uint32_t inland_y,
                                        uint32_t outlet_index,
                                        uint64_t attempt)
{
    uint32_t index;
    for (index = 0U; index < FD_U01_ENTITY_COUNT; ++index) {
        world->entities[index].id = fd_make_entity_id(index + UINT32_C(1));
        world->entities[index].x = UINT32_MAX;
        world->entities[index].y = UINT32_MAX;
        world->entities[index].region_id = fd_make_entity_id(UINT32_C(1));
    }
    world->entities[0].kind = FD_ENTITY_REGION;
    world->entities[0].owner_id = world->entities[3].id;
    world->entities[1].kind = FD_ENTITY_SETTLEMENT_COASTAL;
    world->entities[1].x = coastal_x;
    world->entities[1].y = coastal_y;
    world->entities[1].owner_id = world->entities[3].id;
    world->entities[2].kind = FD_ENTITY_SETTLEMENT_INLAND;
    world->entities[2].x = inland_x;
    world->entities[2].y = inland_y;
    world->entities[2].owner_id = world->entities[3].id;
    world->entities[3].kind = FD_ENTITY_POLITY_LOCAL;
    world->entities[3].owner_id = world->entities[3].id;
    world->entities[4].kind = FD_ENTITY_FACTION_FOREIGN;
    world->entities[4].owner_id = world->entities[4].id;
    world->entities[5].kind = FD_ENTITY_FACTION_FOREIGN;
    world->entities[5].owner_id = world->entities[5].id;
    for (index = 0U; index < FD_U01_ENTITY_COUNT; ++index) {
        static const char *const suffixes[FD_U01_ENTITY_COUNT] = {
            " Reach", " Haven", " Market", " Council", " Survey", " Company"
        };
        uint32_t domain = index < UINT32_C(4) ? FD_RNG_DOMAIN_NAMING :
                                                FD_RNG_DOMAIN_FACTION_INITIALIZATION;
        fd_result result = fd_assign_name(world, &world->entities[index], domain,
                                          (uint64_t)index + UINT64_C(1), attempt,
                                          suffixes[index]);
        if (result != FD_OK) {
            return result;
        }
    }
    world->region.id = world->entities[0].id;
    world->region.political_owner_id = world->entities[3].id;
    world->region.settlement_ids[0] = world->entities[1].id;
    world->region.settlement_ids[1] = world->entities[2].id;
    world->region.foreign_faction_ids[0] = world->entities[4].id;
    world->region.foreign_faction_ids[1] = world->entities[5].id;
    world->region.river_outlet_tile_id = fd_tile_id_from_index(outlet_index);
    world->region.road_start_tile_id =
        fd_tile_id_from_index(coastal_y * world->config.width + coastal_x);
    world->region.road_end_tile_id =
        fd_tile_id_from_index(inland_y * world->config.width + inland_x);
    return FD_OK;
}

static bool fd_set_river_tile(fd_world *world,
                              uint32_t index,
                              uint32_t elevation)
{
    uint32_t previous;
    uint32_t carve;
    if (world->tiles[index].terrain == FD_TERRAIN_WATER ||
        world->tiles[index].elevation_cm < INT32_C(0)) {
        ++world->river_raised_tile_count;
        return false;
    }
    previous = (uint32_t)world->tiles[index].elevation_cm;
    if (elevation > previous) {
        ++world->river_raised_tile_count;
        return false;
    }
    carve = previous - elevation;
    if (carve < world->river_min_carve_cm) {
        world->river_min_carve_cm = carve;
    }
    world->tiles[index].overlays |= FD_TILE_OVERLAY_RIVER;
    world->tiles[index].terrain = FD_TERRAIN_GRASSLAND;
    world->tiles[index].elevation_cm = (int32_t)elevation;
    return true;
}

static fd_result fd_construct_river(fd_world *world,
                                    bool west_water,
                                    uint32_t water_width,
                                    uint32_t outlet_y,
                                    uint32_t source_y,
                                    uint32_t tie_choice,
                                    uint32_t *out_outlet,
                                    uint32_t *out_count)
{
    uint32_t width = world->config.width;
    uint32_t outlet_x = fd_coast_x(west_water, width, water_width);
    uint32_t source_x = west_water ? width - UINT32_C(2) : UINT32_C(1);
    uint32_t bend_x = (outlet_x + source_x + tie_choice) / UINT32_C(2);
    uint32_t x = outlet_x;
    uint32_t y = outlet_y;
    uint32_t elevation = UINT32_C(100);
    uint32_t count = UINT32_C(0);
    uint32_t index = y * width + x;
    *out_outlet = index;
    if (!fd_set_river_tile(world, index, elevation)) {
        return FD_ERR_GENERATION_EXHAUSTED;
    }
    world->tiles[index].overlays |= FD_TILE_RIVER_OUTLET;
    ++count;
    while (x != bend_x) {
        x = x < bend_x ? x + UINT32_C(1) : x - UINT32_C(1);
        elevation += UINT32_C(1);
        if (!fd_set_river_tile(world, y * width + x, elevation)) {
            return FD_ERR_GENERATION_EXHAUSTED;
        }
        ++count;
    }
    while (y != source_y) {
        y = y < source_y ? y + UINT32_C(1) : y - UINT32_C(1);
        elevation += UINT32_C(1);
        if (!fd_set_river_tile(world, y * width + x, elevation)) {
            return FD_ERR_GENERATION_EXHAUSTED;
        }
        ++count;
    }
    while (x != source_x) {
        x = x < source_x ? x + UINT32_C(1) : x - UINT32_C(1);
        elevation += UINT32_C(1);
        if (!fd_set_river_tile(world, y * width + x, elevation)) {
            return FD_ERR_GENERATION_EXHAUSTED;
        }
        ++count;
    }
    world->tiles[y * width + x].terrain = FD_TERRAIN_HILLS;
    *out_count = count;
    return FD_OK;
}

static uint32_t fd_road_cost(const fd_world *world, uint32_t index,
                             uint64_t attempt)
{
    uint32_t base;
    uint32_t random;
    switch (world->tiles[index].terrain) {
    case FD_TERRAIN_SETTLEMENT:
        base = UINT32_C(1);
        break;
    case FD_TERRAIN_GRASSLAND:
        base = UINT32_C(2);
        break;
    case FD_TERRAIN_FOREST:
        base = UINT32_C(4);
        break;
    case FD_TERRAIN_HILLS:
        base = UINT32_C(7);
        break;
    case FD_TERRAIN_WETLANDS:
        base = UINT32_C(8);
        break;
    default:
        return UINT32_MAX;
    }
    if ((world->tiles[index].overlays & FD_TILE_OVERLAY_RIVER) != UINT32_C(0)) {
        base += UINT32_C(20);
    }
    random = fd_rng_u32(world->root_seed_le, world->config.generator_version,
                        world->config.ruleset_version,
                        FD_RNG_DOMAIN_ROAD_CANDIDATE, (uint64_t)index, attempt,
                        UINT64_C(0), FD_RNG_SITE_ROAD_TIE);
    return base + (random < UINT32_C(0x80000000) ? UINT32_C(0) : UINT32_C(1));
}

static void fd_relax_road_neighbor(const fd_world *world,
                                   uint32_t current,
                                   uint32_t neighbor,
                                   uint64_t attempt,
                                   uint32_t *distances,
                                   uint32_t *previous,
                                   fd_heap_internal *heap)
{
    uint32_t cost = fd_road_cost(world, neighbor, attempt);
    uint32_t candidate;
    if (cost == UINT32_MAX || distances[current] == UINT32_MAX ||
        distances[current] > UINT32_MAX - cost) {
        return;
    }
    candidate = distances[current] + cost;
    if (candidate < distances[neighbor] ||
        (candidate == distances[neighbor] && current < previous[neighbor])) {
        distances[neighbor] = candidate;
        previous[neighbor] = current;
        if (heap->positions[neighbor] == UINT32_MAX) {
            fd_heap_insert(heap, neighbor);
        } else {
            fd_heap_up(heap, heap->positions[neighbor]);
        }
    }
}

static fd_result fd_construct_road(fd_world *world,
                                   uint32_t start,
                                   uint32_t goal,
                                   uint64_t attempt)
{
    uint64_t array_bytes = (uint64_t)world->tile_count * UINT64_C(4);
    uint32_t *distances = (uint32_t *)fd_context_alloc(world->context, array_bytes);
    uint32_t *previous = (uint32_t *)fd_context_alloc(world->context, array_bytes);
    uint32_t *positions = (uint32_t *)fd_context_alloc(world->context, array_bytes);
    uint32_t *nodes = (uint32_t *)fd_context_alloc(world->context, array_bytes);
    fd_heap_internal heap;
    uint32_t index;
    uint32_t current;
    uint32_t count = UINT32_C(0);
    uint32_t intersection = UINT32_MAX;
    if (distances == NULL || previous == NULL || positions == NULL || nodes == NULL) {
        fd_context_free(world->context, distances, array_bytes);
        fd_context_free(world->context, previous, array_bytes);
        fd_context_free(world->context, positions, array_bytes);
        fd_context_free(world->context, nodes, array_bytes);
        return FD_ERR_OUT_OF_MEMORY;
    }
    for (index = 0U; index < world->tile_count; ++index) {
        distances[index] = UINT32_MAX;
        previous[index] = UINT32_MAX;
        positions[index] = UINT32_MAX;
    }
    heap.nodes = nodes;
    heap.positions = positions;
    heap.distances = distances;
    heap.size = UINT32_C(0);
    distances[start] = UINT32_C(0);
    fd_heap_insert(&heap, start);
    while (heap.size > UINT32_C(0)) {
        uint32_t x;
        uint32_t y;
        current = fd_heap_pop(&heap);
        if (current == goal) {
            break;
        }
        x = current % world->config.width;
        y = current / world->config.width;
        if (y > UINT32_C(0)) {
            fd_relax_road_neighbor(world, current,
                                   current - world->config.width, attempt,
                                   distances, previous, &heap);
        }
        if (x > UINT32_C(0)) {
            fd_relax_road_neighbor(world, current, current - UINT32_C(1), attempt,
                                   distances, previous, &heap);
        }
        if (x + UINT32_C(1) < world->config.width) {
            fd_relax_road_neighbor(world, current, current + UINT32_C(1), attempt,
                                   distances, previous, &heap);
        }
        if (y + UINT32_C(1) < world->config.height) {
            fd_relax_road_neighbor(world, current,
                                   current + world->config.width, attempt,
                                   distances, previous, &heap);
        }
    }
    if (distances[goal] == UINT32_MAX) {
        fd_context_free(world->context, distances, array_bytes);
        fd_context_free(world->context, previous, array_bytes);
        fd_context_free(world->context, positions, array_bytes);
        fd_context_free(world->context, nodes, array_bytes);
        return FD_ERR_GENERATION_EXHAUSTED;
    }
    current = goal;
    while (true) {
        world->tiles[current].overlays |= FD_TILE_OVERLAY_ROAD_CANDIDATE;
        if ((world->tiles[current].overlays & FD_TILE_OVERLAY_RIVER) != UINT32_C(0)) {
            if (intersection != UINT32_MAX) {
                /* A multi-tile river overlap is not a legal single crossing. */
                fd_context_free(world->context, distances, array_bytes);
                fd_context_free(world->context, previous, array_bytes);
                fd_context_free(world->context, positions, array_bytes);
                fd_context_free(world->context, nodes, array_bytes);
                return FD_ERR_GENERATION_EXHAUSTED;
            }
            intersection = current;
        }
        ++count;
        if (current == start) {
            break;
        }
        current = previous[current];
        if (current == UINT32_MAX || count > world->tile_count) {
            fd_context_free(world->context, distances, array_bytes);
            fd_context_free(world->context, previous, array_bytes);
            fd_context_free(world->context, positions, array_bytes);
            fd_context_free(world->context, nodes, array_bytes);
            return FD_ERR_INTERNAL;
        }
    }
    if (intersection != UINT32_MAX) {
        world->tiles[intersection].overlays |= FD_TILE_ROAD_CROSSING;
    }
    world->road_tile_count = count;
    fd_context_free(world->context, distances, array_bytes);
    fd_context_free(world->context, previous, array_bytes);
    fd_context_free(world->context, positions, array_bytes);
    fd_context_free(world->context, nodes, array_bytes);
    return FD_OK;
}

static fd_result fd_generate_candidate(fd_world *world,
                                       uint64_t attempt,
                                       bool constructive_fallback,
                                       fd_validation_report *validation,
                                       uint32_t *rejection_reason,
                                       fd_diagnostic *diag)
{
    uint32_t width = world->config.width;
    uint32_t height = world->config.height;
    uint32_t base_water_width = (width * UINT32_C(30)) / UINT32_C(100);
    uint32_t amplitude = width / UINT32_C(32);
    uint32_t facing = UINT32_C(0);
    bool west_water;
    uint32_t outlet_y_range = height / UINT32_C(3);
    uint32_t outlet_y_offset = UINT32_C(0);
    uint32_t outlet_y;
    uint32_t source_y_choice = UINT32_C(0);
    uint32_t river_tie_choice = UINT32_C(0);
    uint32_t source_y;
    uint32_t coastal_y;
    uint32_t inland_y = height - UINT32_C(3);
    uint32_t coastal_x;
    uint32_t inland_x;
    uint32_t outlet_index;
    uint32_t y;
    fd_result result;
    if (amplitude == UINT32_C(0)) {
        amplitude = UINT32_C(1);
    }
    *rejection_reason = FD_GEN_REJECT_INVARIANT;
    result = fd_rng_bounded(world->root_seed_le,
                            world->config.generator_version,
                            world->config.ruleset_version, FD_RNG_DOMAIN_COAST,
                            UINT64_C(0), attempt, UINT64_C(0),
                            FD_RNG_SITE_COAST_FACING, UINT32_C(2), &facing);
    if (result != FD_OK) {
        return result;
    }
    west_water = facing == UINT32_C(0);
    if (outlet_y_range < UINT32_C(3)) {
        outlet_y_range = UINT32_C(3);
    }
    result = fd_rng_bounded(world->root_seed_le, world->config.generator_version,
                            world->config.ruleset_version,
                            FD_RNG_DOMAIN_SETTLEMENT_PLACEMENT, UINT64_C(0), attempt,
                            UINT64_C(0), FD_RNG_SITE_SETTLEMENT_ROW,
                            outlet_y_range, &outlet_y_offset);
    if (result != FD_OK) {
        return result;
    }
    outlet_y = UINT32_C(2) + outlet_y_offset;
    if (outlet_y + UINT32_C(2) >= height) {
        outlet_y = height / UINT32_C(3);
    }
    coastal_y = outlet_y + UINT32_C(1);
    result = fd_rng_bounded(world->root_seed_le, world->config.generator_version,
                            world->config.ruleset_version,
                            FD_RNG_DOMAIN_RIVER_SOURCE, UINT64_C(0), attempt,
                            UINT64_C(0), FD_RNG_SITE_RIVER_SOURCE_ROW,
                            UINT32_C(3), &source_y_choice);
    if (result != FD_OK) {
        return result;
    }
    result = fd_rng_bounded(world->root_seed_le, world->config.generator_version,
                            world->config.ruleset_version,
                            FD_RNG_DOMAIN_RIVER_TIE_BREAK, UINT64_C(0), attempt,
                            UINT64_C(0), FD_RNG_SITE_RIVER_TIE, UINT32_C(2),
                            &river_tie_choice);
    if (result != FD_OK) {
        return result;
    }
    source_y = outlet_y + source_y_choice;
    if (source_y >= coastal_y) {
        source_y += UINT32_C(1);
    }
    if (source_y + UINT32_C(2) >= height) {
        source_y = outlet_y;
    }
    memset(world->tiles, 0,
           (size_t)world->tile_count * sizeof(*world->tiles));
    world->land_tile_count = UINT32_C(0);
    world->river_raised_tile_count = UINT32_C(0);
    world->river_min_carve_cm = UINT32_MAX;
    for (y = 0U; y < height; ++y) {
        uint32_t water_width = base_water_width;
        uint32_t offset_value = UINT32_C(0);
        int32_t signed_offset;
        uint32_t x;
        if (y != outlet_y && y != coastal_y) {
            if (constructive_fallback) {
                offset_value = y % (amplitude * UINT32_C(2) + UINT32_C(1));
            } else {
                result = fd_rng_bounded(world->root_seed_le,
                                        world->config.generator_version,
                                        world->config.ruleset_version,
                                        FD_RNG_DOMAIN_COAST, (uint64_t)y, attempt,
                                        UINT64_C(0), FD_RNG_SITE_COAST_ROW_OFFSET,
                                        amplitude * UINT32_C(2) + UINT32_C(1),
                                        &offset_value);
                if (result != FD_OK) {
                    return result;
                }
            }
            signed_offset = (int32_t)offset_value - (int32_t)amplitude;
            if (signed_offset < INT32_C(0)) {
                water_width -= (uint32_t)(-signed_offset);
            } else {
                water_width += (uint32_t)signed_offset;
            }
        }
        for (x = 0U; x < width; ++x) {
            uint32_t index = y * width + x;
            bool water = west_water ? x < water_width :
                                      x >= width - water_width;
            if (water) {
                world->tiles[index].terrain = FD_TERRAIN_WATER;
                world->tiles[index].elevation_cm = INT32_C(0);
                world->tiles[index].rainfall_mm = UINT32_C(1400);
            } else {
                uint32_t coast = fd_coast_x(west_water, width, water_width);
                uint32_t distance = x > coast ? x - coast : coast - x;
                world->tiles[index].terrain = FD_TERRAIN_GRASSLAND;
                world->tiles[index].elevation_cm =
                    (int32_t)(UINT32_C(100) + distance * UINT32_C(100));
                ++world->land_tile_count;
            }
        }
    }
    result = fd_apply_elevation_fields(world, attempt);
    if (result != FD_OK) {
        return result;
    }
    coastal_x = fd_coast_x(west_water, width, base_water_width);
    inland_x = west_water ? width - UINT32_C(3) : UINT32_C(2);
    result = fd_construct_river(
        world, west_water, base_water_width, outlet_y, source_y,
        river_tie_choice, &outlet_index, &world->river_tile_count);
    if (result != FD_OK) {
        *rejection_reason = FD_GEN_REJECT_HYDROLOGY;
        return result;
    }
    for (y = 0U; y < height; ++y) {
        uint32_t x;
        for (x = 0U; x < width; ++x) {
            uint32_t index = y * width + x;
            uint32_t value;
            if (world->tiles[index].terrain == FD_TERRAIN_WATER ||
                (world->tiles[index].overlays & FD_TILE_OVERLAY_RIVER) != UINT32_C(0)) {
                continue;
            }
            result = fd_rng_bounded(world->root_seed_le,
                                    world->config.generator_version,
                                    world->config.ruleset_version,
                                    FD_RNG_DOMAIN_BIOME_ASSIGNMENT,
                                    (uint64_t)index, attempt, UINT64_C(0),
                                    FD_RNG_SITE_BIOME_TILE, UINT32_C(1000000),
                                    &value);
            if (result != FD_OK) {
                return result;
            }
            if (fd_wetland_eligible(world, index) &&
                value < world->config.wetland_density_ppm) {
                world->tiles[index].terrain = FD_TERRAIN_WETLANDS;
            } else if (value < world->config.hill_density_ppm) {
                world->tiles[index].terrain = FD_TERRAIN_HILLS;
            } else if (value < world->config.hill_density_ppm +
                                world->config.forest_density_ppm) {
                world->tiles[index].terrain = FD_TERRAIN_FOREST;
            } else {
                world->tiles[index].terrain = FD_TERRAIN_GRASSLAND;
            }
        }
    }
    /* Only the final bounded fallback may construct missing representatives.
     * Ordinary attempts preserve threshold outcomes and reject on coverage. */
    if (constructive_fallback) {
        uint32_t middle_x = (coastal_x + inland_x) / UINT32_C(2);
        uint32_t wetland_index = coastal_y * width + middle_x;
        uint32_t forest_index = inland_y * width +
                                (west_water ? inland_x - UINT32_C(1) :
                                              inland_x + UINT32_C(1));
        uint32_t grass_index = inland_y * width +
                               (west_water ? inland_x - UINT32_C(2) :
                                             inland_x + UINT32_C(2));
        /* Wetland directly touches the river one row north at the same x. */
        world->tiles[wetland_index].terrain = FD_TERRAIN_WETLANDS;
        world->tiles[forest_index].terrain = FD_TERRAIN_FOREST;
        world->tiles[grass_index].terrain = FD_TERRAIN_GRASSLAND;
    }
    result = fd_initialize_entities(world, coastal_x, coastal_y, inland_x,
                                    inland_y, outlet_index, attempt);
    if (result != FD_OK) {
        return result;
    }
    {
        uint32_t coastal_index = coastal_y * width + coastal_x;
        uint32_t inland_index = inland_y * width + inland_x;
        world->tiles[coastal_index].terrain = FD_TERRAIN_SETTLEMENT;
        world->tiles[coastal_index].settlement_id = world->entities[1].id;
        world->tiles[inland_index].terrain = FD_TERRAIN_SETTLEMENT;
        world->tiles[inland_index].settlement_id = world->entities[2].id;
        result = fd_construct_road(world, coastal_index, inland_index, attempt);
    }
    if (result != FD_OK) {
        *rejection_reason = FD_GEN_REJECT_ROAD_CORRIDOR;
        return result;
    }
    world->region.river_tile_count = world->river_tile_count;
    world->region.road_tile_count = world->road_tile_count;
    result = fd_world_validate_internal(world, validation, diag);
    if (result != FD_OK) {
        if ((validation->failed_invariants & FD_INVARIANT_TERRAIN_COVERAGE) !=
            UINT64_C(0)) {
            *rejection_reason = FD_GEN_REJECT_TERRAIN_COVERAGE;
        } else if ((validation->failed_invariants & FD_INVARIANT_HYDROLOGY) !=
                   UINT64_C(0)) {
            *rejection_reason = FD_GEN_REJECT_HYDROLOGY;
        } else if ((validation->failed_invariants & FD_INVARIANT_SETTLEMENTS) !=
                   UINT64_C(0)) {
            *rejection_reason = FD_GEN_REJECT_SETTLEMENT_PLACEMENT;
        }
        return FD_ERR_GENERATION_EXHAUSTED;
    }
    return fd_world_compute_hash(world);
}

fd_result fd_world_generate(fd_context *context,
                            const fd_world_config *config,
                            uint64_t seed,
                            const fd_content_manifest *content,
                            fd_world **out,
                            fd_generation_report *report,
                            fd_diagnostic *diag)
{
    fd_generation_report local_report;
    fd_world *world;
    uint64_t tile_bytes;
    uint64_t world_bytes;
    uint32_t attempt;
    fd_result result;
    if (out == NULL) {
        return fd_diag_set(diag, FD_ERR_INVALID_ARGUMENT, FD_FIELD_NONE,
                           UINT32_C(0), "world output is null");
    }
    *out = NULL;
    if (context == NULL) {
        return fd_diag_set(diag, FD_ERR_INVALID_ARGUMENT, FD_FIELD_NONE,
                           UINT32_C(0), "context is null");
    }
    if (report != NULL &&
        !fd_output_struct_valid(report, (uint32_t)sizeof(*report))) {
        return fd_diag_set(diag, FD_ERR_INVALID_SIZE, FD_FIELD_STRUCT_SIZE,
                           UINT32_C(0), "invalid generation report output");
    }
    result = fd_validate_generation_inputs_internal(config, content, diag);
    if (result != FD_OK) {
        return result;
    }
    memset(&local_report, 0, sizeof(local_report));
    local_report.struct_size = (uint32_t)sizeof(local_report);
    local_report.abi_version = FD_ABI_VERSION;
    local_report.accepted_attempt = UINT32_MAX;
    fd_store_u64_le(local_report.root_seed_le, seed);
    fd_hash_config_internal(config, local_report.configuration_sha256);
    fd_hash_content_internal(content, local_report.content_sha256);
    if (config->forest_density_ppm == UINT32_C(0) ||
        config->hill_density_ppm == UINT32_C(0) ||
        config->wetland_density_ppm == UINT32_C(0)) {
        for (attempt = 0U; attempt < config->attempt_budget; ++attempt) {
            local_report.attempts[attempt].attempt_index = attempt;
            local_report.attempts[attempt].reason_code =
                FD_GEN_REJECT_REQUIRED_DENSITY_ZERO;
        }
        local_report.rejected_count = config->attempt_budget;
        local_report.recorded_count = config->attempt_budget;
        if (report != NULL) {
            memcpy(report, &local_report, sizeof(local_report));
        }
        return fd_diag_set(diag, FD_ERR_GENERATION_EXHAUSTED,
                           FD_FIELD_TERRAIN_DENSITY,
                           config->attempt_budget - UINT32_C(1),
                           "required terrain density makes generation impossible");
    }
    tile_bytes = (uint64_t)config->width * (uint64_t)config->height *
                 (uint64_t)sizeof(fd_tile_internal);
    world_bytes = tile_bytes + (uint64_t)sizeof(fd_world);
    if (world_bytes > context->max_live_world_bytes ||
        tile_bytes > (uint64_t)UINT32_MAX) {
        return fd_diag_set(diag, FD_ERR_CAPACITY, FD_FIELD_BUFFER_CAPACITY,
                           UINT32_C(0), "configured world exceeds context byte limit");
    }
    world = (fd_world *)fd_context_alloc(context, (uint64_t)sizeof(*world));
    if (world == NULL) {
        return fd_diag_set(diag, FD_ERR_OUT_OF_MEMORY, FD_FIELD_NONE,
                           UINT32_C(0), "world allocation failed");
    }
    memset(world, 0, sizeof(*world));
    world->context = context;
    world->config = *config;
    world->config.struct_size = (uint32_t)sizeof(world->config);
    world->config.abi_version = FD_ABI_VERSION;
    world->tile_count = config->width * config->height;
    world->allocation_size = (uint32_t)world_bytes;
    fd_store_u64_le(world->root_seed_le, seed);
    world->content_count = content->pack_count;
    if (content->pack_count > UINT32_C(0)) {
        memcpy(world->content, content->packs,
               (size_t)content->pack_count * sizeof(world->content[0]));
    }
    memcpy(world->configuration_sha256, local_report.configuration_sha256, 32U);
    memcpy(world->content_sha256, local_report.content_sha256, 32U);
    world->tiles = (fd_tile_internal *)fd_context_alloc(context, tile_bytes);
    if (world->tiles == NULL) {
        fd_context_free(context, world, (uint64_t)sizeof(*world));
        return fd_diag_set(diag, FD_ERR_OUT_OF_MEMORY, FD_FIELD_NONE,
                           UINT32_C(0), "world tile allocation failed");
    }
    for (attempt = 0U; attempt < config->attempt_budget; ++attempt) {
        fd_validation_report validation;
        uint32_t rejection_reason = FD_GEN_REJECT_INVARIANT;
        memset(&validation, 0, sizeof(validation));
        validation.struct_size = (uint32_t)sizeof(validation);
        validation.abi_version = FD_ABI_VERSION;
        result = fd_generate_candidate(
                                       world, (uint64_t)attempt,
                                       attempt + UINT32_C(1) == config->attempt_budget,
                                       &validation,
                                       &rejection_reason, diag);
        if (result == FD_OK) {
            local_report.accepted_attempt = attempt;
            local_report.land_tile_count = validation.land_tile_count;
            local_report.river_tile_count = validation.river_tile_count;
            local_report.road_tile_count = validation.road_tile_count;
            local_report.river_raised_tile_count =
                world->river_raised_tile_count;
            local_report.river_min_carve_cm = world->river_min_carve_cm;
            memcpy(local_report.initial_state_sha256, world->state_sha256, 32U);
            if (report != NULL) {
                memcpy(report, &local_report, sizeof(local_report));
            }
            (void)atomic_fetch_add_explicit(&context->live_handles, UINT32_C(1),
                                            memory_order_release);
            *out = world;
            return fd_diag_ok(diag);
        }
        local_report.attempts[local_report.recorded_count].attempt_index = attempt;
        local_report.attempts[local_report.recorded_count].reason_code =
            rejection_reason;
        ++local_report.recorded_count;
        ++local_report.rejected_count;
        if (result == FD_ERR_OUT_OF_MEMORY) {
            break;
        }
    }
    fd_context_free(context, world->tiles, tile_bytes);
    fd_context_free(context, world, (uint64_t)sizeof(*world));
    if (report != NULL) {
        memcpy(report, &local_report, sizeof(local_report));
    }
    return fd_diag_set(diag,
                       result == FD_ERR_OUT_OF_MEMORY ? FD_ERR_OUT_OF_MEMORY :
                                                       FD_ERR_GENERATION_EXHAUSTED,
                       FD_FIELD_ATTEMPT_BUDGET,
                       local_report.rejected_count,
                       "bounded generation exhausted without publishing a world");
}
