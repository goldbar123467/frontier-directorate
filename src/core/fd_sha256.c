#include "fd_internal.h"

#include <string.h>

static uint32_t fd_rotr32(uint32_t value, uint32_t count)
{
    return (value >> count) | (value << (UINT32_C(32) - count));
}

static void fd_sha256_compress(fd_sha256_ctx_internal *ctx,
                               const uint8_t block[64])
{
    static const uint32_t constants[64] = {
        UINT32_C(0x428a2f98), UINT32_C(0x71374491), UINT32_C(0xb5c0fbcf), UINT32_C(0xe9b5dba5),
        UINT32_C(0x3956c25b), UINT32_C(0x59f111f1), UINT32_C(0x923f82a4), UINT32_C(0xab1c5ed5),
        UINT32_C(0xd807aa98), UINT32_C(0x12835b01), UINT32_C(0x243185be), UINT32_C(0x550c7dc3),
        UINT32_C(0x72be5d74), UINT32_C(0x80deb1fe), UINT32_C(0x9bdc06a7), UINT32_C(0xc19bf174),
        UINT32_C(0xe49b69c1), UINT32_C(0xefbe4786), UINT32_C(0x0fc19dc6), UINT32_C(0x240ca1cc),
        UINT32_C(0x2de92c6f), UINT32_C(0x4a7484aa), UINT32_C(0x5cb0a9dc), UINT32_C(0x76f988da),
        UINT32_C(0x983e5152), UINT32_C(0xa831c66d), UINT32_C(0xb00327c8), UINT32_C(0xbf597fc7),
        UINT32_C(0xc6e00bf3), UINT32_C(0xd5a79147), UINT32_C(0x06ca6351), UINT32_C(0x14292967),
        UINT32_C(0x27b70a85), UINT32_C(0x2e1b2138), UINT32_C(0x4d2c6dfc), UINT32_C(0x53380d13),
        UINT32_C(0x650a7354), UINT32_C(0x766a0abb), UINT32_C(0x81c2c92e), UINT32_C(0x92722c85),
        UINT32_C(0xa2bfe8a1), UINT32_C(0xa81a664b), UINT32_C(0xc24b8b70), UINT32_C(0xc76c51a3),
        UINT32_C(0xd192e819), UINT32_C(0xd6990624), UINT32_C(0xf40e3585), UINT32_C(0x106aa070),
        UINT32_C(0x19a4c116), UINT32_C(0x1e376c08), UINT32_C(0x2748774c), UINT32_C(0x34b0bcb5),
        UINT32_C(0x391c0cb3), UINT32_C(0x4ed8aa4a), UINT32_C(0x5b9cca4f), UINT32_C(0x682e6ff3),
        UINT32_C(0x748f82ee), UINT32_C(0x78a5636f), UINT32_C(0x84c87814), UINT32_C(0x8cc70208),
        UINT32_C(0x90befffa), UINT32_C(0xa4506ceb), UINT32_C(0xbef9a3f7), UINT32_C(0xc67178f2)
    };
    uint32_t words[64];
    uint32_t a;
    uint32_t b;
    uint32_t c;
    uint32_t d;
    uint32_t e;
    uint32_t f;
    uint32_t g;
    uint32_t h;
    uint32_t index;

    for (index = 0U; index < 16U; ++index) {
        uint32_t offset = index * UINT32_C(4);
        words[index] = ((uint32_t)block[offset] << 24U) |
                       ((uint32_t)block[offset + 1U] << 16U) |
                       ((uint32_t)block[offset + 2U] << 8U) |
                       (uint32_t)block[offset + 3U];
    }
    for (index = 16U; index < 64U; ++index) {
        uint32_t x = words[index - 15U];
        uint32_t y = words[index - 2U];
        uint32_t s0 = fd_rotr32(x, 7U) ^ fd_rotr32(x, 18U) ^ (x >> 3U);
        uint32_t s1 = fd_rotr32(y, 17U) ^ fd_rotr32(y, 19U) ^ (y >> 10U);
        words[index] = words[index - 16U] + s0 + words[index - 7U] + s1;
    }

    a = ctx->state[0];
    b = ctx->state[1];
    c = ctx->state[2];
    d = ctx->state[3];
    e = ctx->state[4];
    f = ctx->state[5];
    g = ctx->state[6];
    h = ctx->state[7];
    for (index = 0U; index < 64U; ++index) {
        uint32_t sum1 = fd_rotr32(e, 6U) ^ fd_rotr32(e, 11U) ^ fd_rotr32(e, 25U);
        uint32_t choice = (e & f) ^ ((~e) & g);
        uint32_t temp1 = h + sum1 + choice + constants[index] + words[index];
        uint32_t sum0 = fd_rotr32(a, 2U) ^ fd_rotr32(a, 13U) ^ fd_rotr32(a, 22U);
        uint32_t majority = (a & b) ^ (a & c) ^ (b & c);
        uint32_t temp2 = sum0 + majority;
        h = g;
        g = f;
        f = e;
        e = d + temp1;
        d = c;
        c = b;
        b = a;
        a = temp1 + temp2;
    }
    ctx->state[0] += a;
    ctx->state[1] += b;
    ctx->state[2] += c;
    ctx->state[3] += d;
    ctx->state[4] += e;
    ctx->state[5] += f;
    ctx->state[6] += g;
    ctx->state[7] += h;
}

