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


def create_registry_package_fixture(
    workspace: Path,
    *,
    suffix: str = "",
    declare_package_default: bool = True,
) -> Path:
    source = workspace / f"answer-package-source{suffix}"
    (source / "include").mkdir(parents=True)
    (source / "src").mkdir()
    (source / "include" / "answer.hpp").write_text("constexpr int answer_offset() { return 0; }\n")
    (source / "src" / "answer.cppm").write_text("export module answer;\n\nexport int answer() { return 42; }\n")

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
    package_metadata = (
        "package answer\n"
        "version 1.0.0\n"
        "package-revision 0\n\n"
        f'source git "{source.as_posix()}"\n'
        f"source-revision {revision}\n\n"
        "build registry build.bspm\n\n"
    )
    if declare_package_default:
        package_metadata += "default-target answer\n"
    package_metadata += "export-target answer\n"
    (package / "package.bspm").write_text(package_metadata)
    (package / "build.bspm").write_text(
        "project answer\n"
        "default answer\n\n"
        "target answer . --lib -o answer \\\n"
        "    --source src/answer.cppm \\\n"
        "    --public-include include\n"
        "target private . --lib -o answer-private --source src/answer.cppm\n"
    )

    run(["git", "init"], cwd=registry)
    run(["git", "config", "user.name", "bspm smoke"], cwd=registry)
    run(["git", "config", "user.email", "bspm-smoke@example.invalid"], cwd=registry)
    run(["git", "add", "."], cwd=registry)
    run(["git", "commit", "-m", "Create test registry"], cwd=registry)

    consumer = copy_fixture("package-consumer", workspace, suffix=suffix)
    (consumer / "bspm.deps").write_text(f'registry "{registry.as_uri()}"\nrequire answer\n')
    return consumer


def create_implicit_registry_package_fixture(workspace: Path, *, suffix: str = "") -> Path:
    consumer = create_registry_package_fixture(workspace, suffix=suffix, declare_package_default=False)
    (consumer / "bspm.build").unlink()
    shutil.move(consumer / "app" / "main.cpp", consumer / "main.cpp")
    (consumer / "app").rmdir()

    registry_line = (consumer / "bspm.deps").read_text().splitlines()[0]
    (consumer / "bspm.deps").write_text(f"{registry_line}\n")
    return consumer


def write_semver_registry_package(
    registry: Path,
    source: Path,
    source_revision: str,
    name: str,
    version: str,
    requirements: list[tuple[str, str]],
) -> None:
    package = registry / "packages" / name / version
    package.mkdir(parents=True)
    requirement_lines = "".join(f"require {dependency} {constraint}\n" for dependency, constraint in requirements)
    (package / "package.bspm").write_text(
        f"package {name}\n"
        f"version {version}\n"
        "package-revision 0\n\n"
        f'source git "{source.as_posix()}"\n'
        f"source-revision {source_revision}\n\n"
        "build registry build.bspm\n\n"
        "default-target core\n"
        "export-target core\n"
        f"{requirement_lines}"
    )
    (package / "build.bspm").write_text(
        f"project {name}\n"
        "default core\n\n"
        f"target core . --lib -o {name} --source src/package.cpp --public-include include\n"
    )


