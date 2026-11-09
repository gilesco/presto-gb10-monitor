# Host (native) build of upstream's Unity test suite. The tested layers —
# gfx, ui, metrics — are plain C++ with no hardware dependency, exactly as
# upstream's `pio test -e native` builds them (build_src_filter
# +<gfx/> +<ui/> +<metrics/>).
cmake_minimum_required(VERSION 3.12)
project(sysd-tests C CXX)
set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 17)

set(SRC ${CMAKE_CURRENT_LIST_DIR}/../src)
set(ARDUINOJSON_SRC ${CMAKE_CURRENT_LIST_DIR}/../../deps/ArduinoJson/src)

add_library(unity unity/unity.c)
target_include_directories(unity PUBLIC ${CMAKE_CURRENT_LIST_DIR}/unity)

function(sysd_test name srcfile)
  add_executable(${name} ${srcfile}
    ${SRC}/gfx/font.cpp
    ${SRC}/gfx/grid.cpp
    ${SRC}/gfx/raster.cpp
    ${SRC}/gfx/theme.cpp
    ${SRC}/ui/chrome.cpp
    ${SRC}/ui/demo_view.cpp
    ${SRC}/ui/fmt.cpp
    ${SRC}/ui/page_graphs.cpp
    ${SRC}/ui/page_overview.cpp
    ${SRC}/ui/page_settings.cpp
    ${SRC}/ui/widgets.cpp
    ${SRC}/metrics/derive.cpp
    ${SRC}/metrics/prom.cpp
    ${SRC}/metrics/server_info.cpp
  )
  target_include_directories(${name} PRIVATE
    ${SRC}
    ${CMAKE_CURRENT_LIST_DIR}/../include
    ${ARDUINOJSON_SRC}
  )
  target_link_libraries(${name} unity)
endfunction()

sysd_test(test_fmt test_fmt/test_main.cpp)
sysd_test(test_metrics test_metrics/test_main.cpp)
sysd_test(test_render test_render/test_main.cpp)
sysd_test(test_tz test_tz/test_main.cpp)
target_sources(test_tz PRIVATE ${SRC}/net/posix_tz.cpp)
target_sources(test_metrics PRIVATE ${SRC}/net/http_split.cpp)
