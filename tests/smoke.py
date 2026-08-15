from __future__ import annotations

import argparse
import json
import os
import shlex
import shutil
import subprocess
import sys
import tempfile
import time
from dataclasses import dataclass
from pathlib import Path
from typing import Iterable, Literal


ROOT = Path(__file__).resolve().parents[1]
FIXTURES = ROOT / "tests" / "smoke"
Level = Literal["core", "full"]


@dataclass(frozen=True)
class Toolchain:
    name: str
    bspm_selector: str
    profile: str
    executable_suffix: str
    static_library_name: str
    shared_library_name: str
    app_name: str
    build_driver: str


def host_executable_suffix() -> str:
    return ".exe" if os.name == "nt" else ""


def shared_library_name_for(prefix: str) -> str:
    if os.name == "nt":
        return f"{prefix}.dll"
    if sys.platform == "darwin":
        return f"lib{prefix}.dylib"
    return f"lib{prefix}.so"


def static_library_name_for(toolchain: str, prefix: str) -> str:
    if toolchain == "msvc":
        return f"{prefix}.lib"
    return f"lib{prefix}.a"


def create_toolchain(name: str) -> Toolchain:
    executable_suffix = host_executable_suffix()
    if name == "gcc":
        return Toolchain(
            name="gcc",
            bspm_selector="g++",
            profile="gcc-debug",
            executable_suffix=executable_suffix,
            static_library_name=static_library_name_for("gcc", "math"),
            shared_library_name=shared_library_name_for("greeting"),
            app_name=f"demo{executable_suffix}",
            build_driver=os.environ.get("CXX", "g++"),
        )
    if name == "clang":
        return Toolchain(
            name="clang",
            bspm_selector="clang++",
            profile="clang-debug",
            executable_suffix=executable_suffix,
            static_library_name=static_library_name_for("clang", "math"),
            shared_library_name=shared_library_name_for("greeting"),
            app_name=f"demo{executable_suffix}",
            build_driver=os.environ.get("CXX", "clang++"),
        )
    if name == "msvc":
        return Toolchain(
            name="msvc",
            bspm_selector="msvc",
            profile="msvc-debug",
            executable_suffix=".exe",
            static_library_name=static_library_name_for("msvc", "math"),
            shared_library_name="greeting.dll",
            app_name="demo.exe",
            build_driver="cl",
        )
    raise ValueError(f"unsupported toolchain: {name}")


def format_command(command: Iterable[object]) -> str:
    return " ".join(shlex.quote(str(part)) for part in command)


def run(
    command: list[object],
    *,
    cwd: Path | None = None,
    expected: int = 0,
    env: dict[str, str] | None = None,
) -> subprocess.CompletedProcess[str]:
    printable = format_command(command)
    location = cwd or ROOT
    print(f"+ ({location}) {printable}")

    result = subprocess.run(
        [str(part) for part in command],
        cwd=location,
        text=True,
        capture_output=True,
        env=env,
    )

    if result.returncode != expected:
        raise AssertionError(
            f"expected exit code {expected}, got {result.returncode}\n"
            f"command: {printable}\n"
            f"stdout:\n{result.stdout}\n"
            f"stderr:\n{result.stderr}"
        )

    return result


def expect(condition: bool, message: str) -> None:
    if not condition:
        raise AssertionError(message)


def copy_fixture(name: str, workspace: Path, *, suffix: str = "") -> Path:
    source = FIXTURES / name
    destination = workspace / f"{name}{suffix}"
    shutil.copytree(source, destination)
    return destination