def create_semver_registry_fixture(workspace: Path) -> tuple[Path, Path, Path, str]:
    source = workspace / "semver-package-source"
    (source / "src").mkdir(parents=True)
    (source / "include").mkdir()
    (source / "src" / "package.cpp").write_text("int semver_package_value() { return 1; }\n")
    (source / "include" / "package.hpp").write_text("int semver_package_value();\n")
    run(["git", "init"], cwd=source)
    run(["git", "config", "user.name", "bspm smoke"], cwd=source)
    run(["git", "config", "user.email", "bspm-smoke@example.invalid"], cwd=source)
    run(["git", "add", "."], cwd=source)
    run(["git", "commit", "-m", "Create semantic-version package source"], cwd=source)
    source_revision = run(["git", "rev-parse", "HEAD"], cwd=source).stdout.strip()

    registry = workspace / "semver-registry"
    registry.mkdir()
    (registry / "registry.bspm").write_text("registry semver-smoke\nformat 1\n")
    (registry / "baseline.bspm").write_text(
        "package addon 2.0.0#0\n"
        "package chooser 2.0.0#0\n"
        "package leaf 1.5.0#0\n"
        "package narrow 1.0.0#0\n"
        "package right 1.0.0#0\n"
    )

    for version in ("1.0.0", "1.5.0", "1.9.0", "2.0.0"):
        write_semver_registry_package(registry, source, source_revision, "leaf", version, [])
    write_semver_registry_package(registry, source, source_revision, "chooser", "1.0.0", [("leaf", "^1.0.0")])
    write_semver_registry_package(registry, source, source_revision, "chooser", "2.0.0", [("leaf", "^2.0.0")])
    write_semver_registry_package(registry, source, source_revision, "right", "1.0.0", [("leaf", "^1.5.0")])
    write_semver_registry_package(registry, source, source_revision, "narrow", "1.0.0", [("leaf", "~1.5.0")])
    write_semver_registry_package(registry, source, source_revision, "narrow", "2.0.0", [("leaf", "^2.0.0")])
    for version in ("1.0.0", "1.2.0", "2.0.0"):
        write_semver_registry_package(registry, source, source_revision, "addon", version, [])

    run(["git", "init"], cwd=registry)
    run(["git", "config", "user.name", "bspm smoke"], cwd=registry)
    run(["git", "config", "user.email", "bspm-smoke@example.invalid"], cwd=registry)
    run(["git", "add", "."], cwd=registry)
    run(["git", "commit", "-m", "Create semantic-version registry"], cwd=registry)

    consumer = workspace / "semver-consumer"
    consumer.mkdir()
    (consumer / "main.cpp").write_text("int main() { return 0; }\n")
    (consumer / "bspm.deps").write_text(
        f'registry "{registry.as_uri()}"\n'
        "require chooser >=1.0.0 <3.0.0\n"
        "require right ^1.0.0\n"
        "require narrow ^1.0.0\n"
    )
    return consumer, registry, source, source_revision


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

    install_help_result = run([bspm, "help", "install"])
    expect("bspm.lock" in install_help_result.stdout, "install help should describe locked installation")

    search_help_result = run([bspm, "help", "search"])
    expect("selected registry" in search_help_result.stdout, "search help should describe registry discovery")

    info_help_result = run([bspm, "help", "info"])
    expect("all registry versions" in info_help_result.stdout, "info help should describe package metadata")

    outdated_help_result = run([bspm, "help", "outdated"])
    expect("newest compatible solution" in outdated_help_result.stdout, "outdated help should describe comparison")

    package_help_result = run([bspm, "help", "package"])
    expect("package validate" in package_help_result.stdout, "package help should describe package validation")

    registry_help_result = run([bspm, "help", "registry"])
    expect("registry validate" in registry_help_result.stdout, "registry help should describe registry validation")

    update_help_result = run([bspm, "help", "update"])
    expect("[package]" in update_help_result.stdout, "update help should describe selective updates")

    add_help_result = run([bspm, "help", "add"])
    expect("<package> [constraint]" in add_help_result.stdout, "add help should describe package constraints")

    remove_help_result = run([bspm, "help", "remove"])
    expect("prune" in remove_help_result.stdout, "remove help should describe lockfile pruning")

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
    registry = workspace / "test-registry-dry-run"
    stable_package = registry / "packages" / "stable" / "1.0.0"
    shutil.copytree(registry / "packages" / "answer" / "1.0.0", stable_package)
    stable_metadata = stable_package / "package.bspm"
    stable_metadata.write_text(stable_metadata.read_text().replace("package answer", "package stable", 1))
    (registry / "baseline.bspm").write_text(
        "package answer 1.0.0#0\n"
        "package stable 1.0.0#0\n"
    )
    dependencies_path = package_consumer / "bspm.deps"
    dependencies_path.write_text(dependencies_path.read_text() + "# keep this dependency comment\n")
    run(["git", "add", "baseline.bspm", "packages/stable"], cwd=registry)
    run(["git", "commit", "-m", "Add stable test package"], cwd=registry)

    add_result = run([bspm, "add", "stable", "1.0.0#0", "-C", package_consumer])
    expect("added 'stable 1.0.0#0'" in add_result.stdout, "add should report the exact direct requirement")
    expect("# keep this dependency comment" in dependencies_path.read_text(), "add should preserve manifest comments")
    expect("require stable 1.0.0#0" in dependencies_path.read_text(), "add should append the requirement")
    lock_path = package_consumer / "bspm.lock"
    expect(lock_path.is_file(), "add should resolve dependencies and create bspm.lock")

    manifest_after_add = dependencies_path.read_text()
    lock_after_add = lock_path.read_text()
    duplicate_add_result = run([bspm, "add", "stable", "-C", package_consumer], expected=1)
    expect("already a direct requirement" in duplicate_add_result.stdout, "add should reject duplicate requirements")
    expect(dependencies_path.read_text() == manifest_after_add, "duplicate add should preserve bspm.deps")
    expect(lock_path.read_text() == lock_after_add, "duplicate add should preserve bspm.lock")

    missing_add_result = run([bspm, "add", "missing", "-C", package_consumer], expected=1)
    expect("no requested version or registry baseline" in missing_add_result.stdout, "failed add should report resolution errors")
    expect(dependencies_path.read_text() == manifest_after_add, "failed add should roll back bspm.deps")
    expect(lock_path.read_text() == lock_after_add, "failed add should preserve bspm.lock")

    package_result = run(build_command(bspm, toolchain, package_consumer, "--project", "--dry-run"))
    expect("answer.cppm" in package_result.stdout, "package dry-run should compile the registry module target")
    expect("include" in package_result.stdout, "package public includes should propagate to the consumer")
    expect(lock_path.is_file(), "successful package resolution should create bspm.lock")
    lock_contents = lock_path.read_text()
    expect(lock_contents.startswith("format 1\nregistry "), "bspm.lock should declare its format and registry")
    expect("package answer 1.0.0#0 " in lock_contents, "bspm.lock should pin the package recipe")
    stable_lock_line = next(line for line in lock_contents.splitlines() if line.startswith("package stable "))

    locked_registry_revision = run(["git", "rev-parse", "HEAD"], cwd=registry).stdout.strip()
    expect(locked_registry_revision in lock_contents, "bspm.lock should pin the registry commit")
    (registry / "baseline.bspm").write_text(
        "package answer 2.0.0#0\n"
        "package stable 1.0.0#0\n"
    )
    run(["git", "add", "baseline.bspm"], cwd=registry)
    run(["git", "commit", "-m", "Move test baseline"], cwd=registry)
    (package_consumer / ".bspm" / "registry").rename(package_consumer / ".bspm" / "registry-before-lock")

    install_result = run([bspm, "install", "-C", package_consumer])
    expect("installed dependencies" in install_result.stdout, "install should report successful materialization")
    expect(lock_path.read_text() == lock_contents, "install should preserve an existing lockfile")

    package_graph = run([bspm, "graph", package_consumer, "--project", "-c", toolchain.bspm_selector])
    expect("target order: answer::answer app" in package_graph.stdout, "package target should precede its consumer")

    consumer_manifest = package_consumer / "bspm.build"
    shorthand_manifest = consumer_manifest.read_text()
    consumer_manifest.write_text(shorthand_manifest.replace("@answer", "answer::answer"))
    explicit_graph = run([bspm, "graph", package_consumer, "--project", "-c", toolchain.bspm_selector])
    expect("target order: answer::answer app" in explicit_graph.stdout, "explicit package targets should remain supported")
    consumer_manifest.write_text(shorthand_manifest.replace("@answer", "answer::private"))
    private_result = run(
        [bspm, "graph", package_consumer, "--project", "-c", toolchain.bspm_selector],
        expected=1,
    )
    expect("unexported package target 'answer::private'" in private_result.stdout, "private package targets should be rejected")
    consumer_manifest.write_text(shorthand_manifest)
    cached_registry_revision = run(
        ["git", "rev-parse", "HEAD"], cwd=package_consumer / ".bspm" / "registry"
    ).stdout.strip()
    expect(cached_registry_revision == locked_registry_revision, "bspm.lock should restore the pinned registry commit")

    run([bspm, "compile-commands", package_consumer, "--project", "-c", toolchain.bspm_selector])
    package_compile_commands = json.loads((package_consumer / "compile_commands.json").read_text())
    expect(
        any("answer.cppm" in entry["file"] for entry in package_compile_commands),
        "package compile_commands should include registry module sources",
    )

    version_one = registry / "packages" / "answer" / "1.0.0"
    version_two = registry / "packages" / "answer" / "2.0.0"
    shutil.copytree(version_one, version_two)
    package_metadata = version_two / "package.bspm"
    package_metadata.write_text(
        package_metadata.read_text().replace("version 1.0.0", "version 2.0.0") + "require helper\n"
    )
    helper_package = registry / "packages" / "helper" / "1.0.0"
    shutil.copytree(version_one, helper_package)
    helper_metadata = helper_package / "package.bspm"
    helper_metadata.write_text(helper_metadata.read_text().replace("package answer", "package helper", 1))
    (registry / "baseline.bspm").write_text(
        "package answer 2.0.0#0\n"
        "package helper 1.0.0#0\n"
        "package stable 1.0.0#0\n"
    )
    run(["git", "add", "baseline.bspm", "packages/answer/2.0.0", "packages/helper"], cwd=registry)
    run(["git", "commit", "-m", "Publish answer 2.0.0"], cwd=registry)

    named_update_result = run([bspm, "update", "answer", "-C", package_consumer])
    expect("updated 'answer'" in named_update_result.stdout, "named update should report its package closure")
    named_lock_contents = lock_path.read_text()
    expect("package answer 2.0.0#0 " in named_lock_contents, "named update should select the latest baseline")
    expect("package helper 1.0.0#0 " in named_lock_contents, "named update should include new transitive packages")
    expect(stable_lock_line in named_lock_contents, "named update should preserve unrelated locked packages")

    missing_update_result = run([bspm, "update", "missing", "-C", package_consumer], expected=1)
    expect("not a direct requirement" in missing_update_result.stdout, "named updates should require a direct dependency")
    expect(lock_path.read_text() == named_lock_contents, "a failed named update should preserve bspm.lock")

    dependencies_path.write_text(dependencies_path.read_text().replace("require answer", "require answer 1.0.0#1"))
    revision_result = run(
        build_command(bspm, toolchain, package_consumer, "--project", "--dry-run"),
        expected=1,
    )
    expect("bspm.lock is out of date" in revision_result.stdout, "package recipe revisions should be exact")

    dependencies_path.write_text(dependencies_path.read_text().replace("require answer 1.0.0#1", "require answer"))
    lock_path.write_text("not a lock\n")
    full_update_result = run([bspm, "update", "-C", package_consumer])
    expect("updated all dependencies" in full_update_result.stdout, "full update should report success")
    expect(
        "package answer 2.0.0#0 " in lock_path.read_text(),
        "full update should atomically replace an invalid old lockfile",
    )

    alternate_registry = workspace / "alternate-test-registry-dry-run"
    shutil.copytree(registry, alternate_registry, ignore=shutil.ignore_patterns(".git"))
    run(["git", "init"], cwd=alternate_registry)
    run(["git", "config", "user.name", "bspm smoke"], cwd=alternate_registry)
    run(["git", "config", "user.email", "bspm-smoke@example.invalid"], cwd=alternate_registry)
    run(["git", "add", "."], cwd=alternate_registry)
    run(["git", "commit", "-m", "Create alternate test registry"], cwd=alternate_registry)
    dependencies_path.write_text(dependencies_path.read_text().replace(registry.as_uri(), alternate_registry.as_uri()))
    run([bspm, "update", "-C", package_consumer])
    expect(
        f"registry {alternate_registry.as_uri()} " in lock_path.read_text(),
        "full update should safely switch the cached registry origin",
    )

    manifest_before_failed_remove = dependencies_path.read_text()
    lock_before_failed_remove = lock_path.read_text()
    failed_remove_result = run([bspm, "remove", "answer", "-C", package_consumer], expected=1)
    expect("unknown package alias '@answer'" in failed_remove_result.stdout, "remove should validate project consumers")
    expect(dependencies_path.read_text() == manifest_before_failed_remove, "failed remove should restore bspm.deps")
    expect(lock_path.read_text() == lock_before_failed_remove, "failed remove should preserve bspm.lock")

    dependencies_path.write_text(
        dependencies_path.read_text().replace(
            "require stable 1.0.0#0\n",
            "require \\\n    stable 1.0.0#0 # remove this logical directive\n",
        )
    )
    remove_result = run([bspm, "remove", "stable", "-C", package_consumer])
    expect("removed 'stable'" in remove_result.stdout, "remove should report the removed requirement")
    expect("stable" not in dependencies_path.read_text(), "remove should delete a continued requirement")
    expect("# keep this dependency comment" in dependencies_path.read_text(), "remove should preserve unrelated comments")
    expect("package stable " not in lock_path.read_text(), "remove should prune the package from bspm.lock")
    expect("package helper 1.0.0#0 " in lock_path.read_text(), "remove should retain reachable transitive packages")

    implicit_consumer = create_implicit_registry_package_fixture(workspace, suffix="-implicit-dry-run")
    implicit_add = run([bspm, "add", "answer"], cwd=implicit_consumer)
    expect("added 'answer'" in implicit_add.stdout, "add should support simple projects without bspm.build")
    expect("require answer" in (implicit_consumer / "bspm.deps").read_text(), "add should update the simple manifest")
    expect((implicit_consumer / "bspm.lock").is_file(), "add should lock simple-project dependencies")

    (implicit_consumer / ".bspm" / "should-not-build.cpp").write_text("int package_cache_probe;\n")

    implicit_build = run(
        [bspm, "build", "--dry-run", "-c", toolchain.bspm_selector],
        cwd=implicit_consumer,
    )
    expect("answer.cppm" in implicit_build.stdout, "simple builds should plan their package default targets")
    expect("main.cpp" in implicit_build.stdout, "simple builds should retain their implicit application target")
    expect(
        "should-not-build.cpp" not in implicit_build.stdout,
        "simple source discovery should exclude the package cache",
    )

    implicit_graph = run([bspm, "graph", "-c", toolchain.bspm_selector], cwd=implicit_consumer)
    expect(
        "target order: answer::answer app" in implicit_graph.stdout,
        "simple dependency targets should precede the implicit app",
    )

    run([bspm, "compile-commands", "-c", toolchain.bspm_selector], cwd=implicit_consumer)
    implicit_compile_commands = json.loads((implicit_consumer / "compile_commands.json").read_text())
    expect(
        any("answer.cppm" in entry["file"] for entry in implicit_compile_commands),
        "simple compile_commands should include package targets",
    )
    expect(
        any(entry["file"].endswith("main.cpp") for entry in implicit_compile_commands),
        "simple compile_commands should include the implicit app",
    )

    implicit_remove = run([bspm, "remove", "answer"], cwd=implicit_consumer)
    expect("removed 'answer'" in implicit_remove.stdout, "remove should support simple projects")
    expect("require answer" not in (implicit_consumer / "bspm.deps").read_text(), "remove should update the manifest")
    expect(not (implicit_consumer / "bspm.lock").exists(), "removing the last simple dependency should prune the lock")


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


