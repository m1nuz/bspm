# bspm

[![build](https://github.com/m1nuz/bspm/actions/workflows/smoke.yml/badge.svg?event=push)](https://github.com/m1nuz/bspm/actions/workflows/smoke.yml)

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

Initialize a configured multi-target project
```console
bspm init hello --project
```

Directory structure:
<pre>
├── hello
│   ├── bspm.build
│   └── app
│       └── main.cpp
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

Named module targets may use primary interfaces, implementation units, exported
partitions, internal partitions, and local partition imports:
```cpp
export module math;
export import :ops;

export module math:ops;
module math:detail;
module math;
import :detail;
```

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

Print the discovered source/module graph before building
```console
bspm build <dir> --explain
```

Compile independent source files in parallel
```console
bspm build <dir> -j 4
bspm build <dir> --jobs 4
```

Add compile, link, definition, include, and discovery options
```console
bspm build <dir> --include include --define APP_DEBUG=1
bspm build <dir> --cxxflag -Wall --ldflag -pthread
bspm build <dir> --source src --exclude src/generated
```

Build current directory
```console
bspm build
bspm build -v
```

## Optional project config

Configuration is optional. A folder can always be built directly without a
config file:
```console
bspm build examples/nested-sources
```

Use a non-empty `bspm.build` file only when one project contains several related
folder targets or when you want shared target names, default targets, and build
ordering:
```text
project demo
default app
common --include include --define DEMO_TRACE=1

target math libs/math --lib -o math
target greeting libs/greeting --shared -o greeting
target app app --bin -o demo --depends math --depends greeting
```

From the directory containing that `bspm.build` file:
```console
bspm build
bspm build app
bspm build all
bspm run
bspm run app
bspm clean app
bspm clean all
```

From outside the project directory, use `--project` to treat a directory as a
project root instead of as one plain folder target:
```console
bspm build examples/project-config --project
```

Inspect discovered targets, source units, module declarations, and imports
without compiling:
```console
bspm graph <dir>
bspm graph all
bspm graph examples/project-config --project
```

Generate an IDE-friendly compilation database without compiling:
```console
bspm compile-commands <dir>
bspm compile-commands all
bspm compile-commands examples/project-config --project
```

`bspm.build` uses a small shell-like syntax:
- `project <name>` names the project.
- `default <target>` chooses what bare `build`, `run`, and `clean` mean.
- `common [options]` applies shared build options to every target before each
  target's own options.
- `target <name> <path> [options]` defines a folder target.
- Target options currently accept the same build flags as the CLI plus repeated
  `--depends <target>` entries.
- Repeated `--cxxflag`, `--ldflag`, `--define`, `--include`, `--source`, and
  `--exclude` entries are allowed. Values that contain spaces can be quoted.
- `--source` restricts discovery to one file or directory relative to the target
  root, and `--exclude` removes matching files or directories from discovery.
- `--depends` builds dependencies first, exposes their module artifacts to the
  consumer target, and adds library outputs to the consumer link step.
- `#` starts a comment, and quoted paths such as `"tools/code gen"` are allowed.

`bspm init <dir>` keeps config out of the simple path. Use
`bspm init <dir> --project` when you want a starter `bspm.build` file and a
configured `app` target.

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

Check local compiler/linker tools
```console
bspm doctor
bspm doctor -c clang++
bspm doctor -c msvc
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
bspm help graph
bspm help compile-commands
bspm help doctor
```

See `examples/README.md` for a tour of the included example targets.

## Tests

The examples are user-facing demonstrations. CI smoke coverage lives separately
under `tests/` and can be run with:
```console
python tests/smoke.py
python tests/smoke.py --toolchain clang --level core
```

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
