#include "ascii_renderer.hpp"

#include <algorithm>
#include <array>
#include <charconv>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <exception>
#include <limits>
#include <new>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace frontier_directorate::terminal {
namespace {

constexpr std::uint32_t kMinimumColumns = FD_ASCII_MIN_COLUMNS;
constexpr std::uint32_t kMinimumRows = FD_ASCII_MIN_ROWS;
constexpr std::uint32_t kMaximumColumns = FD_ASCII_MAX_COLUMNS;
constexpr std::uint32_t kMaximumRows = FD_ASCII_MAX_ROWS;
constexpr std::uint32_t kInspectorWidth = 24U;
constexpr std::uint32_t kMapPrefixWidth = 4U;

Error render_error(const fd_result code, std::string message) {
    Error error{};
    error.code = code;
    error.message = std::move(message);
    return error;
}

template <class Integer>
std::string decimal(const Integer value) {
    std::array<char, 32U> buffer{};
    const auto converted =
        std::to_chars(buffer.data(), buffer.data() + buffer.size(), value);
    if (converted.ec != std::errc{}) {
        return "?";
    }
    return std::string(buffer.data(), converted.ptr);
}

std::string ascii_text(const char* const bytes, const std::size_t length) {
    std::string result;
    result.reserve(length);
    for (std::size_t index = 0U; index < length; ++index) {
        const unsigned char byte = static_cast<unsigned char>(bytes[index]);
        result.push_back(byte >= 0x20U && byte <= 0x7eU
                             ? static_cast<char>(byte)
                             : '?');
    }
    return result;
}

std::string ascii_text(const std::string_view text) {
    return ascii_text(text.data(), text.size());
}

std::string entity_name(const fd_entity_view& entity) {
    const std::uint32_t bounded =
        std::min(entity.name_length, FD_MAX_NAME_BYTES);
    return ascii_text(entity.name, static_cast<std::size_t>(bounded));
}

std::string hex_prefix(const std::uint8_t* const bytes,
                       const std::size_t byte_count) {
    static constexpr std::array<char, 16U> digits{
        '0', '1', '2', '3', '4', '5', '6', '7',
        '8', '9', 'a', 'b', 'c', 'd', 'e', 'f'};
    std::string result;
    result.reserve(byte_count * 2U);
    for (std::size_t index = 0U; index < byte_count; ++index) {
        const std::uint8_t byte = bytes[index];
        result.push_back(digits[static_cast<std::size_t>(byte >> 4U)]);
        result.push_back(digits[static_cast<std::size_t>(byte & UINT8_C(0x0f))]);
    }
    return result;
}

std::string seed_hex(const fd_world_info& info) {
    static constexpr std::array<char, 16U> digits{
        '0', '1', '2', '3', '4', '5', '6', '7',
        '8', '9', 'a', 'b', 'c', 'd', 'e', 'f'};
    std::string result;
    result.reserve(32U);
    for (std::size_t index = 0U; index < 16U; ++index) {
        const std::uint8_t byte = info.root_seed_le[15U - index];
        result.push_back(digits[static_cast<std::size_t>(byte >> 4U)]);
        result.push_back(digits[static_cast<std::size_t>(byte & UINT8_C(0x0f))]);
    }
    return result;
}

std::string seed_short(const fd_world_info& info) {
    const std::string full = seed_hex(info);
    return full.substr(16U);
}

std::string fit(std::string text, const std::uint32_t columns) {
    const std::size_t capacity = static_cast<std::size_t>(columns);
    if (text.size() > capacity) {
        text.resize(capacity);
    }
    while (!text.empty() && text.back() == ' ') {
        text.pop_back();
    }
    return text;
}

std::string compose_columns(std::string left,
                            std::string right,
                            const std::uint32_t left_width,
                            const std::uint32_t total_width) {
    left = fit(std::move(left), left_width);
    if (left.size() < static_cast<std::size_t>(left_width)) {
        left.append(static_cast<std::size_t>(left_width) - left.size(), ' ');
    }
    left.append("  ");
    if (left.size() >= static_cast<std::size_t>(total_width)) {
        return fit(std::move(left), total_width);
    }
    const std::uint32_t remaining = total_width -
                                    static_cast<std::uint32_t>(left.size());
    right = fit(std::move(right), remaining);
    left.append(right);
    return fit(std::move(left), total_width);
}

std::string pad_number(const std::uint32_t value, const std::uint32_t width) {
    std::string result = decimal(value);
    const std::size_t requested = static_cast<std::size_t>(width);
    if (result.size() < requested) {
        result.insert(result.begin(), requested - result.size(), ' ');
    }
    return result;
}

std::string coordinate(const std::uint32_t x, const std::uint32_t y) {
    return "(" + decimal(x) + "," + decimal(y) + ")";
}

std::string terrain_name(const fd_terrain terrain) {
    switch (terrain) {
        case FD_TERRAIN_WATER:
            return "water";
        case FD_TERRAIN_GRASSLAND:
            return "grassland";
        case FD_TERRAIN_FOREST:
            return "forest";
        case FD_TERRAIN_HILLS:
            return "hills";
        case FD_TERRAIN_WETLANDS:
            return "wetlands";
        case FD_TERRAIN_SETTLEMENT:
            return "settlement";
        default:
            return "INVALID(" + decimal(terrain) + ")";
    }
}

std::string overlay_names(const std::uint32_t overlays) {
    std::string result;
    const auto add = [&result](const std::string_view name) {
        if (!result.empty()) {
            result.push_back(',');
        }
        result.append(name);
    };
    if ((overlays & FD_TILE_OVERLAY_RIVER) != 0U) {
        add("river");
    }
    if ((overlays & FD_TILE_OVERLAY_ROAD_CANDIDATE) != 0U) {
        add("road-candidate");
    }
    if ((overlays & FD_TILE_RIVER_OUTLET) != 0U) {
        add("outlet");
    }
    if ((overlays & FD_TILE_ROAD_CROSSING) != 0U) {
        add("crossing");
    }
    return result.empty() ? "none" : result;
}

const fd_entity_view* find_entity(const std::vector<fd_entity_view>& entities,
                                  const fd_entity_id id) noexcept {
    const auto found = std::find_if(
        entities.begin(), entities.end(),
        [id](const fd_entity_view& entity) { return entity.id == id; });
    return found == entities.end() ? nullptr : &*found;
}

std::string identity(const std::vector<fd_entity_view>& entities,
                     const fd_entity_id id) {
    if (id == 0U) {
        return "none";
    }
    const fd_entity_view* const entity = find_entity(entities, id);
    if (entity == nullptr) {
        return "INVALID-ID #" + decimal(id);
    }
    return entity_name(*entity) + " #" + decimal(id);
}

Result<std::vector<fd_entity_view>> capture_entities(
    const Snapshot& snapshot,
    const fd_world_info& info) {
    std::vector<fd_entity_view> entities;
    entities.reserve(static_cast<std::size_t>(info.entity_count));
    for (std::uint32_t index = 0U; index < info.entity_count; ++index) {
        auto entity = snapshot.entity_by_index(index);
        if (!entity) {
            return Result<std::vector<fd_entity_view>>::failure(entity.error());
        }
        entities.push_back(entity.value());
    }
    return Result<std::vector<fd_entity_view>>::success(std::move(entities));
}

Result<std::vector<fd_tile_view>> capture_tiles(const Snapshot& snapshot,
                                                const fd_world_info& info) {
    const std::uint64_t count = static_cast<std::uint64_t>(info.width) *
                                static_cast<std::uint64_t>(info.height);
    if (count > static_cast<std::uint64_t>(
                    std::numeric_limits<std::size_t>::max())) {
        return Result<std::vector<fd_tile_view>>::failure(render_error(
            FD_ERR_CAPACITY, "tile snapshot does not fit address space"));
    }
    std::vector<fd_tile_view> tiles;
    tiles.reserve(static_cast<std::size_t>(count));
    for (std::uint32_t y = 0U; y < info.height; ++y) {
        for (std::uint32_t x = 0U; x < info.width; ++x) {
            auto tile = snapshot.tile(x, y);
            if (!tile) {
                return Result<std::vector<fd_tile_view>>::failure(tile.error());
            }
            tiles.push_back(tile.value());
        }
    }
    return Result<std::vector<fd_tile_view>>::success(std::move(tiles));
}

const fd_tile_view* find_tile(const std::vector<fd_tile_view>& tiles,
                              const std::uint64_t id) noexcept {
    const auto found = std::find_if(
        tiles.begin(), tiles.end(),
        [id](const fd_tile_view& tile) { return tile.tile_id == id; });
    return found == tiles.end() ? nullptr : &*found;
}

Result<char> unselected_glyph(const fd_tile_view& tile,
                              const fd_world_info& info) {
    if (tile.settlement_id == info.coastal_settlement_id) {
        return Result<char>::success('c');
    }
    if (tile.settlement_id == info.inland_settlement_id) {
        return Result<char>::success('n');
    }
    if ((tile.overlays & FD_TILE_OVERLAY_ROAD_CANDIDATE) != 0U) {
        return Result<char>::success(':');
    }
    if ((tile.overlays & FD_TILE_OVERLAY_RIVER) != 0U) {
        return Result<char>::success('r');
    }
    switch (tile.terrain) {
        case FD_TERRAIN_WATER:
            return Result<char>::success('~');
        case FD_TERRAIN_GRASSLAND:
            return Result<char>::success('.');
        case FD_TERRAIN_FOREST:
            return Result<char>::success('f');
        case FD_TERRAIN_HILLS:
            return Result<char>::success('^');
        case FD_TERRAIN_WETLANDS:
            return Result<char>::success('v');
        case FD_TERRAIN_SETTLEMENT:
            return Result<char>::failure(render_error(
                FD_ERR_STATE,
                "settlement terrain had no known settlement identity"));
        default:
            return Result<char>::failure(
                render_error(FD_ERR_STATE, "unknown terrain wire value"));
    }
}

std::string date_text(const std::uint64_t tick) {
    constexpr std::uint64_t ticks_per_day = UINT64_C(24);
    constexpr std::uint64_t ticks_per_year = UINT64_C(8640);
    const std::uint64_t year = tick / ticks_per_year + UINT64_C(1);
    const std::uint64_t day =
        (tick % ticks_per_year) / ticks_per_day + UINT64_C(1);
    return "Day " + decimal(day) + ", Year " + decimal(year);
}

std::string local_status(const fd_world_info& info) {
    return "Tick " + decimal(info.tick) + " | " + date_text(info.tick) +
           " | PAUSED | Terminal: N/A-U01";
}

std::string footer_message(const RenderOptions& options) {
    return "Message: " + ascii_text(options.message);
}

std::string local_help() {
    return "Keys: ? help q quit arrows/hjkl move Tab pane Enter inspect m map pass";
}

std::string strategic_help() {
    return "Keys: ? help q quit m local-map Tab pane messages replay pass";
}

void pad_before_footer(std::vector<std::string>& lines,
                       const std::uint32_t rows,
                       const std::size_t footer_lines) {
    const std::size_t requested = static_cast<std::size_t>(rows);
    while (lines.size() + footer_lines < requested) {
        lines.emplace_back();
    }
}

std::string finalize(std::vector<std::string> lines,
                     const std::uint32_t columns,
                     const std::uint32_t rows) {
    const std::size_t row_count = static_cast<std::size_t>(rows);
    if (lines.size() > row_count) {
        lines.resize(row_count);
    }
    while (lines.size() < row_count) {
        lines.emplace_back();
    }

    std::string result;
    result.reserve(static_cast<std::size_t>(columns) * row_count + row_count);
    for (std::string& line : lines) {
        result.append(fit(ascii_text(line), columns));
        result.push_back('\n');
    }
    return result;
}

Result<std::string> render_size_notice(const RenderOptions& options) {
    std::vector<std::string> lines;
    lines.emplace_back(
        "FRONTIER DIRECTORATE | TERMINAL SIZE NOTICE | ADMIN/OMNISCIENT REFERENCE");
    lines.emplace_back("Minimum supported terminal: 80x24");
    lines.emplace_back("Requested terminal: " + decimal(options.columns) + "x" +
                       decimal(options.rows));
    lines.emplace_back("Simulation state was not mutated or discarded.");
    lines.emplace_back("Resize and render again. Keys: ?=help q=quit");
    return Result<std::string>::success(
        finalize(std::move(lines), options.columns, options.rows));
}

Result<std::string> render_local(const Snapshot& snapshot,
                                 const RenderOptions& options,
                                 const fd_world_info& info,
                                 const std::vector<fd_entity_view>& entities) {
    if (info.width == 0U || info.height == 0U) {
        return Result<std::string>::failure(
            render_error(FD_ERR_STATE, "world dimensions are zero"));
    }

    const std::uint32_t selected_x =
        std::min(options.selected_x, info.width - 1U);
    const std::uint32_t selected_y =
        std::min(options.selected_y, info.height - 1U);
    const std::uint32_t available_map = options.columns -
                                        kInspectorWidth - 2U -
                                        kMapPrefixWidth - 2U;
    const std::uint32_t view_width = std::min(available_map, info.width);
    const std::uint32_t maximum_map_rows = options.rows - 9U;
    const std::uint32_t view_height = std::min(maximum_map_rows, info.height);
    // Normalize the origin rather than shrinking an edge viewport. This keeps
    // the independent inspector panel present and complete at every pan bound.
    const std::uint32_t origin_x =
        std::min(options.viewport_x, info.width - view_width);
    const std::uint32_t origin_y =
        std::min(options.viewport_y, info.height - view_height);
    const std::uint32_t left_width =
        kMapPrefixWidth + 1U + view_width + 1U;

    auto selected_result = snapshot.tile(selected_x, selected_y);
    if (!selected_result) {
        return Result<std::string>::failure(selected_result.error());
    }
    const fd_tile_view& selected = selected_result.value();
    auto selected_glyph_result = unselected_glyph(selected, info);
    if (!selected_glyph_result) {
        return Result<std::string>::failure(selected_glyph_result.error());
    }

    std::vector<std::string> inspector;
    inspector.emplace_back("Scope: REFERENCE snapshot");
    inspector.emplace_back("Selected: " + coordinate(selected_x, selected_y));
    inspector.emplace_back("Glyph: @ over " +
                           std::string(1U, selected_glyph_result.value()));
    inspector.emplace_back("Terrain: " + terrain_name(selected.terrain));
    inspector.emplace_back("Elev/rain: " + decimal(selected.elevation_cm) +
                           "cm / " + decimal(selected.rainfall_mm) + "mm");
    inspector.emplace_back("Overlays: " + overlay_names(selected.overlays));
    inspector.emplace_back("Settlement: " +
                           identity(entities, selected.settlement_id));
    inspector.emplace_back("Region: " + identity(entities, selected.region_id));
    inspector.emplace_back("Tile owner: " +
                           identity(entities, selected.polity_owner_id));
    inspector.emplace_back("Local polity: " +
                           identity(entities, info.local_polity_id));
    inspector.emplace_back("Foreign A: " +
                           identity(entities, info.foreign_faction_ids[0]));
    inspector.emplace_back("Foreign B: " +
                           identity(entities, info.foreign_faction_ids[1]));
    inspector.emplace_back("Manifest: " +
                           hex_prefix(info.content_sha256, 4U));
    inspector.emplace_back("State: " + hex_prefix(info.state_sha256, 4U));
    inspector.emplace_back("Seed: " + seed_short(info));

    std::vector<std::string> lines;
    lines.emplace_back(
        "FRONTIER DIRECTORATE | LOCAL TILE MAP | ADMIN/OMNISCIENT REFERENCE");
    lines.emplace_back(fit(local_status(info), options.columns));

    std::string x_axis(kMapPrefixWidth + 1U, ' ');
    for (std::uint32_t offset = 0U; offset < view_width; ++offset) {
        x_axis.push_back(static_cast<char>('0' + ((origin_x + offset) % 10U)));
    }
    lines.push_back(compose_columns(std::move(x_axis),
                                    "INSPECTOR | immutable C snapshot",
                                    left_width, options.columns));
    lines.push_back(compose_columns(
        std::string(kMapPrefixWidth, ' ') + "+" +
            std::string(static_cast<std::size_t>(view_width), '-') + "+",
        inspector[0], left_width, options.columns));

    for (std::uint32_t row = 0U; row < view_height; ++row) {
        const std::uint32_t y = origin_y + row;
        std::string map_line = pad_number(y, 3U) + " |";
        for (std::uint32_t column = 0U; column < view_width; ++column) {
            const std::uint32_t x = origin_x + column;
            auto tile_result = snapshot.tile(x, y);
            if (!tile_result) {
                return Result<std::string>::failure(tile_result.error());
            }
            char glyph = '@';
            if (x != selected_x || y != selected_y) {
                auto glyph_result = unselected_glyph(tile_result.value(), info);
                if (!glyph_result) {
                    return Result<std::string>::failure(glyph_result.error());
                }
                glyph = glyph_result.value();
            }
            map_line.push_back(glyph);
        }
        map_line.push_back('|');
        const std::size_t inspector_index = static_cast<std::size_t>(row) + 1U;
        const std::string right = inspector_index < inspector.size()
                                      ? inspector[inspector_index]
                                      : std::string{};
        lines.push_back(compose_columns(std::move(map_line), right, left_width,
                                        options.columns));
    }
    lines.push_back(compose_columns(
        std::string(kMapPrefixWidth, ' ') + "+" +
            std::string(static_cast<std::size_t>(view_width), '-') + "+",
        view_height + 1U < inspector.size() ? inspector[view_height + 1U]
                                             : std::string{},
        left_width, options.columns));

    pad_before_footer(lines, options.rows, 4U);
    lines.emplace_back(
        "Legend: ~=water .=grass f=forest ^=hills v=wetland r=river");
    lines.emplace_back(
        "Legend: :=road-candidate c=coastal n=inland @=selected/inspect");
    lines.push_back(footer_message(options));
    lines.push_back(local_help());
    return Result<std::string>::success(
        finalize(std::move(lines), options.columns, options.rows));
}

Result<std::string> render_strategic(
    const Snapshot& snapshot,
    const RenderOptions& options,
    const fd_world_info& info,
    const std::vector<fd_entity_view>& entities) {
    auto region_result = snapshot.region();
    if (!region_result) {
        return Result<std::string>::failure(region_result.error());
    }
    const fd_region_view& region = region_result.value();
    auto tiles_result = capture_tiles(snapshot, info);
    if (!tiles_result) {
        return Result<std::string>::failure(tiles_result.error());
    }
    const std::vector<fd_tile_view>& tiles = tiles_result.value();
    const fd_tile_view* const outlet =
        find_tile(tiles, region.river_outlet_tile_id);
    const fd_tile_view* const road_start =
        find_tile(tiles, region.road_start_tile_id);
    const fd_tile_view* const road_end =
        find_tile(tiles, region.road_end_tile_id);
    if (outlet == nullptr || road_start == nullptr || road_end == nullptr) {
        return Result<std::string>::failure(render_error(
            FD_ERR_STATE,
            "strategic physical reference did not resolve to a public tile"));
    }

    const auto node_line = [&options](std::string body) {
        if (options.columns <= 4U) {
            return fit(std::move(body), options.columns);
        }
        body = fit(std::move(body), options.columns - 4U);
        return "| " + body +
               std::string(static_cast<std::size_t>(options.columns - 4U) -
                               body.size(),
                           ' ') +
               " |";
    };

    std::vector<std::string> lines;
    lines.emplace_back(
        "FRONTIER DIRECTORATE | STRATEGIC REGION GRAPH | ADMIN/OMNISCIENT REFERENCE");
    lines.emplace_back("Scale: REGION | Tick " + decimal(info.tick) + " | " +
                       date_text(info.tick) +
                       " | PAUSED | Terminal: N/A-U01");
    lines.emplace_back("+" +
                       std::string(static_cast<std::size_t>(options.columns - 2U),
                                   '-') +
                       "+");
    lines.push_back(node_line("[R] " + identity(entities, region.id)));
    lines.push_back(node_line("    owner -> [P] " +
                              identity(entities, region.political_owner_id)));

    for (std::uint32_t index = 0U;
         index < std::min(region.settlement_count, UINT32_C(2)); ++index) {
        const fd_entity_view* const settlement =
            find_entity(entities, region.settlement_ids[index]);
        if (settlement == nullptr) {
            return Result<std::string>::failure(render_error(
                FD_ERR_STATE, "region settlement identity was not queryable"));
        }
        const std::string marker =
            settlement->kind == FD_ENTITY_SETTLEMENT_COASTAL ? "[C] " : "[I] ";
        lines.push_back(node_line("    member -> " + marker +
                                  identity(entities, settlement->id) + " @" +
                                  coordinate(settlement->x, settlement->y)));
    }
    lines.push_back(node_line("    river ~> outlet tile #" +
                              decimal(region.river_outlet_tile_id) + " @" +
                              coordinate(outlet->x, outlet->y) + " (" +
                              decimal(region.river_tile_count) + " tiles)"));
    lines.push_back(node_line("    road  [C] :: [I] candidate " +
                              coordinate(road_start->x, road_start->y) + " -> " +
                              coordinate(road_end->x, road_end->y) + " (" +
                              decimal(region.road_tile_count) + " tiles)"));
    for (std::uint32_t index = 0U;
         index < std::min(region.foreign_presence_count, FD_U01_SEAT_COUNT);
         ++index) {
        lines.push_back(node_line("    presence -> [F] " +
                                  identity(entities,
                                           region.foreign_faction_ids[index])));
    }
    lines.push_back(node_line("    manifest " +
                              hex_prefix(info.content_sha256, 4U) +
                              " | state " + hex_prefix(info.state_sha256, 4U)));
    lines.emplace_back("+" +
                       std::string(static_cast<std::size_t>(options.columns - 2U),
                                   '-') +
                       "+");

    pad_before_footer(lines, options.rows, 4U);
    lines.emplace_back(
        "Legend: [R]=region [P]=polity [C]=coastal [I]=inland [F]=foreign");
    lines.emplace_back(
        "Legend: ->=relation ~>=river outlet ::=road candidate @x,y=coordinates");
    lines.push_back(footer_message(options));
    lines.push_back(strategic_help());
    return Result<std::string>::success(
        finalize(std::move(lines), options.columns, options.rows));
}

}  // namespace

