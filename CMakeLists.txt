# Minimum required version of CMake
cmake_minimum_required(VERSION 3.10)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

project(jams)

file(GLOB_RECURSE SOURCES "src/*.cpp")

# Add the main executable
add_executable(${PROJECT_NAME} ${SOURCES})


# RTMIDI: midi library

set(RTMIDI_BUILD_STATIC_LIBS ON CACHE BOOL "Build RtMidi as static")
add_subdirectory(external_libraries/rtmidi)
find_package(ftxui)

target_link_libraries(${PROJECT_NAME} PRIVATE rtmidi ftxui::ftxui)
