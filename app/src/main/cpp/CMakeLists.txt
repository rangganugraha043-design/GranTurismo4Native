cmake_minimum_required(VERSION 3.22.1)
project(gt4native LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# SDL3 provides Android's native window, input, audio and renderer integration.
# Gradle and CMake share one pinned SDL checkout (Java glue + native sources).
include(FetchContent)
set(SDL_SHARED OFF CACHE BOOL "" FORCE)
set(SDL_STATIC ON CACHE BOOL "" FORCE)
set(SDL_TEST_LIBRARY OFF CACHE BOOL "" FORCE)
set(SDL_TESTS OFF CACHE BOOL "" FORCE)
FetchContent_Declare(SDL3
    GIT_REPOSITORY https://github.com/libsdl-org/SDL.git
    GIT_TAG release-3.4.12
    GIT_SHALLOW TRUE
)
FetchContent_MakeAvailable(SDL3)

add_library(main SHARED sdl-main.cpp)
target_link_libraries(main PRIVATE SDL3::SDL3)

add_library(gt4native SHARED native-lib.cpp)
find_library(log-lib log)
find_library(z-lib z)
target_link_libraries(gt4native PRIVATE ${log-lib} ${z-lib})
