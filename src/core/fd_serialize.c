#include "fd_internal.h"

#include <limits.h>
#include <string.h>

typedef struct fd_writer_internal {
    uint8_t *bytes;
    uint64_t position;
} fd_writer_internal;

typedef struct fd_section_internal {
    const uint8_t *payload;
    uint64_t length;
    uint16_t flags;
    bool present;
} fd_section_internal;

static const uint8_t fd_save_magic[8] = {
    'F', 'D', 'V', 'S', UINT8_C(0), UINT8_C(0), UINT8_C(0), UINT8_C(1)
};

static void fd_write_bytes(fd_writer_internal *writer,
                           const void *bytes,
                           uint64_t size)
{
    memcpy(writer->bytes + writer->position, bytes, (size_t)size);
    writer->position += size;
}

static void fd_write_u16(fd_writer_internal *writer, uint16_t value)
{
    fd_store_u16_le(writer->bytes + writer->position, value);
    writer->position += UINT64_C(2);
}

static void fd_write_u32(fd_writer_internal *writer, uint32_t value)
{
    fd_store_u32_le(writer->bytes + writer->position, value);
    writer->position += UINT64_C(4);
}

static void fd_write_u64(fd_writer_internal *writer, uint64_t value)
{
    fd_store_u64_le(writer->bytes + writer->position, value);
    writer->position += UINT64_C(8);
}

static uint64_t fd_begin_section(fd_writer_internal *writer,
                                 uint16_t tag,
                                 uint64_t length)
{
    uint64_t payload_position;
    fd_write_u16(writer, tag);
    fd_write_u16(writer, FD_SECTION_CRITICAL);
    fd_write_u32(writer, UINT32_C(0));
    fd_write_u64(writer, length);
    payload_position = writer->position;
    return payload_position;
}

static void fd_end_section(fd_writer_internal *writer,
                           uint64_t payload_position,
                           uint64_t length)
{
    uint8_t digest[32];
    (void)fd_sha256(writer->bytes + payload_position, length, digest);
    fd_write_bytes(writer, digest, UINT64_C(32));
}

static void fd_footer_digest(const uint8_t *bytes, uint64_t size, uint8_t out[32])
{
    static const uint8_t domain[] = "FrontierDirectorate/Save/Footer/v1";
    fd_sha256_ctx_internal sha;
    uint8_t length_bytes[4];
    fd_sha256_init_internal(&sha);
    fd_store_u32_le(length_bytes, (uint32_t)(sizeof(domain) - 1U));
    fd_sha256_update_internal(&sha, length_bytes, UINT64_C(4));
    fd_sha256_update_internal(&sha, domain, (uint64_t)(sizeof(domain) - 1U));
    fd_sha256_update_internal(&sha, bytes, size);
    fd_sha256_final_internal(&sha, out);
}

static uint64_t fd_entity_section_length(const fd_world *world)
{
    uint64_t length = UINT64_C(8);
    uint32_t index;
    for (index = 0U; index < FD_U01_ENTITY_COUNT; ++index) {
        length += UINT64_C(40) +
                  (uint64_t)world->entities[index].name_length;
    }
    return length;
}

static fd_result fd_save_size_internal(const fd_world *world, uint64_t *out_size)
{
    uint64_t content_length;
    uint64_t tile_length;
    uint64_t entity_length;
    uint64_t payload_total;
    uint64_t section_overhead;
    uint64_t total;
    if (world == NULL || out_size == NULL) {
        return FD_ERR_INVALID_ARGUMENT;
    }
    content_length = UINT64_C(4) +
                     (uint64_t)world->content_count * UINT64_C(40);
    tile_length = UINT64_C(104) +
                  (uint64_t)world->tile_count * UINT64_C(24);
    entity_length = fd_entity_section_length(world);
    payload_total = UINT64_C(16) + UINT64_C(40) + UINT64_C(68) +
                    content_length + UINT64_C(24) + UINT64_C(56) +
                    tile_length + entity_length + UINT64_C(4) +
                    UINT64_C(16) + UINT64_C(32);
    section_overhead = (uint64_t)FD_SAVE_SECTION_COUNT * UINT64_C(48);
    if (!fd_u64_add(UINT64_C(8), payload_total, &total) ||
        !fd_u64_add(total, section_overhead, &total) ||
        !fd_u64_add(total, UINT64_C(32), &total)) {
        return FD_ERR_CAPACITY;
    }
    *out_size = total;
    return FD_OK;
}

