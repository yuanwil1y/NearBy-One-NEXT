#pragma once

#include "esp_err.h"
#include "nearby_board.h"

/** Run only the BENCH_REQUIRED cases enabled in Kconfig. */
esp_err_t agent_d_bench_run(const nearby_board_lcd_handles_t *lcd);