Result<std::string> render_ascii(const Snapshot& snapshot,
                                 const RenderOptions& options) {
    if (options.columns == 0U || options.rows == 0U) {
        return Result<std::string>::failure(render_error(
            FD_ERR_OUT_OF_RANGE, "terminal columns and rows must be nonzero"));
    }
    if (options.columns > kMaximumColumns || options.rows > kMaximumRows) {
        return Result<std::string>::failure(render_error(
            FD_ERR_CAPACITY, "terminal dimensions exceed renderer bounds"));
    }
    if (options.columns < kMinimumColumns || options.rows < kMinimumRows) {
        return render_size_notice(options);
    }
    if (!snapshot.valid()) {
        return Result<std::string>::failure(
            render_error(FD_ERR_STATE, "snapshot is empty or moved-from"));
    }

    auto info_result = snapshot.info();
    if (!info_result) {
        return Result<std::string>::failure(info_result.error());
    }
    const fd_world_info& info = info_result.value();
    auto entities_result = capture_entities(snapshot, info);
    if (!entities_result) {
        return Result<std::string>::failure(entities_result.error());
    }

    if (options.mode == ScreenMode::local) {
        return render_local(snapshot, options, info, entities_result.value());
    }
    return render_strategic(snapshot, options, info, entities_result.value());
}

