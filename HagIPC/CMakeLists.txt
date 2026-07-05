cmake_minimum_required(VERSION 3.21)
project(HagIPC VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(spdlog CONFIG REQUIRED)

add_library(HagIPC SHARED
    src/Plugin.cpp
    src/Log.cpp
    src/Config.cpp
    src/ipc/Server.cpp
    src/ipc/MemAccess.cpp
    src/ipc/MainThread.cpp
    src/ipc/CallExec.cpp
    src/ipc/ConsoleExec.cpp
    src/ipc/SaveLoad.cpp
)

target_include_directories(HagIPC PRIVATE src "${CMAKE_CURRENT_SOURCE_DIR}/../shared")
target_precompile_headers(HagIPC PRIVATE src/PCH.h)
target_compile_definitions(HagIPC PRIVATE UNICODE _UNICODE)
target_link_libraries(HagIPC PRIVATE spdlog::spdlog Ws2_32 Shell32 Ole32)

# DLL named exactly HagIPC.dll (no "lib" prefix)
set_target_properties(HagIPC PROPERTIES PREFIX "" OUTPUT_NAME "HagIPC")
