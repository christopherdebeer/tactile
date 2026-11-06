cmake_minimum_required(VERSION 3.16)
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS
             "${CMAKE_CURRENT_LIST_DIR}/version.txt")
file(READ "${CMAKE_CURRENT_LIST_DIR}/version.txt" PROJECT_VER)
string(STRIP "${PROJECT_VER}" PROJECT_VER)
include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(tactile)

# M5GFX commit 28d4794 uses esp_psram.h during ESP32-S3 board detection but
# does not yet declare esp_psram in its ESP-IDF 5.x component requirements.
idf_component_get_property(m5gfx_lib m5gfx COMPONENT_LIB)
idf_component_get_property(esp_psram_lib esp_psram COMPONENT_LIB)
target_link_libraries(${m5gfx_lib} PRIVATE ${esp_psram_lib})