Result<std::string> render_debug_hash_ascii(const Snapshot& snapshot,
                                            const RenderOptions& options) {
    if (options.columns == 0U || options.rows == 0U) {
        return Result<std::string>::failure(render_error(
            FD_ERR_OUT_OF_RANGE, "terminal columns and rows must be nonzero"));
    }
    if (options.columns > kMaximumColumns || options.rows > kMaximumRows) {
        return Result<std::string>::failure(render_error(
            FD_ERR_CAPACITY, "terminal dimensions exceed renderer bounds"));
    }
    if (options.columns < kMinimumColumns || options.rows < kMinimumRows) {
        return render_size_notice(options);
    }
    auto info_result = snapshot.info();
    if (!info_result) {
        return Result<std::string>::failure(info_result.error());
    }
    const fd_world_info& info = info_result.value();
    std::vector<std::string> lines;
    lines.emplace_back(
        "FRONTIER DIRECTORATE | DEBUG STATE HASH | ADMIN/OMNISCIENT REFERENCE");
    lines.emplace_back(local_status(info));
    lines.emplace_back("+-- IMMUTABLE CANONICAL IDENTITY -------------------------------------------+");
    lines.emplace_back("State SHA-256 [00..15]: " +
                       hex_prefix(info.state_sha256, 16U));
    lines.emplace_back("State SHA-256 [16..31]: " +
                       hex_prefix(info.state_sha256 + 16U, 16U));
    lines.emplace_back("Config SHA-256[00..15]: " +
                       hex_prefix(info.configuration_sha256, 16U));
    lines.emplace_back("Config SHA-256[16..31]: " +
                       hex_prefix(info.configuration_sha256 + 16U, 16U));
    lines.emplace_back("Content manifest short: " +
                       hex_prefix(info.content_sha256, 8U));
    lines.emplace_back("Root seed: " + seed_hex(info));
    lines.emplace_back("Versions: generator=" + decimal(info.generator_version) +
                       " ruleset=" + decimal(info.ruleset_version) +
                       " rng-registry=" + decimal(info.rng_registry_version));
    lines.emplace_back("World: " + decimal(info.width) + "x" +
                       decimal(info.height) + " tiles; entities=" +
                       decimal(info.entity_count) + "; region=" +
                       decimal(info.region_id));
    lines.emplace_back("+----------------------------------------------------------------------------+");
    pad_before_footer(lines, options.rows, 3U);
    lines.emplace_back(
        "Columns: all hashes/versions are copied from the immutable C snapshot");
    lines.push_back(footer_message(options));
    lines.emplace_back("Keys: ? help q quit m map local strategic pass");
    return Result<std::string>::success(
        finalize(std::move(lines), options.columns, options.rows));
}

