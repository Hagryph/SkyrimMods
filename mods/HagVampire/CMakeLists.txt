cmake_minimum_required(VERSION 3.21)
project(HagVampire VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(spdlog CONFIG REQUIRED)
find_package(minhook CONFIG REQUIRED)

add_library(HagVampire SHARED
    src/Plugin.cpp
    src/Log.cpp
)

target_include_directories(HagVampire PRIVATE src "${CMAKE_CURRENT_SOURCE_DIR}/../../shared")
target_precompile_headers(HagVampire PRIVATE src/PCH.h)
target_compile_definitions(HagVampire PRIVATE UNICODE _UNICODE)
target_link_libraries(HagVampire PRIVATE spdlog::spdlog minhook::minhook Shell32 Ole32)

set_target_properties(HagVampire PROPERTIES PREFIX "" OUTPUT_NAME "HagVampire")
