cmake_minimum_required(VERSION 3.10)
project(job_scheduler C)

set(CMAKE_C_STANDARD 11)

set(SOURCES
        client.c
        server_ctl.c
        server.c
        msg.c
        jobs.c
        cJSON.c)

add_executable(job_scheduler main.c ${SOURCES})

find_path(LIBUUID_INCLUDE_DIR uuid.h PATH_SUFFIXES uuid)
find_library(LIBUUID_LIBRARY libuuid.a)
include_directories(${LIBUUID_INCLUDE_DIR})
target_link_libraries(job_scheduler PUBLIC ${LIBUUID_LIBRARY})