Result<std::string> render_replay_inspector_ascii(
    const Replay& replay,
    const std::uint64_t attempt_index,
    const RenderOptions& options) {
    if (options.columns == 0U || options.rows == 0U) {
        return Result<std::string>::failure(render_error(
            FD_ERR_OUT_OF_RANGE, "terminal columns and rows must be nonzero"));
    }
    if (options.columns > kMaximumColumns || options.rows > kMaximumRows) {
        return Result<std::string>::failure(render_error(
            FD_ERR_CAPACITY, "terminal dimensions exceed renderer bounds"));
    }
    if (options.columns < kMinimumColumns || options.rows < kMinimumRows) {
        return render_size_notice(options);
    }
    auto info_result = replay.info();
    if (!info_result) {
        return Result<std::string>::failure(info_result.error());
    }
    const fd_replay_info& info = info_result.value();
    const std::uint64_t total_attempts =
        info.decision_count + static_cast<std::uint64_t>(info.audit_count);
    if (total_attempts == 0U) {
        if (attempt_index != 0U) {
            return Result<std::string>::failure(render_error(
                FD_ERR_OUT_OF_RANGE,
                "replay attempt index is outside the audit"));
        }
        std::vector<std::string> lines;
        lines.emplace_back(
            "FRONTIER DIRECTORATE | REPLAY INSPECTOR | EMPTY ACTION AUDIT");
        lines.emplace_back("Accepted: 0 | Rejected: 0 | replay cursor " +
                           decimal(info.cursor));
        lines.emplace_back("No action attempts are recorded in this replay.");
        lines.emplace_back("Tick-zero state [00..15]: " +
                           hex_prefix(info.tick_zero_state_sha256, 16U));
        lines.emplace_back("Tick-zero state [16..31]: " +
                           hex_prefix(info.tick_zero_state_sha256 + 16U, 16U));
        pad_before_footer(lines, options.rows, 3U);
        lines.emplace_back(
            "Columns: empty state is authoritative public C replay metadata");
        lines.push_back(footer_message(options));
        lines.emplace_back(
            "Keys: ? help q quit messages local strategic pass");
        return Result<std::string>::success(
            finalize(std::move(lines), options.columns, options.rows));
    }
    if (attempt_index >= total_attempts) {
        return Result<std::string>::failure(render_error(
            FD_ERR_OUT_OF_RANGE,
            "replay attempt index is outside the audit"));
    }

    std::uint32_t rejected_before = 0U;
    fd_replay_audit_view rejected{};
    bool is_rejected = false;
    for (std::uint32_t index = 0U; index < info.audit_count; ++index) {
        auto audit_result = replay.audit(index);
        if (!audit_result) {
            return Result<std::string>::failure(audit_result.error());
        }
        if (audit_result.value().attempt_index == attempt_index) {
            rejected = audit_result.value();
            is_rejected = true;
            break;
        }
        if (audit_result.value().attempt_index > attempt_index) {
            break;
        }
        ++rejected_before;
    }
    std::vector<std::string> lines;
    if (is_rejected) {
        const std::uint32_t message_length =
            std::min(rejected.message_length, UINT32_C(160));
        lines.emplace_back(
            "FRONTIER DIRECTORATE | REPLAY INSPECTOR | REJECTED ACTION ATTEMPT");
        lines.emplace_back("Attempt " + decimal(attempt_index) + " of " +
                           decimal(total_attempts) + " | accepted " +
                           decimal(info.decision_count) + " | rejected " +
                           decimal(info.audit_count) + " | cursor " +
                           decimal(info.cursor));
        lines.emplace_back("Tick: " + decimal(rejected.tick) +
                           " | result code " + decimal(rejected.result));
        lines.emplace_back("Diagnostic: field=" +
                           decimal(rejected.field_id) + " item=" +
                           decimal(rejected.item_index));
        lines.emplace_back(
            "Diagnostic text: " +
            ascii_text(rejected.message,
                       static_cast<std::size_t>(message_length)));
        lines.emplace_back("Expected tick: " +
                           decimal(rejected.decision.expected_tick));
        lines.emplace_back("Expected pre-state [00..15]: " +
                           hex_prefix(
                               rejected.decision.expected_pre_state_sha256,
                               16U));
        lines.emplace_back("Expected pre-state [16..31]: " +
                           hex_prefix(
                               rejected.decision.expected_pre_state_sha256 +
                                   16U,
                               16U));
        lines.emplace_back("Seat 0 actor #" +
                           decimal(rejected.decision.seats[0].actor_id) +
                           " | category " +
                           decimal(rejected.decision.seats[0].action.category) +
                           " | parameters=" +
                           decimal(rejected.decision.seats[0]
                                       .action.parameter_count));
        lines.emplace_back("Seat 1 actor #" +
                           decimal(rejected.decision.seats[1].actor_id) +
                           " | category " +
                           decimal(rejected.decision.seats[1].action.category) +
                           " | parameters=" +
                           decimal(rejected.decision.seats[1]
                                       .action.parameter_count));
    } else {
        const std::uint64_t accepted_index =
            attempt_index - static_cast<std::uint64_t>(rejected_before);
        auto record_result = replay.record(accepted_index);
        if (!record_result) {
            return Result<std::string>::failure(record_result.error());
        }
        const fd_replay_record_view& record = record_result.value();
        lines.emplace_back(
            "FRONTIER DIRECTORATE | REPLAY INSPECTOR | VERIFIED ACCEPTED DECISION");
        lines.emplace_back("Attempt " + decimal(attempt_index) + " of " +
                           decimal(total_attempts) + " | accepted record " +
                           decimal(record.decision_index) +
                           " | replay cursor " + decimal(info.cursor));
        lines.emplace_back("Tick: " + decimal(record.tick_before) + " -> " +
                           decimal(record.tick_after) + " | expected " +
                           decimal(record.decision.expected_tick));
        lines.emplace_back("Seat 0 actor #" +
                           decimal(record.decision.seats[0].actor_id) +
                           " | category " +
                           decimal(record.decision.seats[0].action.category) +
                           " (PASS=1) | parameters=" +
                           decimal(record.decision.seats[0]
                                       .action.parameter_count));
        lines.emplace_back("Seat 1 actor #" +
                           decimal(record.decision.seats[1].actor_id) +
                           " | category " +
                           decimal(record.decision.seats[1].action.category) +
                           " (PASS=1) | parameters=" +
                           decimal(record.decision.seats[1]
                                       .action.parameter_count));
        lines.emplace_back("RNG domain trace entries: " +
                           decimal(record.rng_domain_count));
        lines.emplace_back("Pre-state [00..15]:  " +
                           hex_prefix(record.pre_state_sha256, 16U));
        lines.emplace_back("Pre-state [16..31]:  " +
                           hex_prefix(record.pre_state_sha256 + 16U, 16U));
        lines.emplace_back("Post-state [00..15]: " +
                           hex_prefix(record.post_state_sha256, 16U));
        lines.emplace_back("Post-state [16..31]: " +
                           hex_prefix(record.post_state_sha256 + 16U, 16U));
        lines.emplace_back("Event hash [00..15]: " +
                           hex_prefix(record.event_sha256, 16U));
        lines.emplace_back("Event hash [16..31]: " +
                           hex_prefix(record.event_sha256 + 16U, 16U));
    }
    pad_before_footer(lines, options.rows, 3U);
    lines.emplace_back(
        "Columns: actions/hashes/diagnostics are immutable public C replay values");
    lines.push_back(footer_message(options));
    lines.emplace_back(
        "Keys: ? help q quit PgUp/PgDn attempt messages local");
    return Result<std::string>::success(
        finalize(std::move(lines), options.columns, options.rows));
}

