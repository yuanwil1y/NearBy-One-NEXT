cmake_minimum_required(VERSION 3.16)

# Frozen Agent A/C/E components live at repository-root/components while the
# ESP-IDF application itself is rooted in firmware/. Keep one component graph.
set(EXTRA_COMPONENT_DIRS "${CMAKE_CURRENT_LIST_DIR}/../components")

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(nearby_one_next)