fd_result fd_world_save_size(const fd_world *world,
                             uint64_t *out_size,
                             fd_diagnostic *diag)
{
    fd_result result;
    if (world == NULL || out_size == NULL) {
        return fd_diag_set(diag, FD_ERR_INVALID_ARGUMENT, FD_FIELD_NONE,
                           UINT32_C(0), "world or save size output is null");
    }
    result = fd_save_size_internal(world, out_size);
    if (result != FD_OK || *out_size > world->context->max_file_bytes) {
        return fd_diag_set(diag, FD_ERR_CAPACITY, FD_FIELD_BUFFER_CAPACITY,
                           UINT32_C(0), "canonical save exceeds configured bound");
    }
    return fd_diag_ok(diag);
}

fd_result fd_world_save(const fd_world *world,
                        void *buffer,
                        uint64_t capacity,
                        uint64_t *written,
                        fd_diagnostic *diag)
{
    fd_writer_internal writer;
    uint64_t required;
    uint64_t payload;
    uint64_t length;
    uint32_t index;
    uint8_t footer[32];
    fd_result result;
    if (written == NULL) {
        return fd_diag_set(diag, FD_ERR_INVALID_ARGUMENT, FD_FIELD_NONE,
                           UINT32_C(0), "save written output is null");
    }
    *written = UINT64_C(0);
    if (world == NULL) {
        return fd_diag_set(diag, FD_ERR_INVALID_ARGUMENT, FD_FIELD_NONE,
                           UINT32_C(0), "world is null");
    }
    result = fd_save_size_internal(world, &required);
    if (result != FD_OK || required > world->context->max_file_bytes) {
        return fd_diag_set(diag, FD_ERR_CAPACITY, FD_FIELD_BUFFER_CAPACITY,
                           UINT32_C(0), "canonical save exceeds configured bound");
    }
    if (buffer == NULL || capacity < required) {
        return fd_diag_set(diag, FD_ERR_BUFFER_TOO_SMALL,
                           FD_FIELD_BUFFER_CAPACITY, UINT32_C(0),
                           "save buffer is smaller than required size");
    }
    writer.bytes = (uint8_t *)buffer;
    writer.position = UINT64_C(0);
    fd_write_bytes(&writer, fd_save_magic, UINT64_C(8));

    payload = fd_begin_section(&writer, UINT16_C(1), UINT64_C(16));
    fd_write_u16(&writer, FD_SAVE_VERSION_MAJOR);
    fd_write_u16(&writer, FD_SAVE_VERSION_MINOR);
    fd_write_u16(&writer, FD_SAVE_VERSION_MAJOR);
    fd_write_u16(&writer, UINT16_C(0));
    fd_write_u32(&writer, FD_SAVE_SECTION_COUNT);
    fd_write_u32(&writer, UINT32_C(0));
    fd_end_section(&writer, payload, UINT64_C(16));

    payload = fd_begin_section(&writer, UINT16_C(2), UINT64_C(40));
    fd_write_u32(&writer, (uint32_t)FD_API_VERSION_MAJOR);
    fd_write_u32(&writer, (uint32_t)FD_API_VERSION_MINOR);
    fd_write_u32(&writer, (uint32_t)FD_API_VERSION_PATCH);
    fd_write_u32(&writer, world->config.generator_version);
    fd_write_u32(&writer, world->config.ruleset_version);
    fd_write_u32(&writer, (uint32_t)FD_SAVE_VERSION_MAJOR);
    fd_write_u32(&writer, (uint32_t)FD_SAVE_VERSION_MINOR);
    fd_write_u32(&writer, (uint32_t)FD_REPLAY_VERSION_MAJOR);
    fd_write_u32(&writer, (uint32_t)FD_REPLAY_VERSION_MINOR);
    fd_write_u32(&writer, FD_RNG_REGISTRY_VERSION);
    fd_end_section(&writer, payload, UINT64_C(40));

    payload = fd_begin_section(&writer, UINT16_C(3), UINT64_C(68));
    fd_write_u32(&writer, world->config.width);
    fd_write_u32(&writer, world->config.height);
    fd_write_u32(&writer, world->config.attempt_budget);
    fd_write_u32(&writer, world->config.generator_version);
    fd_write_u32(&writer, world->config.ruleset_version);
    fd_write_u32(&writer, world->config.forest_density_ppm);
    fd_write_u32(&writer, world->config.hill_density_ppm);
    fd_write_u32(&writer, world->config.wetland_density_ppm);
    fd_write_u32(&writer, world->config.reference_mode);
    fd_write_bytes(&writer, world->configuration_sha256, UINT64_C(32));
    fd_end_section(&writer, payload, UINT64_C(68));

    length = UINT64_C(4) + (uint64_t)world->content_count * UINT64_C(40);
    payload = fd_begin_section(&writer, UINT16_C(4), length);
    fd_write_u32(&writer, world->content_count);
    for (index = 0U; index < world->content_count; ++index) {
        fd_write_u64(&writer, world->content[index].stable_id);
        fd_write_bytes(&writer, world->content[index].sha256, UINT64_C(32));
    }
    fd_end_section(&writer, payload, length);

    payload = fd_begin_section(&writer, UINT16_C(5), UINT64_C(24));
    fd_write_bytes(&writer, world->root_seed_le, UINT64_C(16));
    fd_write_u32(&writer, FD_RNG_REGISTRY_VERSION);
    fd_write_u32(&writer, UINT32_C(0)); /* persistent sequential counters */
    fd_end_section(&writer, payload, UINT64_C(24));

    payload = fd_begin_section(&writer, UINT16_C(6), UINT64_C(56));
    fd_write_u64(&writer, world->tick);
    fd_write_u64(&writer, UINT64_C(1));
    fd_write_u64(&writer, FD_OPERATIONAL_TICKS);
    fd_write_u64(&writer, UINT64_C(168));
    fd_write_u64(&writer, UINT64_C(720));
    fd_write_u64(&writer, UINT64_C(2160));
    fd_write_u64(&writer, UINT64_C(8640));
    fd_end_section(&writer, payload, UINT64_C(56));

    length = UINT64_C(104) + (uint64_t)world->tile_count * UINT64_C(24);
    payload = fd_begin_section(&writer, UINT16_C(7), length);
    fd_write_u32(&writer, world->tile_count);
    fd_write_u32(&writer, world->config.width);
    fd_write_u32(&writer, world->config.height);
    fd_write_u32(&writer, world->river_tile_count);
    fd_write_u32(&writer, world->road_tile_count);
    fd_write_u32(&writer, world->land_tile_count);
    for (index = 0U; index < world->tile_count; ++index) {
        fd_write_u32(&writer, world->tiles[index].terrain);
        fd_write_u32(&writer, world->tiles[index].overlays);
        fd_write_u32(&writer, (uint32_t)world->tiles[index].elevation_cm);
        fd_write_u32(&writer, world->tiles[index].rainfall_mm);
        fd_write_u64(&writer, world->tiles[index].settlement_id);
    }
    fd_write_u64(&writer, world->region.id);
    fd_write_u64(&writer, world->region.political_owner_id);
    fd_write_u64(&writer, world->region.settlement_ids[0]);
    fd_write_u64(&writer, world->region.settlement_ids[1]);
    fd_write_u64(&writer, world->region.foreign_faction_ids[0]);
    fd_write_u64(&writer, world->region.foreign_faction_ids[1]);
    fd_write_u64(&writer, world->region.river_outlet_tile_id);
    fd_write_u64(&writer, world->region.road_start_tile_id);
    fd_write_u64(&writer, world->region.road_end_tile_id);
    fd_write_u32(&writer, world->region.river_tile_count);
    fd_write_u32(&writer, world->region.road_tile_count);
    fd_end_section(&writer, payload, length);

    length = fd_entity_section_length(world);
    payload = fd_begin_section(&writer, UINT16_C(8), length);
    fd_write_u32(&writer, FD_U01_ENTITY_COUNT);
    fd_write_u32(&writer, FD_U01_ENTITY_COUNT);
    for (index = 0U; index < FD_U01_ENTITY_COUNT; ++index) {
        const fd_entity_internal *entity = &world->entities[index];
        fd_write_u64(&writer, entity->id);
        fd_write_u32(&writer, entity->kind);
        fd_write_u32(&writer, entity->x);
        fd_write_u32(&writer, entity->y);
        fd_write_u64(&writer, entity->region_id);
        fd_write_u64(&writer, entity->owner_id);
        fd_write_u32(&writer, entity->name_length);
        fd_write_bytes(&writer, entity->name, (uint64_t)entity->name_length);
    }
    fd_end_section(&writer, payload, length);

    payload = fd_begin_section(&writer, UINT16_C(9), UINT64_C(4));
    fd_write_u32(&writer, UINT32_C(0));
    fd_end_section(&writer, payload, UINT64_C(4));

    payload = fd_begin_section(&writer, UINT16_C(10), UINT64_C(16));
    fd_write_u32(&writer, UINT32_C(0));
    fd_write_u32(&writer, UINT32_C(0));
    fd_write_u32(&writer, UINT32_C(0));
    fd_write_u32(&writer, UINT32_C(0));
    fd_end_section(&writer, payload, UINT64_C(16));

    payload = fd_begin_section(&writer, UINT16_C(11), UINT64_C(32));
    fd_write_bytes(&writer, world->state_sha256, UINT64_C(32));
    fd_end_section(&writer, payload, UINT64_C(32));

    fd_footer_digest(writer.bytes, writer.position, footer);
    fd_write_bytes(&writer, footer, UINT64_C(32));
    if (writer.position != required) {
        return fd_diag_set(diag, FD_ERR_INTERNAL, FD_FIELD_FILE_BYTES,
                           UINT32_C(0), "save size implementation mismatch");
    }
    *written = writer.position;
    return fd_diag_ok(diag);
}

