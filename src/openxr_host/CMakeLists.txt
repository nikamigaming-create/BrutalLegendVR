cmake_minimum_required(VERSION 3.20)

project(blvr_xr_host LANGUAGES C CXX)
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

if(NOT WIN32)
  message(FATAL_ERROR "blvr_xr_host is Windows-only.")
endif()

if(NOT CMAKE_SIZEOF_VOID_P EQUAL 8)
  message(FATAL_ERROR "blvr_xr_host must be configured for x64.")
endif()

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

set(OPENXR_SDK_ROOT "${CMAKE_CURRENT_LIST_DIR}/../../third_party/OpenXR-SDK-Source"
    CACHE PATH "Bundled Khronos OpenXR SDK source")
if(NOT EXISTS "${OPENXR_SDK_ROOT}/CMakeLists.txt")
  message(FATAL_ERROR "Bundled OpenXR SDK was not found at ${OPENXR_SDK_ROOT}.")
endif()

set(BUILD_LOADER ON CACHE BOOL "" FORCE)
set(DYNAMIC_LOADER OFF CACHE BOOL "" FORCE)
set(BUILD_API_LAYERS OFF CACHE BOOL "" FORCE)
set(BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(BUILD_CONFORMANCE_TESTS OFF CACHE BOOL "" FORCE)
set(BUILD_SDK_TESTS OFF CACHE BOOL "" FORCE)
set(BUILD_ALL_EXTENSIONS OFF CACHE BOOL "" FORCE)
set(BUILD_WITH_SYSTEM_JSONCPP OFF CACHE BOOL "" FORCE)
set(INSTALL_SYMBOLS OFF CACHE BOOL "" FORCE)

add_subdirectory(
  "${OPENXR_SDK_ROOT}"
  "${CMAKE_CURRENT_BINARY_DIR}/openxr-sdk"
  EXCLUDE_FROM_ALL)
set_target_properties(openxr_loader PROPERTIES MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

set(BLVR_XR_BUILD_ID "dev" CACHE STRING "Build identity written to the BLVR host log")

add_executable(blvr_xr_host
  main.cpp
  eddie_lobby.cpp
  game_bridge.cpp
  haptic_bridge.cpp
  pose_bridge.cpp)
target_sources(blvr_xr_host PRIVATE ../input/control_config.cpp)
target_compile_definitions(blvr_xr_host PRIVATE
  WIN32_LEAN_AND_MEAN
  NOMINMAX
  XR_USE_PLATFORM_WIN32
  XR_USE_GRAPHICS_API_D3D11
  BLVR_XR_BUILD_ID="${BLVR_XR_BUILD_ID}")
target_compile_options(blvr_xr_host PRIVATE /W4 /WX /permissive- /EHsc /fp:strict)
target_link_libraries(blvr_xr_host PRIVATE
  openxr_loader
  d3d11
  d3dcompiler
  gdi32
  windowscodecs
  ole32
  dxgi)

get_filename_component(REPOSITORY_ROOT "${CMAKE_CURRENT_LIST_DIR}/../.." ABSOLUTE)
set(HOST_OUTPUT_DIR "${REPOSITORY_ROOT}/tools")
add_executable(blvr_audio_record audio_record.cpp)
target_compile_definitions(blvr_audio_record PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX)
target_link_libraries(blvr_audio_record PRIVATE ole32 uuid)
set_target_properties(blvr_audio_record PROPERTIES RUNTIME_OUTPUT_DIRECTORY_RELEASE "${HOST_OUTPUT_DIR}")
add_executable(blvr_audio_probe audio_probe.cpp)
target_compile_definitions(blvr_audio_probe PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX)
target_link_libraries(blvr_audio_probe PRIVATE ole32 uuid)
set_target_properties(blvr_audio_probe PROPERTIES RUNTIME_OUTPUT_DIRECTORY_RELEASE "${HOST_OUTPUT_DIR}")
set_target_properties(blvr_xr_host PROPERTIES
  RUNTIME_OUTPUT_DIRECTORY "${HOST_OUTPUT_DIR}"
  RUNTIME_OUTPUT_DIRECTORY_DEBUG "${HOST_OUTPUT_DIR}"
  RUNTIME_OUTPUT_DIRECTORY_RELEASE "${HOST_OUTPUT_DIR}"
  RUNTIME_OUTPUT_DIRECTORY_RELWITHDEBINFO "${HOST_OUTPUT_DIR}"
  RUNTIME_OUTPUT_DIRECTORY_MINSIZEREL "${HOST_OUTPUT_DIR}")

add_custom_command(TARGET blvr_xr_host POST_BUILD
  COMMAND "${CMAKE_COMMAND}" -E copy_if_different
    "${OPENXR_SDK_ROOT}/LICENSE"
    "${HOST_OUTPUT_DIR}/THIRD-PARTY-OpenXR-LICENSE.txt"
  VERBATIM)