def assert_semantic_version_resolution(bspm: Path, workspace: Path) -> None:
    consumer, registry, source, source_revision = create_semver_registry_fixture(workspace)
    lock_path = consumer / "bspm.lock"
    dependencies_path = consumer / "bspm.deps"

    run([bspm, "update", "-C", consumer])
    lock_contents = lock_path.read_text()
    expect(
        "package chooser 1.0.0#0 " in lock_contents,
        "the solver should backtrack from the newest incompatible root package",
    )
    expect("package right 1.0.0#0 " in lock_contents, "caret constraints should select a compatible root")
    expect(
        "package leaf 1.5.0#0 " in lock_contents,
        "intersected caret and tilde constraints should select the highest compatible leaf",
    )

    search_result = run([bspm, "search", "LEA", "-C", consumer])
    expect("PACKAGE\tBASELINE\tLATEST\tLOCKED" in search_result.stdout, "search should print version columns")
    expect(
        "leaf\t1.5.0#0\t2.0.0#0\t1.5.0#0" in search_result.stdout,
        "search should be case-insensitive and show baseline, latest, and locked versions",
    )

    all_search_result = run([bspm, "search", "-C", consumer])
    expect("addon\t2.0.0#0\t2.0.0#0\t-" in all_search_result.stdout, "empty search should list packages")

    info_result = run([bspm, "info", "chooser", "-C", consumer])
    expect("package: chooser" in info_result.stdout, "info should identify the requested package")
    expect("2.0.0#0 [latest] [baseline]" in info_result.stdout, "info should label latest and baseline versions")
    expect("leaf ^2.0.0" in info_result.stdout, "info should show transitive package requirements")
    missing_info = run([bspm, "info", "missing", "-C", consumer], expected=1)
    expect("was not found" in missing_info.stdout, "info should diagnose unknown packages")

    add_result = run([bspm, "add", "addon", "^1.0.0", "-C", consumer])
    expect("added 'addon ^1.0.0'" in add_result.stdout, "add should accept semantic-version constraints")
    expect("require addon ^1.0.0" in dependencies_path.read_text(), "add should preserve the requested constraint")
    lock_after_add = lock_path.read_text()
    expect("package addon 1.2.0#0 " in lock_after_add, "caret constraints should select the newest compatible version")

    write_semver_registry_package(registry, source, source_revision, "leaf", "1.5.1", [])
    run(["git", "add", "packages/leaf/1.5.1"], cwd=registry)
    run(["git", "commit", "-m", "Publish compatible leaf update"], cwd=registry)

    run([bspm, "install", "-C", consumer])
    expect(lock_path.read_text() == lock_after_add, "install should keep exact locked versions after registry updates")

    lock_before_outdated = lock_path.read_text()
    outdated_result = run([bspm, "outdated", "-C", consumer])
    expect(
        "PACKAGE\tCURRENT\tCOMPATIBLE\tLATEST\tCONSTRAINT" in outdated_result.stdout,
        "outdated should print comparison columns",
    )
    expect(
        "leaf\t1.5.0#0\t1.5.1#0\t2.0.0#0\t<transitive>" in outdated_result.stdout,
        "outdated should distinguish compatible and unconstrained latest transitive versions",
    )
    expect(
        "addon\t1.2.0#0\t1.2.0#0\t2.0.0#0\t^1.0.0" in outdated_result.stdout,
        "outdated should show a direct constraint that blocks the latest version",
    )
    expect(lock_path.read_text() == lock_before_outdated, "outdated should not rewrite bspm.lock")

    addon_lock_line = next(line for line in lock_after_add.splitlines() if line.startswith("package addon "))
    run([bspm, "update", "narrow", "-C", consumer])
    updated_lock = lock_path.read_text()
    expect("package leaf 1.5.1#0 " in updated_lock, "a named update should refresh its compatible transitive closure")
    expect(addon_lock_line in updated_lock, "a named update should preserve unrelated locked packages")

    dependencies_path.write_text(dependencies_path.read_text().replace("require narrow ^1.0.0", "require narrow ^2.0.0"))
    lock_before_conflict = lock_path.read_text()
    conflict_result = run([bspm, "update", "-C", consumer], expected=1)
    expect("cannot resolve package 'leaf'" in conflict_result.stdout, "incompatible ranges should identify the package")
    expect(
        "project -> right requires leaf ^1.5.0" in conflict_result.stdout,
        "conflict diagnostics should show one dependency chain",
    )
    expect(
        "project -> narrow requires leaf ^2.0.0" in conflict_result.stdout,
        "conflict diagnostics should show the incompatible dependency chain",
    )
    expect(lock_path.read_text() == lock_before_conflict, "failed range resolution should preserve bspm.lock")