static fd_result fd_parse_save_sections(const uint8_t *bytes,
                                        uint64_t size,
                                        fd_section_internal sections[12],
                                        uint32_t *out_section_count,
                                        fd_diagnostic *diag)
{
    uint64_t position = UINT64_C(8);
    uint64_t footer_position = size - UINT64_C(32);
    uint16_t previous_tag = UINT16_C(0);
    uint32_t section_count = UINT32_C(0);
    uint8_t digest[32];
    memset(sections, 0, 12U * sizeof(sections[0]));
    while (position < footer_position) {
        uint16_t tag;
        uint16_t flags;
        uint32_t reserved;
        uint64_t length;
        uint64_t framed;
        const uint8_t *payload;
        if (footer_position - position < UINT64_C(48)) {
            return fd_diag_set(diag, FD_ERR_FORMAT, FD_FIELD_SECTION,
                               section_count, "truncated save section header");
        }
        tag = (uint16_t)((uint16_t)bytes[position] |
                         (uint16_t)((uint16_t)bytes[position + 1U] << 8U));
        flags = (uint16_t)((uint16_t)bytes[position + 2U] |
                           (uint16_t)((uint16_t)bytes[position + 3U] << 8U));
        reserved = fd_load_u32_le(bytes + position + UINT64_C(4));
        length = fd_load_u64_le(bytes + position + UINT64_C(8));
        if (tag <= previous_tag || reserved != UINT32_C(0) ||
            (flags & (uint16_t)~FD_SECTION_CRITICAL) != UINT16_C(0)) {
            return fd_diag_set(diag, FD_ERR_FORMAT, FD_FIELD_SECTION,
                               section_count, "noncanonical save section header");
        }
        if (!fd_u64_add(UINT64_C(48), length, &framed) ||
            framed > footer_position - position) {
            return fd_diag_set(diag, FD_ERR_FORMAT, FD_FIELD_SECTION,
                               section_count, "save section length exceeds file");
        }
        payload = bytes + position + UINT64_C(16);
        (void)fd_sha256(payload, length, digest);
        if (memcmp(digest, payload + length, 32U) != 0) {
            return fd_diag_set(diag, FD_ERR_CHECKSUM, FD_FIELD_SECTION,
                               section_count, "save section payload digest mismatch");
        }
        if (tag <= UINT16_C(11)) {
            sections[tag].payload = payload;
            sections[tag].length = length;
            sections[tag].flags = flags;
            sections[tag].present = true;
        } else if ((flags & FD_SECTION_CRITICAL) != UINT16_C(0)) {
            return fd_diag_set(diag, FD_ERR_VERSION, FD_FIELD_SECTION,
                               section_count, "unknown critical save section");
        }
        previous_tag = tag;
        position += framed;
        ++section_count;
        if (section_count > UINT32_C(64)) {
            return fd_diag_set(diag, FD_ERR_CAPACITY, FD_FIELD_SECTION,
                               section_count, "too many save sections");
        }
    }
    if (position != footer_position) {
        return fd_diag_set(diag, FD_ERR_FORMAT, FD_FIELD_FILE_BYTES,
                           UINT32_C(0), "unframed trailing save bytes");
    }
    for (section_count = 1U; section_count <= 11U; ++section_count) {
        if (!sections[section_count].present ||
            (sections[section_count].flags & FD_SECTION_CRITICAL) == UINT16_C(0)) {
            return fd_diag_set(diag, FD_ERR_FORMAT, FD_FIELD_SECTION,
                               section_count, "missing mandatory save section");
        }
    }
    *out_section_count = previous_tag == UINT16_C(0) ? UINT32_C(0) :
                         *out_section_count;
    /* Recount without relying on the loop variable reused above. */
    {
        uint64_t scan = UINT64_C(8);
        uint32_t count = UINT32_C(0);
        while (scan < footer_position) {
            uint64_t section_length = fd_load_u64_le(bytes + scan + UINT64_C(8));
            scan += UINT64_C(48) + section_length;
            ++count;
        }
        *out_section_count = count;
    }
    return FD_OK;
}