def create_registry_package_fixture(workspace: Path, *, suffix: str = "") -> Path:
    source = workspace / f"answer-package-source{suffix}"
    (source / "include").mkdir(parents=True)
    (source / "src").mkdir()
    (source / "include" / "answer.hpp").write_text("int answer();\n")
    (source / "src" / "answer.cc").write_text('#include <answer.hpp>\n\nint answer() { return 42; }\n')

    run(["git", "init"], cwd=source)
    run(["git", "config", "user.name", "bspm smoke"], cwd=source)
    run(["git", "config", "user.email", "bspm-smoke@example.invalid"], cwd=source)
    run(["git", "add", "."], cwd=source)
    run(["git", "commit", "-m", "Create answer package"], cwd=source)
    revision = run(["git", "rev-parse", "HEAD"], cwd=source).stdout.strip()

    registry = workspace / f"test-registry{suffix}"
    package = registry / "packages" / "answer" / "1.0.0"
    package.mkdir(parents=True)
    (registry / "registry.bspm").write_text("registry smoke\nformat 1\n")
    (registry / "baseline.bspm").write_text("package answer 1.0.0#0\n")
    (package / "package.bspm").write_text(
        "package answer\n"
        "version 1.0.0\n"
        "package-revision 0\n\n"
        f'source git "{source.as_posix()}"\n'
        f"source-revision {revision}\n\n"
        "build registry build.bspm\n"
    )
    (package / "build.bspm").write_text(
        "project answer\n"
        "default answer\n\n"
        "target answer . --lib -o answer \\\n"
        "    --source src/answer.cc \\\n"
        "    --public-include include\n"
    )

    run(["git", "init"], cwd=registry)
    run(["git", "config", "user.name", "bspm smoke"], cwd=registry)
    run(["git", "config", "user.email", "bspm-smoke@example.invalid"], cwd=registry)
    run(["git", "add", "."], cwd=registry)
    run(["git", "commit", "-m", "Create test registry"], cwd=registry)

    consumer = copy_fixture("package-consumer", workspace, suffix=suffix)
    (consumer / "bspm.deps").write_text(f'registry "{registry.as_uri()}"\nrequire answer\n')
    return consumer


def build_bspm(workspace: Path, toolchain: Toolchain) -> Path:
    executable = workspace / f"bspm{toolchain.executable_suffix}"

    if toolchain.name == "gcc":
        run(
            [
                toolchain.build_driver,
                "-std=c++23",
                "-Wpedantic",
                "-Wall",
                "-Wextra",
                "-Werror",
                ROOT / "bspm.cpp",
                "-O2",
                "-lstdc++exp",
                "-o",
                executable,
            ]
        )
    elif toolchain.name == "clang":
        run(
            [
                toolchain.build_driver,
                "-std=c++23",
                "-Wpedantic",
                "-Wall",
                "-Wextra",
                "-Werror",
                ROOT / "bspm.cpp",
                "-O2",
                "-o",
                executable,
            ]
        )
    else:
        run(["cl", "/std:c++latest", "/EHsc", "/nologo", ROOT / "bspm.cpp", f"/Fe{executable}"])

    return executable


def build_command(bspm: Path, toolchain: Toolchain, *args: object) -> list[object]:
    return [bspm, "build", *args, "-c", toolchain.bspm_selector]


def assert_generated_project_scaffolds(bspm: Path, workspace: Path) -> None:
    simple = workspace / "generated-simple"
    run([bspm, "init", simple])
    expect((simple / "bspm.deps").is_file(), "plain init should create bspm.deps")
    expect(not (simple / "bspm.lock").exists(), "plain init should not create an unresolved lockfile")
    expect(".bspm/" in (simple / ".gitignore").read_text(), "plain init should ignore package caches")
    expect((simple / "main.cpp").is_file(), "plain init should create main.cpp")

    project = workspace / "generated-project"
    run([bspm, "init", project, "--project"])
    expect((project / "bspm.deps").is_file(), "project init should create bspm.deps")
    expect(not (project / "bspm.lock").exists(), "project init should not create an unresolved lockfile")
    expect(".bspm/" in (project / ".gitignore").read_text(), "project init should ignore package caches")
    expect((project / "bspm.build").is_file(), "project init should create bspm.build")
    expect((project / "app" / "main.cpp").is_file(), "project init should create app/main.cpp")


def assert_cli_basics(bspm: Path, toolchain: Toolchain) -> None:
    help_result = run([bspm, "help"])
    expect("Usage:" in help_result.stdout, "help output should include Usage")

    doctor_result = run([bspm, "doctor", "-c", toolchain.bspm_selector])
    expect("bspm doctor" in doctor_result.stdout, "doctor output should include command title")
    expect("profile:" in doctor_result.stdout, "doctor output should include selected profile")

    graph_help_result = run([bspm, "help", "graph"])
    expect("discovered project targets" in graph_help_result.stdout, "graph help should describe graph output")

    compile_commands_help_result = run([bspm, "help", "compile-commands"])
    expect("compile_commands.json" in compile_commands_help_result.stdout, "compile-commands help should describe output")

    version_result = run([bspm, "version"])
    expect(version_result.stdout.startswith("bspm "), "version output should start with 'bspm '")

    unknown_result = run([bspm, "unknown-command"], expected=1)
    expect("unknown command" in unknown_result.stdout, "unknown command should be reported")


