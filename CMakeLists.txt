cmake_minimum_required(VERSION 3.20)
project(job_scheduler C)

set(CMAKE_C_STANDARD 11)

set(SOURCES
        client.c
        server_ctl.c
        server.c
        msg.c
        jobs.c)

add_executable(job_scheduler main.c ${SOURCES})
