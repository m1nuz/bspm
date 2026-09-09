# bspm

[![build](https://github.com/m1nuz/bspm/actions/workflows/smoke.yml/badge.svg?event=push)](https://github.com/m1nuz/bspm/actions/workflows/smoke.yml)

An experimental, single-file tool for building C++ projects with modules.

`bspm` is dependency-free: compile [`bspm.cpp`](bspm.cpp) with a C++23 compiler
and use the resulting executable. Build a folder directly, add an optional
`bspm.build` file for multiple targets, or use `bspm.deps` to consume registry
packages.

## Table of contents

- [Features](#features)
- [Build from source](#build-from-source)
- [Quick start](#quick-start)
- [Documentation](#documentation)
- [Examples](#examples)
- [Development and tests](#development-and-tests)
- [License](#license)

## Features

- Recursive source discovery, C++ modules and partitions, and header units.
- Executable, static library, and shared library targets with GCC, Clang, or MSVC.
- Debug and release profiles, incremental rebuilds, and parallel compilation.
- Optional multi-target projects with module, library, and public include dependencies.
- Source packages from Git registries, version constraints, and reproducible lockfiles.
- Build graph inspection, compiler diagnostics, and compilation databases for editors.

## Build from source

Use a C++23 compiler and standard library with support for `<print>` and
`<format>`. Module and standard-library import support also depends on the
toolchain used to build your projects. Git is needed when fetching registry
packages; Python is needed only to run the smoke tests.

From the repository root, choose one build command.

With GCC:

```console
g++ -std=c++23 -Wpedantic -Wall -Wextra -Werror bspm.cpp -O2 -lstdc++exp -o bspm
```

On Windows, use `-o bspm.exe` instead of `-o bspm`.

With Clang:

```console
clang++ -std=c++23 -Wpedantic -Wall -Wextra -Werror bspm.cpp -O2 -o bspm
```

With MSVC, run this in a Visual Studio Developer terminal, or after running
`VsDevCmd.bat`:

```console
cl /std:c++latest /EHsc /nologo bspm.cpp /Febspm.exe
```

Put the executable in a directory on your `PATH` to use `bspm` as shown below.
To invoke it directly from the repository root, use `./bspm` on Unix-like
systems or `.\bspm.exe` in PowerShell.

## Quick start

Create, build, and run a folder project:

```console
bspm doctor
bspm init hello
bspm build hello
bspm run hello
```

The generated program prints `Hello, world!`. It uses `import <print>;`, so
your compiler and standard library need header-unit support. Builds default
to GCC and debug mode; select another compiler with `-c clang++` or `-c msvc`.

`init` creates the source file, an empty dependency manifest, and ignore rules:

```text
hello/
├── .gitignore
├── bspm.deps
└── main.cpp
```

For a project with named targets, create a starter configuration:

```console
bspm init hello-project --project
cd hello-project
bspm build
bspm run
```

This adds `bspm.build` and places the program in `app/main.cpp`. See the
[usage guide](docs/usage.md) for build options and the
[project guide](docs/projects.md) for adding library targets.

## Documentation

Start with the [documentation index](docs/README.md), or jump to a guide:

| Guide | Covers |
| --- | --- |
| [Using bspm](docs/usage.md) | Build, run, clean, source discovery, modules, and editor setup |
| [Command reference](docs/cli.md) | Commands, accepted options, defaults, and directory selection |
| [Project configuration](docs/projects.md) | `bspm.build`, named targets, shared options, and dependencies |
| [Using packages](docs/packages.md) | `bspm.deps`, version constraints, lockfiles, and package commands |
| [Registry authoring](docs/registries.md) | Package recipes, exported targets, and validation |

Built-in help is also available:

```console
bspm help
bspm help build
```

## Examples

The [example catalog](examples/README.md) lists build and run commands for
each example, including nested sources, module partitions, libraries,
configured projects, and registry packages.

Try a project with nested source and module folders from the repository root:

```console
bspm build examples/nested-sources
bspm run examples/nested-sources
```

## Development and tests

The implementation lives in [`bspm.cpp`](bspm.cpp). User-facing demonstrations
live in [`examples/`](examples/README.md); automated smoke coverage lives in
[`tests/smoke.py`](tests/smoke.py).

Run the full GCC suite or the core checks for a selected toolchain:

```console
python tests/smoke.py
python tests/smoke.py --toolchain clang --level core
python tests/smoke.py --toolchain msvc --level core
```

The runner builds `bspm` in a temporary workspace. `--toolchain` accepts `gcc`,
`clang`, or `msvc`; `--level` accepts `core` or `full`. Defaults are `gcc` and
`full`. See the [CI workflow](.github/workflows/smoke.yml) for tested environments
and the [changelog](CHANGELOG.md) for project history.

## License

`bspm` is available under the [MIT License](LICENSE).
