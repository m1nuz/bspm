# bspm
Experimental tool for building C++ with Modules

## Dependencies
included:
* [nlohmann/json](https://github.com/nlohmann/json)
* [cxxopts](https://github.com/jarro2783/cxxopts)

Need install:
* [fmt](https://github.com/fmtlib/fmt)

## How to build

with CMake
```console
mkdir build
cd build
cmake ..
cmake --build . -- -j$(nproc)
```

with make
```console
mkdir build
make
```