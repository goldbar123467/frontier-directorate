#ifndef FD_FUZZ_FORMAT_HELPERS_H
#define FD_FUZZ_FORMAT_HELPERS_H

#include "fd_internal.h"

#include <stddef.h>
#include <stdint.h>
#include <string.h>

#define FD_FUZZ_FILE_LIMIT UINT64_C(1048576)

static int fd_fuzz_find_section(uint8_t *bytes,
                                uint64_t size,
                                uint16_t wanted_tag,
                                uint8_t **out_payload,
                                uint64_t *out_length)
{
    uint64_t position = UINT64_C(8);
    uint64_t footer;
    if (bytes == NULL || out_payload == NULL || out_length == NULL ||
        size < UINT64_C(40)) {
        return 1;
    }
    footer = size - UINT64_C(32);
    while (position < footer) {
        uint16_t tag;
        uint64_t length;
        if (footer - position < UINT64_C(48)) {
            return 1;
        }
        tag = (uint16_t)((uint16_t)bytes[position] |
                         (uint16_t)((uint16_t)bytes[position + UINT64_C(1)]
                                    << UINT16_C(8)));
        length = fd_load_u64_le(bytes + position + UINT64_C(8));
        position += UINT64_C(16);
        if (length > footer - position ||
            footer - position - length < UINT64_C(32)) {
            return 1;
        }
        if (tag == wanted_tag) {
            *out_payload = bytes + position;
            *out_length = length;
            return 0;
        }
        position += length + UINT64_C(32);
    }
    return 1;
}

static void fd_fuzz_repair_section(uint8_t *payload, uint64_t length)
{
    uint8_t digest[32];
    (void)fd_sha256(payload, length, digest);
    memcpy(payload + length, digest, sizeof(digest));
}

static void fd_fuzz_repair_footer(uint8_t *bytes,
                                  uint64_t size,
                                  const char *domain)
{
    fd_sha256_ctx_internal sha;
    uint8_t domain_length[4];
    uint8_t digest[32];
    uint64_t payload_size = size - UINT64_C(32);
    size_t domain_size = strlen(domain);
    fd_store_u32_le(domain_length, (uint32_t)domain_size);
    fd_sha256_init_internal(&sha);
    fd_sha256_update_internal(&sha, domain_length, UINT64_C(4));
    fd_sha256_update_internal(&sha, (const uint8_t *)domain,
                              (uint64_t)domain_size);
    fd_sha256_update_internal(&sha, bytes, payload_size);
    fd_sha256_final_internal(&sha, digest);
    memcpy(bytes + payload_size, digest, sizeof(digest));
}

static void fd_fuzz_mutate_payload(uint8_t *payload,
                                   uint64_t length,
                                   const uint8_t *data,
                                   size_t size)
{
    size_t index;
    size_t mutations = size < 256U ? size : 256U;
    if (length == UINT64_C(0)) {
        return;
    }
    for (index = 0U; index < mutations; ++index) {
        uint64_t offset = ((uint64_t)data[index] * UINT64_C(257) +
                           (uint64_t)index * UINT64_C(17)) % length;
        payload[offset] ^= (uint8_t)(data[(index + 1U) % size] |
                                     UINT8_C(1));
    }
}

#endif
