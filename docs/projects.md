# Project configuration

[Documentation index](README.md) · [Command reference](cli.md)

## Table of contents

- [Create a configured project](#create-a-configured-project)
- [Define targets](#define-targets)
- [Select targets](#select-targets)
- [Configuration syntax](#configuration-syntax)
- [Target dependencies](#target-dependencies)
- [Public include directories](#public-include-directories)

## Create a configured project

Configuration is optional. Build a folder directly when it contains one
target. Add a non-empty `bspm.build` file when you want named targets, shared
options, or dependencies between several folders.

```console
bspm init hello-project --project
```

The generated layout is:

```text
hello-project/
├── .gitignore
├── bspm.build
├── bspm.deps
└── app/
    └── main.cpp
```

`bspm.build` defines `app` as the default executable target. `bspm.deps` starts
empty; `bspm.lock` is created only after dependency resolution succeeds.

## Define targets

For an application using a static library and a shared library, arrange the
sources like the [project-config example](../examples/project-config):

```text
demo/
├── bspm.build
├── app/
│   └── main.cpp
└── libs/
    ├── math/
    │   └── math.cppm
    └── greeting/
        └── greeting.cppm
```

Define each folder target in `bspm.build`:

```text
project demo
default app
common --define DEMO_TRACE=1

target math libs/math --lib -o math
target greeting libs/greeting --shared -o greeting
target app app --bin -o demo --depends math --depends greeting
```

Target paths are relative to the project root. Include paths and source or
exclude filters are relative to the individual target root.

## Select targets

Run these commands from the directory containing `bspm.build`:

```console
bspm build
bspm build app
bspm build all
bspm graph all
bspm compile-commands all
bspm run
bspm run app
bspm clean app
bspm clean all
```

Bare `build`, `run`, and `clean` select the default target. `build app` also
builds its dependencies; `clean app` removes only that target's `build/`
directory. Use `clean all` to clean every configured target.

From outside the project, select its default target with `--project`:

```console
bspm build examples/project-config --project
bspm graph examples/project-config --project
bspm compile-commands examples/project-config --project
```

For `run`, `clean`, a named target, or `all`, change into the project root.
Passing a directory to `build` without `--project` treats it as a folder
target rather than loading that directory's target configuration.

## Configuration syntax

| Directive | Meaning |
| --- | --- |
| `project <name>` | Name the project |
| `default <target>` | Choose the default target |
| `common [options]` | Apply shared build options to every target |
| `target <name> <path> [options]` | Define a named folder target |

A single-target recipe automatically uses that target as its default if
`default` is omitted. A project with multiple targets needs an explicit
default for commands that do not name a target.

Options are applied in this order: `common`, target-specific options, then
CLI options. Later scalar choices, such as compiler or build mode, override
earlier ones. Repeated `--cxxflag`, `--ldflag`, `--define`, `--include`,
`--public-include`, `--source`, and `--exclude` options accumulate.

Target options accept the [build flags](cli.md#build-options) plus repeated
`--depends <target>` entries. Use double quotes around values containing
spaces. A `#` starts a comment when it begins a token; a package revision such
as `12.2.0#1` remains part of its token. A trailing `\` continues a directive
on the next line:

```text
# A target with a source filter and a path containing spaces.
target codegen "tools/code gen" --bin -o codegen \
    --source src \
    --exclude src/generated \
    --include include
```

These continuations are configuration-file syntax. CLI examples use single
lines so they can be used across shells.

## Target dependencies

`--depends` builds dependencies first, makes their module artifacts available
to the consumer, and adds library outputs to the consumer's link step:

```text
target app app --bin -o demo --depends math --depends greeting
```

Registry packages use namespaced targets, or the shorter `@package` form to
select a package's default target:

```text
target app app --bin -o demo --depends @fmt
```

Declare the package in `bspm.deps` as well. See [using packages](packages.md)
for a complete consumer example.

## Public include directories

`--include` adds a directory to the current target.
`--public-include` adds it to the target and exports it transitively to
targets that depend on it:

```text
target math libs/math --lib -o math --public-include include
target app app --bin -o demo --depends math
```

Here both targets receive `libs/math/include` on their include path. An
application depending on another library that depends on `math` also receives
that path.
