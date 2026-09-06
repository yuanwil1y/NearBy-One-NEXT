#include "nearby_recognition_db.h"

#include <ctype.h>
#include <limits.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#define NBY_FLAT_ENTRY_SIZE 16u
#define NBY_COMPARE_CHUNK 64u
#define NBY_PREFIX_MAX 384u
#define NBY_DOMAIN_MAX 96u
#define NBY_JSON_KEY_MAX 64u
#define NBY_JSON_STRING_MAX 256u
#define NBY_MANUFACTURER_PREFIX_MAX 64u
#define NBY_KNOWN_VALUE_FLAGS NEARBY_DB_VALUE_FLAG_AMBIGUOUS
#define NBY_JSON_MAX_DEPTH 8u

typedef struct {
    const char *ptr;
    size_t len;
} nby_json_slice_t;

typedef struct {
    const char *p;
    const char *end;
} nby_json_cursor_t;

typedef enum {
    NBY_JSON_ERROR = -1,
    NBY_JSON_MISSING = 0,
    NBY_JSON_FOUND = 1,
} nby_json_find_t;

typedef enum {
    NBY_MATCH_KIND_BLE = 0,
    NBY_MATCH_KIND_ZEROCONF = 1,
    NBY_MATCH_KIND_SSDP = 2,
    NBY_MATCH_KIND_DHCP = 3,
} nby_match_kind_t;

typedef struct {
    nearby_db_match_result_t *out;
    char first_domain[NBY_DOMAIN_MAX];
    bool have_first_domain;
} nby_match_accumulator_t;

static uint16_t nby_rd16(const uint8_t *p) {
    return (uint16_t)p[0] | ((uint16_t)p[1] << 8);
}

static uint32_t nby_rd32(const uint8_t *p) {
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) |
           ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

static bool nby_seek_abs(FILE *fp, uint64_t offset) {
    if (!fp || offset > (uint64_t)LONG_MAX) {
        return false;
    }
    return fseek(fp, (long)offset, SEEK_SET) == 0;
}

static nearby_db_result_t nby_read_exact(FILE *fp, uint64_t offset, void *buf, size_t len) {
    if (!nby_seek_abs(fp, offset)) {
        return NEARBY_DB_ERR_IO;
    }
    return fread(buf, 1, len, fp) == len ? NEARBY_DB_OK : NEARBY_DB_ERR_IO;
}

static nearby_db_result_t nby_read_entry(
    nearby_db_t *db,
    uint32_t index,
    uint32_t *key_off,
    uint16_t *key_len,
    nearby_db_value_ref_t *out_ref) {
    uint8_t raw[NBY_FLAT_ENTRY_SIZE];
    if (!db || !db->fp || !key_off || !key_len || !out_ref) {
        return NEARBY_DB_ERR_ARG;
    }
    if (index >= db->record_count) {
        return NEARBY_DB_ERR_RANGE;
    }
    nearby_db_result_t rc = nby_read_exact(
        db->fp,
        db->index_offset + (uint64_t)index * NBY_FLAT_ENTRY_SIZE,
        raw,
        sizeof(raw));
    if (rc != NEARBY_DB_OK) {
        return rc;
    }
    uint16_t flags = nby_rd16(raw + 6);
    if ((flags & (uint16_t)~NBY_KNOWN_VALUE_FLAGS) != 0u) {
        return NEARBY_DB_ERR_UNSUPPORTED;
    }
    uint32_t value_off = nby_rd32(raw + 8);
    uint32_t value_len = nby_rd32(raw + 12);
    if ((uint64_t)value_off + value_len > db->payload_size ||
        db->blob_offset + (uint64_t)value_off + value_len > db->file_size) {
        return NEARBY_DB_ERR_FORMAT;
    }
    *key_off = nby_rd32(raw);
    *key_len = nby_rd16(raw + 4);
    out_ref->value_offset = db->blob_offset + value_off;
    out_ref->value_size = value_len;
    out_ref->flags = flags;
    return NEARBY_DB_OK;
}

static nearby_db_result_t nby_compare_stored_key(
    nearby_db_t *db,
    uint32_t key_off,
    uint16_t stored_len,
    const uint8_t *wanted,
    size_t wanted_len,
    int *out_cmp) {
    uint8_t buf[NBY_COMPARE_CHUNK];
    size_t common = stored_len < wanted_len ? stored_len : wanted_len;
    size_t done = 0;
    while (done < common) {
        size_t take = common - done;
        if (take > sizeof(buf)) {
            take = sizeof(buf);
        }
        nearby_db_result_t rc = nby_read_exact(
            db->fp, db->blob_offset + key_off + done, buf, take);
        if (rc != NEARBY_DB_OK) {
            return rc;
        }
        int cmp = memcmp(buf, wanted + done, take);
        if (cmp != 0) {
            *out_cmp = cmp < 0 ? -1 : 1;
            return NEARBY_DB_OK;
        }
        done += take;
    }
    *out_cmp = stored_len < wanted_len ? -1 : (stored_len > wanted_len ? 1 : 0);
    return NEARBY_DB_OK;
}

static nearby_db_result_t nby_stored_key_has_prefix(
    nearby_db_t *db,
    uint32_t key_off,
    uint16_t stored_len,
    const char *prefix,
    size_t prefix_len,
    bool *out_match) {
    uint8_t buf[NBY_COMPARE_CHUNK];
    if (stored_len < prefix_len) {
        *out_match = false;
        return NEARBY_DB_OK;
    }
    size_t done = 0;
    while (done < prefix_len) {
        size_t take = prefix_len - done;
        if (take > sizeof(buf)) {
            take = sizeof(buf);
        }
        nearby_db_result_t rc = nby_read_exact(
            db->fp, db->blob_offset + key_off + done, buf, take);
        if (rc != NEARBY_DB_OK) {
            return rc;
        }
        if (memcmp(buf, prefix + done, take) != 0) {
            *out_match = false;
            return NEARBY_DB_OK;
        }
        done += take;
    }
    *out_match = true;
    return NEARBY_DB_OK;
}

static nearby_db_result_t nby_lower_bound_prefix(
    nearby_db_t *db,
    const char *prefix,
    size_t prefix_len,
    uint32_t *out_index) {
    uint32_t lo = 0;
    uint32_t hi = db->record_count;
    while (lo < hi) {
        uint32_t mid = lo + (hi - lo) / 2;
        uint32_t key_off = 0;
        uint16_t key_len = 0;
        nearby_db_value_ref_t ref;
        nearby_db_result_t rc = nby_read_entry(db, mid, &key_off, &key_len, &ref);
        if (rc != NEARBY_DB_OK) {
            return rc;
        }
        int cmp = 0;
        rc = nby_compare_stored_key(
            db,
            key_off,
            key_len,
            (const uint8_t *)prefix,
            prefix_len,
            &cmp);
        if (rc != NEARBY_DB_OK) {
            return rc;
        }
        if (cmp < 0) {
            lo = mid + 1;
        } else {
            hi = mid;
        }
    }
    *out_index = lo;
    return NEARBY_DB_OK;
}