Result<std::string> render_message_log_ascii(const Replay& replay,
                                             const std::uint64_t first_record,
                                             const RenderOptions& options,
                                             const std::string_view filter) {
    if (options.columns == 0U || options.rows == 0U) {
        return Result<std::string>::failure(render_error(
            FD_ERR_OUT_OF_RANGE, "terminal columns and rows must be nonzero"));
    }
    if (options.columns > kMaximumColumns || options.rows > kMaximumRows) {
        return Result<std::string>::failure(render_error(
            FD_ERR_CAPACITY, "terminal dimensions exceed renderer bounds"));
    }
    if (options.columns < kMinimumColumns || options.rows < kMinimumRows) {
        return render_size_notice(options);
    }
    auto info_result = replay.info();
    if (!info_result) {
        return Result<std::string>::failure(info_result.error());
    }
    const std::uint64_t total_attempts =
        info_result.value().decision_count +
        static_cast<std::uint64_t>(info_result.value().audit_count);
    if (first_record > total_attempts) {
        return Result<std::string>::failure(render_error(
            FD_ERR_OUT_OF_RANGE, "message log first record is outside replay"));
    }

    std::vector<std::string> lines;
    lines.emplace_back(
        "FRONTIER DIRECTORATE | MESSAGE LOG | ACTION ACCEPT/REJECT AUDIT");
    lines.emplace_back("Accepted: " +
                       decimal(info_result.value().decision_count) +
                       " | Rejected: " +
                       decimal(info_result.value().audit_count) +
                       " | First displayed: " + decimal(first_record) +
                       " | Cursor: " + decimal(info_result.value().cursor));
    lines.emplace_back(
        "+-- ATTEMPT -- RESULT / TICK / STRUCTURED DIAGNOSTIC ----------------+");
    const std::size_t footer_count = 4U;
    const std::size_t maximum_records =
        static_cast<std::size_t>(options.rows) - lines.size() - footer_count;
    std::uint32_t audit_index = 0U;
    fd_replay_audit_view current_audit{};
    bool has_audit = info_result.value().audit_count != 0U;
    if (has_audit) {
        auto audit_result = replay.audit(audit_index);
        if (!audit_result) {
            return Result<std::string>::failure(audit_result.error());
        }
        current_audit = audit_result.value();
    }
    while (has_audit && current_audit.attempt_index < first_record) {
        ++audit_index;
        has_audit = audit_index < info_result.value().audit_count;
        if (has_audit) {
            auto audit_result = replay.audit(audit_index);
            if (!audit_result) {
                return Result<std::string>::failure(audit_result.error());
            }
            current_audit = audit_result.value();
        }
    }
    std::uint64_t accepted_index =
        first_record - static_cast<std::uint64_t>(audit_index);
    std::uint64_t attempt_index = first_record;
    std::size_t displayed = 0U;
    const std::string ascii_filter = ascii_text(filter);
    while (attempt_index < total_attempts &&
           displayed < maximum_records) {
        std::string line;
        if (has_audit && current_audit.attempt_index == attempt_index) {
            const std::uint32_t message_length =
                std::min(current_audit.message_length, UINT32_C(160));
            line = "#" + decimal(attempt_index) + " | tick " +
                   decimal(current_audit.tick) + " | REJECT code=" +
                   decimal(current_audit.result) + " field=" +
                   decimal(current_audit.field_id) + " item=" +
                   decimal(current_audit.item_index) + " | " +
                   ascii_text(current_audit.message,
                              static_cast<std::size_t>(message_length));
            ++audit_index;
            has_audit = audit_index < info_result.value().audit_count;
            if (has_audit) {
                auto audit_result = replay.audit(audit_index);
                if (!audit_result) {
                    return Result<std::string>::failure(audit_result.error());
                }
                current_audit = audit_result.value();
            }
        } else {
            if (has_audit && current_audit.attempt_index < attempt_index) {
                return Result<std::string>::failure(render_error(
                    FD_ERR_STATE, "replay audit attempts are not ordered"));
            }
            auto record_result = replay.record(accepted_index);
            if (!record_result) {
                return Result<std::string>::failure(record_result.error());
            }
            const fd_replay_record_view& record = record_result.value();
            const auto action_text = [](const fd_action_category category) {
                return category == FD_ACTION_PASS
                           ? std::string("PASS")
                           : std::string("category-") + decimal(category);
            };
            line = "#" + decimal(attempt_index) + " | " +
                   decimal(record.tick_before) + "->" +
                   decimal(record.tick_after) + " | " +
                   action_text(record.decision.seats[0].action.category) +
                   " | " +
                   action_text(record.decision.seats[1].action.category) +
                   " | " + hex_prefix(record.post_state_sha256, 6U);
            ++accepted_index;
        }
        if (ascii_filter.empty() ||
            line.find(ascii_filter) != std::string::npos) {
            lines.push_back(line);
            ++displayed;
        }
        ++attempt_index;
    }
    if (total_attempts == 0U) {
        lines.emplace_back("No action attempts are recorded in this replay.");
    } else if (displayed == 0U) {
        lines.emplace_back("No action audit records match filter: " +
                           ascii_filter);
    }
    pad_before_footer(lines, options.rows, footer_count);
    lines.emplace_back("+----------------------------------------------------------------+");
    lines.emplace_back(
        "Legend: PASS=accepted; REJECT preserves code/field/item/message");
    lines.push_back(footer_message(options));
    lines.emplace_back(
        "Keys: ? help q quit PgUp/PgDn scroll /text replay local");
    return Result<std::string>::success(
        finalize(std::move(lines), options.columns, options.rows));
}

}  // namespace frontier_directorate::terminal