def assert_package_registry_validation(bspm: Path, workspace: Path) -> None:
    create_registry_package_fixture(workspace, suffix="-validation")
    registry = workspace / "test-registry-validation"
    source = workspace / "answer-package-source-validation"
    package = registry / "packages" / "answer" / "1.0.0"
    metadata_path = package / "package.bspm"

    package_result = run([bspm, "package", "validate", package])
    expect(
        "validated package 'answer 1.0.0#0'" in package_result.stdout,
        "package validate should accept a complete registry-owned recipe",
    )

    (source / "bspm.build").write_text((package / "build.bspm").read_text())
    run(["git", "add", "bspm.build"], cwd=source)
    run(["git", "commit", "-m", "Add source-owned package recipe"], cwd=source)
    source_revision = run(["git", "rev-parse", "HEAD"], cwd=source).stdout.strip()
    metadata_lines = metadata_path.read_text().splitlines()
    metadata_path.write_text(
        "\n".join(
            f"source-revision {source_revision}"
            if line.startswith("source-revision ")
            else "build source bspm.build"
            if line == "build registry build.bspm"
            else line
            for line in metadata_lines
        )
        + "\n"
    )

    source_package_result = run([bspm, "package", "validate", metadata_path])
    expect(
        "validated package 'answer 1.0.0#0'" in source_package_result.stdout,
        "package validate should load a build recipe from the pinned source revision",
    )

    registry_result = run([bspm, "registry", "validate", registry])
    expect(
        "1 packages, 1 versions" in registry_result.stdout,
        "registry validate should report the validated package and version counts",
    )

    invalid_baseline = workspace / "invalid-baseline-registry"
    shutil.copytree(registry, invalid_baseline, ignore=shutil.ignore_patterns(".git"))
    (invalid_baseline / "baseline.bspm").write_text("package answer 2.0.0#0\n")
    baseline_result = run([bspm, "registry", "validate", invalid_baseline], expected=1)
    expect(
        "does not reference an available package version" in baseline_result.stdout,
        "registry validate should reject unavailable baseline references",
    )

    invalid_export = workspace / "invalid-export-registry"
    shutil.copytree(registry, invalid_export, ignore=shutil.ignore_patterns(".git"))
    invalid_metadata = invalid_export / "packages" / "answer" / "1.0.0" / "package.bspm"
    invalid_metadata.write_text(invalid_metadata.read_text().replace("export-target answer", "export-target missing"))
    export_result = run([bspm, "package", "validate", invalid_metadata], expected=1)
    expect(
        "export target 'missing' is not defined" in export_result.stdout,
        "package validate should reject exports missing from the build recipe",
    )

    invalid_dependency = workspace / "invalid-dependency-registry"
    shutil.copytree(registry, invalid_dependency, ignore=shutil.ignore_patterns(".git"))
    dependency_metadata = invalid_dependency / "packages" / "answer" / "1.0.0" / "package.bspm"
    dependency_metadata.write_text(dependency_metadata.read_text() + "require unavailable ^1.0.0\n")
    dependency_result = run([bspm, "registry", "validate", invalid_dependency], expected=1)
    expect(
        "package is not present in the registry" in dependency_result.stdout,
        "registry validate should reject unavailable package dependencies",
    )

    cyclic_dependency = workspace / "cyclic-dependency-registry"
    shutil.copytree(registry, cyclic_dependency, ignore=shutil.ignore_patterns(".git"))
    cyclic_metadata = cyclic_dependency / "packages" / "answer" / "1.0.0" / "package.bspm"
    cyclic_metadata.write_text(cyclic_metadata.read_text() + "require answer 1.0.0#0\n")
    cycle_result = run([bspm, "registry", "validate", cyclic_dependency], expected=1)
    expect(
        "cyclic package dependency involving 'answer'" in cycle_result.stdout,
        "registry validate should reject package dependency cycles",
    )


