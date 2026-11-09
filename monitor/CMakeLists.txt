cmake_minimum_required(VERSION 3.12)

# Presto: RP2350 (B variant, 48 GPIO), 16 MB flash, 8 MB PSRAM, CYW43 WiFi.
# pico-sdk master ships a first-class board definition for the Presto.
set(PICO_PLATFORM rp2350-arm-s)
set(PICO_BOARD pimoroni_presto)

set(NAME monitor)

include(pimoroni_pico_import.cmake)
include(pico_sdk_import.cmake)

# Gooey boilerplate
project(${NAME} C CXX ASM)
set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 17)

# Initialize the SDK
pico_sdk_init()

# --- Pimoroni Presto C++ libraries (drivers/st7701 PIO scanout driver) ---
if (NOT PIMORONI_PRESTO_PATH)
    set(PIMORONI_PRESTO_PATH ${CMAKE_CURRENT_LIST_DIR}/../deps/presto)
endif()
get_filename_component(PIMORONI_PRESTO_PATH "${PIMORONI_PRESTO_PATH}" ABSOLUTE)
message("PIMORONI_PRESTO_PATH is ${PIMORONI_PRESTO_PATH}")
set(PIMORONI_PRESTO_PATH ${PIMORONI_PRESTO_PATH} CACHE PATH "Path to the Presto libraries" FORCE)
include_directories(${PIMORONI_PRESTO_PATH})
list(APPEND CMAKE_MODULE_PATH ${PIMORONI_PRESTO_PATH})
include(drivers/st7701/st7701_presto)

# Add your source files
add_executable(${NAME}
    src/main.cpp
    # Presto hardware layer (replaces upstream's ESP32 src/hw and src/net)
    src/hw/panel.cpp
    src/hw/backlight.cpp
    src/hw/screen.cpp
    src/hw/settings.cpp
    src/hw/touch_ft6236.cpp
    src/net/wifi.cpp
    src/net/http_get.cpp
    src/net/http_split.cpp
    src/net/poller.cpp
    src/net/posix_tz.cpp
    # Portable layers reused from upstream system-d-monitor
    src/gfx/font.cpp
    src/gfx/grid.cpp
    src/gfx/raster.cpp
    src/gfx/theme.cpp
    src/ui/chrome.cpp
    src/ui/demo_view.cpp
    src/ui/fmt.cpp
    src/ui/page_graphs.cpp
    src/ui/page_overview.cpp
    src/ui/page_settings.cpp
    src/ui/widgets.cpp
    src/metrics/derive.cpp
    src/metrics/prom.cpp
    src/metrics/server_info.cpp
)

# Project headers (src/ for "gfx/..." style includes, include/ for config.h).
# src/net is on the path so lwIP's sources (compiled as INTERFACE sources of
# this target) find our lwipopts.h.
target_include_directories(${NAME} PRIVATE
    ${CMAKE_CURRENT_LIST_DIR}/src
    ${CMAKE_CURRENT_LIST_DIR}/src/net
    ${CMAKE_CURRENT_LIST_DIR}/include
)

# ArduinoJson (server_info.cpp parses /v1/models; upstream dependency)
if (NOT ARDUINOJSON_PATH)
    set(ARDUINOJSON_PATH ${CMAKE_CURRENT_LIST_DIR}/../deps/ArduinoJson)
endif()
get_filename_component(ARDUINOJSON_PATH "${ARDUINOJSON_PATH}" ABSOLUTE)
target_include_directories(${NAME} PRIVATE ${ARDUINOJSON_PATH}/src)

# pimoroni-pico libraries the ST7701 driver depends on
# (st7701_presto is an INTERFACE target linking pimoroni_bus + pico_graphics)
include(common/pimoroni_i2c)
include(common/pimoroni_bus)
include(libraries/pico_graphics/pico_graphics)

# Don't forget to link the libraries you need!
target_link_libraries(${NAME}
    st7701_presto
    pico_stdlib
    pico_multicore
    pimoroni_i2c
    hardware_interp
    hardware_psram
    pico_graphics
    # WiFi: CYW43 driver + lwIP, driven synchronously from the main loop
    # (poll arch: cyw43_arch_poll() from net::up()/net::link(); the
    # threadsafe-background IRQ worker proved unreliable alongside the
    # display scanout — joins stalled at LINK_JOIN)
    pico_cyw43_arch_lwip_poll
    pico_lwip_sntp
)

# 4 KB main stack (default 2 KB overflowed under lwIP + printf depth).
# NOTE: PICO_USE_STACK_GUARDS deliberately NOT set — the MPU guard regions
# changed the failure mode alongside the display driver's core 1 scanout.
target_compile_definitions(${NAME} PRIVATE
    PICO_STACK_SIZE=4096
)

# create map/bin/hex file etc.
pico_add_extra_outputs(${NAME})

# Enable USB UART output only
pico_enable_stdio_uart(${NAME} 0)
pico_enable_stdio_usb(${NAME} 1)