static bool fd_u01_name_valid(const uint8_t *bytes, uint32_t length)
{
    uint32_t index;
    if (length == UINT32_C(0) || length >= FD_MAX_NAME_BYTES) {
        return false;
    }
    for (index = 0U; index < length; ++index) {
        if (bytes[index] < UINT8_C(0x20) || bytes[index] > UINT8_C(0x7e)) {
            return false;
        }
    }
    return true;
}

static fd_result fd_decode_world(fd_context *context,
                                 const fd_section_internal sections[12],
                                 uint32_t parsed_section_count,
                                 fd_world **out,
                                 fd_diagnostic *diag)
{
    const uint8_t *payload;
    fd_world_config config;
    fd_content_manifest manifest;
    fd_content_pack packs[FD_MAX_CONTENT_PACKS];
    fd_world *world = NULL;
    uint32_t content_count;
    uint64_t tile_bytes;
    uint64_t expected;
    uint32_t index;
    uint8_t hash[32];
    uint16_t file_minor;
    fd_validation_report validation;
    fd_result result;
    payload = sections[1].payload;
    if (sections[1].length < UINT64_C(16)) {
        return fd_diag_set(diag, FD_ERR_FORMAT, FD_FIELD_SECTION,
                           UINT32_C(1), "save manifest prefix is truncated");
    }
    file_minor = (uint16_t)((uint16_t)payload[2] |
                            (uint16_t)((uint16_t)payload[3] << 8U));
    if ((uint16_t)((uint16_t)payload[0] | (uint16_t)((uint16_t)payload[1] << 8U)) !=
            FD_SAVE_VERSION_MAJOR ||
        (uint16_t)((uint16_t)payload[4] | (uint16_t)((uint16_t)payload[5] << 8U)) !=
            FD_SAVE_VERSION_MAJOR ||
        (uint16_t)((uint16_t)payload[6] | (uint16_t)((uint16_t)payload[7] << 8U)) >
            FD_SAVE_VERSION_MINOR ||
        fd_load_u32_le(payload + 8U) != parsed_section_count ||
        fd_load_u32_le(payload + 12U) != UINT32_C(0)) {
        return fd_diag_set(diag, FD_ERR_VERSION, FD_FIELD_FORMAT_VERSION,
                           UINT32_C(0), "unsupported or inconsistent save manifest");
    }
    payload = sections[2].payload;
    if (sections[2].length < UINT64_C(40) ||
        fd_load_u32_le(payload) != (uint32_t)FD_API_VERSION_MAJOR ||
        fd_load_u32_le(payload + 12U) != FD_GENERATOR_VERSION ||
        fd_load_u32_le(payload + 16U) != FD_RULESET_VERSION ||
        fd_load_u32_le(payload + 20U) != (uint32_t)FD_SAVE_VERSION_MAJOR ||
        fd_load_u32_le(payload + 24U) != (uint32_t)file_minor ||
        fd_load_u32_le(payload + 28U) != (uint32_t)FD_REPLAY_VERSION_MAJOR ||
        fd_load_u32_le(payload + 36U) != FD_RNG_REGISTRY_VERSION) {
        return fd_diag_set(diag, FD_ERR_VERSION, FD_FIELD_FORMAT_VERSION,
                           UINT32_C(0), "unsupported authoritative version tuple");
    }
    payload = sections[3].payload;
    if (sections[3].length != UINT64_C(68)) {
        return fd_diag_set(diag, FD_ERR_FORMAT, FD_FIELD_SECTION,
                           UINT32_C(3), "invalid configuration section length");
    }
    (void)fd_world_config_init(&config);
    config.width = fd_load_u32_le(payload);
    config.height = fd_load_u32_le(payload + 4U);
    config.attempt_budget = fd_load_u32_le(payload + 8U);
    config.generator_version = fd_load_u32_le(payload + 12U);
    config.ruleset_version = fd_load_u32_le(payload + 16U);
    config.forest_density_ppm = fd_load_u32_le(payload + 20U);
    config.hill_density_ppm = fd_load_u32_le(payload + 24U);
    config.wetland_density_ppm = fd_load_u32_le(payload + 28U);
    config.reference_mode = fd_load_u32_le(payload + 32U);
    if (config.width < FD_MIN_WIDTH || config.width > FD_MAX_WIDTH ||
        config.height < FD_MIN_HEIGHT || config.height > FD_MAX_HEIGHT ||
        (uint64_t)config.width * (uint64_t)config.height > (uint64_t)FD_MAX_TILES ||
        config.attempt_budget == UINT32_C(0) ||
        config.attempt_budget > FD_MAX_GENERATION_ATTEMPTS ||
        config.generator_version != FD_GENERATOR_VERSION ||
        config.ruleset_version != FD_RULESET_VERSION ||
        config.reference_mode != UINT32_C(1)) {
        return fd_diag_set(diag, FD_ERR_FORMAT, FD_FIELD_WIDTH,
                           UINT32_C(0), "invalid canonical world configuration");
    }
    fd_hash_config_internal(&config, hash);
    if (memcmp(hash, payload + 36U, 32U) != 0) {
        return fd_diag_set(diag, FD_ERR_CHECKSUM, FD_FIELD_CONTENT,
                           UINT32_C(3), "configuration hash mismatch");
    }
    payload = sections[4].payload;
    if (sections[4].length < UINT64_C(4)) {
        return fd_diag_set(diag, FD_ERR_FORMAT, FD_FIELD_SECTION,
                           UINT32_C(4), "truncated content section");
    }
    content_count = fd_load_u32_le(payload);
    if (content_count > FD_MAX_CONTENT_PACKS ||
        sections[4].length != UINT64_C(4) + (uint64_t)content_count * UINT64_C(40)) {
        return fd_diag_set(diag, FD_ERR_CAPACITY, FD_FIELD_CONTENT,
                           content_count, "invalid content pack count or length");
    }
    memset(packs, 0, sizeof(packs));
    for (index = 0U; index < content_count; ++index) {
        const uint8_t *entry = payload + UINT64_C(4) + (uint64_t)index * UINT64_C(40);
        packs[index].stable_id = fd_load_u64_le(entry);
        memcpy(packs[index].sha256, entry + 8U, 32U);
        if (packs[index].stable_id == UINT64_C(0) ||
            (index > UINT32_C(0) &&
             packs[index - UINT32_C(1)].stable_id >= packs[index].stable_id)) {
            return fd_diag_set(diag, FD_ERR_FORMAT, FD_FIELD_CONTENT,
                               index, "noncanonical content pack order");
        }
    }
    manifest.struct_size = (uint32_t)sizeof(manifest);
    manifest.abi_version = FD_ABI_VERSION;
    manifest.pack_count = content_count;
    manifest.reserved = UINT32_C(0);
    manifest.packs = packs;
    result = fd_validate_generation_inputs_internal(&config, &manifest, diag);
    if (result != FD_OK || config.forest_density_ppm == UINT32_C(0) ||
        config.hill_density_ppm == UINT32_C(0) ||
        config.wetland_density_ppm == UINT32_C(0)) {
        return fd_diag_set(diag, FD_ERR_FORMAT, FD_FIELD_TERRAIN_DENSITY,
                           UINT32_C(0), "save contains impossible generator configuration");
    }
    fd_hash_content_internal(&manifest, hash);
    payload = sections[5].payload;
    if (sections[5].length != UINT64_C(24) ||
        fd_load_u32_le(payload + 16U) != FD_RNG_REGISTRY_VERSION ||
        fd_load_u32_le(payload + 20U) != UINT32_C(0)) {
        return fd_diag_set(diag, FD_ERR_VERSION, FD_FIELD_SECTION,
                           UINT32_C(5), "invalid RNG identity section");
    }
    if (sections[6].length != UINT64_C(56)) {
        return fd_diag_set(diag, FD_ERR_FORMAT, FD_FIELD_SECTION,
                           UINT32_C(6), "invalid time section length");
    }
    {
        const uint8_t *time = sections[6].payload;
        if (fd_load_u64_le(time + 8U) != UINT64_C(1) ||
            fd_load_u64_le(time + 16U) != FD_OPERATIONAL_TICKS ||
            fd_load_u64_le(time + 24U) != UINT64_C(168) ||
            fd_load_u64_le(time + 32U) != UINT64_C(720) ||
            fd_load_u64_le(time + 40U) != UINT64_C(2160) ||
            fd_load_u64_le(time + 48U) != UINT64_C(8640)) {
            return fd_diag_set(diag, FD_ERR_FORMAT, FD_FIELD_TICK,
                               UINT32_C(0), "time-scale definition mismatch");
        }
    }
    tile_bytes = (uint64_t)config.width * (uint64_t)config.height *
                 (uint64_t)sizeof(fd_tile_internal);
    if (tile_bytes + (uint64_t)sizeof(fd_world) > context->max_live_world_bytes) {
        return fd_diag_set(diag, FD_ERR_CAPACITY, FD_FIELD_BUFFER_CAPACITY,
                           UINT32_C(0), "loaded world exceeds context live bound");
    }
    world = (fd_world *)fd_context_alloc(context, (uint64_t)sizeof(*world));
    if (world == NULL) {
        return fd_diag_set(diag, FD_ERR_OUT_OF_MEMORY, FD_FIELD_NONE,
                           UINT32_C(0), "loaded world allocation failed");
    }
    memset(world, 0, sizeof(*world));
    world->tiles = (fd_tile_internal *)fd_context_alloc(context, tile_bytes);
    if (world->tiles == NULL) {
        fd_context_free(context, world, (uint64_t)sizeof(*world));
        return fd_diag_set(diag, FD_ERR_OUT_OF_MEMORY, FD_FIELD_NONE,
                           UINT32_C(0), "loaded tile allocation failed");
    }
    world->context = context;
    world->config = config;
    world->tile_count = config.width * config.height;
    world->allocation_size = (uint32_t)(tile_bytes + (uint64_t)sizeof(*world));
    world->content_count = content_count;
    memcpy(world->content, packs, (size_t)content_count * sizeof(packs[0]));
    memcpy(world->configuration_sha256, sections[3].payload + 36U, 32U);
    memcpy(world->content_sha256, hash, 32U);
    memcpy(world->root_seed_le, sections[5].payload, 16U);
    world->tick = fd_load_u64_le(sections[6].payload);
    if (world->tick % FD_OPERATIONAL_TICKS != UINT64_C(0) ||
        world->tick > UINT64_MAX - FD_OPERATIONAL_TICKS) {
        result = FD_ERR_FORMAT;
        goto load_fail;
    }
    payload = sections[7].payload;
    expected = UINT64_C(104) + (uint64_t)world->tile_count * UINT64_C(24);
    if (sections[7].length != expected ||
        fd_load_u32_le(payload) != world->tile_count ||
        fd_load_u32_le(payload + 4U) != config.width ||
        fd_load_u32_le(payload + 8U) != config.height) {
        result = FD_ERR_FORMAT;
        goto load_fail;
    }
    world->river_tile_count = fd_load_u32_le(payload + 12U);
    world->road_tile_count = fd_load_u32_le(payload + 16U);
    world->land_tile_count = fd_load_u32_le(payload + 20U);
    payload += 24U;
    for (index = 0U; index < world->tile_count; ++index) {
        world->tiles[index].terrain = fd_load_u32_le(payload);
        world->tiles[index].overlays = fd_load_u32_le(payload + 4U);
        world->tiles[index].elevation_cm = (int32_t)fd_load_u32_le(payload + 8U);
        world->tiles[index].rainfall_mm = fd_load_u32_le(payload + 12U);
        world->tiles[index].settlement_id = fd_load_u64_le(payload + 16U);
        payload += 24U;
    }
    world->region.id = fd_load_u64_le(payload);
    world->region.political_owner_id = fd_load_u64_le(payload + 8U);
    world->region.settlement_ids[0] = fd_load_u64_le(payload + 16U);
    world->region.settlement_ids[1] = fd_load_u64_le(payload + 24U);
    world->region.foreign_faction_ids[0] = fd_load_u64_le(payload + 32U);
    world->region.foreign_faction_ids[1] = fd_load_u64_le(payload + 40U);
    world->region.river_outlet_tile_id = fd_load_u64_le(payload + 48U);
    world->region.road_start_tile_id = fd_load_u64_le(payload + 56U);
    world->region.road_end_tile_id = fd_load_u64_le(payload + 64U);
    world->region.river_tile_count = fd_load_u32_le(payload + 72U);
    world->region.road_tile_count = fd_load_u32_le(payload + 76U);
    payload = sections[8].payload;
    if (sections[8].length < UINT64_C(8) ||
        fd_load_u32_le(payload) != FD_U01_ENTITY_COUNT ||
        fd_load_u32_le(payload + 4U) != FD_U01_ENTITY_COUNT) {
        result = FD_ERR_FORMAT;
        goto load_fail;
    }
    {
        uint64_t position = UINT64_C(8);
        for (index = 0U; index < FD_U01_ENTITY_COUNT; ++index) {
            uint32_t name_length;
            if (sections[8].length - position < UINT64_C(40)) {
                result = FD_ERR_FORMAT;
                goto load_fail;
            }
            payload = sections[8].payload + position;
            name_length = fd_load_u32_le(payload + 36U);
            if (name_length >= FD_MAX_NAME_BYTES ||
                (uint64_t)name_length > sections[8].length - position - UINT64_C(40) ||
                !fd_u01_name_valid(payload + 40U, name_length)) {
                result = FD_ERR_FORMAT;
                goto load_fail;
            }
            world->entities[index].id = fd_load_u64_le(payload);
            world->entities[index].kind = fd_load_u32_le(payload + 8U);
            world->entities[index].x = fd_load_u32_le(payload + 12U);
            world->entities[index].y = fd_load_u32_le(payload + 16U);
            world->entities[index].region_id = fd_load_u64_le(payload + 20U);
            world->entities[index].owner_id = fd_load_u64_le(payload + 28U);
            world->entities[index].name_length = name_length;
            memcpy(world->entities[index].name, payload + 40U, (size_t)name_length);
            position += UINT64_C(40) + (uint64_t)name_length;
        }
        if (position != sections[8].length) {
            result = FD_ERR_FORMAT;
            goto load_fail;
        }
    }
    if (sections[9].length != UINT64_C(4) ||
        fd_load_u32_le(sections[9].payload) != UINT32_C(0) ||
        sections[10].length != UINT64_C(16) ||
        fd_load_u32_le(sections[10].payload) != UINT32_C(0) ||
        fd_load_u32_le(sections[10].payload + 4U) != UINT32_C(0) ||
        fd_load_u32_le(sections[10].payload + 8U) != UINT32_C(0) ||
        fd_load_u32_le(sections[10].payload + 12U) != UINT32_C(0) ||
        sections[11].length != UINT64_C(32)) {
        result = FD_ERR_FORMAT;
        goto load_fail;
    }
    memset(&validation, 0, sizeof(validation));
    validation.struct_size = (uint32_t)sizeof(validation);
    validation.abi_version = FD_ABI_VERSION;
    result = fd_world_validate_internal(world, &validation, diag);
    if (result != FD_OK) {
        result = FD_ERR_STATE;
        goto load_fail;
    }
    result = fd_world_compute_hash(world);
    if (result != FD_OK ||
        memcmp(world->state_sha256, sections[11].payload, 32U) != 0) {
        result = FD_ERR_CHECKSUM;
        goto load_fail;
    }
    (void)atomic_fetch_add_explicit(&context->live_handles, UINT32_C(1),
                                    memory_order_release);
    *out = world;
    return fd_diag_ok(diag);

load_fail:
    fd_context_free(context, world->tiles, tile_bytes);
    fd_context_free(context, world, (uint64_t)sizeof(*world));
    return fd_diag_set(diag, result, FD_FIELD_SECTION, UINT32_C(0),
                       "save payload failed canonical state validation");
}