def assert_gcc_header_unit_incremental_build(bspm: Path, workspace: Path, toolchain: Toolchain) -> None:
    fixture = copy_fixture("header-unit", workspace)
    run(build_command(bspm, toolchain, fixture))

    incremental_result = run(build_command(bspm, toolchain, fixture, "-v"))
    expect(
        "up to date: header unit <cstdio>" in incremental_result.stdout,
        "unchanged GCC header units should be reused",
    )
    expect(
        "-xc++-system-header" not in incremental_result.stdout,
        "an unchanged GCC header unit should not invoke the compiler",
    )

    changed_result = run(
        build_command(bspm, toolchain, fixture, "--define", "BSPM_HEADER_UNIT_MODE=1", "-v")
    )
    expect(
        "up to date: header unit <cstdio>" not in changed_result.stdout,
        "changed GCC header-unit compile options should rebuild the artifact",
    )
    expect(
        "-xc++-system-header" in changed_result.stdout,
        "a stale GCC header unit should invoke the compiler",
    )

    changed_incremental_result = run(
        build_command(bspm, toolchain, fixture, "--define", "BSPM_HEADER_UNIT_MODE=1", "-v")
    )
    expect(
        "up to date: header unit <cstdio>" in changed_incremental_result.stdout,
        "the rebuilt GCC header unit should be reused",
    )

    result = run([bspm, "run", fixture])
    expect(result.stdout.strip() == "header-unit: ok", "the GCC header-unit binary should run")


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
    install_result = run([bspm, "install", "-C", fixture])
    expect("installed dependencies" in install_result.stdout, "install should resolve dependencies before a build")
    expect((fixture / "bspm.lock").is_file(), "install should create bspm.lock when it is missing")
    run(build_command(bspm, toolchain, fixture, "--project"))
    incremental_result = run(build_command(bspm, toolchain, fixture, "--project", "-v"))
    expect(
        "up to date: src/answer.cppm" in incremental_result.stdout,
        "cached package sources should build incrementally",
    )
    result = run([bspm, "run"], cwd=fixture)
    expect(result.stdout.strip() == "package-consumer: 42", "registry package should build, link, and run")

    implicit_fixture = create_implicit_registry_package_fixture(workspace, suffix="-implicit")
    run([bspm, "add", "answer"], cwd=implicit_fixture)
    run([bspm, "build", "-c", toolchain.bspm_selector], cwd=implicit_fixture)
    implicit_incremental = run([bspm, "build", "-v", "-c", toolchain.bspm_selector], cwd=implicit_fixture)
    expect(
        "up to date: src/answer.cppm" in implicit_incremental.stdout,
        "simple package sources should build incrementally",
    )
    implicit_result = run([bspm, "run"], cwd=implicit_fixture)
    expect(
        implicit_result.stdout.strip() == "package-consumer: 42",
        "a simple project should import, link, and run its package default target",
    )


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
        assert_semantic_version_resolution(bspm, workspace)
        assert_package_registry_validation(bspm, workspace)

        if level == "full":
            assert_simple_binary(bspm, workspace, toolchain)
            if toolchain.name == "gcc":
                assert_gcc_header_unit_incremental_build(bspm, workspace, toolchain)
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
