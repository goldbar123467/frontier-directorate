#include "fd_internal.h"

#include <limits.h>
#include <string.h>

static uint32_t fd_abs_diff_u32(uint32_t left, uint32_t right)
{
    return left >= right ? left - right : right - left;
}

static uint32_t fd_manhattan(uint32_t ax, uint32_t ay,
                             uint32_t bx, uint32_t by)
{
    return fd_abs_diff_u32(ax, bx) + fd_abs_diff_u32(ay, by);
}

static uint32_t fd_neighbor_count_with_overlay(const fd_world *world,
                                                uint32_t index,
                                                uint32_t overlay,
                                                uint32_t previous,
                                                uint32_t *next)
{
    uint32_t width = world->config.width;
    uint32_t height = world->config.height;
    uint32_t x = index % width;
    uint32_t y = index / width;
    uint32_t count = UINT32_C(0);
    uint32_t candidate;

    if (x > UINT32_C(0)) {
        candidate = index - UINT32_C(1);
        if (candidate != previous &&
            (world->tiles[candidate].overlays & overlay) != UINT32_C(0)) {
            *next = candidate;
            ++count;
        }
    }
    if (x + UINT32_C(1) < width) {
        candidate = index + UINT32_C(1);
        if (candidate != previous &&
            (world->tiles[candidate].overlays & overlay) != UINT32_C(0)) {
            *next = candidate;
            ++count;
        }
    }
    if (y > UINT32_C(0)) {
        candidate = index - width;
        if (candidate != previous &&
            (world->tiles[candidate].overlays & overlay) != UINT32_C(0)) {
            *next = candidate;
            ++count;
        }
    }
    if (y + UINT32_C(1) < height) {
        candidate = index + width;
        if (candidate != previous &&
            (world->tiles[candidate].overlays & overlay) != UINT32_C(0)) {
            *next = candidate;
            ++count;
        }
    }
    return count;
}

static bool fd_adjacent_terrain(const fd_world *world,
                                uint32_t index,
                                uint32_t terrain)
{
    uint32_t width = world->config.width;
    uint32_t height = world->config.height;
    uint32_t x = index % width;
    uint32_t y = index / width;
    if (x > UINT32_C(0) && world->tiles[index - UINT32_C(1)].terrain == terrain) {
        return true;
    }
    if (x + UINT32_C(1) < width &&
        world->tiles[index + UINT32_C(1)].terrain == terrain) {
        return true;
    }
    if (y > UINT32_C(0) && world->tiles[index - width].terrain == terrain) {
        return true;
    }
    return y + UINT32_C(1) < height &&
           world->tiles[index + width].terrain == terrain;
}

static bool fd_validate_coast_land(const fd_world *world,
                                   uint32_t land_count)
{
    uint32_t width = world->config.width;
    uint32_t height = world->config.height;
    uint32_t previous_start = UINT32_C(0);
    uint32_t previous_end = UINT32_C(0);
    uint32_t y;
    uint64_t scaled = (uint64_t)land_count * UINT64_C(100);
    uint64_t total = (uint64_t)world->tile_count;
    if (scaled < total * UINT64_C(55) || scaled > total * UINT64_C(80)) {
        return false;
    }
    for (y = 0U; y < height; ++y) {
        uint32_t x;
        uint32_t start = width;
        uint32_t end = UINT32_C(0);
        bool saw_land = false;
        bool gap_after_land = false;
        for (x = 0U; x < width; ++x) {
            bool land = world->tiles[y * width + x].terrain != FD_TERRAIN_WATER;
            if (land) {
                if (gap_after_land) {
                    return false;
                }
                if (!saw_land) {
                    start = x;
                    saw_land = true;
                }
                end = x;
            } else if (saw_land) {
                gap_after_land = true;
            }
        }
        if (!saw_land) {
            return false;
        }
        if (y > UINT32_C(0) &&
            (end < previous_start || start > previous_end)) {
            return false;
        }
        previous_start = start;
        previous_end = end;
    }
    return true;
}

