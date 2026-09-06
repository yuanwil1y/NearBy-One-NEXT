#include "nearby_recognition_db.h"

#include <limits.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#define NBY_HEADER_SIZE 128u
#define NBY_FORMAT_MAJOR 1u
#define NBY_SCHEMA_VERSION 1u
#define NBY_READER_ABI 1u
#define NBY_FLAG_SHA256 0x0001u
#define NBY_KNOWN_FLAGS NBY_FLAG_SHA256
#define NBY_STREAM_BYTES 4096u

static const uint8_t k_magic[8] = {'N','B','Y','D','B',0,'\r','\n'};
/* One bounded scratch buffer is reused for manifest policy and payload integrity. */
static uint8_t s_stream[NBY_STREAM_BYTES];

typedef struct {
    uint32_t state[8];
    uint64_t bit_count;
    uint8_t block[64];
    size_t block_len;
} sha256_ctx_t;

static uint16_t rd16(const uint8_t *p)
{
    return (uint16_t)p[0] | ((uint16_t)p[1] << 8);
}

static uint32_t rd32(const uint8_t *p)
{
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) |
           ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

static uint64_t rd64(const uint8_t *p)
{
    return (uint64_t)rd32(p) | ((uint64_t)rd32(p + 4) << 32);
}

static uint32_t crc32_step(uint32_t crc, const uint8_t *data, size_t len)
{
    crc = ~crc;
    for (size_t i = 0; i < len; ++i) {
        crc ^= data[i];
        for (unsigned bit = 0; bit < 8; ++bit) {
            const uint32_t mask = (uint32_t)-(int32_t)(crc & 1u);
            crc = (crc >> 1) ^ (0xEDB88320u & mask);
        }
    }
    return ~crc;
}

static uint32_t rotr32(uint32_t v, unsigned n)
{
    return (v >> n) | (v << (32u - n));
}