static void nby_json_skip_ws(nby_json_cursor_t *cur) {
    while (cur->p < cur->end && isspace((unsigned char)*cur->p)) {
        ++cur->p;
    }
}

static bool nby_json_read_string_slice(
    nby_json_cursor_t *cur, nby_json_slice_t *out) {
    nby_json_skip_ws(cur);
    if (cur->p >= cur->end || *cur->p != '"') {
        return false;
    }
    const char *start = cur->p++;
    while (cur->p < cur->end) {
        unsigned char ch = (unsigned char)*cur->p++;
        if (ch == '"') {
            out->ptr = start;
            out->len = (size_t)(cur->p - start);
            return true;
        }
        if (ch != '\\') {
            continue;
        }
        if (cur->p >= cur->end) {
            return false;
        }
        char esc = *cur->p++;
        if (esc == 'u') {
            if ((size_t)(cur->end - cur->p) < 4u) {
                return false;
            }
            for (int i = 0; i < 4; ++i) {
                if (!isxdigit((unsigned char)cur->p[i])) {
                    return false;
                }
            }
            cur->p += 4;
        }
    }
    return false;
}

static bool nby_json_skip_value_depth(nby_json_cursor_t *cur, unsigned depth);

static bool nby_json_skip_array(nby_json_cursor_t *cur, unsigned depth) {
    if (depth > NBY_JSON_MAX_DEPTH || cur->p >= cur->end || *cur->p != '[') {
        return false;
    }
    ++cur->p;
    nby_json_skip_ws(cur);
    if (cur->p < cur->end && *cur->p == ']') {
        ++cur->p;
        return true;
    }
    for (;;) {
        if (!nby_json_skip_value_depth(cur, depth + 1u)) {
            return false;
        }
        nby_json_skip_ws(cur);
        if (cur->p >= cur->end) {
            return false;
        }
        if (*cur->p == ']') {
            ++cur->p;
            return true;
        }
        if (*cur->p != ',') {
            return false;
        }
        ++cur->p;
    }
}

static bool nby_json_skip_object(nby_json_cursor_t *cur, unsigned depth) {
    if (depth > NBY_JSON_MAX_DEPTH || cur->p >= cur->end || *cur->p != '{') {
        return false;
    }
    ++cur->p;
    nby_json_skip_ws(cur);
    if (cur->p < cur->end && *cur->p == '}') {
        ++cur->p;
        return true;
    }
    for (;;) {
        nby_json_slice_t key;
        if (!nby_json_read_string_slice(cur, &key)) {
            return false;
        }
        (void)key;
        nby_json_skip_ws(cur);
        if (cur->p >= cur->end || *cur->p != ':') {
            return false;
        }
        ++cur->p;
        if (!nby_json_skip_value_depth(cur, depth + 1u)) {
            return false;
        }
        nby_json_skip_ws(cur);
        if (cur->p >= cur->end) {
            return false;
        }
        if (*cur->p == '}') {
            ++cur->p;
            return true;
        }
        if (*cur->p != ',') {
            return false;
        }
        ++cur->p;
    }
}

static bool nby_json_skip_value_depth(nby_json_cursor_t *cur, unsigned depth) {
    nby_json_skip_ws(cur);
    if (depth > NBY_JSON_MAX_DEPTH || cur->p >= cur->end) {
        return false;
    }
    if (*cur->p == '"') {
        nby_json_slice_t value;
        return nby_json_read_string_slice(cur, &value);
    }
    if (*cur->p == '{') {
        return nby_json_skip_object(cur, depth);
    }
    if (*cur->p == '[') {
        return nby_json_skip_array(cur, depth);
    }
    const char *start = cur->p;
    while (cur->p < cur->end && *cur->p != ',' && *cur->p != '}' &&
           *cur->p != ']' && !isspace((unsigned char)*cur->p)) {
        ++cur->p;
    }
    return cur->p > start;
}

static int nby_hex_value(char ch) {
    if (ch >= '0' && ch <= '9') return ch - '0';
    if (ch >= 'a' && ch <= 'f') return ch - 'a' + 10;
    if (ch >= 'A' && ch <= 'F') return ch - 'A' + 10;
    return -1;
}

static bool nby_json_decode_string(
    nby_json_slice_t raw, char *out, size_t out_cap, size_t *out_len) {
    if (!out || out_cap == 0u || raw.len < 2u || raw.ptr[0] != '"' ||
        raw.ptr[raw.len - 1u] != '"') {
        return false;
    }
    size_t used = 0;
    for (size_t i = 1; i + 1u < raw.len; ++i) {
        unsigned char ch = (unsigned char)raw.ptr[i];
        if (ch != '\\') {
            if (used + 1u >= out_cap) return false;
            out[used++] = (char)ch;
            continue;
        }
        if (++i + 1u >= raw.len) return false;
        char esc = raw.ptr[i];
        char decoded = 0;
        switch (esc) {
            case '"': decoded = '"'; break;
            case '\\': decoded = '\\'; break;
            case '/': decoded = '/'; break;
            case 'b': decoded = '\b'; break;
            case 'f': decoded = '\f'; break;
            case 'n': decoded = '\n'; break;
            case 'r': decoded = '\r'; break;
            case 't': decoded = '\t'; break;
            case 'u': {
                if (i + 4u >= raw.len - 1u) return false;
                uint32_t cp = 0;
                for (int n = 0; n < 4; ++n) {
                    int hex = nby_hex_value(raw.ptr[++i]);
                    if (hex < 0) return false;
                    cp = (cp << 4) | (uint32_t)hex;
                }
                if (cp >= 0xD800u && cp <= 0xDFFFu) return false;
                if (cp <= 0x7Fu) {
                    if (used + 1u >= out_cap) return false;
                    out[used++] = (char)cp;
                } else if (cp <= 0x7FFu) {
                    if (used + 2u >= out_cap) return false;
                    out[used++] = (char)(0xC0u | (cp >> 6));
                    out[used++] = (char)(0x80u | (cp & 0x3Fu));
                } else {
                    if (used + 3u >= out_cap) return false;
                    out[used++] = (char)(0xE0u | (cp >> 12));
                    out[used++] = (char)(0x80u | ((cp >> 6) & 0x3Fu));
                    out[used++] = (char)(0x80u | (cp & 0x3Fu));
                }
                continue;
            }
            default: return false;
        }
        if (used + 1u >= out_cap) return false;
        out[used++] = decoded;
    }
    out[used] = '\0';
    if (out_len) *out_len = used;
    return true;
}

