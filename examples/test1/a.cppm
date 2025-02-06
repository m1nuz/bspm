export module a;
import <cstdlib>;
import <cmath>;
import c;

export namespace data {

const double pi = std::acos(-1);

int get() {
    return data2::bar(2) + rand() + std::sin(pi / 6);
}

} // namespace data