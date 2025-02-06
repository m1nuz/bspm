export module hello;

import <print>;

export namespace hello {

auto say_hi() {
    std::println("Hello, cruel world!");
}

} // namespace hello