static nby_json_find_t nby_json_object_find(
    nby_json_slice_t object,
    const char *wanted_key,
    nby_json_slice_t *out_value) {
    nby_json_cursor_t cur = {object.ptr, object.ptr + object.len};
    nby_json_skip_ws(&cur);
    if (cur.p >= cur.end || *cur.p != '{') return NBY_JSON_ERROR;
    ++cur.p;
    nby_json_skip_ws(&cur);
    if (cur.p < cur.end && *cur.p == '}') return NBY_JSON_MISSING;
    for (;;) {
        nby_json_slice_t raw_key;
        char key[NBY_JSON_KEY_MAX];
        if (!nby_json_read_string_slice(&cur, &raw_key) ||
            !nby_json_decode_string(raw_key, key, sizeof(key), NULL)) {
            return NBY_JSON_ERROR;
        }
        nby_json_skip_ws(&cur);
        if (cur.p >= cur.end || *cur.p != ':') return NBY_JSON_ERROR;
        ++cur.p;
        nby_json_skip_ws(&cur);
        const char *value_start = cur.p;
        if (!nby_json_skip_value_depth(&cur, 0u)) return NBY_JSON_ERROR;
        nby_json_slice_t value = {value_start, (size_t)(cur.p - value_start)};
        if (strcmp(key, wanted_key) == 0) {
            *out_value = value;
            return NBY_JSON_FOUND;
        }
        nby_json_skip_ws(&cur);
        if (cur.p >= cur.end) return NBY_JSON_ERROR;
        if (*cur.p == '}') return NBY_JSON_MISSING;
        if (*cur.p != ',') return NBY_JSON_ERROR;
        ++cur.p;
    }
}

static nearby_db_result_t nby_json_get_string(
    nby_json_slice_t object,
    const char *key,
    char *out,
    size_t out_cap,
    bool *out_present) {
    nby_json_slice_t value;
    nby_json_find_t found = nby_json_object_find(object, key, &value);
    if (found == NBY_JSON_ERROR) return NEARBY_DB_ERR_FORMAT;
    if (found == NBY_JSON_MISSING) {
        *out_present = false;
        return NEARBY_DB_OK;
    }
    if (!nby_json_decode_string(value, out, out_cap, NULL)) return NEARBY_DB_ERR_FORMAT;
    *out_present = true;
    return NEARBY_DB_OK;
}

static nearby_db_result_t nby_json_get_bool(
    nby_json_slice_t object,
    const char *key,
    bool *out_value,
    bool *out_present) {
    nby_json_slice_t value;
    nby_json_find_t found = nby_json_object_find(object, key, &value);
    if (found == NBY_JSON_ERROR) return NEARBY_DB_ERR_FORMAT;
    if (found == NBY_JSON_MISSING) {
        *out_present = false;
        return NEARBY_DB_OK;
    }
    if (value.len == 4u && memcmp(value.ptr, "true", 4u) == 0) *out_value = true;
    else if (value.len == 5u && memcmp(value.ptr, "false", 5u) == 0) *out_value = false;
    else return NEARBY_DB_ERR_FORMAT;
    *out_present = true;
    return NEARBY_DB_OK;
}

static nearby_db_result_t nby_json_get_u32(
    nby_json_slice_t object,
    const char *key,
    uint32_t *out_value,
    bool *out_present) {
    nby_json_slice_t value;
    nby_json_find_t found = nby_json_object_find(object, key, &value);
    if (found == NBY_JSON_ERROR) return NEARBY_DB_ERR_FORMAT;
    if (found == NBY_JSON_MISSING) {
        *out_present = false;
        return NEARBY_DB_OK;
    }
    if (value.len == 0u || value.len > 10u) return NEARBY_DB_ERR_FORMAT;
    uint64_t parsed = 0;
    for (size_t i = 0; i < value.len; ++i) {
        unsigned char ch = (unsigned char)value.ptr[i];
        if (!isdigit(ch)) return NEARBY_DB_ERR_FORMAT;
        parsed = parsed * 10u + (uint64_t)(ch - '0');
        if (parsed > UINT32_MAX) return NEARBY_DB_ERR_FORMAT;
    }
    *out_value = (uint32_t)parsed;
    *out_present = true;
    return NEARBY_DB_OK;
}

static nearby_db_result_t nby_json_get_object(
    nby_json_slice_t object,
    const char *key,
    nby_json_slice_t *out_value,
    bool *out_present) {
    nby_json_find_t found = nby_json_object_find(object, key, out_value);
    if (found == NBY_JSON_ERROR) return NEARBY_DB_ERR_FORMAT;
    if (found == NBY_JSON_MISSING) {
        *out_present = false;
        return NEARBY_DB_OK;
    }
    if (out_value->len < 2u || out_value->ptr[0] != '{' ||
        out_value->ptr[out_value->len - 1u] != '}') return NEARBY_DB_ERR_FORMAT;
    *out_present = true;
    return NEARBY_DB_OK;
}

static nearby_db_result_t nby_json_get_u8_array(
    nby_json_slice_t object,
    const char *key,
    uint8_t *out,
    size_t out_cap,
    size_t *out_len,
    bool *out_present) {
    nby_json_slice_t value;
    nby_json_find_t found = nby_json_object_find(object, key, &value);
    if (found == NBY_JSON_ERROR) return NEARBY_DB_ERR_FORMAT;
    if (found == NBY_JSON_MISSING) {
        *out_present = false;
        *out_len = 0;
        return NEARBY_DB_OK;
    }
    nby_json_cursor_t cur = {value.ptr, value.ptr + value.len};
    nby_json_skip_ws(&cur);
    if (cur.p >= cur.end || *cur.p != '[') return NEARBY_DB_ERR_FORMAT;
    ++cur.p;
    size_t count = 0;
    nby_json_skip_ws(&cur);
    if (cur.p < cur.end && *cur.p == ']') {
        *out_present = true;
        *out_len = 0;
        return NEARBY_DB_OK;
    }
    for (;;) {
        nby_json_skip_ws(&cur);
        const char *start = cur.p;
        while (cur.p < cur.end && isdigit((unsigned char)*cur.p)) ++cur.p;
        if (cur.p == start || (size_t)(cur.p - start) > 3u || count >= out_cap)
            return NEARBY_DB_ERR_RANGE;
        unsigned value_u = 0;
        for (const char *p = start; p < cur.p; ++p) value_u = value_u * 10u + (unsigned)(*p - '0');
        if (value_u > 255u) return NEARBY_DB_ERR_FORMAT;
        out[count++] = (uint8_t)value_u;
        nby_json_skip_ws(&cur);
        if (cur.p >= cur.end) return NEARBY_DB_ERR_FORMAT;
        if (*cur.p == ']') {
            ++cur.p;
            break;
        }
        if (*cur.p != ',') return NEARBY_DB_ERR_FORMAT;
        ++cur.p;
    }
    nby_json_skip_ws(&cur);
    if (cur.p != cur.end) return NEARBY_DB_ERR_FORMAT;
    *out_present = true;
    *out_len = count;
    return NEARBY_DB_OK;
}

static unsigned char nby_fold_ascii(unsigned char ch) {
    if (ch >= 'A' && ch <= 'Z') return (unsigned char)(ch - 'A' + 'a');
    return ch;
}

static bool nby_byte_equal(unsigned char a, unsigned char b, bool fold_ascii) {
    if (fold_ascii) {
        a = nby_fold_ascii(a);
        b = nby_fold_ascii(b);
    }
    return a == b;
}

