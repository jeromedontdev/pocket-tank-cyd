# Pocket Tank CYD-only firmware.
cmake_minimum_required(VERSION 3.16)
set(IDF_TARGET esp32)
include($ENV{IDF_PATH}/tools/cmake/project.cmake)
# the build id: the commit's short hash (+ "-dirty"), never a tag - the
# release number lives in common/version.h and is shown beside it (ESP-IDF's
# own default is `git describe --tags`, which on the tagged release commit
# would read "v0.2.0" and say the number twice; 2026-09-29)
execute_process(COMMAND git describe --always --dirty --exclude=*
                WORKING_DIRECTORY ${CMAKE_CURRENT_LIST_DIR}
                OUTPUT_VARIABLE PT_BUILD_ID OUTPUT_STRIP_TRAILING_WHITESPACE ERROR_QUIET)
if(PT_BUILD_ID)
    set(PROJECT_VER ${PT_BUILD_ID})
endif()
# execute_process runs at configure time only: without this, a build after a
# new commit keeps the previous id (2026-09-30: the tank reported bb7751a from
# a 092a913 tree). A change of HEAD or of the branch's ref reconfigures.
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS
             ${CMAKE_CURRENT_LIST_DIR}/../.git/HEAD ${CMAKE_CURRENT_LIST_DIR}/../.git/index)
project(pocket_tank)
