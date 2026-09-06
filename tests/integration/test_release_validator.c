#include <assert.h>
#include <stdio.h>
#include <stdlib.h>

#include "nearby_recognition_db.h"

static void make_corrupt_copy(const char *source, const char *dest)
{
    FILE *in = fopen(source, "rb");
    FILE *out = fopen(dest, "wb+");
    assert(in != NULL && out != NULL);
    unsigned char buf[4096];
    size_t n;
    while ((n = fread(buf, 1, sizeof(buf), in)) != 0) {
        assert(fwrite(buf, 1, n, out) == n);
    }
    assert(fclose(in) == 0);
    assert(fseek(out, -1L, SEEK_END) == 0);
    int value = fgetc(out);
    assert(value != EOF);
    assert(fseek(out, -1L, SEEK_END) == 0);
    assert(fputc(value ^ 0x01, out) != EOF);
    assert(fclose(out) == 0);
}

int main(int argc, char **argv)
{
    assert(argc == 2);
    nearby_db_file_info_t info;
    assert(nearby_db_validate_release_file(argv[1], &info) == NEARBY_DB_OK);
    assert(info.db_version > 0);
    assert(info.schema_version == 1);
    assert(info.source_count > 0);
    assert(info.file_size > 128);

    const char *corrupt = "/tmp/nearby-validator-corrupt.nbdb";
    make_corrupt_copy(argv[1], corrupt);
    assert(nearby_db_validate_release_file(corrupt, NULL) != NEARBY_DB_OK);
    assert(remove(corrupt) == 0);

    printf("release validator integration: ok (db v%u)\n", info.db_version);
    return 0;
}
