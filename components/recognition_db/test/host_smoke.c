#include "nearby_recognition_db.h"
#include <stdio.h>
#include <string.h>

int main(int argc, char **argv) {
    if (argc != 3) return 2;
    nearby_db_t db;
    if (nearby_db_open(&db, argv[1]) != NEARBY_DB_OK) return 3;
    nearby_db_value_ref_t ref;
    nearby_db_result_t find_rc = nearby_db_find(&db, argv[2], strlen(argv[2]), &ref);
    if (find_rc != NEARBY_DB_OK && find_rc != NEARBY_DB_AMBIGUOUS) return 4;
    if (find_rc == NEARBY_DB_AMBIGUOUS) puts("AMBIGUOUS");
    char buf[4096];
    size_t n = 0;
    if (nearby_db_read_value(&db, &ref, 0, buf, sizeof(buf) - 1, &n) != NEARBY_DB_OK) return 5;
    buf[n] = 0;
    puts(buf);
    nearby_db_close(&db);
    return 0;
}
