cmake_minimum_required(VERSION 3.20.0)
project(bspm VERSION 0.1.0)

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

target_link_libraries(${APP_NAME}
    PUBLIC
    stdc++fs
)