static bool nby_glob_match_impl(
    const char *pattern,
    size_t pattern_len,
    size_t pi,
    const char *text,
    size_t text_len,
    size_t ti,
    bool fold_ascii) {
    while (pi < pattern_len) {
        char pc = pattern[pi++];
        if (pc == '*') {
            while (pi < pattern_len && pattern[pi] == '*') ++pi;
            if (pi == pattern_len) return true;
            for (size_t candidate = ti; candidate <= text_len; ++candidate) {
                if (nby_glob_match_impl(pattern, pattern_len, pi, text, text_len, candidate, fold_ascii))
                    return true;
            }
            return false;
        }
        if (ti >= text_len) return false;
        if (pc == '?') {
            ++ti;
            continue;
        }
        if (pc == '[') {
            size_t class_start = pi;
            bool negate = false;
            if (pi < pattern_len && (pattern[pi] == '!' || pattern[pi] == '^')) {
                negate = true;
                ++pi;
            }
            bool matched = false;
            bool closed = false;
            unsigned char tc = (unsigned char)text[ti];
            if (fold_ascii) tc = nby_fold_ascii(tc);
            while (pi < pattern_len) {
                if (pattern[pi] == ']' && pi > class_start) {
                    ++pi;
                    closed = true;
                    break;
                }
                unsigned char start = (unsigned char)pattern[pi++];
                if (fold_ascii) start = nby_fold_ascii(start);
                unsigned char end = start;
                if (pi + 1u < pattern_len && pattern[pi] == '-' && pattern[pi + 1u] != ']') {
                    ++pi;
                    end = (unsigned char)pattern[pi++];
                    if (fold_ascii) end = nby_fold_ascii(end);
                }
                if (tc >= start && tc <= end) matched = true;
            }
            if (!closed) {
                if (!nby_byte_equal((unsigned char)'[', (unsigned char)text[ti], fold_ascii)) return false;
                pi = class_start;
                ++ti;
                continue;
            }
            if (negate) matched = !matched;
            if (!matched) return false;
            ++ti;
            continue;
        }
        if (!nby_byte_equal((unsigned char)pc, (unsigned char)text[ti], fold_ascii)) return false;
        ++ti;
    }
    return ti == text_len;
}

static bool nby_glob_match(const char *pattern, const nearby_db_text_t *text, bool fold_ascii) {
    if (!pattern || !text || (!text->ptr && text->len != 0u)) return false;
    return nby_glob_match_impl(pattern, strlen(pattern), 0u, text->ptr, text->len, 0u, fold_ascii);
}

static bool nby_text_equals_cstr(const nearby_db_text_t *text, const char *value, bool fold_ascii) {
    size_t value_len = strlen(value);
    if (!text || text->len != value_len || (!text->ptr && text->len != 0u)) return false;
    for (size_t i = 0; i < value_len; ++i) {
        if (!nby_byte_equal((unsigned char)text->ptr[i], (unsigned char)value[i], fold_ascii)) return false;
    }
    return true;
}

static bool nby_text_equals_text(const nearby_db_text_t *a, const nearby_db_text_t *b, bool fold_ascii) {
    if (!a || !b || a->len != b->len || (!a->ptr && a->len != 0u) || (!b->ptr && b->len != 0u))
        return false;
    for (size_t i = 0; i < a->len; ++i) {
        if (!nby_byte_equal((unsigned char)a->ptr[i], (unsigned char)b->ptr[i], fold_ascii)) return false;
    }
    return true;
}

static const nearby_db_text_t *nby_find_kv_value(
    const nearby_db_kv_t *items,
    size_t count,
    const char *key,
    bool key_fold_ascii) {
    nearby_db_text_t wanted = {key, strlen(key)};
    for (size_t i = 0; i < count; ++i) {
        if (nby_text_equals_text(&items[i].key, &wanted, key_fold_ascii)) return &items[i].value;
    }
    return NULL;
}

static const nearby_db_ble_manufacturer_data_t *nby_find_manufacturer(
    const nearby_db_ble_facts_t *facts, uint16_t id) {
    for (size_t i = 0; i < facts->manufacturer_data_count; ++i) {
        if (facts->manufacturer_data[i].manufacturer_id == id) return &facts->manufacturer_data[i];
    }
    return NULL;
}

static bool nby_text_list_contains_cstr(
    const nearby_db_text_t *items, size_t count, const char *value, bool fold_ascii) {
    for (size_t i = 0; i < count; ++i) {
        if (nby_text_equals_cstr(&items[i], value, fold_ascii)) return true;
    }
    return false;
}

static nearby_db_result_t nby_load_value(
    nearby_db_t *db,
    const nearby_db_value_ref_t *ref,
    nearby_db_match_workspace_t *workspace,
    nby_json_slice_t *out) {
    if (!workspace || !workspace->data || workspace->size == 0u) return NEARBY_DB_ERR_ARG;
    if (ref->value_size > workspace->size) return NEARBY_DB_ERR_RANGE;
    size_t read = 0;
    nearby_db_result_t rc = nearby_db_read_value(db, ref, 0u, workspace->data, ref->value_size, &read);
    if (rc != NEARBY_DB_OK) return rc;
    if (read != ref->value_size) return NEARBY_DB_ERR_IO;
    out->ptr = (const char *)workspace->data;
    out->len = read;
    return NEARBY_DB_OK;
}

static nearby_db_result_t nby_get_record_type_and_domain(
    nby_json_slice_t object,
    const char *expected_type,
    char *domain,
    size_t domain_cap,
    bool *out_is_type) {
    char record_type[NBY_JSON_STRING_MAX];
    bool present = false;
    nearby_db_result_t rc = nby_json_get_string(object, "record_type", record_type, sizeof(record_type), &present);
    if (rc != NEARBY_DB_OK) return rc;
    if (!present || strcmp(record_type, expected_type) != 0) {
        *out_is_type = false;
        return NEARBY_DB_OK;
    }
    rc = nby_json_get_string(object, "domain", domain, domain_cap, &present);
    if (rc != NEARBY_DB_OK) return rc;
    if (!present) return NEARBY_DB_ERR_FORMAT;
    *out_is_type = true;
    return NEARBY_DB_OK;
}