def assert_dry_run_plans(bspm: Path, workspace: Path, toolchain: Toolchain) -> None:
    simple = copy_fixture("simple-bin", workspace, suffix="-dry-run")
    simple_result = run(build_command(bspm, toolchain, simple, "--dry-run"))
    expect("command:" in simple_result.stdout, "simple build dry-run should print commands")

    explain_result = run(build_command(bspm, toolchain, simple, "--dry-run", "--explain"))
    expect("units:" in explain_result.stdout, "build --explain should print discovered units")

    flag_result = run(
        build_command(
            bspm,
            toolchain,
            simple,
            "--dry-run",
            "--cxxflag",
            "-DBSPM_DRY_RUN=1",
            "--ldflag",
            "-Wl,--dry-run-flag",
        )
    )
    expect("-DBSPM_DRY_RUN=1" in flag_result.stdout, "dry-run should include user compiler flags")
    expect("-Wl,--dry-run-flag" in flag_result.stdout, "dry-run should include user linker flags")

    nested = copy_fixture("nested-module", workspace, suffix="-dry-run")
    nested_result = run(build_command(bspm, toolchain, nested, "--dry-run"))
    expect("command:" in nested_result.stdout, "module build dry-run should print commands")

    project = copy_fixture("project-config", workspace, suffix="-dry-run")
    project_result = run(build_command(bspm, toolchain, project, "--project", "--dry-run"))
    expect("command:" in project_result.stdout, "project build dry-run should print commands")

    graph_result = run([bspm, "graph", nested, "-c", toolchain.bspm_selector])
    expect("module imports:" in graph_result.stdout, "graph should print module imports")

    run([bspm, "compile-commands", nested, "-c", toolchain.bspm_selector])
    nested_compile_commands = json.loads((nested / "compile_commands.json").read_text())
    expect(len(nested_compile_commands) == 2, "nested compile_commands should include both source units")
    compile_driver = "cl" if toolchain.name == "msvc" else toolchain.bspm_selector
    expect(
        any("main.cpp" in entry["file"] and compile_driver in entry["command"] for entry in nested_compile_commands),
        "nested compile_commands should include the main source compile command",
    )

    project_graph_result = run([bspm, "graph", project, "--project", "-c", toolchain.bspm_selector])
    expect("target order:" in project_graph_result.stdout, "project graph should print target order")

    run([bspm, "compile-commands", project, "--project", "-c", toolchain.bspm_selector])
    project_compile_commands = json.loads((project / "compile_commands.json").read_text())
    expect(len(project_compile_commands) >= 3, "project compile_commands should include dependency and app units")
    expect(
        any("app" in entry["file"] and "main.cpp" in entry["file"] for entry in project_compile_commands),
        "project compile_commands should include the app main source",
    )

    package_consumer = create_registry_package_fixture(workspace, suffix="-dry-run")
    package_result = run(build_command(bspm, toolchain, package_consumer, "--project", "--dry-run"))
    expect("answer.cc" in package_result.stdout, "package dry-run should compile the registry target")
    expect("include" in package_result.stdout, "package public includes should propagate to the consumer")
    lock_path = package_consumer / "bspm.lock"
    expect(lock_path.is_file(), "successful package resolution should create bspm.lock")
    lock_contents = lock_path.read_text()
    expect(lock_contents.startswith("format 1\nregistry "), "bspm.lock should declare its format and registry")
    expect("package answer 1.0.0#0 " in lock_contents, "bspm.lock should pin the package recipe")

    registry = workspace / "test-registry-dry-run"
    locked_registry_revision = run(["git", "rev-parse", "HEAD"], cwd=registry).stdout.strip()
    expect(locked_registry_revision in lock_contents, "bspm.lock should pin the registry commit")
    (registry / "baseline.bspm").write_text("package answer 2.0.0#0\n")
    run(["git", "add", "baseline.bspm"], cwd=registry)
    run(["git", "commit", "-m", "Move test baseline"], cwd=registry)
    (package_consumer / ".bspm" / "registry").rename(package_consumer / ".bspm" / "registry-before-lock")

    package_graph = run([bspm, "graph", package_consumer, "--project", "-c", toolchain.bspm_selector])
    expect("target order: answer::answer app" in package_graph.stdout, "package target should precede its consumer")
    cached_registry_revision = run(
        ["git", "rev-parse", "HEAD"], cwd=package_consumer / ".bspm" / "registry"
    ).stdout.strip()
    expect(cached_registry_revision == locked_registry_revision, "bspm.lock should restore the pinned registry commit")

    run([bspm, "compile-commands", package_consumer, "--project", "-c", toolchain.bspm_selector])
    package_compile_commands = json.loads((package_consumer / "compile_commands.json").read_text())
    expect(
        any("answer.cc" in entry["file"] for entry in package_compile_commands),
        "package compile_commands should include registry target sources",
    )

    dependencies_path = package_consumer / "bspm.deps"
    dependencies_path.write_text(dependencies_path.read_text().replace("require answer", "require answer 1.0.0#1"))
    revision_result = run(
        build_command(bspm, toolchain, package_consumer, "--project", "--dry-run"),
        expected=1,
    )
    expect("bspm.lock is out of date" in revision_result.stdout, "package recipe revisions should be exact")

    dependencies_path.write_text(dependencies_path.read_text().replace("require answer 1.0.0#1", "require answer"))
    lock_path.unlink()
    refresh_result = run(
        build_command(bspm, toolchain, package_consumer, "--project", "--dry-run"),
        expected=1,
    )
    expect("2.0.0" in refresh_result.stdout, "resolving without a lock should use the latest registry baseline")


