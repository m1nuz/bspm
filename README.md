# bspm
Experimental tool for building C++ with Modules

## How to use

Initialize project
```console
bspm init <dir>
```
Directory structure:
<pre>
├── example
│   ├── main.cpp
│   ├── manifest.conf
│   └── packages.conf
</pre>

Build project
```console
bspm build <dir>
```

Run executable
```console
bspm run <dir>
```

Clean project and remove generated files
```console
bspm clean <dir>
```

## Dependencies
included for CMale build:
* [nlohmann/json](https://github.com/nlohmann/json)
* [cxxopts](https://github.com/jarro2783/cxxopts)
* [fmt](https://github.com/fmtlib/fmt)

## How to build

with CMake
```console
mkdir build
cd build
cmake ..
cmake --build . -- -j$(nproc)
```