namespace frontier_directorate::terminal {

struct BorrowedSnapshotRenderer final {
    [[nodiscard]] static Result<std::string> render(
        const fd_snapshot* const snapshot,
        const RenderOptions& options) {
        const Snapshot borrowed = Snapshot::borrow(snapshot);
        return render_ascii(borrowed, options);
    }
};

}  // namespace frontier_directorate::terminal

namespace {

fd_result ascii_diagnostic(fd_diagnostic* const diagnostic,
                           const fd_result code,
                           const std::uint32_t field,
                           const std::uint32_t item,
                           const char* const message) noexcept {
    if (diagnostic != nullptr &&
        diagnostic->struct_size >= sizeof(fd_diagnostic) &&
        diagnostic->abi_version == FD_ABI_VERSION) {
        const std::size_t available = sizeof(diagnostic->message) - 1U;
        const std::size_t source_length =
            message == nullptr ? 0U : std::strlen(message);
        const std::size_t length = std::min(source_length, available);
        diagnostic->code = code;
        diagnostic->field_id = field;
        diagnostic->item_index = item;
        std::memset(diagnostic->message, 0, sizeof(diagnostic->message));
        if (length != 0U) {
            std::memcpy(diagnostic->message, message, length);
        }
    }
    return code;
}

fd_result validate_ascii_options(const fd_ascii_options* const options,
                                 fd_diagnostic* const diagnostic) noexcept {
    if (options == nullptr) {
        return ascii_diagnostic(diagnostic, FD_ERR_INVALID_ARGUMENT,
                                FD_FIELD_NONE, 0U,
                                "ASCII options are null");
    }
    if (options->struct_size < sizeof(fd_ascii_options) ||
        options->abi_version != FD_ABI_VERSION) {
        return ascii_diagnostic(diagnostic, FD_ERR_INVALID_SIZE,
                                FD_FIELD_STRUCT_SIZE, 0U,
                                "invalid ASCII options size or ABI");
    }
    for (std::uint32_t index = 0U; index < 8U; ++index) {
        if (options->reserved[index] != 0U) {
            return ascii_diagnostic(diagnostic, FD_ERR_INVALID_ARGUMENT,
                                    FD_FIELD_RESERVED, index,
                                    "ASCII option reserved fields must be zero");
        }
    }
    if (options->screen != FD_ASCII_SCREEN_LOCAL &&
        options->screen != FD_ASCII_SCREEN_STRATEGIC) {
        return ascii_diagnostic(diagnostic, FD_ERR_OUT_OF_RANGE,
                                FD_FIELD_ASCII_SCREEN, 0U,
                                "unknown ASCII screen mode");
    }
    if (options->columns == 0U || options->columns > FD_ASCII_MAX_COLUMNS) {
        return ascii_diagnostic(
            diagnostic,
            options->columns == 0U ? FD_ERR_OUT_OF_RANGE : FD_ERR_CAPACITY,
            FD_FIELD_WIDTH, 0U, "ASCII column count is outside renderer bounds");
    }
    if (options->rows == 0U || options->rows > FD_ASCII_MAX_ROWS) {
        return ascii_diagnostic(
            diagnostic,
            options->rows == 0U ? FD_ERR_OUT_OF_RANGE : FD_ERR_CAPACITY,
            FD_FIELD_HEIGHT, 0U, "ASCII row count is outside renderer bounds");
    }
    if (options->message_length > FD_ASCII_MAX_MESSAGE_BYTES) {
        return ascii_diagnostic(diagnostic, FD_ERR_OUT_OF_RANGE,
                                FD_FIELD_ASCII_MESSAGE, 0U,
                                "ASCII message length exceeds its bound");
    }
    return FD_OK;
}

frontier_directorate::terminal::RenderOptions cpp_options(
    const fd_ascii_options& options) {
    frontier_directorate::terminal::RenderOptions result{};
    result.columns = options.columns;
    result.rows = options.rows;
    result.viewport_x = options.viewport_x;
    result.viewport_y = options.viewport_y;
    result.selected_x = options.selected_x;
    result.selected_y = options.selected_y;
    result.mode = options.screen == FD_ASCII_SCREEN_LOCAL
                      ? frontier_directorate::terminal::ScreenMode::local
                      : frontier_directorate::terminal::ScreenMode::strategic;
    result.message.assign(options.message,
                          static_cast<std::size_t>(options.message_length));
    return result;
}

fd_result render_for_c(const fd_snapshot* const snapshot,
                       const fd_ascii_options* const options,
                       std::string& frame,
                       fd_diagnostic* const diagnostic) {
    if (snapshot == nullptr) {
        return ascii_diagnostic(diagnostic, FD_ERR_INVALID_ARGUMENT,
                                FD_FIELD_NONE, 0U,
                                "ASCII snapshot is null");
    }
    const fd_result options_result =
        validate_ascii_options(options, diagnostic);
    if (options_result != FD_OK) {
        return options_result;
    }

    auto rendered =
        frontier_directorate::terminal::BorrowedSnapshotRenderer::render(
            snapshot, cpp_options(*options));
    if (!rendered) {
        const frontier_directorate::Error& error = rendered.error();
        return ascii_diagnostic(diagnostic, error.code, error.field_id,
                                error.item_index, error.message.c_str());
    }
    frame = std::move(rendered).value();
    return FD_OK;
}

}  // namespace

