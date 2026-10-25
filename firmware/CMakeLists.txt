# Pocket Tank CYD-only firmware.
cmake_minimum_required(VERSION 3.16)
set(IDF_TARGET esp32)
set(PT_BOARD_VARIANT "cyd" CACHE STRING "Display board: cyd or elegoo")
set_property(CACHE PT_BOARD_VARIANT PROPERTY STRINGS cyd elegoo)
if(NOT PT_BOARD_VARIANT STREQUAL "cyd" AND NOT PT_BOARD_VARIANT STREQUAL "elegoo")
    message(FATAL_ERROR "Unsupported PT_BOARD_VARIANT: ${PT_BOARD_VARIANT}")
endif()
include($ENV{IDF_PATH}/tools/cmake/project.cmake)
# Keep the displayed commit/build id separate from ESP-IDF's app release,
# which OTA compares against the release manifest.
execute_process(COMMAND git describe --always --dirty --exclude=*
                WORKING_DIRECTORY ${CMAKE_CURRENT_LIST_DIR}
                OUTPUT_VARIABLE PT_BUILD_ID OUTPUT_STRIP_TRAILING_WHITESPACE ERROR_QUIET)
if(NOT PT_BUILD_ID)
    set(PT_BUILD_ID "unknown")
endif()
file(STRINGS "${CMAKE_CURRENT_LIST_DIR}/../common/version.h" PT_RELEASE_LINE
     REGEX "^#define PT_RELEASE[ \t]+\"")
string(REGEX MATCH "[0-9]+\\.[0-9]+\\.[0-9]+" PROJECT_VER "${PT_RELEASE_LINE}")
if(NOT PROJECT_VER)
    message(FATAL_ERROR "Missing app release in common/version.h")
endif()
# execute_process runs at configure time only: without this, a build after a
# new commit keeps the previous id (2026-09-30: the tank reported bb7751a from
# a 092a913 tree). A change of HEAD or of the branch's ref reconfigures.
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS
             ${CMAKE_CURRENT_LIST_DIR}/../.git/HEAD ${CMAKE_CURRENT_LIST_DIR}/../.git/index
             ${CMAKE_CURRENT_LIST_DIR}/../common/version.h)
project(pocket_tank)