static bool fd_validate_river(const fd_world *world,
                              uint32_t *out_count,
                              uint32_t *out_outlet)
{
    uint32_t index;
    uint32_t count = UINT32_C(0);
    uint32_t endpoints = UINT32_C(0);
    uint32_t outlet_count = UINT32_C(0);
    uint32_t outlet = UINT32_MAX;
    for (index = 0U; index < world->tile_count; ++index) {
        if ((world->tiles[index].overlays & FD_TILE_OVERLAY_RIVER) != UINT32_C(0)) {
            uint32_t next = UINT32_MAX;
            uint32_t degree = fd_neighbor_count_with_overlay(world, index,
                                                              FD_TILE_OVERLAY_RIVER,
                                                              UINT32_MAX, &next);
            ++count;
            if (degree == UINT32_C(1)) {
                ++endpoints;
            } else if (degree != UINT32_C(2)) {
                return false;
            }
            if (world->tiles[index].terrain == FD_TERRAIN_WATER ||
                world->tiles[index].terrain == FD_TERRAIN_SETTLEMENT) {
                return false;
            }
            if ((world->tiles[index].overlays & FD_TILE_RIVER_OUTLET) != UINT32_C(0)) {
                ++outlet_count;
                outlet = index;
                if (!fd_adjacent_terrain(world, index, FD_TERRAIN_WATER) ||
                    degree != UINT32_C(1)) {
                    return false;
                }
            }
        } else if ((world->tiles[index].overlays & FD_TILE_RIVER_OUTLET) != UINT32_C(0)) {
            return false;
        }
    }
    if (count < UINT32_C(9) || endpoints != UINT32_C(2) ||
        outlet_count != UINT32_C(1)) {
        return false;
    }
    {
        uint32_t visited = UINT32_C(1);
        uint32_t previous = UINT32_MAX;
        uint32_t current = outlet;
        while (true) {
            uint32_t next = UINT32_MAX;
            uint32_t remaining = fd_neighbor_count_with_overlay(world, current,
                                                                 FD_TILE_OVERLAY_RIVER,
                                                                 previous, &next);
            if (remaining == UINT32_C(0)) {
                break;
            }
            if (remaining != UINT32_C(1) ||
                world->tiles[next].elevation_cm <=
                world->tiles[current].elevation_cm) {
                return false;
            }
            previous = current;
            current = next;
            ++visited;
            if (visited > count) {
                return false;
            }
        }
        if (visited != count) {
            return false;
        }
        if (world->tiles[current].terrain != FD_TERRAIN_HILLS) {
            return false;
        }
        {
            uint32_t source_x = current % world->config.width;
            uint32_t source_y = current / world->config.width;
            uint32_t minimum_water_distance = UINT32_MAX;
            uint32_t water_index;
            for (water_index = 0U; water_index < world->tile_count; ++water_index) {
                if (world->tiles[water_index].terrain == FD_TERRAIN_WATER) {
                    uint32_t distance = fd_manhattan(
                        source_x, source_y,
                        water_index % world->config.width,
                        water_index / world->config.width);
                    if (distance < minimum_water_distance) {
                        minimum_water_distance = distance;
                    }
                }
            }
            if (minimum_water_distance < UINT32_C(8)) {
                return false;
            }
        }
    }
    *out_count = count;
    *out_outlet = outlet;
    return true;
}

static bool fd_validate_settlements(const fd_world *world,
                                    uint32_t outlet)
{
    const fd_entity_internal *coastal = &world->entities[1];
    const fd_entity_internal *inland = &world->entities[2];
    uint32_t coastal_index;
    uint32_t inland_index;
    uint32_t index;
    uint32_t settlement_tiles = UINT32_C(0);
    uint32_t inland_sea_distance = UINT32_MAX;
    if (coastal->kind != FD_ENTITY_SETTLEMENT_COASTAL ||
        inland->kind != FD_ENTITY_SETTLEMENT_INLAND ||
        coastal->x >= world->config.width || coastal->y >= world->config.height ||
        inland->x >= world->config.width || inland->y >= world->config.height) {
        return false;
    }
    coastal_index = coastal->y * world->config.width + coastal->x;
    inland_index = inland->y * world->config.width + inland->x;
    if (world->tiles[coastal_index].terrain != FD_TERRAIN_SETTLEMENT ||
        world->tiles[coastal_index].settlement_id != coastal->id ||
        world->tiles[inland_index].terrain != FD_TERRAIN_SETTLEMENT ||
        world->tiles[inland_index].settlement_id != inland->id ||
        !fd_adjacent_terrain(world, coastal_index, FD_TERRAIN_WATER) ||
        fd_manhattan(coastal->x, coastal->y,
                     outlet % world->config.width,
                     outlet / world->config.width) != UINT32_C(1) ||
        fd_manhattan(coastal->x, coastal->y, inland->x, inland->y) < UINT32_C(12)) {
        return false;
    }
    for (index = 0U; index < world->tile_count; ++index) {
        if (world->tiles[index].terrain == FD_TERRAIN_SETTLEMENT) {
            ++settlement_tiles;
        } else if (world->tiles[index].settlement_id != UINT64_C(0)) {
            return false;
        }
        if (world->tiles[index].terrain == FD_TERRAIN_WATER) {
            uint32_t distance = fd_manhattan(inland->x, inland->y,
                                              index % world->config.width,
                                              index / world->config.width);
            if (distance < inland_sea_distance) {
                inland_sea_distance = distance;
            }
        }
    }
    return settlement_tiles == UINT32_C(2) && inland_sea_distance >= UINT32_C(4);
}

