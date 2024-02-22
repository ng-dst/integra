cmake_minimum_required(VERSION 3.26)
project(integra C)

set(CMAKE_C_STANDARD 23)

#-------------------------------------#

# Service name
set(SVC_NAME Integra)

# Report on successful checks for each file (for testing)
# set(REPORT_SUCCESSFUL_CHECKS TRUE)

# Disable verification on Change Notifications
# set(CHANGE_NOTIFICATIONS_DISABLE TRUE)

# Build directory
set(PROJECT_BINARY_DIR ${PROJECT_SOURCE_DIR}/build)

#-------------------------------------#


include_directories(src)
include_directories(include)
include_directories(lib/cjson)
include_directories(lib/md5)

add_definitions(-D SVCNAME=\\"${SVC_NAME}\\")
if (REPORT_SUCCESSFUL_CHECKS)
    add_definitions(-D REPORT_SUCCESSFUL_CHECKS)
endif()
if (CHANGE_NOTIFICATIONS_DISABLE)
    add_definitions(-D CHANGE_NOTIFICATIONS_DISABLE)
endif()

add_library(cjson lib/cjson/cjson.c)
add_library(md5 lib/md5/md5.c)

add_executable(integra main.c src/service.c src/event.c src/cfg.c src/integra.c src/snapshot.c src/utils.c)
target_link_libraries(integra cjson md5 -static)
set_target_properties(integra PROPERTIES RUNTIME_OUTPUT_DIRECTORY ${PROJECT_BINARY_DIR})