static nearby_db_result_t nby_match_ble_value(
    nearby_db_t *db,
    const nearby_db_value_ref_t *ref,
    const nearby_db_ble_facts_t *facts,
    nearby_db_match_workspace_t *workspace,
    bool *out_match,
    char *domain,
    size_t domain_cap) {
    nby_json_slice_t root;
    nearby_db_result_t rc = nby_load_value(db, ref, workspace, &root);
    if (rc != NEARBY_DB_OK) return rc;
    bool is_type = false;
    rc = nby_get_record_type_and_domain(root, "ha_bluetooth_matcher", domain, domain_cap, &is_type);
    if (rc != NEARBY_DB_OK || !is_type) {
        *out_match = false;
        return rc;
    }
    bool connectable = true;
    bool present = false;
    rc = nby_json_get_bool(root, "connectable", &connectable, &present);
    if (rc != NEARBY_DB_OK) return rc;
    if (connectable && !facts->connectable) {
        *out_match = false;
        return NEARBY_DB_OK;
    }
    char value[NBY_JSON_STRING_MAX];
    rc = nby_json_get_string(root, "service_uuid", value, sizeof(value), &present);
    if (rc != NEARBY_DB_OK) return rc;
    if (present && !nby_text_list_contains_cstr(facts->service_uuids, facts->service_uuid_count, value, true)) {
        *out_match = false;
        return NEARBY_DB_OK;
    }
    rc = nby_json_get_string(root, "service_data_uuid", value, sizeof(value), &present);
    if (rc != NEARBY_DB_OK) return rc;
    if (present && !nby_text_list_contains_cstr(facts->service_data_uuids, facts->service_data_uuid_count, value, true)) {
        *out_match = false;
        return NEARBY_DB_OK;
    }
    uint32_t manufacturer_id = 0;
    rc = nby_json_get_u32(root, "manufacturer_id", &manufacturer_id, &present);
    if (rc != NEARBY_DB_OK) return rc;
    const nearby_db_ble_manufacturer_data_t *manufacturer = NULL;
    if (present) {
        if (manufacturer_id > UINT16_MAX) return NEARBY_DB_ERR_FORMAT;
        manufacturer = nby_find_manufacturer(facts, (uint16_t)manufacturer_id);
        if (!manufacturer) {
            *out_match = false;
            return NEARBY_DB_OK;
        }
    }
    uint8_t required_prefix[NBY_MANUFACTURER_PREFIX_MAX];
    size_t required_prefix_len = 0;
    bool prefix_present = false;
    rc = nby_json_get_u8_array(root, "manufacturer_data_start", required_prefix, sizeof(required_prefix), &required_prefix_len, &prefix_present);
    if (rc != NEARBY_DB_OK) return rc;
    if (prefix_present) {
        if (!manufacturer || manufacturer->data_len < required_prefix_len ||
            (required_prefix_len != 0u && memcmp(manufacturer->data, required_prefix, required_prefix_len) != 0)) {
            *out_match = false;
            return NEARBY_DB_OK;
        }
    }
    rc = nby_json_get_string(root, "local_name", value, sizeof(value), &present);
    if (rc != NEARBY_DB_OK) return rc;
    if (present && !nby_glob_match(value, &facts->local_name, false)) {
        *out_match = false;
        return NEARBY_DB_OK;
    }
    *out_match = true;
    return NEARBY_DB_OK;
}

static nearby_db_result_t nby_match_zeroconf_properties(
    nby_json_slice_t properties,
    const nearby_db_zeroconf_facts_t *facts,
    bool *out_match) {
    nby_json_cursor_t cur = {properties.ptr, properties.ptr + properties.len};
    nby_json_skip_ws(&cur);
    if (cur.p >= cur.end || *cur.p != '{') return NEARBY_DB_ERR_FORMAT;
    ++cur.p;
    nby_json_skip_ws(&cur);
    if (cur.p < cur.end && *cur.p == '}') {
        *out_match = true;
        return NEARBY_DB_OK;
    }
    for (;;) {
        nby_json_slice_t raw_key;
        char key[NBY_JSON_KEY_MAX];
        char pattern[NBY_JSON_STRING_MAX];
        if (!nby_json_read_string_slice(&cur, &raw_key) || !nby_json_decode_string(raw_key, key, sizeof(key), NULL))
            return NEARBY_DB_ERR_FORMAT;
        nby_json_skip_ws(&cur);
        if (cur.p >= cur.end || *cur.p != ':') return NEARBY_DB_ERR_FORMAT;
        ++cur.p;
        nby_json_skip_ws(&cur);
        nby_json_slice_t raw_value;
        if (!nby_json_read_string_slice(&cur, &raw_value) || !nby_json_decode_string(raw_value, pattern, sizeof(pattern), NULL))
            return NEARBY_DB_ERR_FORMAT;
        const nearby_db_text_t *fact = nby_find_kv_value(facts->properties, facts->property_count, key, false);
        if (!fact || !nby_glob_match(pattern, fact, true)) {
            *out_match = false;
            return NEARBY_DB_OK;
        }
        nby_json_skip_ws(&cur);
        if (cur.p >= cur.end) return NEARBY_DB_ERR_FORMAT;
        if (*cur.p == '}') {
            ++cur.p;
            break;
        }
        if (*cur.p != ',') return NEARBY_DB_ERR_FORMAT;
        ++cur.p;
    }
    nby_json_skip_ws(&cur);
    if (cur.p != cur.end) return NEARBY_DB_ERR_FORMAT;
    *out_match = true;
    return NEARBY_DB_OK;
}

static nearby_db_result_t nby_match_zeroconf_value(
    nearby_db_t *db,
    const nearby_db_value_ref_t *ref,
    const nearby_db_zeroconf_facts_t *facts,
    nearby_db_match_workspace_t *workspace,
    bool *out_match,
    char *domain,
    size_t domain_cap) {
    nby_json_slice_t root;
    nearby_db_result_t rc = nby_load_value(db, ref, workspace, &root);
    if (rc != NEARBY_DB_OK) return rc;
    bool is_type = false;
    rc = nby_get_record_type_and_domain(root, "ha_zeroconf_matcher", domain, domain_cap, &is_type);
    if (rc != NEARBY_DB_OK || !is_type) {
        *out_match = false;
        return rc;
    }
    nby_json_slice_t matcher;
    bool present = false;
    rc = nby_json_get_object(root, "matcher", &matcher, &present);
    if (rc != NEARBY_DB_OK) return rc;
    if (!present) return NEARBY_DB_ERR_FORMAT;
    char pattern[NBY_JSON_STRING_MAX];
    rc = nby_json_get_string(matcher, "name", pattern, sizeof(pattern), &present);
    if (rc != NEARBY_DB_OK) return rc;
    if (present && !nby_glob_match(pattern, &facts->name, true)) {
        *out_match = false;
        return NEARBY_DB_OK;
    }
    nby_json_slice_t properties;
    rc = nby_json_get_object(matcher, "properties", &properties, &present);
    if (rc != NEARBY_DB_OK) return rc;
    if (present) return nby_match_zeroconf_properties(properties, facts, out_match);
    *out_match = true;
    return NEARBY_DB_OK;
}

