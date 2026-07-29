#include "frontier_directorate/fd.h"
#include "fuzz_format_helpers.h"

#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

static fd_content_manifest empty_content(void)
{
    fd_content_manifest content;
    memset(&content, 0, sizeof(content));
    content.struct_size = (uint32_t)sizeof(content);
    content.abi_version = FD_ABI_VERSION;
    return content;
}

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size)
{
    fd_context_config config;
    fd_world_config world_config;
    fd_content_manifest content = empty_content();
    fd_context *context = NULL;
    fd_world *world = NULL;
    fd_world *loaded = NULL;
    uint8_t *canonical = NULL;
    uint64_t canonical_size = UINT64_C(0);
    uint64_t written = UINT64_C(0);
    uint8_t *payload = NULL;
    uint64_t payload_length = UINT64_C(0);
    if (size > (size_t)FD_FUZZ_FILE_LIMIT) {
        return 0;
    }
    (void)fd_context_config_init(&config);
    (void)fd_world_config_init(&world_config);
    config.max_file_bytes = FD_FUZZ_FILE_LIMIT;
    world_config.attempt_budget = UINT32_C(1);
    if (fd_context_create(&config, &context, NULL) != FD_OK) {
        return 0;
    }

    /* Raw framing/capacity path. */
    (void)fd_world_load(context, data, (uint64_t)size, &loaded, NULL);
    (void)fd_world_destroy(&loaded, NULL);

    /* Authenticated deep path: mutate a canonical payload and repair digests. */
    if (fd_world_generate(context, &world_config,
                          size > 0U ? (uint64_t)data[0] : UINT64_C(0),
                          &content, &world, NULL, NULL) == FD_OK &&
        fd_world_save_size(world, &canonical_size, NULL) == FD_OK &&
        canonical_size <= FD_FUZZ_FILE_LIMIT) {
        canonical = (uint8_t *)malloc((size_t)canonical_size);
    }
    if (canonical != NULL &&
        fd_world_save(world, canonical, canonical_size, &written, NULL) == FD_OK &&
        written == canonical_size) {
        uint16_t tag = (uint16_t)(UINT16_C(1) +
            (uint16_t)(size > 1U ? data[1] % UINT8_C(11) : UINT8_C(0)));
        if (fd_fuzz_find_section(canonical, canonical_size, tag,
                                 &payload, &payload_length) == 0) {
            if (size > 2U) {
                fd_fuzz_mutate_payload(payload, payload_length,
                                       data + 2U, size - 2U);
            }
            fd_fuzz_repair_section(payload, payload_length);
            fd_fuzz_repair_footer(
                canonical, canonical_size,
                "FrontierDirectorate/Save/Footer/v1");
            (void)fd_world_load(context, canonical, canonical_size,
                                &loaded, NULL);
            (void)fd_world_destroy(&loaded, NULL);
        }
    }
    free(canonical);
    (void)fd_world_destroy(&world, NULL);
    (void)fd_context_destroy(&context, NULL);
    return 0;
}
