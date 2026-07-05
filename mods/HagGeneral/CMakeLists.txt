cmake_minimum_required(VERSION 3.21)
project(HagGeneral VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(spdlog CONFIG REQUIRED)
find_package(minhook CONFIG REQUIRED)

add_library(HagGeneral SHARED
    src/Plugin.cpp
    src/Log.cpp
    src/Hooking.cpp
    src/Config.cpp
    src/Display.cpp
    src/SettingsHook.cpp
    src/HagUIBridge.cpp
)

# ../../shared gives us GameOffsets.h, HagUIAPI.h, and SkyrimModAPI.h.
target_include_directories(HagGeneral PRIVATE src "${CMAKE_CURRENT_SOURCE_DIR}/../../shared")
target_precompile_headers(HagGeneral PRIVATE src/PCH.h)
target_compile_definitions(HagGeneral PRIVATE UNICODE _UNICODE)
target_link_libraries(HagGeneral PRIVATE spdlog::spdlog minhook::minhook Shell32 Ole32)

# DLL named exactly HagGeneral.dll (no "lib" prefix)
set_target_properties(HagGeneral PROPERTIES PREFIX "" OUTPUT_NAME "HagGeneral")