static nearby_db_result_t nby_match_ssdp_object(
    nby_json_slice_t matcher,
    const nearby_db_ssdp_facts_t *facts,
    bool *out_match) {
    nby_json_cursor_t cur = {matcher.ptr, matcher.ptr + matcher.len};
    nby_json_skip_ws(&cur);
    if (cur.p >= cur.end || *cur.p != '{') return NEARBY_DB_ERR_FORMAT;
    ++cur.p;
    nby_json_skip_ws(&cur);
    if (cur.p < cur.end && *cur.p == '}') {
        *out_match = true;
        return NEARBY_DB_OK;
    }
    for (;;) {
        nby_json_slice_t raw_key;
        char key[NBY_JSON_KEY_MAX];
        char expected[NBY_JSON_STRING_MAX];
        if (!nby_json_read_string_slice(&cur, &raw_key) || !nby_json_decode_string(raw_key, key, sizeof(key), NULL))
            return NEARBY_DB_ERR_FORMAT;
        nby_json_skip_ws(&cur);
        if (cur.p >= cur.end || *cur.p != ':') return NEARBY_DB_ERR_FORMAT;
        ++cur.p;
        nby_json_skip_ws(&cur);
        nby_json_slice_t raw_value;
        if (!nby_json_read_string_slice(&cur, &raw_value) || !nby_json_decode_string(raw_value, expected, sizeof(expected), NULL))
            return NEARBY_DB_ERR_FORMAT;
        const nearby_db_text_t *fact = nby_find_kv_value(facts->fields, facts->field_count, key, true);
        if (!fact) {
            *out_match = false;
            return NEARBY_DB_OK;
        }
        if (!(strcmp(expected, "*") == 0 || nby_text_equals_cstr(fact, expected, false))) {
            *out_match = false;
            return NEARBY_DB_OK;
        }
        nby_json_skip_ws(&cur);
        if (cur.p >= cur.end) return NEARBY_DB_ERR_FORMAT;
        if (*cur.p == '}') {
            ++cur.p;
            break;
        }
        if (*cur.p != ',') return NEARBY_DB_ERR_FORMAT;
        ++cur.p;
    }
    nby_json_skip_ws(&cur);
    if (cur.p != cur.end) return NEARBY_DB_ERR_FORMAT;
    *out_match = true;
    return NEARBY_DB_OK;
}

static nearby_db_result_t nby_match_ssdp_value(
    nearby_db_t *db,
    const nearby_db_value_ref_t *ref,
    const nearby_db_ssdp_facts_t *facts,
    nearby_db_match_workspace_t *workspace,
    bool *out_match,
    char *domain,
    size_t domain_cap) {
    nby_json_slice_t root;
    nearby_db_result_t rc = nby_load_value(db, ref, workspace, &root);
    if (rc != NEARBY_DB_OK) return rc;
    bool is_type = false;
    rc = nby_get_record_type_and_domain(root, "ha_ssdp_matcher", domain, domain_cap, &is_type);
    if (rc != NEARBY_DB_OK || !is_type) {
        *out_match = false;
        return rc;
    }
    nby_json_slice_t matcher;
    bool present = false;
    rc = nby_json_get_object(root, "matcher", &matcher, &present);
    if (rc != NEARBY_DB_OK) return rc;
    if (!present) return NEARBY_DB_ERR_FORMAT;
    return nby_match_ssdp_object(matcher, facts, out_match);
}

static nearby_db_result_t nby_match_dhcp_value(
    nearby_db_t *db,
    const nearby_db_value_ref_t *ref,
    const nearby_db_dhcp_facts_t *facts,
    nearby_db_match_workspace_t *workspace,
    bool *out_match,
    char *domain,
    size_t domain_cap) {
    nby_json_slice_t root;
    nearby_db_result_t rc = nby_load_value(db, ref, workspace, &root);
    if (rc != NEARBY_DB_OK) return rc;
    bool is_type = false;
    rc = nby_get_record_type_and_domain(root, "ha_dhcp_matcher", domain, domain_cap, &is_type);
    if (rc != NEARBY_DB_OK || !is_type) {
        *out_match = false;
        return rc;
    }
    nby_json_slice_t matcher;
    bool present = false;
    rc = nby_json_get_object(root, "matcher", &matcher, &present);
    if (rc != NEARBY_DB_OK) return rc;
    if (!present) return NEARBY_DB_ERR_FORMAT;
    char pattern[NBY_JSON_STRING_MAX];
    rc = nby_json_get_string(matcher, "hostname", pattern, sizeof(pattern), &present);
    if (rc != NEARBY_DB_OK) return rc;
    if (present && !nby_glob_match(pattern, &facts->hostname, true)) {
        *out_match = false;
        return NEARBY_DB_OK;
    }
    rc = nby_json_get_string(matcher, "macaddress", pattern, sizeof(pattern), &present);
    if (rc != NEARBY_DB_OK) return rc;
    if (present && !nby_glob_match(pattern, &facts->macaddress, true)) {
        *out_match = false;
        return NEARBY_DB_OK;
    }
    bool registered = false;
    rc = nby_json_get_bool(matcher, "registered_devices", &registered, &present);
    if (rc != NEARBY_DB_OK) return rc;
    if (present && registered && !facts->registered_device) {
        *out_match = false;
        return NEARBY_DB_OK;
    }
    if (present && !registered) return NEARBY_DB_ERR_UNSUPPORTED;
    *out_match = true;
    return NEARBY_DB_OK;
}

static nearby_db_result_t nby_accumulate_match(
    nby_match_accumulator_t *acc,
    const nearby_db_value_ref_t *ref,
    const char *domain,
    bool already_ambiguous) {
    ++acc->out->matched_record_count;
    if (already_ambiguous) {
        acc->out->status = NEARBY_DB_MATCH_AMBIGUOUS;
        memset(&acc->out->value, 0, sizeof(acc->out->value));
        return NEARBY_DB_OK;
    }
    if (acc->out->status == NEARBY_DB_MATCH_AMBIGUOUS) return NEARBY_DB_OK;
    size_t domain_len = strlen(domain);
    if (domain_len + 1u > sizeof(acc->first_domain)) return NEARBY_DB_ERR_RANGE;
    if (!acc->have_first_domain) {
        memcpy(acc->first_domain, domain, domain_len + 1u);
        acc->have_first_domain = true;
        acc->out->status = NEARBY_DB_MATCHED;
        acc->out->value = *ref;
        return NEARBY_DB_OK;
    }
    if (strcmp(acc->first_domain, domain) != 0) {
        acc->out->status = NEARBY_DB_MATCH_AMBIGUOUS;
        memset(&acc->out->value, 0, sizeof(acc->out->value));
    }
    return NEARBY_DB_OK;
}

static nearby_db_result_t nby_match_candidate(
    nearby_db_t *db,
    const nearby_db_value_ref_t *ref,
    nby_match_kind_t kind,
    const void *facts,
    nearby_db_match_workspace_t *workspace,
    nby_match_accumulator_t *acc) {
    if ((ref->flags & NEARBY_DB_VALUE_FLAG_AMBIGUOUS) != 0u)
        return nby_accumulate_match(acc, ref, "", true);
    bool matched = false;
    char domain[NBY_DOMAIN_MAX];
    nearby_db_result_t rc;
    switch (kind) {
        case NBY_MATCH_KIND_BLE:
            rc = nby_match_ble_value(db, ref, (const nearby_db_ble_facts_t *)facts, workspace, &matched, domain, sizeof(domain));
            break;
        case NBY_MATCH_KIND_ZEROCONF:
            rc = nby_match_zeroconf_value(db, ref, (const nearby_db_zeroconf_facts_t *)facts, workspace, &matched, domain, sizeof(domain));
            break;
        case NBY_MATCH_KIND_SSDP:
            rc = nby_match_ssdp_value(db, ref, (const nearby_db_ssdp_facts_t *)facts, workspace, &matched, domain, sizeof(domain));
            break;
        case NBY_MATCH_KIND_DHCP:
            rc = nby_match_dhcp_value(db, ref, (const nearby_db_dhcp_facts_t *)facts, workspace, &matched, domain, sizeof(domain));
            break;
        default:
            return NEARBY_DB_ERR_ARG;
    }
    if (rc != NEARBY_DB_OK || !matched) return rc;
    return nby_accumulate_match(acc, ref, domain, false);
}

