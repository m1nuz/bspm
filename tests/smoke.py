from __future__ import annotations

import argparse
import os
import shlex
import shutil
import subprocess
import sys
import tempfile
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
    expect((simple / ".dependencies").is_file(), "plain init should create .dependencies")
    expect((simple / "main.cpp").is_file(), "plain init should create main.cpp")

    project = workspace / "generated-project"
    run([bspm, "init", project, "--project"])
    expect((project / ".dependencies").is_file(), "project init should create .dependencies")
    expect((project / "bspm.build").is_file(), "project init should create bspm.build")
    expect((project / "app" / "main.cpp").is_file(), "project init should create app/main.cpp")


def assert_cli_basics(bspm: Path) -> None:
    help_result = run([bspm, "help"])
    expect("Usage:" in help_result.stdout, "help output should include Usage")

    version_result = run([bspm, "version"])
    expect(version_result.stdout.startswith("bspm "), "version output should start with 'bspm '")

    unknown_result = run([bspm, "unknown-command"], expected=1)
    expect("unknown command" in unknown_result.stdout, "unknown command should be reported")


def assert_dry_run_plans(bspm: Path, workspace: Path, toolchain: Toolchain) -> None:
    simple = copy_fixture("simple-bin", workspace, suffix="-dry-run")
    simple_result = run(build_command(bspm, toolchain, simple, "--dry-run"))
    expect("command:" in simple_result.stdout, "simple build dry-run should print commands")

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


def assert_user_build_options(bspm: Path, workspace: Path, toolchain: Toolchain) -> None:
    fixture = copy_fixture("user-options", workspace)
    run(
        build_command(
            bspm,
            toolchain,
            fixture,
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
        )
    )
    result = run([bspm, "run", fixture])
    expect(result.stdout.strip() == "user-options: 42", "user build options should affect compile and discovery")


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

        assert_cli_basics(bspm)
        assert_generated_project_scaffolds(bspm, workspace)
        assert_dry_run_plans(bspm, workspace, toolchain)

        if level == "full":
            assert_simple_binary(bspm, workspace, toolchain)
            assert_nested_module_binary(bspm, workspace, toolchain)
            assert_parallel_build(bspm, workspace, toolchain)
            assert_user_build_options(bspm, workspace, toolchain)
            assert_library_outputs(bspm, workspace, toolchain)
            assert_project_build_run_and_clean(bspm, workspace, toolchain)

    print(f"{toolchain.name} {level} smoke tests passed")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
