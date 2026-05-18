# Examples

Each example is a small target that demonstrates one `bspm` capability.

| Example | Demonstrates | Build | Run |
| --- | --- | --- | --- |
| `test1` | Multiple module interfaces, module dependency sorting, and header-unit imports | `bspm build examples/test1` | `bspm run examples/test1` |
| `test2` | Minimal executable with a standard-library header unit | `bspm build examples/test2` | `bspm run examples/test2` |
| `test3` | Recursive source discovery with nested `src/` and `modules/` folders | `bspm build examples/test3` | `bspm run examples/test3` |
| `test4` | Standard-library module import with `import std;` | `bspm build examples/test4` | `bspm run examples/test4` |
| `test5` | Static library target | `bspm build examples/test5 --lib -o math` | Not runnable |
| `test6` | Shared library target | `bspm build examples/test6 --shared -o greeting` | Not runnable |
| `test7` | Optional `bspm.build` project config with cross-target module and link dependency consumption | `cd examples/test7 && bspm build` | `cd examples/test7 && bspm run` |
| `test8` | Primary interface, exported partition, internal partition, and implementation unit | `bspm build examples/test8` | `bspm run examples/test8` |

## Notes

- Add `-c g++`, `-c clang++`, or `-c msvc` to choose a compiler explicitly.
- `test3` is the example for nested source trees:
  ```text
  test3/
  ├── modules/
  │   └── hello.cppm
  └── src/
      └── main.cpp
  ```
- `test4` may need compiler-specific standard-library module support to be available.
- `test5` produces `libmath.a` with GCC/Clang or `math.lib` with MSVC.
- `test6` produces `greeting.dll` on Windows or `libgreeting.so` on Unix-like systems.
- `test7` defines `math`, `greeting`, and `app` targets in `bspm.build`; bare `build`
  builds the default `app` target after its dependencies, reuses their module
  artifacts, links their libraries, and `build all` builds every configured target.
- `test8` shows all currently supported named-module unit forms in one target:
  ```text
  math.cppm    export module math;
  ops.cppm     export module math:ops;
  detail.cppm  module math:detail;
  math.cpp     module math;
  ```
- Generated files are written under each target's `build/` directory.