static nearby_db_result_t nby_scan_prefix(
    nearby_db_t *db,
    const char *prefix,
    size_t prefix_len,
    nby_match_kind_t kind,
    const void *facts,
    nearby_db_match_workspace_t *workspace,
    nby_match_accumulator_t *acc) {
    if (prefix_len == 0u || prefix_len > NBY_PREFIX_MAX) return NEARBY_DB_ERR_RANGE;
    uint32_t index = 0;
    nearby_db_result_t rc = nby_lower_bound_prefix(db, prefix, prefix_len, &index);
    if (rc != NEARBY_DB_OK) return rc;
    while (index < db->record_count) {
        uint32_t key_off = 0;
        uint16_t key_len = 0;
        nearby_db_value_ref_t ref;
        rc = nby_read_entry(db, index, &key_off, &key_len, &ref);
        if (rc != NEARBY_DB_OK) return rc;
        bool has_prefix = false;
        rc = nby_stored_key_has_prefix(db, key_off, key_len, prefix, prefix_len, &has_prefix);
        if (rc != NEARBY_DB_OK) return rc;
        if (!has_prefix) break;
        rc = nby_match_candidate(db, &ref, kind, facts, workspace, acc);
        if (rc != NEARBY_DB_OK) return rc;
        ++index;
    }
    return NEARBY_DB_OK;
}

static nearby_db_result_t nby_prefix_append(
    char *buf,
    size_t cap,
    size_t *used,
    const char *data,
    size_t len,
    int case_mode) {
    if ((len != 0u && !data) || *used > cap || len > cap - *used)
        return len != 0u && !data ? NEARBY_DB_ERR_ARG : NEARBY_DB_ERR_RANGE;
    for (size_t i = 0; i < len; ++i) {
        unsigned char ch = (unsigned char)data[i];
        if (case_mode < 0) ch = nby_fold_ascii(ch);
        else if (case_mode > 0 && ch >= 'a' && ch <= 'z') ch = (unsigned char)(ch - 'a' + 'A');
        buf[(*used)++] = (char)ch;
    }
    return NEARBY_DB_OK;
}

static nearby_db_result_t nby_scan_text_prefix(
    nearby_db_t *db,
    const char *base,
    const nearby_db_text_t *value,
    size_t value_limit,
    const char *suffix,
    int case_mode,
    nby_match_kind_t kind,
    const void *facts,
    nearby_db_match_workspace_t *workspace,
    nby_match_accumulator_t *acc) {
    char prefix[NBY_PREFIX_MAX];
    size_t used = 0;
    nearby_db_result_t rc = nby_prefix_append(prefix, sizeof(prefix), &used, base, strlen(base), 0);
    if (rc != NEARBY_DB_OK) return rc;
    size_t take = value->len;
    if (take > value_limit) take = value_limit;
    rc = nby_prefix_append(prefix, sizeof(prefix), &used, value->ptr, take, case_mode);
    if (rc != NEARBY_DB_OK) return rc;
    rc = nby_prefix_append(prefix, sizeof(prefix), &used, suffix, strlen(suffix), 0);
    if (rc != NEARBY_DB_OK) return rc;
    return nby_scan_prefix(db, prefix, used, kind, facts, workspace, acc);
}

static nearby_db_result_t nby_validate_common(
    nearby_db_t *db,
    nearby_db_match_workspace_t *workspace,
    nearby_db_match_result_t *out_result) {
    if (!db || !db->fp || !workspace || !workspace->data || workspace->size == 0u || !out_result)
        return NEARBY_DB_ERR_ARG;
    memset(out_result, 0, sizeof(*out_result));
    out_result->status = NEARBY_DB_MATCH_NOT_FOUND;
    return NEARBY_DB_OK;
}

static bool nby_text_seen_before(const nearby_db_text_t *items, size_t index, bool fold_ascii) {
    for (size_t i = 0; i < index; ++i) {
        if (nby_text_equals_text(&items[i], &items[index], fold_ascii)) return true;
    }
    return false;
}

nearby_db_result_t nearby_db_match_ble(
    nearby_db_t *db,
    const nearby_db_ble_facts_t *facts,
    nearby_db_match_workspace_t *workspace,
    nearby_db_match_result_t *out_result) {
    if (!facts) return NEARBY_DB_ERR_ARG;
    nearby_db_result_t rc = nby_validate_common(db, workspace, out_result);
    if (rc != NEARBY_DB_OK) return rc;
    if (facts->service_uuid_count > NEARBY_DB_MATCH_MAX_FACT_VALUES ||
        facts->service_data_uuid_count > NEARBY_DB_MATCH_MAX_FACT_VALUES ||
        facts->manufacturer_data_count > NEARBY_DB_MATCH_MAX_FACT_VALUES ||
        (facts->service_uuid_count && !facts->service_uuids) ||
        (facts->service_data_uuid_count && !facts->service_data_uuids) ||
        (facts->manufacturer_data_count && !facts->manufacturer_data) ||
        (!facts->local_name.ptr && facts->local_name.len != 0u)) return NEARBY_DB_ERR_ARG;
    for (size_t i = 0; i < facts->manufacturer_data_count; ++i) {
        if (facts->manufacturer_data[i].data_len != 0u && !facts->manufacturer_data[i].data)
            return NEARBY_DB_ERR_ARG;
    }
    nby_match_accumulator_t acc = {out_result, {0}, false};
    for (size_t i = 0; i < facts->manufacturer_data_count; ++i) {
        bool duplicate = false;
        for (size_t j = 0; j < i; ++j) {
            if (facts->manufacturer_data[j].manufacturer_id == facts->manufacturer_data[i].manufacturer_id) {
                duplicate = true;
                break;
            }
        }
        if (duplicate) continue;
        char prefix[32];
        int written = snprintf(prefix, sizeof(prefix), "ble:%u|", (unsigned)facts->manufacturer_data[i].manufacturer_id);
        if (written <= 0 || (size_t)written >= sizeof(prefix)) return NEARBY_DB_ERR_RANGE;
        rc = nby_scan_prefix(db, prefix, (size_t)written, NBY_MATCH_KIND_BLE, facts, workspace, &acc);
        if (rc != NEARBY_DB_OK) return rc;
    }
    for (size_t i = 0; i < facts->service_uuid_count; ++i) {
        if (nby_text_seen_before(facts->service_uuids, i, true)) continue;
        rc = nby_scan_text_prefix(db, "ble:|", &facts->service_uuids[i], facts->service_uuids[i].len, "|", -1, NBY_MATCH_KIND_BLE, facts, workspace, &acc);
        if (rc != NEARBY_DB_OK) return rc;
    }
    for (size_t i = 0; i < facts->service_data_uuid_count; ++i) {
        if (nby_text_seen_before(facts->service_data_uuids, i, true)) continue;
        rc = nby_scan_text_prefix(db, "ble:||", &facts->service_data_uuids[i], facts->service_data_uuids[i].len, "|", -1, NBY_MATCH_KIND_BLE, facts, workspace, &acc);
        if (rc != NEARBY_DB_OK) return rc;
    }
    if (facts->local_name.len != 0u) {
        rc = nby_scan_text_prefix(db, "ble:|||", &facts->local_name, 3u, "", 0, NBY_MATCH_KIND_BLE, facts, workspace, &acc);
        if (rc != NEARBY_DB_OK) return rc;
    }
    return NEARBY_DB_OK;
}