static bool fd_validate_road(const fd_world *world,
                             uint32_t *out_count,
                             uint32_t *out_crossings)
{
    uint32_t index;
    uint32_t count = UINT32_C(0);
    uint32_t endpoints = UINT32_C(0);
    uint32_t start = UINT32_MAX;
    uint32_t crossing_entries = UINT32_C(0);
    uint32_t explicit_crossings = UINT32_C(0);
    uint32_t river_intersections = UINT32_C(0);
    uint32_t coastal_index;
    uint32_t inland_index;
    if (world->entities[1].x >= world->config.width ||
        world->entities[1].y >= world->config.height ||
        world->entities[2].x >= world->config.width ||
        world->entities[2].y >= world->config.height) {
        return false;
    }
    coastal_index = world->entities[1].y * world->config.width +
                     world->entities[1].x;
    inland_index = world->entities[2].y * world->config.width +
                   world->entities[2].x;
    for (index = 0U; index < world->tile_count; ++index) {
        if ((world->tiles[index].overlays & FD_TILE_OVERLAY_ROAD_CANDIDATE) !=
            UINT32_C(0)) {
            uint32_t next = UINT32_MAX;
            uint32_t degree = fd_neighbor_count_with_overlay(
                world, index, FD_TILE_OVERLAY_ROAD_CANDIDATE, UINT32_MAX, &next);
            ++count;
            if (world->tiles[index].terrain == FD_TERRAIN_WATER) {
                return false;
            }
            if (degree == UINT32_C(1)) {
                ++endpoints;
                if (start == UINT32_MAX) {
                    start = index;
                }
            } else if (degree != UINT32_C(2)) {
                return false;
            }
            if ((world->tiles[index].overlays & FD_TILE_ROAD_CROSSING) !=
                UINT32_C(0)) {
                ++explicit_crossings;
                if ((world->tiles[index].overlays & FD_TILE_OVERLAY_RIVER) ==
                    UINT32_C(0)) {
                    return false;
                }
            }
            if ((world->tiles[index].overlays & FD_TILE_OVERLAY_RIVER) !=
                UINT32_C(0)) {
                ++river_intersections;
            }
        } else if ((world->tiles[index].overlays & FD_TILE_ROAD_CROSSING) !=
                   UINT32_C(0)) {
            return false;
        }
    }
    if (count < UINT32_C(2) || endpoints != UINT32_C(2) ||
        explicit_crossings > UINT32_C(1) || river_intersections > UINT32_C(1) ||
        (world->tiles[coastal_index].overlays & FD_TILE_OVERLAY_ROAD_CANDIDATE) ==
            UINT32_C(0) ||
        (world->tiles[inland_index].overlays & FD_TILE_OVERLAY_ROAD_CANDIDATE) ==
            UINT32_C(0)) {
        return false;
    }
    {
        uint32_t visited = UINT32_C(1);
        uint32_t previous = UINT32_MAX;
        uint32_t current = start;
        bool previous_river =
            (world->tiles[current].overlays & FD_TILE_OVERLAY_RIVER) != UINT32_C(0);
        while (true) {
            uint32_t next = UINT32_MAX;
            uint32_t remaining = fd_neighbor_count_with_overlay(
                world, current, FD_TILE_OVERLAY_ROAD_CANDIDATE, previous, &next);
            if (remaining == UINT32_C(0)) {
                break;
            }
            if (remaining != UINT32_C(1)) {
                return false;
            }
            {
                bool next_river =
                    (world->tiles[next].overlays & FD_TILE_OVERLAY_RIVER) != UINT32_C(0);
                if (next_river && !previous_river) {
                    ++crossing_entries;
                }
                previous_river = next_river;
            }
            previous = current;
            current = next;
            ++visited;
            if (visited > count) {
                return false;
            }
        }
        if (visited != count ||
            !((start == coastal_index && current == inland_index) ||
              (start == inland_index && current == coastal_index))) {
            return false;
        }
    }
    if (crossing_entries > UINT32_C(1) ||
        explicit_crossings != river_intersections ||
        crossing_entries != river_intersections) {
        return false;
    }
    *out_count = count;
    *out_crossings = crossing_entries;
    return true;
}

