cmake_minimum_required(VERSION 3.20.0)
project(bspm VERSION 0.1.0)

include(FetchContent)

FetchContent_Declare(
    fmt
    GIT_REPOSITORY https://github.com/fmtlib/fmt.git
    GIT_TAG 10.1.1
    CMAKE_ARGS -DFMT_DOC=OFF -DFMT_INSTALL=OFF -DFMT_TEST=OFF
)

FetchContent_Declare(
    tomlplusplus
    GIT_REPOSITORY https://github.com/marzer/tomlplusplus.git
    GIT_TAG v3.4.0
)

FetchContent_Declare(
    cxxopts
    GIT_REPOSITORY https://github.com/jarro2783/cxxopts.git
    GIT_TAG v3.1.1
)

FetchContent_MakeAvailable(fmt tomlplusplus cxxopts)

set(APP_NAME "bspm")

add_executable(${APP_NAME}
    bspm.cpp
)

target_compile_options(${APP_NAME}
    PUBLIC
        -pthread
        -pedantic
        -Wall
        -Wextra
        -Werror
)

target_compile_features(${APP_NAME}
    PUBLIC
        cxx_std_23
)

target_include_directories(${APP_NAME}
    PUBLIC
        $<BUILD_INTERFACE:${tomlplusplus_SOURCE_DIR}>
)

target_link_libraries(${APP_NAME}
    PUBLIC
        tomlplusplus::tomlplusplus
        cxxopts::cxxopts
        fmt::fmt
        stdc++fs
)