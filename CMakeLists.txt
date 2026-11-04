cmake_minimum_required(VERSION 3.20)
project(sw_rune_optimizer LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Release par défaut ; -DCMAKE_BUILD_TYPE=Debug reste possible (swgui avec console).
if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "Type de build (Release ou Debug)" FORCE)
endif()
set(CMAKE_CONFIGURATION_TYPES Release Debug CACHE STRING "Configurations" FORCE)

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
    src/swcore/reference_db.cpp
    src/swcore/storage_stacks.cpp
    src/swcore/runes.cpp
    src/swcore/set_effects.cpp
    src/swcore/solver_config.cpp
    src/swcore/solver.cpp
    src/swcore/account.cpp
    src/swcore/account_bonuses.cpp
    src/swcore/craft_audit.cpp
    src/swcore/solve_async.cpp
    src/swcore/build_text.cpp
    src/swcore/locks.cpp
    src/swcore/equip.cpp
    src/swcore/dungeon.cpp
    src/swcore/team_config.cpp)
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

# Interface graphique : Dear ImGui + GLFW + OpenGL3, versions épinglées, statique.
option(SW_BUILD_GUI "Compiler l'interface graphique swgui" ON)
if(SW_BUILD_GUI)
    set(GLFW_BUILD_DOCS OFF CACHE BOOL "" FORCE)
    set(GLFW_BUILD_TESTS OFF CACHE BOOL "" FORCE)
    set(GLFW_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
    set(GLFW_INSTALL OFF CACHE BOOL "" FORCE)
    FetchContent_Declare(glfw
        GIT_REPOSITORY https://github.com/glfw/glfw.git
        GIT_TAG        3.4)
    # Sans CMakeLists à la racine utilisée : SOURCE_SUBDIR inexistant = sources seules.
    FetchContent_Declare(imgui
        GIT_REPOSITORY https://github.com/ocornut/imgui.git
        GIT_TAG        v1.91.8
        SOURCE_SUBDIR  no-cmake)
    FetchContent_Declare(pfd
        GIT_REPOSITORY https://github.com/samhocevar/portable-file-dialogs.git
        GIT_TAG        0.1.0
        SOURCE_SUBDIR  no-cmake)
    FetchContent_MakeAvailable(glfw imgui pfd)
    find_package(OpenGL REQUIRED)

    add_library(imgui STATIC
        ${imgui_SOURCE_DIR}/imgui.cpp
        ${imgui_SOURCE_DIR}/imgui_draw.cpp
        ${imgui_SOURCE_DIR}/imgui_tables.cpp
        ${imgui_SOURCE_DIR}/imgui_widgets.cpp
        ${imgui_SOURCE_DIR}/backends/imgui_impl_glfw.cpp
        ${imgui_SOURCE_DIR}/backends/imgui_impl_opengl3.cpp
        ${imgui_SOURCE_DIR}/misc/cpp/imgui_stdlib.cpp)
    target_include_directories(imgui SYSTEM PUBLIC
        ${imgui_SOURCE_DIR} ${imgui_SOURCE_DIR}/backends)
    target_link_libraries(imgui PUBLIC glfw OpenGL::GL)

    add_executable(swgui
        src/swgui/main.cpp
        src/swgui/app.cpp
        src/swgui/data_store.cpp
        src/swgui/settings.cpp
        src/swgui/text_util.cpp
        src/swgui/gui_common.cpp
        src/swgui/app_monsters.cpp
        src/swgui/app_optimizer.cpp
        src/swgui/app_results.cpp
        src/swgui/app_builds.cpp
        src/swgui/app_workspace.cpp
        src/swgui/gui_components.cpp
        src/swgui/monster_view.cpp
        src/swgui/snapshot.cpp
        src/swgui/app_locks.cpp
        src/swgui/app_equip.cpp
        src/swgui/gui_logic.cpp
        src/swgui/app_dungeons.cpp)
    target_include_directories(swgui SYSTEM PRIVATE ${pfd_SOURCE_DIR})
    target_link_libraries(swgui PRIVATE swcore imgui project_warnings)
    if(WIN32)
        # Release : sous-système fenêtre (pas de console) ; --selftest se rattache
        # à la console parente. Debug : application console.
        if(NOT CMAKE_BUILD_TYPE STREQUAL "Debug")
            set_target_properties(swgui PROPERTIES WIN32_EXECUTABLE TRUE)
        endif()
        target_link_libraries(swgui PRIVATE ole32 uuid shell32 comdlg32)
    endif()
    if(MINGW)
        target_link_options(swgui PRIVATE -static)
    endif()
endif()

# Option propre au projet : BUILD_TESTING est forcé à OFF par une dépendance (cpr).
option(SW_BUILD_TESTS "Compiler les tests unitaires" ON)
if(SW_BUILD_TESTS)
    enable_testing()
    add_executable(tests
        tests/smoke_test.cpp
        tests/reference_db_test.cpp
        tests/storage_stacks_test.cpp
        tests/runes_test.cpp
        tests/set_effects_test.cpp
        tests/solver_config_test.cpp
        tests/solver_test.cpp
        tests/account_test.cpp
        tests/mapping_resolution_test.cpp
        tests/account_bonuses_test.cpp
        tests/craft_audit_test.cpp
        tests/craft_verification_test.cpp
        tests/config_roundtrip_test.cpp
        tests/gui_logic_test.cpp
        tests/locks_test.cpp
        tests/equip_test.cpp
        tests/dungeon_test.cpp
        src/swgui/monster_view.cpp
        src/swgui/snapshot.cpp
        src/swgui/gui_logic.cpp)
    target_include_directories(tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src/swgui)
    target_compile_definitions(tests PRIVATE
        SW_PROJECT_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}")
    target_link_libraries(tests PRIVATE swcore Catch2::Catch2WithMain project_warnings)
    include(Catch)
    catch_discover_tests(tests)
endif()