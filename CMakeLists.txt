cmake_minimum_required(VERSION 3.20.0)
set(BOARD nrf54lm20b_kb/nrf54lm20b/cpuapp)
set(BOARD_ROOT ${CMAKE_CURRENT_SOURCE_DIR})
find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(nrf54lm20b_kb)

file(GLOB_RECURSE APP_SOURCES src/*.c)
target_sources(app PRIVATE ${APP_SOURCES})

target_include_directories(app PRIVATE inc)
