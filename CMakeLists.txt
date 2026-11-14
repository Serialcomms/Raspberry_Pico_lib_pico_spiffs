# pico_spiffs example program. Open this folder in VS Code with the
# Raspberry Pi Pico extension, or build from the command line:
#
#   cmake -B build -DPICO_BOARD=pico2                      # onboard flash (default)
#   cmake -B build -DPICO_BOARD=pico2 -DPICO_SPIFFS_FLASH=EXTERNAL
#   cmake --build build

cmake_minimum_required(VERSION 3.13)

set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# == DO NOT EDIT THE FOLLOWING LINES for the Raspberry Pi Pico VS Code Extension to work ==
if(WIN32)
    set(USERHOME $ENV{USERPROFILE})
else()
    set(USERHOME $ENV{HOME})
endif()
set(sdkVersion 2.3.1)
set(toolchainVersion 15_2_Rel1)
set(picotoolVersion 2.3.1)
set(picoVscode ${USERHOME}/.pico-sdk/cmake/pico-vscode.cmake)
if (EXISTS ${picoVscode})
    include(${picoVscode})
endif()
# ====================================================================================
set(PICO_BOARD pico2 CACHE STRING "Board type")

# Pull in Raspberry Pi Pico SDK (must be before project)
include(pico_sdk_import.cmake)

project(pico_spiffs_example C CXX ASM)

# Initialise the Raspberry Pi Pico SDK
pico_sdk_init()

if (PICO_SDK_VERSION_STRING VERSION_LESS "2.3.1")
    message(FATAL_ERROR "pico_spiffs needs Pico SDK 2.3.1 or later")
endif()

set(PICO_SPIFFS_FLASH ONBOARD CACHE STRING "ONBOARD or EXTERNAL")
if (PICO_SPIFFS_FLASH STREQUAL "EXTERNAL")
    add_subdirectory(lib_winbond_w25q)
endif()

add_subdirectory(lib_pico_spiffs)

add_executable(${CMAKE_PROJECT_NAME})
target_sources(${CMAKE_PROJECT_NAME} PRIVATE example/main.c)
target_link_libraries(${CMAKE_PROJECT_NAME} pico_stdlib pico_bootrom pico_spiffs)
target_compile_options(${CMAKE_PROJECT_NAME} PRIVATE -Wall -Wextra)
target_link_options(${CMAKE_PROJECT_NAME} PRIVATE "-Wl,--print-memory-usage")

# External flash margin test: set the SPI clock to try. At the default
# 150 MHz system clock, requests of 20, 25 and 40 MHz (the default) give
# 18.75, 25 and 37.5 MHz; the example prints the clock actually used.
# target_compile_definitions(${CMAKE_PROJECT_NAME} PRIVATE PICO_SPIFFS_EXT_BAUD=20000000)

pico_enable_stdio_usb(${CMAKE_PROJECT_NAME} 1)
pico_enable_stdio_uart(${CMAKE_PROJECT_NAME} 1)
pico_add_extra_outputs(${CMAKE_PROJECT_NAME})
