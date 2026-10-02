cmake_minimum_required(VERSION 3.20)
project(BrutalLegendVR VERSION 0.1.0 LANGUAGES C CXX)
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

if(NOT WIN32)
    message(FATAL_ERROR "BrutalLegendVR only supports Windows")
endif()

if(NOT CMAKE_SIZEOF_VOID_P EQUAL 4)
    message(WARNING "BrutalLegendVR requires 32-bit (x86) architecture for BrutalLegend.exe")
endif()

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# MinHook dependency from shared deps
set(MINHOOK_DIR "${CMAKE_CURRENT_SOURCE_DIR}/third_party/minhook")
set(OPENXR_DIR "${CMAKE_CURRENT_SOURCE_DIR}/third_party/OpenXR-SDK-Source")
set(BUILD_LOADER ON CACHE BOOL "" FORCE)
set(DYNAMIC_LOADER ON CACHE BOOL "" FORCE)
set(BUILD_API_LAYERS OFF CACHE BOOL "" FORCE)
set(BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(BUILD_ALL_EXTENSIONS OFF CACHE BOOL "" FORCE)
add_subdirectory("${OPENXR_DIR}" "${CMAKE_CURRENT_BINARY_DIR}/openxr-sdk" EXCLUDE_FROM_ALL)
set(MINHOOK_SOURCES
    ${MINHOOK_DIR}/src/buffer.c
    ${MINHOOK_DIR}/src/hook.c
    ${MINHOOK_DIR}/src/trampoline.c
    ${MINHOOK_DIR}/src/hde/hde32.c
    ${MINHOOK_DIR}/src/hde/hde64.c
)

add_library(minhook STATIC ${MINHOOK_SOURCES})
target_include_directories(minhook PUBLIC ${MINHOOK_DIR}/include ${MINHOOK_DIR}/src/hde)

# Main d3d9 proxy DLL
set(BLVR_SOURCES
    src/bootstrap/d3d9_proxy.cpp
    src/bootstrap/video_capture.cpp
    src/bootstrap/audio_capture.cpp
    src/camera/camera_hook.cpp
    src/camera/player_view_rig.cpp
    src/camera/terrain_projection.cpp
    src/camera/camera_relative_effects.cpp
    src/input/retail_input_bridge.cpp
    src/input/control_config.cpp
    src/input/vr_prompts.cpp
    src/openxr/xr_host.cpp
    src/diagnostics/log.cpp
    src/diagnostics/telemetry.cpp
)

add_library(blvr_d3d9 SHARED ${BLVR_SOURCES} src/bootstrap/d3d9.def)
add_dependencies(blvr_d3d9 openxr_loader)
target_include_directories(blvr_d3d9 PRIVATE
    src
    src/bootstrap
    src/camera
    src/openxr
    src/diagnostics
    ${MINHOOK_DIR}/include
    ${OPENXR_DIR}/include
)

target_compile_definitions(blvr_d3d9 PRIVATE
    WIN32_LEAN_AND_MEAN
    NOMINMAX
    _CRT_SECURE_NO_WARNINGS
)

target_link_libraries(blvr_d3d9 PRIVATE
    minhook
    OpenXR::headers
    dxguid
    d3d11
    dxgi
)

target_compile_definitions(blvr_d3d9 PRIVATE
    XR_USE_PLATFORM_WIN32
    XR_USE_GRAPHICS_API_D3D11
)

set_target_properties(blvr_d3d9 PROPERTIES
    OUTPUT_NAME "d3d9"
    PREFIX ""
)

option(BLVR_BUILD_NATIVE_INPUT_TEST "Test against a locally installed retail executable" OFF)
if(BLVR_BUILD_NATIVE_INPUT_TEST)
    add_executable(blvr_telemetry_budget_test tests/telemetry_budget.cpp src/diagnostics/telemetry.cpp)
    target_compile_definitions(blvr_telemetry_budget_test PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX _CRT_SECURE_NO_WARNINGS)
    add_executable(blvr_control_config_test tests/control_config.cpp src/input/control_config.cpp)
    target_compile_definitions(blvr_control_config_test PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX _CRT_SECURE_NO_WARNINGS)
    add_executable(blvr_navigation_test tests/vr_navigation.cpp)
    add_executable(blvr_player_view_rig_test tests/player_view_rig.cpp src/camera/player_view_rig.cpp)
    target_sources(blvr_player_view_rig_test PRIVATE src/input/control_config.cpp)
    target_compile_definitions(blvr_player_view_rig_test PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX _CRT_SECURE_NO_WARNINGS)
    add_executable(blvr_shadow_transform_test tests/shadow_transform.cpp)
    add_executable(blvr_terrain_projection_test tests/terrain_projection.cpp)
    add_executable(blvr_effect_origin_test tests/effect_origin.cpp)
    add_executable(blvr_native_build_ui_test tests/native_build_ui.cpp)
    add_executable(blvr_tracked_basis_test tests/tracked_basis.cpp)
    add_executable(blvr_native_input_test tests/retail_input_native.cpp)
    target_sources(blvr_native_input_test PRIVATE src/input/control_config.cpp)
    target_link_libraries(blvr_native_input_test PRIVATE minhook)
    target_compile_definitions(blvr_native_input_test PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX _CRT_SECURE_NO_WARNINGS)
    add_executable(blvr_native_prompt_test tests/vr_prompts_native.cpp)
    target_sources(blvr_native_prompt_test PRIVATE src/input/control_config.cpp)
    target_link_libraries(blvr_native_prompt_test PRIVATE minhook)
    target_compile_definitions(blvr_native_prompt_test PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX _CRT_SECURE_NO_WARNINGS)
endif()
