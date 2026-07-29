#include "frontier_directorate/fd.h"

#include <stddef.h>
#include <stdint.h>
#include <string.h>

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size)
{
    fd_context_config context_config;
    fd_world_config config;
    fd_content_manifest content;
    fd_content_pack packs[FD_MAX_CONTENT_PACKS];
    fd_context *context = NULL;
    fd_world *world = NULL;
    fd_generation_report report;
    uint32_t width;
    uint32_t height;
    if (size < 8U) {
        return 0;
    }
    (void)fd_context_config_init(&context_config);
    (void)fd_world_config_init(&config);
    width = UINT32_C(32) + (uint32_t)data[0];
    height = UINT32_C(16) + (uint32_t)(data[1] % UINT8_C(113));
    if ((uint64_t)width * (uint64_t)height > UINT64_C(32768)) {
        height = UINT32_C(32768) / width;
    }
    config.width = width;
    config.height = height;
    config.attempt_budget = UINT32_C(1) +
                            (uint32_t)(data[2] % UINT8_C(64));
    config.forest_density_ppm = UINT32_C(1) +
        (uint32_t)data[3] * UINT32_C(1000);
    config.hill_density_ppm = UINT32_C(1) +
        (uint32_t)data[4] * UINT32_C(1000);
    config.wetland_density_ppm = UINT32_C(1) +
        (uint32_t)data[5] * UINT32_C(1000);
    if ((data[6] & UINT8_C(1)) != UINT8_C(0)) {
        config.struct_size = (uint32_t)data[6];
    }
    memset(&content, 0, sizeof(content));
    memset(packs, 0, sizeof(packs));
    content.struct_size = (uint32_t)sizeof(content);
    content.abi_version = FD_ABI_VERSION;
    content.pack_count = (uint32_t)(data[7] % UINT8_C(18));
    content.packs = (data[6] & UINT8_C(2)) != UINT8_C(0) ? NULL : packs;
    content.reserved = (data[6] & UINT8_C(4)) != UINT8_C(0) ? UINT32_C(1) :
                                                             UINT32_C(0);
    {
        uint32_t index;
        for (index = 0U; index < FD_MAX_CONTENT_PACKS; ++index) {
            size_t offset = ((size_t)index + 8U) % size;
            packs[index].stable_id = (uint64_t)data[offset];
            memset(packs[index].sha256, data[(offset + 1U) % size], 32U);
        }
    }
    memset(&report, 0, sizeof(report));
    report.struct_size = (uint32_t)sizeof(report);
    report.abi_version = FD_ABI_VERSION;
    if (fd_context_create(&context_config, &context, NULL) == FD_OK) {
        (void)fd_world_generate(context, &config, (uint64_t)data[6], &content,
                                &world, &report, NULL);
    }
    (void)fd_world_destroy(&world, NULL);
    (void)fd_context_destroy(&context, NULL);
    return 0;
}
