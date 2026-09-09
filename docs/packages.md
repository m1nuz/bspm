# Using packages

[Documentation index](README.md) · [Registry authoring](registries.md)

## Table of contents

- [Add a package to a folder project](#add-a-package-to-a-folder-project)
- [Use packages in configured projects](#use-packages-in-configured-projects)
- [Declare requirements and version constraints](#declare-requirements-and-version-constraints)
- [Choose a registry](#choose-a-registry)
- [Install and lock dependencies](#install-and-lock-dependencies)
- [Update and remove packages](#update-and-remove-packages)
- [Search and inspect packages](#search-and-inspect-packages)
- [Resolution and caches](#resolution-and-caches)

## Add a package to a folder project

Both simple folders and configured projects can consume source packages from
the central [bspm registry](https://github.com/m1nuz/bspm-registry) or a local
registry. Git is required when fetching a registry or package source.

Create a project and add a dependency:

```console
bspm init hello-fmt
cd hello-fmt
bspm add fmt
bspm build
bspm run
```

`add` writes the requirement to `bspm.deps` and resolves its dependencies.
For a project without `bspm.build`, every direct requirement is attached to
an implicit `app` target through the package's default target. You can then
use the package's headers or modules in your source. The
[simple example](../examples/simple) demonstrates this with `fmt::format`.

The implicit application covers the whole project folder and excludes
`.bspm/` from source discovery.

## Use packages in configured projects

Configured projects choose package dependencies per target. Declare a
requirement in `bspm.deps`:

```text
require fmt 12.2.0#0
```

Then reference the package from `bspm.build`:

```text
project demo
default app

target app app --bin -o demo --depends @fmt
```

`@fmt` selects the package's default target. Use the explicit namespaced form
`fmt::fmt` to select a particular exported target:

```text
target app app --bin -o demo --depends fmt::fmt
```

These are alternative target definitions. Package defaults and exports are
defined by the [package recipe and metadata](registries.md#defaults-and-exported-targets).

From the configured project root:

```console
bspm install
bspm build app
bspm run app
```

See the [registry-package example](../examples/registry-package) for a complete
application using an exact fmt package version.

## Declare requirements and version constraints

`bspm.deps` contains one `require` directive per direct package dependency.
Choose the constraint that matches your intended update policy:

| Requirement | Selection |
| --- | --- |
| `require fmt` | Registry baseline exactly |
| `require fmt 12.2.0` | Exact upstream version |
| `require fmt 12.2.0#0` | Exact upstream version and package recipe revision |
| `require fmt ^12.0.0` | Compatible releases from `12.0.0` up to, but excluding, `13.0.0` |
| `require fmt ~12.2.0` | Releases from `12.2.0` up to, but excluding, `12.3.0` |
| `require spdlog >=1.14.0 <2.0.0` | Intersection of the given comparisons |

The fmt rows are alternatives; do not declare the same package more than
once. Versions use SemVer `major.minor.patch`, including prerelease and build
identifiers. Supported comparisons are `<`, `<=`, `>`, and `>=`; package
metadata uses the same syntax for transitive requirements.

When adding a new requirement from the CLI, quote a compound constraint so
the shell passes it as one argument:

```console
bspm add spdlog ">=1.14.0 <2.0.0"
```

For example, a new fmt requirement can instead be added with
`bspm add fmt ^12.0.0` or `bspm add fmt 12.2.0#0`.

An omitted constraint selects the registry baseline rather than the latest
version. Exact pins remain fixed during updates. To change an existing
requirement's constraint, edit `bspm.deps` and run `bspm update`.

## Choose a registry

By default, `bspm` uses the central registry. To use another Git registry or
a local registry checkout, place one `registry` directive before the
requirements in `bspm.deps`:

```text
registry ../my-registry
require answer 1.0.0#0
```

Relative registry paths are resolved from the project root. Use double quotes
around paths containing spaces. A `#` begins a comment at the start of a
token, so `1.0.0#0` remains a single version token.

After changing registries, use a full `bspm update` to resolve against the new
registry and rewrite the lockfile.

## Install and lock dependencies

Install dependencies without building targets:

```console
bspm install
bspm install -C path/to/project
```

With an existing `bspm.lock`, `install` restores and validates the locked
revisions exactly. Without a lockfile, it refreshes the registry, resolves
`bspm.deps`, and creates the lockfile. `build`, `graph`, and `compile-commands`
also resolve dependencies when planning a project.

The lockfile pins the registry Git commit, each exact
`version#package-revision`, and each package source commit. Commit
`bspm.deps` and `bspm.lock`, and ignore `.bspm/` and generated `build/`
directories. New projects receive ignore rules automatically; `init` waits
for successful dependency resolution before creating `bspm.lock`.

## Update and remove packages

Refresh all dependencies or only one direct package and its transitive
dependency closure:

```console
bspm update
bspm update fmt
bspm update fmt -C path/to/project
```

A full update reconsiders every dependency. A named update chooses the newest
compatible versions within the named package's dependency closure, preserves
unrelated locked packages, and moves the registry pin forward. All updates
respect the requirements in `bspm.deps`, including exact version pins.

Add or remove requirements from another directory:

```console
bspm add fmt -C path/to/project
bspm remove fmt -C path/to/project
```

`add` preserves the existing manifest and appends one requirement. `remove`
deletes the matching logical `require` directive and prunes packages that
are no longer reachable. For a configured project, remove its `--depends`
references to the package before running `bspm remove`.

Both commands update `bspm.deps` and `bspm.lock` transactionally. A resolution
or project-validation failure restores the manifest and leaves the previous
lock unchanged.

## Search and inspect packages

Inspect the registry selected by the project's `bspm.deps`, falling back to
the central registry:

```console
bspm search
bspm search format
bspm info fmt
bspm outdated
bspm outdated -C path/to/project
```

| Command | Shows |
| --- | --- |
| `search [query]` | Case-insensitive package-name matches, registry baseline, newest stable version, and current lock |
| `info <package>` | Every available version, its source, build recipe, exported targets, and requirements |
| `outdated` | Current lock, newest compatible whole-graph solution, and newest stable registry version |

These commands leave `bspm.deps` and `bspm.lock` unchanged. `outdated` refreshes
the registry cache for its comparison. Its compatible and latest columns
help distinguish upgrades allowed by the current constraints from releases
that require changing a constraint.

## Resolution and caches

`bspm` resolves one compatible version per package, preferring newer compatible
versions and backtracking when transitive constraints conflict. It validates
`package.bspm`, checks out the full Git `source-revision`, and imports either
the source-owned or registry-owned build recipe into the project graph.

| Directory | Contents |
| --- | --- |
| `.bspm/packages/` | Package source checkouts and their build outputs |
| `.bspm/registry/` | Cached remote registry |

Subsequent installs and build planning restore and validate locked revisions.
Archive sources and binary packages are not yet implemented.
