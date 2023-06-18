export module a;

import <cstdlib>;
import <cmath>;
import c;

export namespace data {

int get() {
    return data2::bar(2) + rand() / M_PI;
}

} // namespace data
