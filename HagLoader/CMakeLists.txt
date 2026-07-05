cmake_minimum_required(VERSION 3.21)
project(HagLoader VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(spdlog CONFIG REQUIRED)
find_package(minhook CONFIG REQUIRED)

add_library(HagLoader SHARED
    src/Plugin.cpp
    src/Log.cpp
    src/Hooking.cpp
    src/GameState.cpp
    src/ConfigStore.cpp
    src/ConsoleExec.cpp
    src/ConsoleQueue.cpp
    src/PapyrusCall.cpp
    src/NativeTaskQueue.cpp
    src/LoaderApiExport.cpp
    src/UI/HagMenu.cpp
    src/UI/GfxInject.cpp
    src/UI/OptionRender.cpp
    src/UI/Model3D.cpp
    src/api/HagApi.cpp
    src/api/HagApiExport.cpp
    src/ModManager.cpp
)

target_include_directories(HagLoader PRIVATE src "${CMAKE_CURRENT_SOURCE_DIR}/../shared")
target_precompile_headers(HagLoader PRIVATE src/PCH.h)
target_compile_definitions(HagLoader PRIVATE UNICODE _UNICODE)
target_link_libraries(HagLoader PRIVATE spdlog::spdlog minhook::minhook d3d11 Shell32 Ole32)

# DLL named exactly HagLoader.dll (no "lib" prefix). The in-game menu/movie remains HagUI.
set_target_properties(HagLoader PROPERTIES PREFIX "" OUTPUT_NAME "HagLoader")