extern "C" fd_result fd_ascii_options_init(fd_ascii_options* const output) {
    if (output == nullptr) {
        return FD_ERR_INVALID_ARGUMENT;
    }
    static constexpr char message[] =
        "Ready. Simulation is paused; no action submitted.";
    std::memset(output, 0, sizeof(*output));
    output->struct_size = static_cast<std::uint32_t>(sizeof(*output));
    output->abi_version = FD_ABI_VERSION;
    output->columns = 80U;
    output->rows = 24U;
    output->screen = FD_ASCII_SCREEN_LOCAL;
    output->message_length =
        static_cast<std::uint32_t>(sizeof(message) - 1U);
    std::memcpy(output->message, message, sizeof(message) - 1U);
    return FD_OK;
}

extern "C" fd_result fd_ascii_measure(const fd_snapshot* const snapshot,
                                      const fd_ascii_options* const options,
                                      std::uint64_t* const output_size,
                                      fd_diagnostic* const diagnostic) {
    if (output_size == nullptr) {
        return ascii_diagnostic(diagnostic, FD_ERR_INVALID_ARGUMENT,
                                FD_FIELD_NONE, 0U,
                                "ASCII size output is null");
    }
    *output_size = 0U;
    try {
        std::string frame;
        const fd_result result =
            render_for_c(snapshot, options, frame, diagnostic);
        if (result != FD_OK) {
            return result;
        }
        *output_size = static_cast<std::uint64_t>(frame.size());
        return ascii_diagnostic(diagnostic, FD_OK, FD_FIELD_NONE, 0U, "ok");
    } catch (const std::bad_alloc&) {
        return ascii_diagnostic(diagnostic, FD_ERR_OUT_OF_MEMORY,
                                FD_FIELD_NONE, 0U,
                                "ASCII measurement allocation failed");
    } catch (const std::exception&) {
        return ascii_diagnostic(diagnostic, FD_ERR_INTERNAL, FD_FIELD_NONE, 0U,
                                "ASCII measurement failed");
    } catch (...) {
        return ascii_diagnostic(diagnostic, FD_ERR_INTERNAL, FD_FIELD_NONE, 0U,
                                "ASCII measurement failed");
    }
}

