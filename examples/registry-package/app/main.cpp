#include <cstdio>
#include <fmt/format.h>

int main() {
    const auto message = fmt::format("registry-package: {}", 42);
    std::printf("%s\n", message.c_str());
    return 0;
}
