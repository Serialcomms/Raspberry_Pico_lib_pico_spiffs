# flash_chip_erase - full chip erase for flash on QSPI CS0/CS1, SPI0 and SPI1.
# RP2350 only (it needs the QMI's two chip selects). Builds a RAM-only UF2,
# so loading it does not write to flash.
#
#   cmake -B build -DPICO_BOARD=pico2
#   cmake -B build -DPICO_BOARD=pimoroni_pico_plus2_rp2350   # probes CS1 on GPIO 47
#   cmake --build build
#
# Wiring can be changed with -DFLASH_ERASE_QSPI_CS1_PIN=..., and
# -DFLASH_ERASE_SPI0_PIN_MISO=... etc. (see src/flash_port.c).

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

project(flash_chip_erase C CXX ASM)

pico_sdk_init()

if (NOT PICO_PLATFORM MATCHES "^rp2350")
    message(FATAL_ERROR "flash_chip_erase needs an RP2350 board, not '${PICO_PLATFORM}'")
endif()

set(FLASH_ERASE_VERSION 2)

add_executable(${CMAKE_PROJECT_NAME})
target_sources(${CMAKE_PROJECT_NAME} PRIVATE
    src/main.c
    src/console.c
    src/flash_chip.c
    src/flash_erase.c
    src/flash_id.c
    src/flash_port.c
    src/led_pulse.c
    src/text_input.c
)

# Runs entirely from RAM: nothing is written to flash when the UF2 is loaded.
pico_set_binary_type(${CMAKE_PROJECT_NAME} no_flash)

target_compile_definitions(${CMAKE_PROJECT_NAME} PRIVATE
    FLASH_ERASE_VERSION="${FLASH_ERASE_VERSION}"
)
target_link_libraries(${CMAKE_PROJECT_NAME}
    pico_stdlib pico_bootrom hardware_flash hardware_spi hardware_pwm
)
target_compile_options(${CMAKE_PROJECT_NAME} PRIVATE -Wall -Wextra)
target_link_options(${CMAKE_PROJECT_NAME} PRIVATE "-Wl,--print-memory-usage")

pico_enable_stdio_usb(${CMAKE_PROJECT_NAME} 1)
pico_enable_stdio_uart(${CMAKE_PROJECT_NAME} 1)
pico_add_extra_outputs(${CMAKE_PROJECT_NAME})

# A copy of the UF2 named with board and version, e.g.
# flash_chip_erase_pico2_v1.uf2, so downloads can be told apart.
add_custom_command(TARGET ${CMAKE_PROJECT_NAME} POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy
        ${CMAKE_PROJECT_NAME}.uf2
        ${CMAKE_PROJECT_NAME}_${PICO_BOARD}_v${FLASH_ERASE_VERSION}.uf2
    WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}
)
