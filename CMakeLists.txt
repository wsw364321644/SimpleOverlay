cmake_minimum_required(VERSION 3.24)
include(FetchContent)
include(CMakePackageConfigHelpers)

set(MY_PRODUCT_NUMBER 1)
set(MY_PRODUCT_VERSION 0)
set(MY_BUILD_NUMBER 0)
set(VERSION "${MY_PRODUCT_NUMBER}.${MY_PRODUCT_VERSION}.${MY_BUILD_NUMBER}")

set(OVERLAY_UTIL_TAG 441250dfddba8d53b52e69a5cefb371d2188af9d)

project(simple_overlay VERSION ${VERSION})
enable_language(CXX ASM)

option(OVERLAY_STATIC_CRT "OVERLAY STATIC CRT Build ." OFF)

if(PROJECT_IS_TOP_LEVEL)
  if(OVERLAY_STATIC_CRT)
    if(MSVC)
      set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
      add_definitions(-D_CRT_SECURE_NO_WARNINGS)
    endif()

    set(CMAKE_C_FLAGS_RELEASE "${CMAKE_C_FLAGS_RELEASE} /MT")
    set(CMAKE_C_FLAGS_DEBUG "${CMAKE_C_FLAGS_DEBUG} /MTd")
  endif()
endif()

set(OBS_URL https://github.com/obsproject/obs-studio/releases/download/30.2.0-rc1/OBS-Studio-30.2.0-rc1-Windows.zip)
set(OBS_DIR ${CMAKE_SOURCE_DIR}/external/obs)
configure_file(${CMAKE_CURRENT_SOURCE_DIR}/obs.txt.in ${OBS_DIR}/CMakeLists.txt @ONLY)
execute_process(COMMAND ${CMAKE_COMMAND} ${CMAKE_GENERATOR_ARGV} .
  WORKING_DIRECTORY ${OBS_DIR})
execute_process(COMMAND ${CMAKE_COMMAND} --build . --config Release
  WORKING_DIRECTORY ${OBS_DIR})

install(
  FILES "${OBS_DIR}/_deps/obs-src/data/obs-plugins/win-capture/get-graphics-offsets32.exe"
  DESTINATION ${CMAKE_INSTALL_PREFIX}/bin
)

install(
  FILES "${OBS_DIR}/_deps/obs-src/data/obs-plugins/win-capture/get-graphics-offsets64.exe"
  DESTINATION ${CMAKE_INSTALL_PREFIX}/bin
)

install(
  FILES "${OBS_DIR}/_deps/obs-src/data/obs-plugins/win-capture/inject-helper32.exe"
  DESTINATION ${CMAKE_INSTALL_PREFIX}/bin
)

install(
  FILES "${OBS_DIR}/_deps/obs-src/data/obs-plugins/win-capture/inject-helper64.exe"
  DESTINATION ${CMAKE_INSTALL_PREFIX}/bin
)

FetchContent_Declare(
  overlay-util
  GIT_REPOSITORY git@ssh.github.com:wsw364321644/overlay-util.git
  GIT_TAG ${OVERLAY_UTIL_TAG}
  GIT_SUBMODULES_RECURSE true
)
FetchContent_MakeAvailable(overlay-util)

ImportTarget(inih SSH TAG 5cc5e2c24642513aaa5b19126aad42d0e4e0923e) # r58

find_package(LIBUV REQUIRED)
add_subdirectory(src/game-captor)