def assert_simple_binary(bspm: Path, workspace: Path, toolchain: Toolchain) -> None:
    fixture = copy_fixture("simple-bin", workspace)
    run(build_command(bspm, toolchain, fixture))
    incremental_result = run(build_command(bspm, toolchain, fixture, "-v"))
    expect("up to date:" in incremental_result.stdout, "second simple build should skip up-to-date steps")
    result = run([bspm, "run", fixture])
    expect(result.stdout.strip() == "simple-bin: ok", "simple binary should run")

    spaced_fixture = copy_fixture("simple-bin", workspace, suffix="-with spaces")
    run(build_command(bspm, toolchain, spaced_fixture))
    spaced_result = run([bspm, "run", spaced_fixture])
    expect(spaced_result.stdout.strip() == "simple-bin: ok", "simple binary should build and run from a path with spaces")


def assert_nested_module_binary(bspm: Path, workspace: Path, toolchain: Toolchain) -> None:
    fixture = copy_fixture("nested-module", workspace)
    run(build_command(bspm, toolchain, fixture))
    result = run([bspm, "run", fixture])
    expect(result.stdout.strip() == "nested-module: 42", "nested module binary should run")


def assert_parallel_build(bspm: Path, workspace: Path, toolchain: Toolchain) -> None:
    fixture = copy_fixture("multi-source", workspace)
    run(build_command(bspm, toolchain, fixture, "-j", "2"))
    result = run([bspm, "run", fixture])
    expect(result.stdout.strip() == "multi-source: 7", "parallel multi-source binary should run")

    module_fixture = copy_fixture("parallel-modules", workspace)
    run(build_command(bspm, toolchain, module_fixture, "-j", "2"))
    module_result = run([bspm, "run", module_fixture])
    expect(module_result.stdout.strip() == "parallel-modules: 42", "parallel module levels should build and run")


def assert_user_build_options(bspm: Path, workspace: Path, toolchain: Toolchain) -> None:
    fixture = copy_fixture("user-options", workspace)
    options = [
        "--source",
        ".",
        "--exclude",
        "ignored",
        "--include",
        "include",
        "--define",
        "BSPM_DEFINE_VALUE=12",
        "--cxxflag",
        "-DBSPM_CXXFLAG_VALUE=30",
    ]
    run(build_command(bspm, toolchain, fixture, *options))
    result = run([bspm, "run", fixture])
    expect(result.stdout.strip() == "user-options: 42", "user build options should affect compile and discovery")

    if toolchain.name != "msvc":
        depfile = fixture / "build" / toolchain.profile / "main.o.d"
        expect(depfile.is_file(), "non-MSVC builds should write a source dependency file")

        incremental_result = run(build_command(bspm, toolchain, fixture, *options, "-v"))
        expect("up to date: main.cpp" in incremental_result.stdout, "unchanged header user-options build should skip main")

        header = fixture / "include" / "config.hpp"
        header.write_text(header.read_text() + "\n// depfile rebuild check\n")
        future = time.time() + 2
        os.utime(header, (future, future))

        header_result = run(build_command(bspm, toolchain, fixture, *options, "-v"))
        expect(
            "up to date: main.cpp" not in header_result.stdout,
            "changing an included header should rebuild the including source",
        )
        result = run([bspm, "run", fixture])
        expect(result.stdout.strip() == "user-options: 42", "header-triggered rebuild should still produce a runnable app")