nearby_db_result_t nearby_db_match_zeroconf(
    nearby_db_t *db,
    const nearby_db_zeroconf_facts_t *facts,
    nearby_db_match_workspace_t *workspace,
    nearby_db_match_result_t *out_result) {
    if (!facts) return NEARBY_DB_ERR_ARG;
    nearby_db_result_t rc = nby_validate_common(db, workspace, out_result);
    if (rc != NEARBY_DB_OK) return rc;
    if (!facts->service_type.ptr || facts->service_type.len == 0u ||
        (!facts->name.ptr && facts->name.len != 0u) ||
        facts->property_count > NEARBY_DB_MATCH_MAX_FACT_VALUES ||
        (facts->property_count && !facts->properties)) return NEARBY_DB_ERR_ARG;
    nby_match_accumulator_t acc = {out_result, {0}, false};
    return nby_scan_text_prefix(db, "lan:zeroconf:", &facts->service_type, facts->service_type.len, "\x1f", 0, NBY_MATCH_KIND_ZEROCONF, facts, workspace, &acc);
}

nearby_db_result_t nearby_db_match_ssdp(
    nearby_db_t *db,
    const nearby_db_ssdp_facts_t *facts,
    nearby_db_match_workspace_t *workspace,
    nearby_db_match_result_t *out_result) {
    if (!facts) return NEARBY_DB_ERR_ARG;
    nearby_db_result_t rc = nby_validate_common(db, workspace, out_result);
    if (rc != NEARBY_DB_OK) return rc;
    if (facts->field_count > NEARBY_DB_MATCH_MAX_FACT_VALUES || (facts->field_count && !facts->fields))
        return NEARBY_DB_ERR_ARG;
    nby_match_accumulator_t acc = {out_result, {0}, false};
    static const char *const anchors[] = {"st", "deviceType", "manufacturer", "manufacturerURL"};
    for (size_t i = 0; i < sizeof(anchors) / sizeof(anchors[0]); ++i) {
        const nearby_db_text_t *value = nby_find_kv_value(facts->fields, facts->field_count, anchors[i], true);
        if (!value) continue;
        char base[64];
        int written = snprintf(base, sizeof(base), "lan:ssdp:%s:", anchors[i]);
        if (written <= 0 || (size_t)written >= sizeof(base)) return NEARBY_DB_ERR_RANGE;
        rc = nby_scan_text_prefix(db, base, value, value->len, "\x1f", 0, NBY_MATCH_KIND_SSDP, facts, workspace, &acc);
        if (rc != NEARBY_DB_OK) return rc;
    }
    return nby_scan_prefix(db, "lan:ssdp:any:*\x1f", strlen("lan:ssdp:any:*\x1f"), NBY_MATCH_KIND_SSDP, facts, workspace, &acc);
}

nearby_db_result_t nearby_db_match_dhcp(
    nearby_db_t *db,
    const nearby_db_dhcp_facts_t *facts,
    nearby_db_match_workspace_t *workspace,
    nearby_db_match_result_t *out_result) {
    if (!facts) return NEARBY_DB_ERR_ARG;
    nearby_db_result_t rc = nby_validate_common(db, workspace, out_result);
    if (rc != NEARBY_DB_OK) return rc;
    if ((!facts->hostname.ptr && facts->hostname.len != 0u) || (!facts->macaddress.ptr && facts->macaddress.len != 0u))
        return NEARBY_DB_ERR_ARG;
    nby_match_accumulator_t acc = {out_result, {0}, false};
    if (facts->macaddress.len >= 6u) {
        rc = nby_scan_text_prefix(db, "lan:dhcp:mac:", &facts->macaddress, 6u, "", 1, NBY_MATCH_KIND_DHCP, facts, workspace, &acc);
        if (rc != NEARBY_DB_OK) return rc;
        rc = nby_scan_prefix(db, "lan:dhcp:mac:*", strlen("lan:dhcp:mac:*"), NBY_MATCH_KIND_DHCP, facts, workspace, &acc);
        if (rc != NEARBY_DB_OK) return rc;
    }
    if (facts->hostname.len != 0u) {
        rc = nby_scan_text_prefix(db, "lan:dhcp:hostname:", &facts->hostname, 1u, "", -1, NBY_MATCH_KIND_DHCP, facts, workspace, &acc);
        if (rc != NEARBY_DB_OK) return rc;
        unsigned char first = nby_fold_ascii((unsigned char)facts->hostname.ptr[0]);
        if (first != (unsigned char)'*') {
            rc = nby_scan_prefix(db, "lan:dhcp:hostname:*", strlen("lan:dhcp:hostname:*"), NBY_MATCH_KIND_DHCP, facts, workspace, &acc);
            if (rc != NEARBY_DB_OK) return rc;
        }
        if (first != (unsigned char)'?') {
            rc = nby_scan_prefix(db, "lan:dhcp:hostname:?", strlen("lan:dhcp:hostname:?"), NBY_MATCH_KIND_DHCP, facts, workspace, &acc);
            if (rc != NEARBY_DB_OK) return rc;
        }
        if (first != (unsigned char)'[') {
            rc = nby_scan_prefix(db, "lan:dhcp:hostname:[", strlen("lan:dhcp:hostname:["), NBY_MATCH_KIND_DHCP, facts, workspace, &acc);
            if (rc != NEARBY_DB_OK) return rc;
        }
    }
    if (facts->registered_device) {
        rc = nby_scan_prefix(db, "lan:dhcp:registered:1\x1f", strlen("lan:dhcp:registered:1\x1f"), NBY_MATCH_KIND_DHCP, facts, workspace, &acc);
        if (rc != NEARBY_DB_OK) return rc;
    }
    return nby_scan_prefix(db, "lan:dhcp:any:*\x1f", strlen("lan:dhcp:any:*\x1f"), NBY_MATCH_KIND_DHCP, facts, workspace, &acc);
}
