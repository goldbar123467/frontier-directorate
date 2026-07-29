#include "frontier_directorate/fd.h"

#include <stdint.h>
#include <string.h>

int main(void)
{
    fd_context_config context_config;
    fd_world_config world_config;
    fd_content_manifest content;
    fd_context *context = NULL;
    fd_world *world = NULL;
    fd_generation_report report;
    int result = 1;
    (void)fd_context_config_init(&context_config);
    (void)fd_world_config_init(&world_config);
    memset(&content, 0, sizeof(content));
    content.struct_size = (uint32_t)sizeof(content);
    content.abi_version = FD_ABI_VERSION;
    memset(&report, 0, sizeof(report));
    report.struct_size = (uint32_t)sizeof(report);
    report.abi_version = FD_ABI_VERSION;
    if (fd_context_create(&context_config, &context, NULL) == FD_OK &&
        fd_world_generate(context, &world_config, UINT64_C(7), &content,
                          &world, &report, NULL) == FD_OK) {
        result = 0;
    }
    (void)fd_world_destroy(&world, NULL);
    (void)fd_context_destroy(&context, NULL);
    return result;
}
