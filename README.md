# bspm
Experimental single-file tool for building C++ with Modules.

`bspm` is intentionally dependency-free: compile `bspm.cpp` with a C++23
compiler and use the resulting executable.

## How to use

Initialize project
```console
bspm init <dir>
```

### Example

```console
bspm init hello
```

Directory structure:
<pre>
├── hello
│   └── main.cpp
</pre>

Build project
```console
bspm build <dir>
```

`bspm` scans the target directory recursively for `.cpp` and `.cppm` files, so
targets may use nested source folders:
```text
<dir>/
├── src/
│   └── main.cpp
└── modules/
    └── hello.cppm
```

Generated directories such as `build/`, `.cache/`, and `gcm.cache/` are skipped
during source discovery.

Choose compiler
```console
bspm build <dir> -c g++
bspm build <dir> -c clang++
bspm build <dir> -c msvc
```

Choose output name
```console
bspm build <dir> -o hello
bspm build <dir> --output hello
```

Choose target type
```console
bspm build <dir> --bin
bspm build <dir> --lib
bspm build <dir> --shared
```

When `-o` omits an extension, `bspm` chooses a target-appropriate name:
```text
--bin    hello.exe on Windows, hello on Unix-like systems
--lib    libhello.a for GCC/Clang, hello.lib for MSVC
--shared hello.dll on Windows, libhello.so on Unix-like systems
```

Choose build mode
```console
bspm build <dir> --debug
bspm build <dir> --release
```

Print build commands without running them
```console
bspm build <dir> --dry-run
```

Build current directory
```console
bspm build
bspm build -v
```

Generated files are kept under a profile-specific build directory:
```text
<dir>/build/gcc-debug/
<dir>/build/gcc-release/
<dir>/build/clang-debug/
<dir>/build/msvc-debug/
```

Run executable
```console
bspm run <dir>
```

Clean project and remove generated files
```console
bspm clean <dir>
```

`clean` removes the target's `build/` directory.

Show help
```console
bspm help
bspm help build
```

See `examples/README.md` for a tour of the included example targets.

## How to build

With g++:
```console
g++ -std=c++23 -Wpedantic -Wall -Wextra -Werror bspm.cpp -O2 -lstdc++exp -o bspm
```

On Windows, build `bspm.exe` with g++:
```console
g++ -std=c++23 -Wpedantic -Wall -Wextra -Werror bspm.cpp -O2 -lstdc++exp -o bspm.exe
```

With clang++:
```console
clang++ -std=c++23 -Wpedantic -Wall -Wextra -Werror bspm.cpp -O2 -o bspm
```

With MSVC, use a Visual Studio Developer terminal or run `VsDevCmd.bat` first:
```console
cl /std:c++latest /EHsc /nologo bspm.cpp /Febspm.exe
```
