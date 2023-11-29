cmake_minimum_required(VERSION 3.26)
project(integra C)

set(CMAKE_C_STANDARD 23)

#-------------------------------------#

# Service name
set(SVC_NAME Integra)

# Interval between integrity checks, ms  (default: 30 minutes)
# set(CHECK_INTERVAL 5000)

#-------------------------------------#


include_directories(src)
include_directories(include)
include_directories(lib/cjson)

add_definitions(-D SVCNAME=\\"${SVC_NAME}\\")
if (CHECK_INTERVAL)
    add_definitions(-D CHECK_DELAY_MS=${CHECK_INTERVAL})
endif()

add_library(cjson lib/cjson/cjson.c)
add_executable(integra main.c src/service.c src/event.c src/cfg.c src/integra.c)

target_link_libraries(integra cjson -static)
