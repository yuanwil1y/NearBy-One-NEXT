#!/usr/bin/env sh
set -eu

CC=${CC:-cc}
CFLAGS="-std=c11 -Wall -Wextra -Werror -Icomponents/ha_core/include"
CORE="components/ha_core/ha_core.c"
HEADER="components/ha_core/include/ha_core.h"

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

$CC $CFLAGS $CORE tests/ha_core_owner_task.c -o /tmp/ha_core_owner_task
/tmp/ha_core_owner_task

# ha_core stays lock-free by ownership, not by internal synchronization.
# Fail if common C/POSIX/FreeRTOS lock or atomic primitives enter production core.
if grep -Eq 'stdatomic\.h|atomic_[[:alnum:]_]+|pthread_|freertos/semphr\.h|xSemaphore|SemaphoreHandle_t|portENTER_CRITICAL|portEXIT_CRITICAL|portMUX_TYPE' "$CORE" "$HEADER"; then
  echo "ha_core ownership violation: synchronization primitive found in production core" >&2
  exit 1
fi

echo "ha_core_owner_contract: ok"
