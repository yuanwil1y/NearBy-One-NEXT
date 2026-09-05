set(agent_d_srcs "main.c")
set(agent_d_requires nearby_board scan_session native_handoff radio_runtime esp_system esp_timer)

if(CONFIG_NEARBY_AGENT_D_LCD_VISUAL_BENCH OR
   CONFIG_NEARBY_AGENT_D_TOUCH_BENCH OR
   CONFIG_NEARBY_AGENT_D_SD_IO_BENCH OR
   CONFIG_NEARBY_AGENT_D_SD_DESTRUCTIVE_BENCH OR
   CONFIG_NEARBY_AGENT_D_WEB_BENCH OR
   CONFIG_NEARBY_AGENT_D_SPI_CONTENTION_BENCH OR
   CONFIG_NEARBY_AGENT_D_STRESS_BENCH)
    list(APPEND agent_d_srcs "agent_d_bench.c")
    list(APPEND agent_d_requires web_mgmt)
endif()

idf_component_register(
    SRCS ${agent_d_srcs}
    INCLUDE_DIRS "."
    REQUIRES ${agent_d_requires}
)
