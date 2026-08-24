import <print>;

#include <fmt/format.h>

int main() {
    std::println("{}", fmt::format("Hello from {}!", "fmt"));
    return 0;
}
