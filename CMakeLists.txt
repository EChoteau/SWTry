cmake_minimum_required(VERSION 3.20)
project(sw_rune_optimizer LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Tout en statique : un seul .exe, aucune DLL à côté
set(BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)

include(FetchContent)
FetchContent_Declare(cpr
    GIT_REPOSITORY https://github.com/libcpr/cpr.git
    GIT_TAG        1.11.0)
FetchContent_Declare(json
    URL https://github.com/nlohmann/json/releases/download/v3.11.3/json.tar.xz)
FetchContent_Declare(catch2
    GIT_REPOSITORY https://github.com/catchorg/Catch2.git
    GIT_TAG v3.5.4)
FetchContent_MakeAvailable(cpr json catch2)

add_library(project_warnings INTERFACE)
if(MSVC)
    target_compile_options(project_warnings INTERFACE /W4 /permissive-)
else()
    target_compile_options(project_warnings INTERFACE -Wall -Wextra -Wpedantic)
endif()

add_library(swcore STATIC
    src/swcore/swcore.cpp
    src/swcore/reference_db.cpp)
target_include_directories(swcore
    PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(swcore
    PUBLIC nlohmann_json::nlohmann_json
    PRIVATE project_warnings)

add_executable(swcli
    src/swcli/main.cpp)
target_link_libraries(swcli PRIVATE swcore project_warnings)

add_executable(swsync tools/swarfarm_sync.cpp)
target_link_libraries(swsync PRIVATE cpr::cpr nlohmann_json::nlohmann_json)
target_link_libraries(swsync PRIVATE project_warnings)

if(MINGW)
    target_link_options(swsync PRIVATE -static)
endif()

include(CTest)
if(BUILD_TESTING)
    add_executable(tests
        tests/smoke_test.cpp
        tests/reference_db_test.cpp)
    target_compile_definitions(tests PRIVATE
        SW_PROJECT_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}")
    target_link_libraries(tests PRIVATE swcore Catch2::Catch2WithMain project_warnings)
    include(Catch)
    catch_discover_tests(tests)
endif()