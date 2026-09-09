# Command reference

[Documentation index](README.md) · [Usage examples](usage.md)

## Table of contents

- [Command syntax](#command-syntax)
- [Commands](#commands)
- [Build options](#build-options)
- [Directory and target selection](#directory-and-target-selection)
- [Help and diagnostics](#help-and-diagnostics)

## Command syntax

```text
bspm <command> [arguments] [options]
```

Angle brackets indicate a required value; square brackets indicate an optional
argument. Supply option values as separate arguments, for example `--jobs 4`.
Quote paths or constraints containing spaces.

## Commands

| Command | Purpose |
| --- | --- |
| `bspm init <dir> [--project]` | Create a folder project, or a configured project with an `app` target |
| `bspm build [dir\|target\|all] [options]` | Compile and link the selected target and its dependencies |
| `bspm run [dir\|target] [-v]` | Run an already-built executable |
| `bspm clean [dir\|target\|all] [-v]` | Remove each selected target's `build/` directory |
| `bspm graph [dir\|target\|all] [options]` | Print discovered targets, sources, modules, and imports |
| `bspm compile-commands [dir\|target\|all] [options]` | Write `compile_commands.json` without compiling |
| `bspm doctor [-c <compiler>] [-v]` | Check compiler and linker tool availability |
| `bspm install [-C <project-dir>]` | Restore locked dependencies, or resolve and create a lockfile |
| `bspm update [package] [-C <project-dir>]` | Refresh all dependencies or one package's dependency closure |
| `bspm add <package> [constraint] [-C <project-dir>]` | Add a direct requirement and resolve its dependencies |
| `bspm remove <package> [-C <project-dir>]` | Remove a direct requirement and prune unreachable packages |
| `bspm search [query] [-C <project-dir>]` | Search package names in the selected registry |
| `bspm info <package> [-C <project-dir>]` | Show available versions and package metadata |
| `bspm outdated [-C <project-dir>]` | Compare locked, compatible, and latest stable versions |
| `bspm package validate [package-dir\|package.bspm]` | Validate a registry package version and its dependencies |
| `bspm registry validate [registry-dir]` | Validate a registry, its baselines, and all package versions |
| `bspm help [command]` | Show general or command-specific help |
| `bspm version` | Show the bspm version |

`graph` and `compile-commands` accept the build options below. `run` and
`clean` accept `-v`, but do not accept build flags or `--project`. `init` also
accepts `-v`. Validation paths default to the current directory.

Package workflows are covered in [using packages](packages.md); validation
is covered in [registry authoring](registries.md#validate-packages-and-registries).

## Build options

| Option | Meaning |
| --- | --- |
| `-c <compiler>` | Select `g++` (default), `clang++`, or `msvc` |
| `-o <name>`, `--output <name>` | Set the output filename |
| `--bin` | Build an executable (default) |
| `--lib` | Build a static library |
| `--shared` | Build a shared library |
| `--debug` | Enable debug flags (default) |
| `--release` | Enable optimization and define `NDEBUG` |
| `-j <count>`, `--jobs <count>` | Set the positive compilation job count (default: `1`) |
| `--dry-run` | Print compile and link commands without executing them |
| `--explain` | Print the discovered graph before building |
| `--project` | Interpret the directory argument as a root containing a non-empty `bspm.build` |
| `--cxxflag <flag>` | Append a compiler flag; repeatable |
| `--ldflag <flag>` | Append a linker flag; repeatable |
| `--define <name[=value]>` | Add a preprocessor definition; repeatable |
| `--include <dir>` | Add an include directory; repeatable |
| `--public-include <dir>` | Add an include directory and export it to dependents; repeatable |
| `--source <path>` | Restrict discovery to a file or directory; repeatable |
| `--exclude <path>` | Exclude a file or directory from discovery; repeatable |
| `-v` | Print commands during the build |

Compiler aliases are `gcc` for `g++`, `clang` for `clang++`, and `cl` or
`cl.exe` for `msvc`. Compiler selection takes one of these names, not an
arbitrary executable path. Flags passed with `--cxxflag` and `--ldflag` must
be appropriate for the selected toolchain.

Without an explicit output name, a plain executable target uses `a.exe` on
Windows or `a.out` on Unix-like systems. See
[output naming](usage.md#build-executables-and-libraries) for library names and
names supplied with `-o`.

`--depends <target>` is a `bspm.build` target option, not a CLI build flag.
See [target dependencies](projects.md#target-dependencies).

## Directory and target selection

| Context | Example | Selection |
| --- | --- | --- |
| Plain folder | `bspm build` | Current folder |
| Explicit folder | `bspm build examples/nested-sources` | That folder's sources |
| Configured project root | `bspm build` | Default target and its dependencies |
| Configured project root | `bspm build app` | Named `app` target and its dependencies |
| Configured project root | `bspm build all` | Every configured target |
| Outside a configured project | `bspm build examples/project-config --project` | That project's default target and its dependencies |
| Package command | `bspm install -C examples/simple` | Dependencies of the specified project |

`--project` works with `build`, `graph`, and `compile-commands`. It selects the
project's default target. Change into the project root to select a named
target or `all`, or to use configured targets with `run` and `clean`:

```console
cd examples/project-config
bspm build all
bspm graph app
bspm compile-commands all
bspm run app
bspm clean all
```

`-C` is supported by the package commands listed above, rather than as a
global option. A folder with `bspm.deps` and no `bspm.build` is treated as an
[implicit package-consuming application](packages.md#add-a-package-to-a-folder-project).

## Help and diagnostics

```console
bspm help
bspm help build
bspm help graph
bspm help compile-commands
bspm help install
bspm help package
bspm help registry
bspm doctor -c g++
bspm version
```

Running `bspm` without a command also prints the general help.
