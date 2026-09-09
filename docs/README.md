# bspm documentation

[Project README](../README.md) · [Example catalog](../examples/README.md)

`bspm` builds C++ folder targets, optionally groups them into projects, and
resolves source packages from a registry. Start with
[building bspm](../README.md#build-from-source) and the
[quick start](../README.md#quick-start) if this is your first time using it.

## Guides

| I want to… | Read |
| --- | --- |
| Build and run C++ code, choose a compiler, or set up my editor | [Using bspm](usage.md) |
| Look up a command or flag | [Command reference](cli.md) |
| Define several targets and their dependencies | [Project configuration](projects.md) |
| Add, install, inspect, or update packages | [Using packages](packages.md) |
| Write a package recipe or validate a registry | [Registry authoring](registries.md) |
| Try working source examples | [Example catalog](../examples/README.md) |
| Run the smoke tests | [Development and tests](../README.md#development-and-tests) |

## Project files at a glance

| File or directory | Purpose |
| --- | --- |
| `bspm.build` | Optional target definitions and shared build options |
| `bspm.deps` | Direct package requirements and an optional registry selection |
| `bspm.lock` | Generated registry, package recipe, and source revision pins; commit this with `bspm.deps` |
| `build/` | Generated outputs for each target, organized by compiler and build mode |
| `.bspm/` | Generated registry and package caches; keep out of source control |
| `compile_commands.json` | Compilation database generated on request for editor tooling |

Commands assume `bspm` is on your `PATH`. Paths beginning with `examples/` are
relative to the repository root; placeholders such as `<dir>` should be
replaced with your own values.
