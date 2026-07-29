#include "frontier_directorate/fd.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void output_header(void *value, size_t size)
{
    uint32_t struct_size = (uint32_t)size;
    uint32_t abi_version = FD_ABI_VERSION;
    unsigned char *bytes = (unsigned char *)value;
    memset(value, 0, size);
    memcpy(bytes, &struct_size, sizeof(struct_size));
    memcpy(bytes + sizeof(struct_size), &abi_version, sizeof(abi_version));
}

int main(void)
{
    fd_context_config context_config;
    fd_world_config world_config;
    fd_content_manifest content;
    fd_generation_report generation;
    fd_ascii_options options;
    fd_context *context = NULL;
    fd_world *world = NULL;
    fd_snapshot *snapshot = NULL;
    uint64_t required = UINT64_C(0);
    uint64_t written = UINT64_C(0);
    char *frame = NULL;
    uint64_t index;
    int failed = 0;
    (void)fd_context_config_init(&context_config);
    (void)fd_world_config_init(&world_config);
    memset(&content, 0, sizeof(content));
    content.struct_size = (uint32_t)sizeof(content);
    content.abi_version = FD_ABI_VERSION;
    output_header(&generation, sizeof(generation));
    if (fd_context_create(&context_config, &context, NULL) != FD_OK ||
        fd_world_generate(context, &world_config, UINT64_C(42), &content,
                          &world, &generation, NULL) != FD_OK ||
        fd_snapshot_create(world, FD_VISIBILITY_REFERENCE, UINT64_C(0),
                           &snapshot, NULL) != FD_OK ||
        fd_ascii_options_init(&options) != FD_OK ||
        fd_ascii_measure(snapshot, &options, &required, NULL) != FD_OK ||
        required == UINT64_C(0) || required > (uint64_t)SIZE_MAX) {
        failed = 1;
    }
    if (!failed) {
        frame = (char *)malloc((size_t)required);
        if (frame == NULL ||
            fd_ascii_render(snapshot, &options, frame, required, &written,
                            NULL) != FD_OK ||
            written != required) {
            failed = 1;
        }
    }
    for (index = UINT64_C(0); !failed && index < written; ++index) {
        unsigned char byte = (unsigned char)frame[index];
        if ((byte < UINT8_C(0x20) || byte > UINT8_C(0x7e)) && byte != '\n') {
            failed = 1;
        }
    }
    free(frame);
    (void)fd_snapshot_destroy(&snapshot, NULL);
    (void)fd_world_destroy(&world, NULL);
    (void)fd_context_destroy(&context, NULL);
    if (failed) {
        (void)fprintf(stderr, "FAIL: pure-C renderer ABI client\n");
        return 1;
    }
    (void)printf("PASS: pure-C renderer ABI client bytes=%llu\n",
                 (unsigned long long)written);
    return 0;
}