void fd_sha256_init_internal(fd_sha256_ctx_internal *ctx)
{
    static const uint32_t initial[8] = {
        UINT32_C(0x6a09e667), UINT32_C(0xbb67ae85),
        UINT32_C(0x3c6ef372), UINT32_C(0xa54ff53a),
        UINT32_C(0x510e527f), UINT32_C(0x9b05688c),
        UINT32_C(0x1f83d9ab), UINT32_C(0x5be0cd19)
    };
    memcpy(ctx->state, initial, sizeof(initial));
    ctx->total_size = UINT64_C(0);
    ctx->block_size = UINT32_C(0);
    memset(ctx->block, 0, sizeof(ctx->block));
}

void fd_sha256_update_internal(fd_sha256_ctx_internal *ctx,
                               const void *bytes,
                               uint64_t size)
{
    const uint8_t *input = (const uint8_t *)bytes;
    uint64_t remaining = size;

    ctx->total_size += size;
    while (remaining > UINT64_C(0)) {
        uint32_t space = UINT32_C(64) - ctx->block_size;
        uint64_t take64 = remaining < (uint64_t)space ? remaining : (uint64_t)space;
        uint32_t take = (uint32_t)take64;
        memcpy(ctx->block + ctx->block_size, input, (size_t)take);
        ctx->block_size += take;
        input += take;
        remaining -= take64;
        if (ctx->block_size == UINT32_C(64)) {
            fd_sha256_compress(ctx, ctx->block);
            ctx->block_size = UINT32_C(0);
        }
    }
}

void fd_sha256_final_internal(fd_sha256_ctx_internal *ctx, uint8_t out[32])
{
    uint64_t bit_size = ctx->total_size * UINT64_C(8);
    uint32_t index;

    ctx->block[ctx->block_size] = UINT8_C(0x80);
    ctx->block_size += UINT32_C(1);
    if (ctx->block_size > UINT32_C(56)) {
        memset(ctx->block + ctx->block_size, 0,
               (size_t)(UINT32_C(64) - ctx->block_size));
        fd_sha256_compress(ctx, ctx->block);
        ctx->block_size = UINT32_C(0);
    }
    memset(ctx->block + ctx->block_size, 0,
           (size_t)(UINT32_C(56) - ctx->block_size));
    for (index = 0U; index < 8U; ++index) {
        uint32_t shift = (UINT32_C(7) - index) * UINT32_C(8);
        ctx->block[56U + index] = (uint8_t)(bit_size >> shift);
    }
    fd_sha256_compress(ctx, ctx->block);
    for (index = 0U; index < 8U; ++index) {
        out[index * 4U] = (uint8_t)(ctx->state[index] >> 24U);
        out[index * 4U + 1U] = (uint8_t)(ctx->state[index] >> 16U);
        out[index * 4U + 2U] = (uint8_t)(ctx->state[index] >> 8U);
        out[index * 4U + 3U] = (uint8_t)ctx->state[index];
    }
}

fd_result fd_sha256(const void *bytes, uint64_t size, uint8_t out_sha256[32])
{
    fd_sha256_ctx_internal ctx;
    if (out_sha256 == NULL || (bytes == NULL && size > UINT64_C(0))) {
        return FD_ERR_INVALID_ARGUMENT;
    }
    fd_sha256_init_internal(&ctx);
    if (bytes != NULL && size > UINT64_C(0)) {
        fd_sha256_update_internal(&ctx, bytes, size);
    }
    fd_sha256_final_internal(&ctx, out_sha256);
    return FD_OK;
}
