#!/usr/bin/env bash
set -euo pipefail

cc="${CC:-gcc}"

"$cc" -std=c11 -Wall -Wextra -Werror \
  -Itests/host/fixtures/d_frozen/include \
  -Ifirmware/components/wireshark/include \
  -Ifirmware/components/kismet/include \
  -Ifirmware/components/home_assistant/include \
  -Ifirmware/components/discovery_parser/include \
  -Icomponents/recognition_db/include \
  -Icomponents/ha_core/include \
  tests/host/test_public_api_skeleton.c \
  -o /tmp/nearby_phase1_public_api

/tmp/nearby_phase1_public_api