fd_result fd_world_load(fd_context *context,
                        const void *bytes_input,
                        uint64_t size,
                        fd_world **out,
                        fd_diagnostic *diag)
{
    const uint8_t *bytes = (const uint8_t *)bytes_input;
    fd_section_internal sections[12];
    uint32_t section_count = UINT32_C(0);
    uint8_t digest[32];
    fd_result result;
    if (out == NULL) {
        return fd_diag_set(diag, FD_ERR_INVALID_ARGUMENT, FD_FIELD_NONE,
                           UINT32_C(0), "loaded world output is null");
    }
    *out = NULL;
    if (context == NULL || bytes == NULL) {
        return fd_diag_set(diag, FD_ERR_INVALID_ARGUMENT, FD_FIELD_NONE,
                           UINT32_C(0), "context or save bytes are null");
    }
    if (size > context->max_file_bytes) {
        return fd_diag_set(diag, FD_ERR_CAPACITY, FD_FIELD_FILE_BYTES,
                           UINT32_C(0), "save exceeds configured file bound");
    }
    if (size < UINT64_C(8) + UINT64_C(32) + UINT64_C(48)) {
        return fd_diag_set(diag, FD_ERR_FORMAT, FD_FIELD_FILE_BYTES,
                           UINT32_C(0), "save is truncated");
    }
    if (memcmp(bytes, fd_save_magic, sizeof(fd_save_magic)) != 0) {
        if (memcmp(bytes, fd_save_magic, 7U) == 0) {
            return fd_diag_set(diag, FD_ERR_VERSION, FD_FIELD_FORMAT_VERSION,
                               UINT32_C(0), "unsupported save major version");
        }
        return fd_diag_set(diag, FD_ERR_FORMAT, FD_FIELD_FILE_BYTES,
                           UINT32_C(0), "invalid save magic");
    }
    fd_footer_digest(bytes, size - UINT64_C(32), digest);
    if (memcmp(digest, bytes + size - UINT64_C(32), 32U) != 0) {
        return fd_diag_set(diag, FD_ERR_CHECKSUM, FD_FIELD_FILE_BYTES,
                           UINT32_C(0), "save footer digest mismatch");
    }
    result = fd_parse_save_sections(bytes, size, sections, &section_count, diag);
    if (result != FD_OK) {
        return result;
    }
    return fd_decode_world(context, sections, section_count, out, diag);
}

fd_result fd_world_clone_via_save(const fd_world *source,
                                  fd_world **out,
                                  fd_diagnostic *diag)
{
    uint64_t size = UINT64_C(0);
    uint64_t written = UINT64_C(0);
    uint8_t *bytes;
    fd_result result;
    if (source == NULL || out == NULL) {
        return fd_diag_set(diag, FD_ERR_INVALID_ARGUMENT, FD_FIELD_NONE,
                           UINT32_C(0), "clone input or output is null");
    }
    *out = NULL;
    result = fd_world_save_size(source, &size, diag);
    if (result != FD_OK) {
        return result;
    }
    bytes = (uint8_t *)fd_context_alloc(source->context, size);
    if (bytes == NULL) {
        return fd_diag_set(diag, FD_ERR_OUT_OF_MEMORY, FD_FIELD_NONE,
                           UINT32_C(0), "clone save allocation failed");
    }
    result = fd_world_save(source, bytes, size, &written, diag);
    if (result == FD_OK) {
        result = fd_world_load(source->context, bytes, written, out, diag);
    }
    fd_context_free(source->context, bytes, size);
    return result;
}
