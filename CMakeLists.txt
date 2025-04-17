# Minimum required version of CMake
cmake_minimum_required(VERSION 3.10)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

project(project_name)


find_package(PkgConfig REQUIRED)
pkg_check_modules(RTMIDI REQUIRED rtmidi)

file(GLOB_RECURSE SOURCES "src/*.cpp")

# Add the main executable
add_executable(${PROJECT_NAME} ${SOURCES})

target_include_directories(${PROJECT_NAME} PRIVATE ${RTMIDI_INCLUDE_DIRS})
target_link_libraries(${PROJECT_NAME} PRIVATE ${RTMIDI_LIBRARIES})
