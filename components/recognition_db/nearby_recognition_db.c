#include "nearby_recognition_db.h"

#include <stdbool.h>
#include <limits.h>
#include <string.h>

#define NBY_HEADER_SIZE 128u
#define NBY_FORMAT_MAJOR 1u
#define NBY_SCHEMA_VERSION 1u
#define NBY_READER_ABI 1u
#define NBY_FLAT_ENTRY_SIZE 16u
#define NBY_COMPARE_CHUNK 64u

static const uint8_t k_db_magic[8] = {'N','B','Y','D','B',0,'\r','\n'};
static const uint8_t k_flat_magic[8] = {'N','B','Y','F','L','T','1',0};

static uint16_t rd16(const uint8_t *p) {
    return (uint16_t)p[0] | ((uint16_t)p[1] << 8);
}

static uint32_t rd32(const uint8_t *p) {
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

static uint64_t rd64(const uint8_t *p) {
    return (uint64_t)rd32(p) | ((uint64_t)rd32(p + 4) << 32);
}

static uint32_t crc32_update(uint32_t crc, const uint8_t *data, size_t len) {
    crc = ~crc;
    for (size_t i = 0; i < len; ++i) {
        crc ^= data[i];
        for (int bit = 0; bit < 8; ++bit) {
            uint32_t mask = (uint32_t)-(int32_t)(crc & 1u);
            crc = (crc >> 1) ^ (0xEDB88320u & mask);
        }
    }
    return ~crc;
}

static bool seek_abs(FILE *fp, uint64_t offset) {
    if (offset > (uint64_t)LONG_MAX) {
        return false;
    }
    return fseek(fp, (long)offset, SEEK_SET) == 0;
}

static bool file_size(FILE *fp, uint64_t *out) {
    if (fseek(fp, 0, SEEK_END) != 0) return false;
    long pos = ftell(fp);
    if (pos < 0 || fseek(fp, 0, SEEK_SET) != 0) return false;
    *out = (uint64_t)pos;
    return true;
}

static nearby_db_result_t read_exact(FILE *fp, uint64_t offset, void *buf, size_t len) {
    if (!seek_abs(fp, offset)) return NEARBY_DB_ERR_IO;
    return fread(buf, 1, len, fp) == len ? NEARBY_DB_OK : NEARBY_DB_ERR_IO;
}

nearby_db_result_t nearby_db_open(nearby_db_t *db, const char *path) {
    if (!db || !path) return NEARBY_DB_ERR_ARG;
    memset(db, 0, sizeof(*db));
    FILE *fp = fopen(path, "rb");
    if (!fp) return NEARBY_DB_ERR_IO;

    uint8_t h[NBY_HEADER_SIZE];
    if (fread(h, 1, sizeof(h), fp) != sizeof(h)) {
        fclose(fp);
        return NEARBY_DB_ERR_IO;
    }
    if (memcmp(h, k_db_magic, sizeof(k_db_magic)) != 0 || rd16(h + 12) != NBY_HEADER_SIZE) {
        fclose(fp);
        return NEARBY_DB_ERR_FORMAT;
    }
    if (rd16(h + 8) != NBY_FORMAT_MAJOR || rd32(h + 16) != NBY_SCHEMA_VERSION || rd32(h + 24) > NBY_READER_ABI) {
        fclose(fp);
        return NEARBY_DB_ERR_UNSUPPORTED;
    }
    uint32_t expected_header_crc = rd32(h + 76);
    memset(h + 76, 0, 4);
    if (crc32_update(0, h, sizeof(h)) != expected_header_crc) {
        fclose(fp);
        return NEARBY_DB_ERR_FORMAT;
    }

    uint64_t actual_size = 0;
    if (!file_size(fp, &actual_size)) {
        fclose(fp);
        return NEARBY_DB_ERR_IO;
    }
    uint64_t declared_size = rd64(h + 32);
    uint64_t manifest_offset = rd64(h + 40);
    uint64_t manifest_size = rd64(h + 48);
    uint64_t payload_offset = rd64(h + 56);
    uint64_t payload_size = rd64(h + 64);
    if (declared_size != actual_size || manifest_offset != NBY_HEADER_SIZE ||
        manifest_offset + manifest_size != payload_offset || payload_offset > declared_size ||
        payload_size > declared_size - payload_offset || payload_offset + payload_size != declared_size) {
        fclose(fp);
        return NEARBY_DB_ERR_FORMAT;
    }

    uint8_t fh[32];
    if (read_exact(fp, payload_offset, fh, sizeof(fh)) != NEARBY_DB_OK) {
        fclose(fp);
        return NEARBY_DB_ERR_IO;
    }
    if (memcmp(fh, k_flat_magic, sizeof(k_flat_magic)) != 0 || rd32(fh + 12) != NBY_FLAT_ENTRY_SIZE) {
        fclose(fp);
        return NEARBY_DB_ERR_UNSUPPORTED;
    }
    uint32_t count = rd32(fh + 8);
    uint64_t index_rel = rd64(fh + 16);
    uint64_t blob_rel = rd64(fh + 24);
    uint64_t index_bytes = (uint64_t)count * NBY_FLAT_ENTRY_SIZE;
    if (index_rel < sizeof(fh) || index_rel > payload_size || index_bytes > payload_size - index_rel ||
        blob_rel < index_rel + index_bytes || blob_rel > payload_size) {
        fclose(fp);
        return NEARBY_DB_ERR_FORMAT;
    }

    db->fp = fp;
    db->db_version = rd32(h + 20);
    db->schema_version = rd32(h + 16);
    db->file_size = declared_size;
    db->payload_offset = payload_offset;
    db->payload_size = payload_size;
    db->record_count = count;
    db->index_offset = payload_offset + index_rel;
    db->blob_offset = payload_offset + blob_rel;
    return NEARBY_DB_OK;
}

void nearby_db_close(nearby_db_t *db) {
    if (!db) return;
    if (db->fp) fclose(db->fp);
    memset(db, 0, sizeof(*db));
}

static nearby_db_result_t read_entry(nearby_db_t *db, uint32_t index, uint32_t *key_off,
                                     uint16_t *key_len, uint32_t *value_off, uint32_t *value_len) {
    uint8_t e[NBY_FLAT_ENTRY_SIZE];
    if (index >= db->record_count) return NEARBY_DB_ERR_RANGE;
    nearby_db_result_t rc = read_exact(db->fp, db->index_offset + (uint64_t)index * NBY_FLAT_ENTRY_SIZE, e, sizeof(e));
    if (rc != NEARBY_DB_OK) return rc;
    *key_off = rd32(e);
    *key_len = rd16(e + 4);
    *value_off = rd32(e + 8);
    *value_len = rd32(e + 12);
    return NEARBY_DB_OK;
}

static nearby_db_result_t compare_stored_key(nearby_db_t *db, uint32_t key_off, uint16_t stored_len,
                                             const uint8_t *wanted, size_t wanted_len, int *out_cmp) {
    uint8_t buf[NBY_COMPARE_CHUNK];
    size_t common = stored_len < wanted_len ? stored_len : wanted_len;
    size_t done = 0;
    while (done < common) {
        size_t take = common - done;
        if (take > sizeof(buf)) take = sizeof(buf);
        nearby_db_result_t rc = read_exact(db->fp, db->blob_offset + key_off + done, buf, take);
        if (rc != NEARBY_DB_OK) return rc;
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

nearby_db_result_t nearby_db_find(nearby_db_t *db, const char *key, size_t key_len, nearby_db_value_ref_t *out_ref) {
    if (!db || !db->fp || !key || !out_ref) return NEARBY_DB_ERR_ARG;
    uint32_t lo = 0, hi = db->record_count;
    while (lo < hi) {
        uint32_t mid = lo + (hi - lo) / 2;
        uint32_t ko, vo, vl;
        uint16_t kl;
        nearby_db_result_t rc = read_entry(db, mid, &ko, &kl, &vo, &vl);
        if (rc != NEARBY_DB_OK) return rc;
        int cmp = 0;
        rc = compare_stored_key(db, ko, kl, (const uint8_t *)key, key_len, &cmp);
        if (rc != NEARBY_DB_OK) return rc;
        if (cmp < 0) lo = mid + 1;
        else hi = mid;
    }
    if (lo >= db->record_count) return NEARBY_DB_ERR_NOT_FOUND;
    uint32_t ko, vo, vl;
    uint16_t kl;
    nearby_db_result_t rc = read_entry(db, lo, &ko, &kl, &vo, &vl);
    if (rc != NEARBY_DB_OK) return rc;
    int cmp = 0;
    rc = compare_stored_key(db, ko, kl, (const uint8_t *)key, key_len, &cmp);
    if (rc != NEARBY_DB_OK) return rc;
    if (cmp != 0) return NEARBY_DB_ERR_NOT_FOUND;
    if ((uint64_t)vo + vl > db->payload_size || db->blob_offset + (uint64_t)vo + vl > db->file_size) {
        return NEARBY_DB_ERR_FORMAT;
    }
    out_ref->value_offset = db->blob_offset + vo;
    out_ref->value_size = vl;
    return NEARBY_DB_OK;
}

nearby_db_result_t nearby_db_read_value(nearby_db_t *db, const nearby_db_value_ref_t *ref,
                                        uint32_t value_offset, void *buf, size_t buf_size, size_t *out_read) {
    if (!db || !db->fp || !ref || (!buf && buf_size) || !out_read) return NEARBY_DB_ERR_ARG;
    if (value_offset > ref->value_size) return NEARBY_DB_ERR_RANGE;
    size_t remaining = ref->value_size - value_offset;
    size_t take = buf_size < remaining ? buf_size : remaining;
    if (ref->value_offset > db->file_size || take > db->file_size - ref->value_offset ||
        value_offset > db->file_size - ref->value_offset || take > db->file_size - ref->value_offset - value_offset) {
        return NEARBY_DB_ERR_RANGE;
    }
    if (take && read_exact(db->fp, ref->value_offset + value_offset, buf, take) != NEARBY_DB_OK) {
        return NEARBY_DB_ERR_IO;
    }
    *out_read = take;
    return NEARBY_DB_OK;
}
