# Registry authoring

[Documentation index](README.md) · [Using packages](packages.md)

## Table of contents

- [Registry layout](#registry-layout)
- [Package metadata and build recipes](#package-metadata-and-build-recipes)
- [Defaults and exported targets](#defaults-and-exported-targets)
- [Validate packages and registries](#validate-packages-and-registries)

## Registry layout

A registry is a Git repository containing registry metadata, baseline package
versions, and versioned package recipes. A minimal layout is:

```text
my-registry/
├── registry.bspm
├── baseline.bspm
└── packages/
    └── answer/
        └── 1.0.0/
            ├── package.bspm
            └── build.bspm
```

Name the registry and declare its format in `registry.bspm`:

```text
registry my-registry
format 1
```

Select the version used by an unconstrained `require answer` in `baseline.bspm`:

```text
package answer 1.0.0#0
```

Commit the registry contents before consuming them from a project: lockfiles
pin a registry Git commit. See [registry selection](packages.md#choose-a-registry)
for configuring a consumer.

## Package metadata and build recipes

Each version has a `packages/<name>/<version>/package.bspm` file. For example:

```text
package answer
version 1.0.0
package-revision 0

source git <source-repository-url>
source-revision <full-git-commit>

build registry build.bspm

default-target answer
export-target answer
```

Replace `<source-repository-url>` and `<full-git-commit>` with the package's
Git repository and full source commit hash. The package name and version must
match their directory names. `package-revision` identifies the recipe revision
separately from the upstream source version.

`build registry build.bspm` loads a recipe beside `package.bspm`. To use a
recipe from the pinned source checkout instead, specify `build source bspm.build`.
Both recipes use the normal [project configuration syntax](projects.md).

For a source repository containing `src/answer.cppm` and an `include/`
directory, a registry-owned `build.bspm` can contain:

```text
project answer
default answer

target answer . --lib -o answer \
    --source src/answer.cppm \
    --public-include include
```

The target path `.` refers to the package source checkout, even when the
recipe itself lives in the registry. `--public-include` makes the source's
`include/` directory available to consumers.

Packages can declare transitive dependencies with `require` directives in
`package.bspm`, using the same [constraints](packages.md#declare-requirements-and-version-constraints)
as `bspm.deps`. Their build targets choose which dependencies to consume with
`--depends @package` or `--depends package::target`.

## Defaults and exported targets

Recipe target names are local to the package. On import, they become namespaced:
the `answer` target above becomes `answer::answer`.

`default-target answer` selects the target used by the shorter `@answer`
dependency form. When metadata omits `default-target`, `bspm` uses the validated
default from the build recipe, including the automatic default of a
single-target recipe.

Repeated `export-target` directives restrict which recipe targets consumers
can reference. The effective default must also be exported when explicit
exports are present. Package-internal dependencies can still reference
private targets. A package with no `export-target` directives exports every
recipe target for compatibility.

For the package above, these are alternative consumer target definitions:

```text
target app app --bin -o demo --depends @answer
```

```text
target app app --bin -o demo --depends answer::answer
```

`bspm` validates defaults and exports when importing the package, then expands
`@answer` before planning the project.

## Validate packages and registries

Validate a package version from the registry root, using either its directory
or metadata file:

```console
bspm package validate packages/answer/1.0.0
bspm package validate packages/answer/1.0.0/package.bspm
```

Package validation finds the containing registry and checks:

- Metadata names and versions match the package's directory location.
- The exact version and its complete dependency graph can be resolved.
- Every pinned source commit can be checked out into a temporary directory.
- Source-owned or registry-owned recipes, defaults, exports, and target references are valid.
- Package and target dependencies contain no cycles.

Validate every package version and the registry configuration for CI:

```console
bspm registry validate .
bspm registry validate path/to/my-registry
```

Registry validation additionally checks `registry.bspm`, every baseline
reference, every indexed package version, and each version's independently
resolvable dependency graph. It exits nonzero on the first error and prints
package and version counts on success.

Validation leaves the registry unchanged and removes its temporary source
cache afterward. Git and access to the pinned sources are required. These
commands validate recipes structurally; build a consumer project as well to
verify that package source code compiles and links.
