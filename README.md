# bspm

[![build](https://github.com/m1nuz/bspm/actions/workflows/smoke.yml/badge.svg?event=push)](https://github.com/m1nuz/bspm/actions/workflows/smoke.yml)

Experimental single-file tool for building C++ with Modules.

`bspm` is intentionally dependency-free: compile `bspm.cpp` with a C++23
compiler and use the resulting executable.

## Table of Contents

- [How to use](#how-to-use)
  - [Example](#example)
- [Optional project config](#optional-project-config)
- [Tests](#tests)
- [How to build](#how-to-build)

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

`bspm` scans target directories recursively for `.cpp`, `.cc`, `.cxx`, `.c++`,
`.cppm`, `.ixx`, and `.mpp` files, so targets may use nested source folders:
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
Importable module units are compiled in dependency levels, so independent module
interfaces in the same level can also compile in parallel.

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
- `--public-include <dir>` adds an include directory to a target and exports it
  transitively to targets that depend on it.
- `--source` restricts discovery to one file or directory relative to the target
  root, and `--exclude` removes matching files or directories from discovery.
- `--depends` builds dependencies first, exposes their module artifacts to the
  consumer target, and adds library outputs to the consumer link step.
- `#` starts a comment when it begins a token, while package revisions such as
  `12.2.0#1` remain part of their version token. Quoted paths such as
  `"tools/code gen"` are allowed.

`bspm init <dir>` keeps config out of the simple path. Use
`bspm init <dir> --project` when you want a starter `bspm.build` file and a
configured `app` target.

## Registry packages

Both simple folders and configured projects can consume source-based packages from the central
[`bspm-registry`](https://github.com/m1nuz/bspm-registry) or a local registry.
Declare requirements in the project's `bspm.deps` file:

```text
# Uses the central registry baseline.
require fmt

# Accept compatible 12.x releases.
require fmt ^12.0.0

# Intersect whitespace-separated comparisons.
require spdlog >=1.14.0 <2.0.0

# Or pin the upstream and package-recipe versions exactly.
require fmt 12.2.0#0
```

Package versions follow SemVer `major.minor.patch`, including standard
prerelease and build identifiers. Requirements accept an exact version,
`version#package-revision`, caret ranges (`^1.2.3`), tilde ranges (`~1.2.3`),
or an intersection of `<`, `<=`, `>`, and `>=` comparisons. Compound ranges
passed to `bspm add` should be quoted so the shell treats them as one argument:

```console
bspm add spdlog ">=1.14.0 <2.0.0"
```

An omitted constraint continues to select the registry baseline exactly.
Package metadata uses the same syntax for transitive requirements.

To use another Git registry or a local registry checkout, add one `registry`
directive before the requirements:

```text
registry ../my-registry
require answer 1.0.0#0
```

Registry packages expose namespaced targets. A consumer can always use the
explicit `package::target` name in `bspm.build`:

```text
project demo
default app

target app app --bin -o demo --depends fmt::fmt
```

A package can also declare its public interface in `package.bspm`:

```text
build registry build.bspm

default-target fmt
export-target fmt
```

`default-target` overrides the build recipe's default for the shorter `@package`
dependency form, while repeated `export-target` directives restrict consumers
to the listed recipe targets:

```text
target app app --bin -o demo --depends @fmt
```

The names in these directives are local build-recipe target names; `bspm`
validates them when importing the package and expands `@fmt` to `fmt::fmt` before
planning the project. When `package.bspm` omits `default-target`, `bspm` uses the
validated `default` from the package's build recipe, including the automatic
default of a single-target recipe. The effective default must also be exported
when explicit exports are present. Package-internal dependencies may still use
private targets. For compatibility, a package with no `export-target` directives
exports every recipe target.

For a simple project without `bspm.build`, every direct requirement is
automatically attached to an implicit `app` target through its package default:

```console
cd examples/simple
bspm add fmt
bspm build
bspm run
```

This implicit target covers the whole project folder and excludes `.bspm/` from
source discovery. Configured projects continue to choose package dependencies
per target with `--depends @package` or `--depends package::target`.

When `build`, `graph`, or `compile-commands` plans the project, `bspm` resolves
a single compatible version for every package, validates `package.bspm`, checks out its full Git
`source-revision`, loads either its source-owned or registry-owned build recipe,
and adds the recipe targets to the normal project dependency graph. Package
sources and their build outputs are cached under `.bspm/packages/`; a remote
registry is cached under `.bspm/registry/`. Git is required when a package source
or registry must be fetched.

After the first successful resolution, `bspm` writes `bspm.lock`. The lockfile
pins the registry Git commit, every exact `version#package-revision`, and every
package source commit. Later `install`, `build`, `graph`, and `compile-commands`
operations restore and validate those revisions before using the registry. Commit both
`bspm.deps` and `bspm.lock`, but ignore `.bspm/`; newly initialized projects
receive an appropriate `.gitignore` automatically. `bspm init` creates
`bspm.deps`, but deliberately waits for a successful resolution before creating
`bspm.lock`.

Install dependencies without building targets:

```console
bspm install
bspm install -C path/to/project
```

`install` uses an existing lockfile exactly. If one does not exist, it refreshes
the registry, resolves `bspm.deps`, and creates the lockfile.

Refresh and rewrite the complete lockfile, or update one direct requirement and
its transitive dependency closure:

```console
bspm update
bspm update fmt
bspm update fmt -C path/to/project
```

A named update selects the newest compatible versions in that package's
transitive closure while preserving unrelated locked packages and moving the
registry pin forward. Use a full `bspm update` after changing registries or when
every dependency should be reconsidered. Resolution prefers newer compatible
versions and backtracks when their transitive constraints conflict.

Add or remove direct requirements without editing `bspm.deps` manually:

```console
bspm add fmt
bspm add fmt ^12.0.0
bspm add fmt 12.2.0#0
bspm add fmt -C path/to/project
bspm remove fmt
```

`add` preserves the existing manifest, appends one requirement, and resolves
that package's dependency closure. `remove` deletes only the matching logical
`require` directive and prunes packages that are no longer reachable. Both
commands update `bspm.deps` and `bspm.lock` transactionally: a resolution or
project-validation failure restores the manifest and leaves the previous lock
unchanged. These commands work with or without `bspm.build`. A package cannot be
removed while a configured target still depends on one of its namespaced targets.

Inspect the project-selected registry without changing `bspm.deps` or
`bspm.lock`:

```console
bspm search
bspm search format
bspm info fmt
bspm outdated
bspm outdated -C path/to/project
```

`search` performs a case-insensitive package-name search and shows each match's
registry baseline, newest stable version, and current lock. `info` lists every
available version together with its source, build recipe, target exports, and
package requirements. Both commands use the registry selected by the project's
`bspm.deps`, falling back to the central registry.

`outdated` compares three exact package references: the current lock, the newest
whole-graph solution allowed by all direct and transitive constraints, and the
newest stable registry version. This makes it visible when `bspm update` can
upgrade a package and when the latest version is blocked by a constraint. It
refreshes the registry cache for the comparison but never rewrites the lockfile.

Validate a package recipe before publishing it, or validate an entire registry
checkout for CI:

```console
bspm package validate packages/fmt/12.2.0
bspm package validate packages/fmt/12.2.0/package.bspm
bspm registry validate .
bspm registry validate path/to/bspm-registry
```

`package validate` finds the containing registry, verifies that the metadata's
name and version match its `packages/<name>/<version>/package.bspm` location,
resolves that exact version and its complete dependency graph, checks out every
pinned source commit into a temporary directory, and validates source-owned or
registry-owned build recipes, defaults, exports, target references, and package
or target cycles.

`registry validate` additionally checks `registry.bspm`, every baseline reference,
every indexed package version, and every version's independently resolvable
dependency graph. It exits nonzero on the first error and prints package/version
counts on success, making it suitable for registry pull-request CI. Validation
does not modify the registry and removes its temporary source cache afterward.
It validates recipes structurally but does not compile package source code.

Exact versions explicitly pinned in `bspm.deps` remain fixed during updates.
Archive sources and binary packages are not yet implemented. Registry recipes
may use a trailing `\` to continue a directive on the next line.

Generated files are kept under a profile-specific build directory:
```text
<dir>/build/gcc-debug/
<dir>/build/gcc-release/
<dir>/build/clang-debug/
<dir>/build/msvc-debug/
```

For GCC and Clang builds, `bspm` also writes compiler dependency files next to
object files and uses them for incremental rebuild checks, so changes to quoted
or included headers rebuild the affected source files.

GCC header units such as `import <print>;` are cached under `gcm.cache` and
reused while the compiler command remains unchanged. Changing compiler flags or
removing the generated `.gcm` artifact rebuilds the header unit and its importer.

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
bspm help search
bspm help info
bspm help outdated
bspm help package
bspm help registry
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