def assert_library_outputs(bspm: Path, workspace: Path, toolchain: Toolchain) -> None:
    static_fixture = copy_fixture("static-library", workspace)
    run(build_command(bspm, toolchain, static_fixture, "--lib", "-o", "math"))
    static_output = static_fixture / "build" / toolchain.profile / toolchain.static_library_name
    expect(
        static_output.is_file(),
        f"static library smoke build should create {toolchain.static_library_name}",
    )

    shared_fixture = copy_fixture("shared-library", workspace)
    run(build_command(bspm, toolchain, shared_fixture, "--shared", "-o", "greeting"))
    shared_output = shared_fixture / "build" / toolchain.profile / toolchain.shared_library_name
    expect(
        shared_output.is_file(),
        f"shared library smoke build should create {toolchain.shared_library_name}",
    )


def assert_project_build_run_and_clean(bspm: Path, workspace: Path, toolchain: Toolchain) -> None:
    fixture = copy_fixture("project-config", workspace)
    run(build_command(bspm, toolchain, fixture, "--project"))

    math_output = fixture / "libs" / "math" / "build" / toolchain.profile / toolchain.static_library_name
    greeting_output = fixture / "libs" / "greeting" / "build" / toolchain.profile / toolchain.shared_library_name
    app_output = fixture / "app" / "build" / toolchain.profile / toolchain.app_name

    expect(math_output.is_file(), "project build should create the static dependency")
    expect(greeting_output.is_file(), "project build should create the shared dependency")
    expect(app_output.is_file(), "project build should create the app executable")

    runtime_env = None
    if os.name != "nt":
        runtime_env = os.environ.copy()
        loader_path_name = "DYLD_LIBRARY_PATH" if sys.platform == "darwin" else "LD_LIBRARY_PATH"
        existing_loader_path = runtime_env.get(loader_path_name)
        runtime_env[loader_path_name] = (
            str(app_output.parent)
            if not existing_loader_path
            else f"{app_output.parent}{os.pathsep}{existing_loader_path}"
        )

    result = run([bspm, "run"], cwd=fixture, env=runtime_env)
    expect(result.stdout.strip() == "project-config: 42", "configured project should run")

    run([bspm, "clean", "all"], cwd=fixture)
    expect(not (fixture / "libs" / "math" / "build").exists(), "clean all should remove math build dir")
    expect(
        not (fixture / "libs" / "greeting" / "build").exists(),
        "clean all should remove greeting build dir",
    )
    expect(not (fixture / "app" / "build").exists(), "clean all should remove app build dir")


def assert_registry_package_build(bspm: Path, workspace: Path, toolchain: Toolchain) -> None:
    fixture = create_registry_package_fixture(workspace)
    run(build_command(bspm, toolchain, fixture, "--project"))
    incremental_result = run(build_command(bspm, toolchain, fixture, "--project", "-v"))
    expect(
        "up to date: src/answer.cc" in incremental_result.stdout,
        "cached package sources should build incrementally",
    )
    result = run([bspm, "run"], cwd=fixture)
    expect(result.stdout.strip() == "package-consumer: 42", "registry package should build, link, and run")


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description="Run bspm smoke coverage.")
    parser.add_argument("--toolchain", choices=("gcc", "clang", "msvc"), default="gcc")
    parser.add_argument("--level", choices=("core", "full"), default="full")
    return parser.parse_args()


def main() -> int:
    args = parse_args()
    toolchain = create_toolchain(args.toolchain)
    level: Level = args.level

    with tempfile.TemporaryDirectory(prefix="bspm-smoke-") as temporary:
        workspace = Path(temporary)
        bspm = build_bspm(workspace, toolchain)

        assert_cli_basics(bspm, toolchain)
        assert_generated_project_scaffolds(bspm, workspace)
        assert_dry_run_plans(bspm, workspace, toolchain)

        if level == "full":
            assert_simple_binary(bspm, workspace, toolchain)
            assert_nested_module_binary(bspm, workspace, toolchain)
            assert_parallel_build(bspm, workspace, toolchain)
            assert_user_build_options(bspm, workspace, toolchain)
            assert_library_outputs(bspm, workspace, toolchain)
            assert_project_build_run_and_clean(bspm, workspace, toolchain)
            assert_registry_package_build(bspm, workspace, toolchain)

    print(f"{toolchain.name} {level} smoke tests passed")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
