#!/usr/bin/env sh
set -eu

CC=${CC:-cc}
CFLAGS="-std=c11 -Wall -Wextra -Werror -Icomponents/ha_core/include"
CORE="components/ha_core/ha_core.c"

$CC $CFLAGS -Itests $CORE tests/ha_test_fixture.c tests/ha_core_smoke.c -o /tmp/ha_core_smoke
/tmp/ha_core_smoke

$CC $CFLAGS $CORE tests/ha_core_footprint.c -o /tmp/ha_core_footprint
/tmp/ha_core_footprint

$CC $CFLAGS $CORE tests/ha_core_semantic_path.c -o /tmp/ha_core_semantic_path
/tmp/ha_core_semantic_path

$CC $CFLAGS \
  -DHA_MAX_DEVICES=4 -DHA_MAX_ENTITIES=3 -DHA_MAX_STATES=2 \
  -DHA_MAX_ATTRIBUTES=2 -DHA_MAX_IDENTIFIERS=1 -DHA_MAX_CONNECTIONS=1 \
  $CORE tests/ha_core_boundaries.c -o /tmp/ha_core_boundaries
/tmp/ha_core_boundaries
