## Unreleased

### Added
- Exact-version packages from the central or a local Git registry through `bspm.deps`.
- Deterministic `bspm.lock` generation that pins the registry commit, package recipe revisions, and source commits.
- Registry and source build recipes imported as namespaced `package::target` dependencies.
- Git source caching under `.bspm/packages` and baseline resolution from `baseline.bspm`.
- `.gitignore` scaffolding for generated `build/` and `.bspm/` caches.
- `--public-include` usage requirements propagated transitively to consuming targets.
- Backslash line continuations in `bspm.build` and registry recipes.
- C++ source discovery for `.cc`, `.cxx`, and `.c++`, plus `.ixx` and `.mpp` module interfaces.
- Offline smoke coverage for registry resolution, source checkout, public includes, compilation databases, and linking.

## [0.0.6] - 2026-08-12

### Added
- `bspm doctor` to report local compiler and linker tool availability.
- `bspm graph` to inspect discovered targets, source units, module declarations, and imports without compiling.
- `bspm build --explain` to print the discovered source/module graph before building.
- `bspm compile-commands` to write `compile_commands.json` for folder targets and projects.
- Parallel compilation for independent source files through `-j` and `--jobs`.
- User-supplied build and discovery options: `--source`, `--exclude`, `--include`, `--define`, `--cxxflag`, and `--ldflag`.
- Smoke coverage for CLI basics, dry-run plans, graph output, compiler selection, custom build options, and parallel builds.

### Changed
- GCC and Clang incremental rebuilds now track compiler depfiles for included headers.
- Parallel builds now compile independent importable module units by dependency level.
- Improved module discovery, dependency sorting, and project build planning used by both `build` and `graph`.
- Expanded README coverage for diagnostics, graph inspection, parallel builds, and custom build options.

## [0.0.5] - 2026-05-15

### Added
- Optional `bspm.build` project configuration with `project`, `default`, `common`, and `target` entries.
- Multi-target project builds with dependency ordering and `build`, `run`, and `clean` support for default targets, named targets, and `all`.
- `bspm init <dir> --project` scaffold for configured projects.
- Example project configuration with library and application targets.

## [0.0.4] - 2026-05-15

### Added
- Static library and shared library target modes through `--lib` and `--shared`.
- Recursive source discovery for nested target layouts.

### Changed
- Moved generated files into compiler/profile-specific build directories.
- Improved target output naming for binaries, static libraries, and shared libraries.

## [0.0.3] - 2026-05-15

### Added
- Build options for compiler selection, output names, target types, debug/release modes, dry runs, and verbose output.
- Command-specific help for `build`, `run`, `clean`, `init`, and `version`.

## [0.0.2] - 2025-02-06

### Changed
- use std::print/std::println

### Removed
- CMake build
- fmt dependency
- cxxopts dependency
- nlohmann/json dependency