extern "C" fd_result fd_ascii_render(const fd_snapshot* const snapshot,
                                     const fd_ascii_options* const options,
                                     char* const buffer,
                                     const std::uint64_t capacity,
                                     std::uint64_t* const written,
                                     fd_diagnostic* const diagnostic) {
    if (written == nullptr) {
        return ascii_diagnostic(diagnostic, FD_ERR_INVALID_ARGUMENT,
                                FD_FIELD_NONE, 0U,
                                "ASCII written output is null");
    }
    *written = 0U;
    try {
        std::string frame;
        const fd_result result =
            render_for_c(snapshot, options, frame, diagnostic);
        if (result != FD_OK) {
            return result;
        }
        const std::uint64_t required =
            static_cast<std::uint64_t>(frame.size());
        if (buffer == nullptr && required != 0U) {
            return ascii_diagnostic(diagnostic, FD_ERR_INVALID_ARGUMENT,
                                    FD_FIELD_NONE, 0U,
                                    "ASCII output buffer is null");
        }
        if (capacity < required) {
            return ascii_diagnostic(diagnostic, FD_ERR_BUFFER_TOO_SMALL,
                                    FD_FIELD_BUFFER_CAPACITY, 0U,
                                    "ASCII output buffer is too small");
        }
        if (required != 0U) {
            std::memcpy(buffer, frame.data(), frame.size());
        }
        *written = required;
        return ascii_diagnostic(diagnostic, FD_OK, FD_FIELD_NONE, 0U, "ok");
    } catch (const std::bad_alloc&) {
        return ascii_diagnostic(diagnostic, FD_ERR_OUT_OF_MEMORY,
                                FD_FIELD_NONE, 0U,
                                "ASCII rendering allocation failed");
    } catch (const std::exception&) {
        return ascii_diagnostic(diagnostic, FD_ERR_INTERNAL, FD_FIELD_NONE, 0U,
                                "ASCII rendering failed");
    } catch (...) {
        return ascii_diagnostic(diagnostic, FD_ERR_INTERNAL, FD_FIELD_NONE, 0U,
                                "ASCII rendering failed");
    }
}
