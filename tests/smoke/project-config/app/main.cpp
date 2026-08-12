import greeting;
import math;

#include <cstdio>

#include "project_config.hpp"

#ifndef PROJECT_CONFIG_CXXFLAG
#error "PROJECT_CONFIG_CXXFLAG should be provided by bspm.build --cxxflag"
#endif

int main() {
    std::printf("%s: %d\n", greeting::message(), math::add(20, 22) + PROJECT_CONFIG_OFFSET);
}
