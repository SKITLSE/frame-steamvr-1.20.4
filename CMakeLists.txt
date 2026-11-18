cmake_minimum_required(VERSION 3.20)
project(framedrv LANGUAGES CXX)

if(NOT WIN32 OR NOT CMAKE_SIZEOF_VOID_P EQUAL 8)
  message(FATAL_ERROR "framedrv is a 64-bit Windows SteamVR driver; configure with -A x64.")
endif()

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
# Static CRT: the DLL is loaded into vrserver.exe and steam.exe and must not depend on a redist.
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

# SteamVR loads <driver>/bin/win64/driver_<name>.dll, so the output has to be driver_vrlink.dll.
add_library(driver_vrlink SHARED
  src/main.cpp
  src/core/config.cpp
  src/core/log.cpp
  src/core/proxy.cpp
  src/driver/device.cpp
  src/driver/driver_context.cpp
  src/driver/provider.cpp
  src/adapters/chaperone.cpp
  src/adapters/direct_mode.cpp
  src/adapters/paths.cpp
  src/adapters/properties.cpp
  src/adapters/server_internal.cpp
  src/adapters/steam_handoff.cpp
)

target_include_directories(driver_vrlink PRIVATE src)
target_compile_definitions(driver_vrlink PRIVATE _CRT_SECURE_NO_WARNINGS NOMINMAX)
target_compile_options(driver_vrlink PRIVATE /W3 /utf-8)
target_link_libraries(driver_vrlink PRIVATE shell32 ole32 advapi32)

# One place for the DLL regardless of configuration: build/bin/driver_vrlink.dll
set_target_properties(driver_vrlink PROPERTIES
  RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
  RUNTIME_OUTPUT_DIRECTORY_RELEASE "${CMAKE_BINARY_DIR}/bin"
  RUNTIME_OUTPUT_DIRECTORY_DEBUG "${CMAKE_BINARY_DIR}/bin"
  RUNTIME_OUTPUT_DIRECTORY_RELWITHDEBINFO "${CMAKE_BINARY_DIR}/bin"
)
