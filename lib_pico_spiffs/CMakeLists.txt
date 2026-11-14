# pico_spiffs - SPIFFS flash file system for Raspberry Pi Pico SDK projects.
#
# In your project's CMakeLists.txt:
#
#   set(PICO_SPIFFS_FLASH ONBOARD)          # or EXTERNAL (Winbond W25Q on SPI)
#   add_subdirectory(path/to/lib_pico_spiffs)
#   target_link_libraries(my_app pico_spiffs)
#
# For EXTERNAL, add lib_winbond_w25q to the project first (add_subdirectory),
# so its target exists.

set(PICO_SPIFFS_FLASH ONBOARD CACHE STRING "Flash holding the volume: ONBOARD or EXTERNAL")
set_property(CACHE PICO_SPIFFS_FLASH PROPERTY STRINGS ONBOARD EXTERNAL)

add_library(pico_spiffs INTERFACE)

target_sources(pico_spiffs INTERFACE
    # SPIFFS
    ${CMAKE_CURRENT_LIST_DIR}/spiffs/spiffs_nucleus.c
    ${CMAKE_CURRENT_LIST_DIR}/spiffs/spiffs_gc.c
    ${CMAKE_CURRENT_LIST_DIR}/spiffs/spiffs_hydrogen.c
    ${CMAKE_CURRENT_LIST_DIR}/spiffs/spiffs_cache.c
    ${CMAKE_CURRENT_LIST_DIR}/spiffs/spiffs_check.c
    # the volume
    ${CMAKE_CURRENT_LIST_DIR}/src/pico_spiffs.c
    ${CMAKE_CURRENT_LIST_DIR}/src/pico_spiffs_errors.c
    ${CMAKE_CURRENT_LIST_DIR}/src/pico_spiffs_leds.c
    ${CMAKE_CURRENT_LIST_DIR}/src/pico_spiffs_list.c
    # POSIX system calls for newlib
    ${CMAKE_CURRENT_LIST_DIR}/src/posix_common.c
    ${CMAKE_CURRENT_LIST_DIR}/src/posix_files.c
    ${CMAKE_CURRENT_LIST_DIR}/src/posix_names.c
    ${CMAKE_CURRENT_LIST_DIR}/src/posix_dirs.c
    ${CMAKE_CURRENT_LIST_DIR}/src/posix_maps.c
    ${CMAKE_CURRENT_LIST_DIR}/src/posix_stdio.c
)

target_include_directories(pico_spiffs INTERFACE
    ${CMAKE_CURRENT_LIST_DIR}/include         # before newlib's: sys/dirent.h
    ${CMAKE_CURRENT_LIST_DIR}/spiffs
)

target_link_libraries(pico_spiffs INTERFACE pico_stdlib pico_sync hardware_gpio)

if (PICO_SPIFFS_FLASH STREQUAL "EXTERNAL")
    target_sources(pico_spiffs INTERFACE ${CMAKE_CURRENT_LIST_DIR}/src/flash_external.c)
    target_compile_definitions(pico_spiffs INTERFACE PICO_SPIFFS_FLASH_EXTERNAL=1)
    target_link_libraries(pico_spiffs INTERFACE lib_winbond_w25q hardware_spi hardware_dma)
elseif (PICO_SPIFFS_FLASH STREQUAL "ONBOARD")
    target_sources(pico_spiffs INTERFACE ${CMAKE_CURRENT_LIST_DIR}/src/flash_onboard.c)
    target_compile_definitions(pico_spiffs INTERFACE PICO_SPIFFS_FLASH_EXTERNAL=0)
    target_link_libraries(pico_spiffs INTERFACE hardware_flash pico_flash)
else()
    message(FATAL_ERROR "PICO_SPIFFS_FLASH must be ONBOARD or EXTERNAL, not '${PICO_SPIFFS_FLASH}'")
endif()

# The SPIFFS core sources ask for -fno-strict-aliasing themselves (#pragma),
# as INTERFACE sources are compiled with the application's own options.

message(STATUS "pico_spiffs: volume on ${PICO_SPIFFS_FLASH} flash")
