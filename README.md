# bspm
Experimental tool for building C++ with Modules

## How to use

Initialize project
```console
bspm init <dir>
```

Build project
```console
bspm build <dir>
```

Build executable
```console
bspm run <dir>
```

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