static bool fd_validate_ownership_graph(const fd_world *world,
                                        uint32_t river_count,
                                        uint32_t road_count,
                                        uint32_t outlet)
{
    const fd_entity_internal *region = &world->entities[0];
    const fd_entity_internal *coastal = &world->entities[1];
    const fd_entity_internal *inland = &world->entities[2];
    const fd_entity_internal *polity = &world->entities[3];
    const fd_entity_internal *foreign0 = &world->entities[4];
    const fd_entity_internal *foreign1 = &world->entities[5];
    uint32_t coastal_index;
    uint32_t inland_index;
    uint32_t entity_index;
    for (entity_index = 0U; entity_index < FD_U01_ENTITY_COUNT; ++entity_index) {
        if (world->entities[entity_index].id !=
                fd_make_entity_id(entity_index + UINT32_C(1)) ||
            world->entities[entity_index].name_length >= FD_MAX_NAME_BYTES) {
            return false;
        }
    }
    if (coastal->x >= world->config.width || coastal->y >= world->config.height ||
        inland->x >= world->config.width || inland->y >= world->config.height ||
        region->x != UINT32_MAX || region->y != UINT32_MAX ||
        polity->x != UINT32_MAX || polity->y != UINT32_MAX ||
        foreign0->x != UINT32_MAX || foreign0->y != UINT32_MAX ||
        foreign1->x != UINT32_MAX || foreign1->y != UINT32_MAX) {
        return false;
    }
    coastal_index = coastal->y * world->config.width + coastal->x;
    inland_index = inland->y * world->config.width + inland->x;
    return region->kind == FD_ENTITY_REGION && polity->kind == FD_ENTITY_POLITY_LOCAL &&
           foreign0->kind == FD_ENTITY_FACTION_FOREIGN &&
           foreign1->kind == FD_ENTITY_FACTION_FOREIGN &&
           coastal->owner_id == polity->id && inland->owner_id == polity->id &&
           region->owner_id == polity->id && polity->owner_id == polity->id &&
           foreign0->owner_id == foreign0->id && foreign1->owner_id == foreign1->id &&
           region->region_id == region->id &&
           coastal->region_id == region->id && inland->region_id == region->id &&
           polity->region_id == region->id && foreign0->region_id == region->id &&
           foreign1->region_id == region->id && world->region.id == region->id &&
           world->region.political_owner_id == polity->id &&
           world->region.settlement_ids[0] == coastal->id &&
           world->region.settlement_ids[1] == inland->id &&
           world->region.foreign_faction_ids[0] == foreign0->id &&
           world->region.foreign_faction_ids[1] == foreign1->id &&
           world->region.river_outlet_tile_id == fd_tile_id_from_index(outlet) &&
           world->region.road_start_tile_id == fd_tile_id_from_index(coastal_index) &&
           world->region.road_end_tile_id == fd_tile_id_from_index(inland_index) &&
           world->region.river_tile_count == river_count &&
           world->region.road_tile_count == road_count;
}

