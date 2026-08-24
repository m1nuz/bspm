# Examples

Each example is a small user-facing target that demonstrates one `bspm`
capability. Automated smoke coverage lives separately under `tests/`.

| Example | Demonstrates | Build | Run |
| --- | --- | --- | --- |
| `simple` | An implicit single-folder project consuming fmt without `bspm.build` | `bspm build examples/simple` | `bspm run examples/simple` |
| `module-dependencies` | Multiple module interfaces, module dependency sorting, and header-unit imports | `bspm build examples/module-dependencies` | `bspm run examples/module-dependencies` |
| `header-unit` | Minimal executable with a standard-library header unit | `bspm build examples/header-unit` | `bspm run examples/header-unit` |
| `nested-sources` | Recursive source discovery with nested `src/` and `modules/` folders | `bspm build examples/nested-sources` | `bspm run examples/nested-sources` |
| `std-module` | Standard-library module import with `import std;` | `bspm build examples/std-module` | `bspm run examples/std-module` |
| `static-library` | Static library target | `bspm build examples/static-library --lib -o math` | Not runnable |
| `shared-library` | Shared library target | `bspm build examples/shared-library --shared -o greeting` | Not runnable |
| `project-config` | Optional `bspm.build` project config with cross-target module and link dependency consumption | `cd examples/project-config && bspm build` | `cd examples/project-config && bspm run` |
| `module-partitions` | Primary interface, exported partition, internal partition, and implementation unit | `bspm build examples/module-partitions` | `bspm run examples/module-partitions` |
| `registry-package` | Exact central-registry dependency, namespaced target, and public include propagation | `bspm build examples/registry-package --project` | `cd examples/registry-package && bspm run` |

## Notes

- Add `-c g++`, `-c clang++`, or `-c msvc` to choose a compiler explicitly.
- `simple` resolves fmt through `bspm.deps`, automatically attaches its default
  target to the implicit application target, and formats its output with
  `fmt::format`.
- `nested-sources` is the example for nested source trees:
  ```text
  nested-sources/
  ├── modules/
  │   └── hello.cppm
  └── src/
      └── main.cpp
  ```
- `std-module` may need compiler-specific standard-library module support to be available.
- `static-library` produces `libmath.a` with GCC/Clang or `math.lib` with MSVC.
- `shared-library` produces `greeting.dll` on Windows or `libgreeting.so` on Unix-like systems.
- `project-config` defines `math`, `greeting`, and `app` targets in `bspm.build`; bare `build`
  builds the default `app` target after its dependencies, reuses their module
  artifacts, links their libraries, and `build all` builds every configured target.
- `module-partitions` shows all currently supported named-module unit forms in one target:
  ```text
  math.cppm    export module math;
  ops.cppm     export module math:ops;
  detail.cppm  module math:detail;
  math.cpp     module math;
  ```
- `registry-package` resolves `fmt 12.2.0#0` through `bspm.deps`, writes a
  reproducible `bspm.lock`, caches its pinned Git source under `.bspm/`, and
  links the registry's `fmt::fmt` target into the example application. Its
  first build requires Git and network
  access. Run `bspm install -C examples/registry-package` to materialize the
  dependency without building, or `bspm update fmt -C examples/registry-package`
  to refresh its package closure. New requirements can be managed with
  `bspm add <package> -C examples/registry-package` and `bspm remove <package>
  -C examples/registry-package`.
- Generated files are written under each target's `build/` directory.
