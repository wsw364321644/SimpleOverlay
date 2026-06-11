cmake_minimum_required(VERSION 3.24)
include(FetchContent)
include(CMakePackageConfigHelpers)

set(MY_PRODUCT_NUMBER 1)
set(MY_PRODUCT_VERSION 0)
set(MY_BUILD_NUMBER 0)
set(VERSION "${MY_PRODUCT_NUMBER}.${MY_PRODUCT_VERSION}.${MY_BUILD_NUMBER}")

set(OVERLAY_UTIL_TAG 69a87b210233126144373099a1f69f00e7070b61)

project(SimpleOverlay VERSION ${VERSION})
enable_language(CXX ASM)
include(GNUInstallDirs)

option(OVERLAY_STATIC_CRT "OVERLAY STATIC CRT Build ." OFF)
option(OVERLAY_GIT_SSH "Git port 22." OFF)
option(OVERLAY_DISABLE_INSTALL "disable install." OFF)

if(OVERLAY_GIT_SSH)
  set(SSH SSH)
else()
  unset(SSH)
endif()

if(OVERLAY_STATIC_CRT)
  if(MSVC)
    set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
    add_definitions(-D_CRT_SECURE_NO_WARNINGS)
  endif()

  set(CMAKE_C_FLAGS_RELEASE "${CMAKE_C_FLAGS_RELEASE} /MT")
  set(CMAKE_C_FLAGS_DEBUG "${CMAKE_C_FLAGS_DEBUG} /MTd")
  set(STATIC_CRT STATIC_CRT)
else()
  unset(STATIC_CRT)
endif()


set(OBS_URL https://github.com/obsproject/obs-studio/releases/download/30.2.0-rc1/OBS-Studio-30.2.0-rc1-Windows.zip)
set(OBS_DIR ${CMAKE_SOURCE_DIR}/external/obs)
configure_file(${CMAKE_CURRENT_SOURCE_DIR}/obs.txt.in ${OBS_DIR}/CMakeLists.txt @ONLY)
execute_process(COMMAND ${CMAKE_COMMAND} ${CMAKE_GENERATOR_ARGV} .
  WORKING_DIRECTORY ${OBS_DIR})
execute_process(COMMAND ${CMAKE_COMMAND} --build . --config Release
  WORKING_DIRECTORY ${OBS_DIR})

file(
  COPY
  "${OBS_DIR}/_deps/obs-src/data/obs-plugins/win-capture/get-graphics-offsets32.exe"
  "${OBS_DIR}/_deps/obs-src/data/obs-plugins/win-capture/inject-helper32.exe"
  DESTINATION
  "${CMAKE_SOURCE_DIR}/additional_install_files/exec32/")
file(
  COPY
  "${OBS_DIR}/_deps/obs-src/data/obs-plugins/win-capture/get-graphics-offsets64.exe"
  "${OBS_DIR}/_deps/obs-src/data/obs-plugins/win-capture/inject-helper64.exe"
  DESTINATION
  "${CMAKE_SOURCE_DIR}/additional_install_files/exec64/")

FetchContent_Declare(
  overlay-util
  GIT_REPOSITORY https://github.com/wsw364321644/overlay-util.git
  GIT_TAG ${OVERLAY_UTIL_TAG}
  GIT_SUBMODULES_RECURSE true
)
FetchContent_MakeAvailable(overlay-util)

ImportProject(inih ${STATIC_CRT} ${SSH} TAG r62)

find_package(LIBUV REQUIRED)
add_subdirectory(src/game-captor)
add_subdirectory(src/game-captor-test)

if(NOT OVERLAY_DISABLE_INSTALL)
  ExportFromInstall(${PROJECT_NAME})
endif()
