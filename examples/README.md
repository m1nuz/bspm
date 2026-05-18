# Examples

Each example is a small user-facing target that demonstrates one `bspm`
capability. Automated smoke coverage lives separately under `tests/`.

| Example | Demonstrates | Build | Run |
| --- | --- | --- | --- |
| `module-dependencies` | Multiple module interfaces, module dependency sorting, and header-unit imports | `bspm build examples/module-dependencies` | `bspm run examples/module-dependencies` |
| `header-unit` | Minimal executable with a standard-library header unit | `bspm build examples/header-unit` | `bspm run examples/header-unit` |
| `nested-sources` | Recursive source discovery with nested `src/` and `modules/` folders | `bspm build examples/nested-sources` | `bspm run examples/nested-sources` |
| `std-module` | Standard-library module import with `import std;` | `bspm build examples/std-module` | `bspm run examples/std-module` |
| `static-library` | Static library target | `bspm build examples/static-library --lib -o math` | Not runnable |
| `shared-library` | Shared library target | `bspm build examples/shared-library --shared -o greeting` | Not runnable |
| `project-config` | Optional `bspm.build` project config with cross-target module and link dependency consumption | `cd examples/project-config && bspm build` | `cd examples/project-config && bspm run` |
| `module-partitions` | Primary interface, exported partition, internal partition, and implementation unit | `bspm build examples/module-partitions` | `bspm run examples/module-partitions` |

## Notes

- Add `-c g++`, `-c clang++`, or `-c msvc` to choose a compiler explicitly.
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
- Generated files are written under each target's `build/` directory.
