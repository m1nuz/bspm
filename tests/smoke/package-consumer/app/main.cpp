import answer;

#include <answer.hpp>
#include <cstdio>

int main() {
    std::printf("package-consumer: %d\n", answer() + answer_offset());
    return 0;
}