fd_result fd_world_validate_internal(const fd_world *world,
                                     fd_validation_report *report,
                                     fd_diagnostic *diag)
{
    uint64_t passed = UINT64_C(0);
    uint32_t index;
    uint32_t terrain_counts[7] = {0U, 0U, 0U, 0U, 0U, 0U, 0U};
    uint32_t land = UINT32_C(0);
    uint32_t water = UINT32_C(0);
    uint32_t river_count = UINT32_C(0);
    uint32_t road_count = UINT32_C(0);
    uint32_t crossings = UINT32_C(0);
    uint32_t outlet = UINT32_MAX;
    if (world == NULL || world->tiles == NULL || report == NULL) {
        return fd_diag_set(diag, FD_ERR_INVALID_ARGUMENT, FD_FIELD_NONE,
                           UINT32_C(0), "world validation input is null");
    }
    report->first_failed_tile_index = UINT32_MAX;
    if (world->config.width >= FD_MIN_WIDTH && world->config.width <= FD_MAX_WIDTH &&
        world->config.height >= FD_MIN_HEIGHT && world->config.height <= FD_MAX_HEIGHT &&
        world->tile_count == world->config.width * world->config.height &&
        world->tile_count <= FD_MAX_TILES) {
        passed |= FD_INVARIANT_BOUNDS;
    }
    for (index = 0U; index < world->tile_count; ++index) {
        uint32_t terrain = world->tiles[index].terrain;
        if ((world->tiles[index].overlays &
             ~(FD_TILE_OVERLAY_RIVER | FD_TILE_OVERLAY_ROAD_CANDIDATE |
               FD_TILE_RIVER_OUTLET | FD_TILE_ROAD_CROSSING)) != UINT32_C(0)) {
            report->first_failed_tile_index = index;
        }
        if (terrain >= FD_TERRAIN_WATER && terrain <= FD_TERRAIN_SETTLEMENT) {
            ++terrain_counts[terrain];
        } else if (report->first_failed_tile_index == UINT32_MAX) {
            report->first_failed_tile_index = index;
        }
        if (terrain == FD_TERRAIN_WATER) {
            ++water;
            if (world->tiles[index].elevation_cm != INT32_C(0)) {
                report->first_failed_tile_index = index;
            }
        } else {
            ++land;
            if (world->tiles[index].elevation_cm <= INT32_C(0)) {
                report->first_failed_tile_index = index;
            }
        }
        if (world->tiles[index].rainfall_mm > UINT32_C(5000)) {
            report->first_failed_tile_index = index;
        }
        if (terrain == FD_TERRAIN_WETLANDS &&
            !fd_adjacent_terrain(world, index, FD_TERRAIN_WATER) &&
            (world->tiles[index].overlays & FD_TILE_OVERLAY_RIVER) == UINT32_C(0)) {
            uint32_t next = UINT32_MAX;
            if (fd_neighbor_count_with_overlay(world, index, FD_TILE_OVERLAY_RIVER,
                                               UINT32_MAX, &next) == UINT32_C(0)) {
                report->first_failed_tile_index = index;
            }
        }
    }
    if (report->first_failed_tile_index == UINT32_MAX &&
        fd_validate_coast_land(world, land)) {
        passed |= FD_INVARIANT_LAND_COAST;
    }
    if (terrain_counts[FD_TERRAIN_WATER] > UINT32_C(0) &&
        terrain_counts[FD_TERRAIN_GRASSLAND] > UINT32_C(0) &&
        terrain_counts[FD_TERRAIN_FOREST] > UINT32_C(0) &&
        terrain_counts[FD_TERRAIN_HILLS] > UINT32_C(0) &&
        terrain_counts[FD_TERRAIN_WETLANDS] > UINT32_C(0) &&
        terrain_counts[FD_TERRAIN_SETTLEMENT] == UINT32_C(2)) {
        passed |= FD_INVARIANT_TERRAIN_COVERAGE;
    }
    if (fd_validate_river(world, &river_count, &outlet) &&
        world->river_tile_count == river_count) {
        passed |= FD_INVARIANT_HYDROLOGY;
    }
    if (outlet != UINT32_MAX && fd_validate_settlements(world, outlet)) {
        passed |= FD_INVARIANT_SETTLEMENTS;
    }
    if (fd_validate_road(world, &road_count, &crossings) &&
        world->road_tile_count == road_count) {
        passed |= FD_INVARIANT_ROAD_CORRIDOR;
        passed |= FD_INVARIANT_OBJECTIVE_REACHABLE;
    }
    if (outlet != UINT32_MAX &&
        fd_validate_ownership_graph(world, river_count, road_count, outlet)) {
        passed |= FD_INVARIANT_OWNERSHIP;
        passed |= FD_INVARIANT_GRAPH_TILE_AGREEMENT;
    }
    report->passed_invariants = passed;
    report->failed_invariants = FD_INVARIANT_ALL & ~passed;
    report->land_tile_count = land;
    report->water_tile_count = water;
    report->river_tile_count = river_count;
    report->road_tile_count = road_count;
    report->road_river_crossings = crossings;
    if (world->land_tile_count != land) {
        report->failed_invariants |= FD_INVARIANT_LAND_COAST;
        report->passed_invariants &= ~FD_INVARIANT_LAND_COAST;
    }
    if (report->failed_invariants != UINT64_C(0)) {
        return fd_diag_set(diag, FD_ERR_STATE, FD_FIELD_NONE,
                           report->first_failed_tile_index,
                           "world invariant validation failed");
    }
    return fd_diag_ok(diag);
}

fd_result fd_world_validate(const fd_world *world,
                            fd_validation_report *report,
                            fd_diagnostic *diag)
{
    if (!fd_output_struct_valid(report, (uint32_t)sizeof(*report))) {
        return fd_diag_set(diag, FD_ERR_INVALID_SIZE, FD_FIELD_STRUCT_SIZE,
                           UINT32_C(0), "invalid validation report output");
    }
    return fd_world_validate_internal(world, report, diag);
}
