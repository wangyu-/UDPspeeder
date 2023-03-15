#note: experimental

cmake_minimum_required(VERSION 3.7)
project(speederv2)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

set(CMAKE_CXX_STANDARD 11)

add_custom_target(git_version DEPENDS "${CMAKE_SOURCE_DIR}/git_version.h")
add_custom_command(
    OUTPUT "${CMAKE_SOURCE_DIR}/git_version.h"
    COMMAND ${CMAKE_MAKE_PROGRAM} -C ${CMAKE_SOURCE_DIR} git_version
)

set(SOURCE_FILES
        main.cpp
        log.cpp
        common.cpp
        lib/fec.cpp
        lib/rs.cpp
        packet.cpp
        delay_manager.cpp
        fd_manager.cpp
        connection.cpp
        fec_manager.cpp
        misc.cpp
        tunnel_client.cpp
        tunnel_server.cpp
        my_ev.cpp
)
set(CMAKE_CXX_FLAGS "-Wall -Wextra -Wno-unused-variable -Wno-unused-parameter -Wno-missing-field-initializers -O2 -g -fsanitize=address,undefined")

#target_include_directories(speederv2 PRIVATE .)
#set(CMAKE_LINK_LIBRARY_FLAG "-lrt")
add_executable(speederv2 ${SOURCE_FILES})
add_dependencies(speederv2 git_version)
target_link_libraries(speederv2 rt)
target_link_libraries(speederv2 pthread)
include_directories(SYSTEM "libev")
include_directories(".")
