from __future__ import annotations

import os
import shlex
import shutil
import subprocess
import sys
import tempfile
from pathlib import Path
from typing import Iterable


ROOT = Path(__file__).resolve().parents[1]
FIXTURES = ROOT / "tests" / "smoke"
PROFILE = "gcc-debug"


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


def copy_fixture(name: str, workspace: Path) -> Path:
    source = FIXTURES / name
    destination = workspace / name
    shutil.copytree(source, destination)
    return destination


def build_bspm(workspace: Path) -> Path:
    executable = workspace / ("bspm.exe" if os.name == "nt" else "bspm")
    compiler = os.environ.get("CXX", "g++")
    run(
        [
            compiler,
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
    return executable


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


def assert_simple_binary(bspm: Path, workspace: Path) -> None:
    fixture = copy_fixture("simple-bin", workspace)
    run([bspm, "build", fixture])
    result = run([bspm, "run", fixture])
    expect(result.stdout.strip() == "simple-bin: ok", "simple binary should run")


def assert_nested_module_binary(bspm: Path, workspace: Path) -> None:
    fixture = copy_fixture("nested-module", workspace)
    run([bspm, "build", fixture])
    result = run([bspm, "run", fixture])
    expect(result.stdout.strip() == "nested-module: 42", "nested module binary should run")


def assert_library_outputs(bspm: Path, workspace: Path) -> None:
    static_fixture = copy_fixture("static-library", workspace)
    run([bspm, "build", static_fixture, "--lib", "-o", "math"])
    static_output = static_fixture / "build" / PROFILE / "libmath.a"
    expect(static_output.is_file(), "static library smoke build should create libmath.a")

    shared_fixture = copy_fixture("shared-library", workspace)
    run([bspm, "build", shared_fixture, "--shared", "-o", "greeting"])
    shared_name = "greeting.dll" if os.name == "nt" else "libgreeting.so"
    shared_output = shared_fixture / "build" / PROFILE / shared_name
    expect(shared_output.is_file(), f"shared library smoke build should create {shared_name}")


def assert_project_build_run_and_clean(bspm: Path, workspace: Path) -> None:
    fixture = copy_fixture("project-config", workspace)
    run([bspm, "build", fixture, "--project"])

    math_output = fixture / "libs" / "math" / "build" / PROFILE / "libmath.a"
    greeting_name = "greeting.dll" if os.name == "nt" else "libgreeting.so"
    greeting_output = fixture / "libs" / "greeting" / "build" / PROFILE / greeting_name
    app_name = "demo.exe" if os.name == "nt" else "demo"
    app_output = fixture / "app" / "build" / PROFILE / app_name

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


def main() -> int:
    with tempfile.TemporaryDirectory(prefix="bspm-smoke-") as temporary:
        workspace = Path(temporary)
        bspm = build_bspm(workspace)

        assert_cli_basics(bspm)
        assert_generated_project_scaffolds(bspm, workspace)
        assert_simple_binary(bspm, workspace)
        assert_nested_module_binary(bspm, workspace)
        assert_library_outputs(bspm, workspace)
        assert_project_build_run_and_clean(bspm, workspace)

    print("smoke tests passed")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