static void sha256_transform(sha256_ctx_t *ctx, const uint8_t block[64])
{
    static const uint32_t k[64] = {
        0x428a2f98u,0x71374491u,0xb5c0fbcfu,0xe9b5dba5u,0x3956c25bu,0x59f111f1u,0x923f82a4u,0xab1c5ed5u,
        0xd807aa98u,0x12835b01u,0x243185beu,0x550c7dc3u,0x72be5d74u,0x80deb1feu,0x9bdc06a7u,0xc19bf174u,
        0xe49b69c1u,0xefbe4786u,0x0fc19dc6u,0x240ca1ccu,0x2de92c6fu,0x4a7484aau,0x5cb0a9dcu,0x76f988dau,
        0x983e5152u,0xa831c66du,0xb00327c8u,0xbf597fc7u,0xc6e00bf3u,0xd5a79147u,0x06ca6351u,0x14292967u,
        0x27b70a85u,0x2e1b2138u,0x4d2c6dfcu,0x53380d13u,0x650a7354u,0x766a0abbu,0x81c2c92eu,0x92722c85u,
        0xa2bfe8a1u,0xa81a664bu,0xc24b8b70u,0xc76c51a3u,0xd192e819u,0xd6990624u,0xf40e3585u,0x106aa070u,
        0x19a4c116u,0x1e376c08u,0x2748774cu,0x34b0bcb5u,0x391c0cb3u,0x4ed8aa4au,0x5b9cca4fu,0x682e6ff3u,
        0x748f82eeu,0x78a5636fu,0x84c87814u,0x8cc70208u,0x90befffau,0xa4506cebu,0xbef9a3f7u,0xc67178f2u,
    };
    uint32_t w[64];
    for (unsigned i = 0; i < 16; ++i) {
        const uint8_t *p = block + i * 4u;
        w[i] = ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) |
               ((uint32_t)p[2] << 8) | (uint32_t)p[3];
    }
    for (unsigned i = 16; i < 64; ++i) {
        const uint32_t s0 = rotr32(w[i - 15], 7) ^ rotr32(w[i - 15], 18) ^ (w[i - 15] >> 3);
        const uint32_t s1 = rotr32(w[i - 2], 17) ^ rotr32(w[i - 2], 19) ^ (w[i - 2] >> 10);
        w[i] = w[i - 16] + s0 + w[i - 7] + s1;
    }

    uint32_t a = ctx->state[0];
    uint32_t b = ctx->state[1];
    uint32_t c = ctx->state[2];
    uint32_t d = ctx->state[3];
    uint32_t e = ctx->state[4];
    uint32_t f = ctx->state[5];
    uint32_t g = ctx->state[6];
    uint32_t h = ctx->state[7];
    for (unsigned i = 0; i < 64; ++i) {
        const uint32_t s1 = rotr32(e, 6) ^ rotr32(e, 11) ^ rotr32(e, 25);
        const uint32_t ch = (e & f) ^ ((~e) & g);
        const uint32_t t1 = h + s1 + ch + k[i] + w[i];
        const uint32_t s0 = rotr32(a, 2) ^ rotr32(a, 13) ^ rotr32(a, 22);
        const uint32_t maj = (a & b) ^ (a & c) ^ (b & c);
        const uint32_t t2 = s0 + maj;
        h = g;
        g = f;
        f = e;
        e = d + t1;
        d = c;
        c = b;
        b = a;
        a = t1 + t2;
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

static void sha256_init(sha256_ctx_t *ctx)
{
    *ctx = (sha256_ctx_t){
        .state = {
            0x6a09e667u,0xbb67ae85u,0x3c6ef372u,0xa54ff53au,
            0x510e527fu,0x9b05688cu,0x1f83d9abu,0x5be0cd19u,
        },
    };
}

static void sha256_update(sha256_ctx_t *ctx, const uint8_t *data, size_t len)
{
    if (len == 0) return;
    ctx->bit_count += (uint64_t)len * 8u;
    while (len > 0) {
        size_t take = sizeof(ctx->block) - ctx->block_len;
        if (take > len) take = len;
        memcpy(ctx->block + ctx->block_len, data, take);
        ctx->block_len += take;
        data += take;
        len -= take;
        if (ctx->block_len == sizeof(ctx->block)) {
            sha256_transform(ctx, ctx->block);
            ctx->block_len = 0;
        }
    }
}

static void sha256_final(sha256_ctx_t *ctx, uint8_t out[32])
{
    const uint64_t bits = ctx->bit_count;
    ctx->block[ctx->block_len++] = 0x80u;
    if (ctx->block_len > 56u) {
        memset(ctx->block + ctx->block_len, 0, sizeof(ctx->block) - ctx->block_len);
        sha256_transform(ctx, ctx->block);
        ctx->block_len = 0;
    }
    memset(ctx->block + ctx->block_len, 0, 56u - ctx->block_len);
    for (unsigned i = 0; i < 8; ++i) {
        ctx->block[63u - i] = (uint8_t)(bits >> (i * 8u));
    }
    sha256_transform(ctx, ctx->block);
    for (unsigned i = 0; i < 8; ++i) {
        out[i * 4u] = (uint8_t)(ctx->state[i] >> 24);
        out[i * 4u + 1u] = (uint8_t)(ctx->state[i] >> 16);
        out[i * 4u + 2u] = (uint8_t)(ctx->state[i] >> 8);
        out[i * 4u + 3u] = (uint8_t)ctx->state[i];
    }
}

static bool seek_abs(FILE *fp, uint64_t offset)
{
    if (offset > (uint64_t)LONG_MAX) return false;
    return fseek(fp, (long)offset, SEEK_SET) == 0;
}

static bool actual_size(FILE *fp, uint64_t *out)
{
    if (fseek(fp, 0, SEEK_END) != 0) return false;
    const long size = ftell(fp);
    if (size < 0 || fseek(fp, 0, SEEK_SET) != 0) return false;
    *out = (uint64_t)size;
    return true;
}

typedef struct {
    const char *token;
    size_t length;
    size_t matched;
    uint8_t count;
} token_matcher_t;

static bool token_feed(token_matcher_t *matcher, uint8_t ch)
{
    if (matcher == NULL || matcher->length == 0u) return false;
    if (ch == (uint8_t)matcher->token[matcher->matched]) {
        ++matcher->matched;
        if (matcher->matched == matcher->length) {
            if (matcher->count < 2u) ++matcher->count;
            matcher->matched = 0u;
            return true;
        }
        return false;
    }
    matcher->matched = ch == (uint8_t)matcher->token[0] ? 1u : 0u;
    return false;
}

static void token_reset(token_matcher_t *matcher)
{
    matcher->matched = 0u;
    matcher->count = 0u;
}

static void source_policy_reset(token_matcher_t required[6], token_matcher_t *allowed)
{
    for (size_t i = 0; i < 6u; ++i) token_reset(&required[i]);
    token_reset(allowed);
}

static bool source_policy_valid(const token_matcher_t required[6],
                                const token_matcher_t *allowed)
{
    for (size_t i = 0; i < 6u; ++i) {
        if (required[i].count != 1u) return false;
    }
    return allowed->count == 1u;
}

static nearby_db_result_t manifest_release_validate(FILE *fp,
                                                    uint64_t offset,
                                                    uint64_t length,
                                                    uint32_t expected_sources)
{
    static const char container_token[] = "\"container\":\"nearby-recognition-db\"";
    static const char schema_token[] = "\"schema_version\":1";
    static const char sources_token[] = "\"sources\":[";
    static const char *const required_tokens[6] = {
        "\"source_id\":",
        "\"upstream_revision\":",
        "\"license_spdx\":",
        "\"redistribution\":",
        "\"classification\":",
        "\"transform\":",
    };
    static const char allowed_token[] = "\"redistribution\":\"allowed\"";

    token_matcher_t container = {container_token, sizeof(container_token) - 1u, 0u, 0u};
    token_matcher_t schema = {schema_token, sizeof(schema_token) - 1u, 0u, 0u};
    token_matcher_t sources = {sources_token, sizeof(sources_token) - 1u, 0u, 0u};
    token_matcher_t required[6];
    for (size_t i = 0; i < 6u; ++i) {
        required[i] = (token_matcher_t){required_tokens[i], strlen(required_tokens[i]), 0u, 0u};
    }
    token_matcher_t allowed = {allowed_token, sizeof(allowed_token) - 1u, 0u, 0u};

    bool sources_started = false;
    bool array_closed = false;
    bool in_string = false;
    bool escape = false;
    unsigned object_depth = 0u;
    uint32_t object_count = 0u;

    if (!seek_abs(fp, offset)) return NEARBY_DB_ERR_IO;
    uint64_t remaining = length;
    while (remaining > 0u) {
        const size_t take = remaining < sizeof(s_stream) ? (size_t)remaining : sizeof(s_stream);
        if (fread(s_stream, 1, take, fp) != take) return NEARBY_DB_ERR_IO;
        remaining -= take;

        for (size_t i = 0; i < take; ++i) {
            const uint8_t ch = s_stream[i];
            (void)token_feed(&container, ch);
            (void)token_feed(&schema, ch);

            if (!sources_started) {
                if (token_feed(&sources, ch)) {
                    sources_started = true;
                    in_string = false;
                    escape = false;
                }
                continue;
            }
            if (array_closed) continue;

            if (object_depth > 0u) {
                for (size_t t = 0; t < 6u; ++t) (void)token_feed(&required[t], ch);
                (void)token_feed(&allowed, ch);
            }

            if (in_string) {
                if (escape) {
                    escape = false;
                } else if (ch == (uint8_t)'\\') {
                    escape = true;
                } else if (ch == (uint8_t)'"') {
                    in_string = false;
                }
                continue;
            }
            if (ch == (uint8_t)'"') {
                in_string = true;
                continue;
            }
            if (ch == (uint8_t)'{') {
                if (object_depth == 0u) source_policy_reset(required, &allowed);
                ++object_depth;
                continue;
            }
            if (ch == (uint8_t)'}') {
                if (object_depth == 0u) return NEARBY_DB_ERR_FORMAT;
                --object_depth;
                if (object_depth == 0u) {
                    if (!source_policy_valid(required, &allowed)) return NEARBY_DB_ERR_FORMAT;
                    ++object_count;
                }
                continue;
            }
            if (ch == (uint8_t)']' && object_depth == 0u) {
                array_closed = true;
            }
        }
    }

    if (container.count == 0u || schema.count == 0u || !sources_started ||
        !array_closed || in_string || object_depth != 0u || object_count != expected_sources) {
        return NEARBY_DB_ERR_FORMAT;
    }
    return NEARBY_DB_OK;
}

nearby_db_result_t nearby_db_validate_release_file(
    const char *path,
    nearby_db_file_info_t *out_info)
{
    if (path == NULL) return NEARBY_DB_ERR_ARG;
    if (out_info != NULL) memset(out_info, 0, sizeof(*out_info));

    FILE *fp = fopen(path, "rb");
    if (fp == NULL) return NEARBY_DB_ERR_IO;

    uint8_t header[NBY_HEADER_SIZE];
    nearby_db_result_t result = NEARBY_DB_ERR_FORMAT;
    if (fread(header, 1, sizeof(header), fp) != sizeof(header)) {
        result = NEARBY_DB_ERR_IO;
        goto done;
    }
    if (memcmp(header, k_magic, sizeof(k_magic)) != 0 ||
        rd16(header + 8) != NBY_FORMAT_MAJOR ||
        rd16(header + 12) != NBY_HEADER_SIZE ||
        rd32(header + 16) != NBY_SCHEMA_VERSION ||
        rd32(header + 24) > NBY_READER_ABI) {
        result = NEARBY_DB_ERR_UNSUPPORTED;
        goto done;
    }

    const uint16_t flags = rd16(header + 14);
    if ((flags & (uint16_t)~NBY_KNOWN_FLAGS) != 0u || (flags & NBY_FLAG_SHA256) == 0u) {
        result = NEARBY_DB_ERR_UNSUPPORTED;
        goto done;
    }
    for (size_t i = 120; i < 128; ++i) {
        if (header[i] != 0u) goto done;
    }

    const uint32_t expected_header_crc = rd32(header + 76);
    uint8_t crc_header[NBY_HEADER_SIZE];
    memcpy(crc_header, header, sizeof(crc_header));
    memset(crc_header + 76, 0, 4);
    if (crc32_step(0, crc_header, sizeof(crc_header)) != expected_header_crc) goto done;

    uint64_t size = 0;
    if (!actual_size(fp, &size)) {
        result = NEARBY_DB_ERR_IO;
        goto done;
    }
    const uint64_t declared_size = rd64(header + 32);
    const uint64_t manifest_offset = rd64(header + 40);
    const uint64_t manifest_size = rd64(header + 48);
    const uint64_t payload_offset = rd64(header + 56);
    const uint64_t payload_size = rd64(header + 64);
    const uint32_t source_count = rd32(header + 28);
    if (declared_size != size || manifest_offset != NBY_HEADER_SIZE ||
        manifest_offset > size || manifest_size > size - manifest_offset ||
        payload_offset != manifest_offset + manifest_size ||
        payload_offset > size || payload_size > size - payload_offset ||
        payload_offset + payload_size != size) {
        goto done;
    }

    result = manifest_release_validate(fp, manifest_offset, manifest_size, source_count);
    if (result != NEARBY_DB_OK) goto done;

    if (!seek_abs(fp, payload_offset)) {
        result = NEARBY_DB_ERR_IO;
        goto done;
    }
    uint64_t remaining = payload_size;
    uint32_t payload_crc = 0;
    sha256_ctx_t sha;
    sha256_init(&sha);
    while (remaining > 0u) {
        const size_t take = remaining < sizeof(s_stream) ? (size_t)remaining : sizeof(s_stream);
        if (fread(s_stream, 1, take, fp) != take) {
            result = NEARBY_DB_ERR_IO;
            goto done;
        }
        payload_crc = crc32_step(payload_crc, s_stream, take);
        sha256_update(&sha, s_stream, take);
        remaining -= take;
    }
    if (payload_crc != rd32(header + 72)) {
        result = NEARBY_DB_ERR_FORMAT;
        goto done;
    }
    uint8_t digest[32];
    sha256_final(&sha, digest);
    if (memcmp(digest, header + 80, sizeof(digest)) != 0) {
        result = NEARBY_DB_ERR_FORMAT;
        goto done;
    }

    fclose(fp);
    fp = NULL;

    nearby_db_t db;
    result = nearby_db_open(&db, path);
    if (result != NEARBY_DB_OK) return result;
    if (out_info != NULL) {
        out_info->db_version = db.db_version;
        out_info->schema_version = db.schema_version;
        out_info->source_count = source_count;
        out_info->file_size = db.file_size;
    }
    nearby_db_close(&db);
    return NEARBY_DB_OK;

done:
    fclose(fp);
    return result;
}
