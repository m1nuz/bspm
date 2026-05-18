#define _CRT_SECURE_NO_WARNINGS

#include <algorithm>
#include <array>
#include <cctype>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <format>
#include <fstream>
#include <functional>
#include <iterator>
#include <print>
#include <regex>
#include <span>
#include <string>
#include <string_view>
#include <system_error>
#include <tuple>
#include <unordered_map>
#include <unordered_set>
#include <variant>
#include <vector>

namespace fs = std::filesystem;

enum class Target { Bin, Lib, Shared };
enum class Compiler { GCC, Clang, MSVC };
enum class ModuleUnitKind { None, PrimaryInterface, Implementation, PartitionInterface, InternalPartition };

constexpr char VersionTag[] = "version";
constexpr char HelpTag[] = "help";
constexpr char BuildTag[] = "build";
constexpr char RunTag[] = "run";
constexpr char CleanTag[] = "clean";
constexpr char InitTag[] = "init";

constexpr char GPPCompilerTag[] = "g++";
constexpr char GCCCompilerTag[] = "gcc";
constexpr char ClangCompilerTag[] = "clang";
constexpr char ClangPPCompilerTag[] = "clang++";
constexpr char MSVCCompilerTag[] = "msvc";
constexpr char CLCompilerTag[] = "cl";
constexpr char CLExeCompilerTag[] = "cl.exe";

constexpr char StdModuleName[] = "std";
constexpr char StdCompatModuleName[] = "std.compat";

std::string_view commands[] {
    HelpTag,
    InitTag,
    BuildTag,
    RunTag,
    CleanTag,
    VersionTag,
};

constexpr std::string_view VerboseOptTag { "-v" };
constexpr std::string_view BinaryOptTag { "--bin" };
constexpr std::string_view LibraryOptTag { "--lib" };
constexpr std::string_view SharedOptTag { "--shared" };
constexpr std::string_view CompilerOptTag { "-c" };
constexpr std::string_view OutputOptTag { "-o" };
constexpr std::string_view LongOutputOptTag { "--output" };
constexpr std::string_view DebugOptTag { "--debug" };
constexpr std::string_view ReleaseOptTag { "--release" };
constexpr std::string_view DryRunOptTag { "--dry-run" };
constexpr std::string_view DependsOptTag { "--depends" };
constexpr std::string_view ProjectOptTag { "--project" };

std::array BuildOpts {
    BinaryOptTag,
    LibraryOptTag,
    SharedOptTag,
    VerboseOptTag,
    CompilerOptTag,
    OutputOptTag,
    LongOutputOptTag,
    DebugOptTag,
    ReleaseOptTag,
    DryRunOptTag,
    ProjectOptTag,
};

std::array InitOpts {
    VerboseOptTag,
    ProjectOptTag,
};

constexpr char DefaultMain[] = R"(import <print>;

int main(int argc, char** argv) {
    std::println("Hello, world!");
    return 0;
}
)";

struct CompileUnit {
    std::string file_name;
    fs::path file_path;
    fs::path relative_path;
    std::unordered_set<std::string> imports;
    std::unordered_set<std::string> std_module_imports;
    std::unordered_set<std::string> module_imports;
    std::string module_name;
    ModuleUnitKind module_kind { ModuleUnitKind::None };

    auto get_dependencies() const -> std::unordered_set<std::string> {
        std::unordered_set<std::string> all_deps = imports;
        all_deps.insert(module_imports.begin(), module_imports.end());
        return all_deps;
    }

    auto declares_module() const -> bool {
        return module_kind != ModuleUnitKind::None;
    }

    auto is_importable_module_unit() const -> bool {
        return module_kind == ModuleUnitKind::PrimaryInterface || module_kind == ModuleUnitKind::PartitionInterface
            || module_kind == ModuleUnitKind::InternalPartition;
    }

    auto is_primary_implementation_unit() const -> bool {
        return module_kind == ModuleUnitKind::Implementation;
    }
};

struct ModuleDeclaration {
    std::string name;
    ModuleUnitKind kind { ModuleUnitKind::None };
};

struct ScopedCurrentPath {
    fs::path previous_path;

    explicit ScopedCurrentPath(const fs::path& path)
        : previous_path(fs::current_path()) {
        fs::current_path(path);
    }

    ~ScopedCurrentPath() {
        std::error_code ec;
        fs::current_path(previous_path, ec);
    }
};

struct Context {

    static constexpr std::string_view version { "0.0.6" };
    static constexpr std::string_view name { "bspm" };

    using Value = std::variant<uint64_t, double, std::string_view>;
    using Options = std::unordered_map<std::string_view, Value>;

    Compiler compiler { Compiler::GCC };
    std::string cc { "gcc" };
    std::string cpp_c { "g++" };
    std::string cpp_standard { "-std=c++23" };
    std::string cpp_flags { "-fmodules" };
    std::string ld_flags { "-lstdc++exp" };
    std::string object_extension { ".o" };
    std::string output_name;
    fs::path source_dir;
    fs::path build_dir;
    fs::path msvc_dev_cmd;

    std::vector<std::string> import_sys_headers;
    std::vector<std::string> import_std_modules;
    std::vector<std::string> dependency_import_sys_headers;
    std::vector<std::string> dependency_import_std_modules;
    std::unordered_map<std::string, fs::path> dependency_module_artifacts;
    std::vector<fs::path> dependency_gcc_cache_dirs;
    std::vector<fs::path> dependency_link_inputs;
    std::vector<fs::path> dependency_runtime_inputs;
    bool process_sys_imports { true };

    std::vector<CompileUnit> compile_units;

    Target target { Target::Bin };

    bool verbose { false };
    bool debug { true };
    bool dry_run { false };
    bool output_name_configured { false };
    bool project_init { false };
};

struct TargetConfig {
    std::string name;
    fs::path path;
    std::vector<std::string> build_options;
    std::vector<std::string> dependencies;
};

struct ProjectConfig {
    std::string name;
    std::string default_target;
    fs::path root;
    std::vector<TargetConfig> targets;
};

struct BuiltTargetArtifacts {
    Compiler compiler { Compiler::GCC };
    bool debug { true };
    Target target { Target::Bin };
    fs::path build_dir;
    fs::path output_path;
    std::unordered_map<std::string, fs::path> module_artifacts;
    std::vector<std::string> import_sys_headers;
    std::vector<std::string> import_std_modules;
};

auto configure_target_paths(Context& context, const fs::path& source_dir) -> void;

auto quote_arg(std::string_view arg) -> std::string {
    const bool needs_quotes = arg.find_first_of(" \t\"") != std::string_view::npos;
    if (!needs_quotes) {
        return std::string { arg };
    }

    std::string quoted;
    quoted.reserve(arg.size() + 2);
    quoted.push_back('"');
    for (char ch : arg) {
        if (ch == '"') {
            quoted.push_back('\\');
        }
        quoted.push_back(ch);
    }
    quoted.push_back('"');
    return quoted;
}

auto path_arg(const fs::path& path) -> std::string {
    return quote_arg(path.generic_string());
}

auto find_file_recursively(const fs::path& root, std::string_view filename) -> fs::path {
    std::error_code ec;
    if (!fs::exists(root, ec)) {
        return {};
    }

    fs::path candidate;
    fs::recursive_directory_iterator it { root, fs::directory_options::skip_permission_denied, ec };
    fs::recursive_directory_iterator end;
    while (!ec && it != end) {
        if (it->is_regular_file(ec) && it->path().filename() == filename) {
            candidate = it->path();
        }
        it.increment(ec);
    }

    return candidate;
}

auto find_vs_dev_cmd() -> fs::path {
    if (const char* vs_install_dir = std::getenv("VSINSTALLDIR"); vs_install_dir && *vs_install_dir) {
        fs::path dev_cmd = fs::path { vs_install_dir } / "Common7" / "Tools" / "VsDevCmd.bat";
        std::error_code ec;
        if (fs::exists(dev_cmd, ec)) {
            return dev_cmd;
        }
    }

#if defined(_WIN32) || defined(_WIN64)
    for (const char* env_root : { "ProgramFiles", "ProgramFiles(x86)" }) {
        if (const char* root = std::getenv(env_root); root && *root) {
            fs::path dev_cmd = find_file_recursively(fs::path { root } / "Microsoft Visual Studio", "VsDevCmd.bat");
            if (!dev_cmd.empty()) {
                return dev_cmd;
            }
        }
    }

    for (const fs::path& root : {
             fs::path { "C:/Program Files/Microsoft Visual Studio" },
             fs::path { "C:/Program Files (x86)/Microsoft Visual Studio" },
         }) {
        fs::path dev_cmd = find_file_recursively(root, "VsDevCmd.bat");
        if (!dev_cmd.empty()) {
            return dev_cmd;
        }
    }
#endif

    return {};
}

auto wrap_msvc_command(const Context& context, std::string_view command) -> std::string {
    if (context.compiler != Compiler::MSVC || context.msvc_dev_cmd.empty()) {
        return std::string { command };
    }

    std::string wrapped { "call " };
    wrapped += path_arg(context.msvc_dev_cmd);
    wrapped += " -arch=x64 -host_arch=x64 >nul && ";
    wrapped += command;
    return wrapped;
}

auto execute_command(Context& context, std::string_view command, std::span<const std::string> args) -> bool {
    std::string raw_command { command };
    for (const auto& arg : args) {
        raw_command += " " + arg;
    }

    std::string full_command = wrap_msvc_command(context, raw_command);

    if (context.verbose) {
        std::println("command: {}", full_command);
        std::fflush(stdout);
    }

    if (context.dry_run) {
        if (!context.verbose) {
            std::println("command: {}", full_command);
        }
        return true;
    }

    const int status = std::system(std::data(full_command));
    if (status != 0) {
        std::println("Error: command failed with status {}", status);
        return false;
    }

    return true;
}

auto help_command(Context& context, std::string_view command) -> void {
    if (command.empty()) {
        std::println("{} {}", context.name, context.version);
        std::println("");
        std::println("Usage:");
        std::println("\t{} <command> [dir] [options]", context.name);
        std::println("");
        std::println("Commands:");
        for (auto cmd : commands) {
            std::println("\t{}", cmd);
        }
        std::println("");
        std::println("Type '{} help <command>' for command-specific help.", context.name);

        return;
    }

    if (command == VersionTag) {
        std::println("Usage:");
        std::println("\t{} {}", context.name, VersionTag);
        std::println("");
        std::println("Show {} current version.", context.name);
        return;
    }

    if (command == InitTag) {
        std::println("Usage:");
        std::println("\t{} {} <dir> [--project]", context.name, InitTag);
        std::println("");
        std::println("Create a simple folder target, or use --project to scaffold bspm.build and an app target.");
        return;
    }

    if (command == BuildTag) {
        std::println("Usage:");
        std::println("\t{} {} [dir|target|all] [options]", context.name, BuildTag);
        std::println("");
        std::println("Options:");
        std::println("\t-c <g++|clang++|msvc>\tChoose compiler");
        std::println("\t-o, --output <name>\tSet output file name");
        std::println("\t--bin\t\t\tBuild executable target");
        std::println("\t--lib\t\t\tBuild static library target");
        std::println("\t--shared\t\tBuild shared library target");
        std::println("\t--debug\t\t\tBuild with debug flags");
        std::println("\t--release\t\tBuild with optimization flags and NDEBUG");
        std::println("\t--dry-run\t\tPrint compile/link commands without running them");
        std::println("\t--project\t\tTreat [dir] as a project root containing bspm.build");
        std::println("\t-v\t\t\tPrint commands while building");
        return;
    }

    if (command == RunTag) {
        std::println("Usage:");
        std::println("\t{} {} [dir|target] [-v]", context.name, RunTag);
        std::println("");
        std::println("Run the executable produced for [dir].");
        return;
    }

    if (command == CleanTag) {
        std::println("Usage:");
        std::println("\t{} {} [dir|target|all] [-v]", context.name, CleanTag);
        std::println("");
        std::println("Remove generated files for [dir].");
        return;
    }

    std::println("Error: unknown help command '{}'", command);
}

auto version_command(Context& context) -> void {
    std::println("{} {}", context.name, context.version);
}

inline auto is_cppm(const fs::directory_entry& entry) -> bool {
    return entry.path().extension() == ".cppm";
}

auto is_std_module_name(std::string_view module_name) -> bool {
    return module_name == StdModuleName || module_name == StdCompatModuleName;
}

auto ordered_std_module_references(const std::unordered_set<std::string>& imports) -> std::vector<std::string> {
    std::vector<std::string> references;
    if (imports.contains(StdModuleName) || imports.contains(StdCompatModuleName)) {
        references.push_back(StdModuleName);
    }
    if (imports.contains(StdCompatModuleName)) {
        references.push_back(StdCompatModuleName);
    }
    return references;
}

auto extract_import_names_from_file(std::string_view filename)
    -> std::tuple<std::vector<std::string>, std::vector<std::string>, std::vector<std::string>> {
    std::vector<std::string> library_names;
    std::vector<std::string> std_module_names;
    std::vector<std::string> module_names;

    std::ifstream file { std::data(filename) };
    if (!file) {
        std::println("Error: failed to open file '{}'", filename);
        return {};
    }

    std::regex import_sys_regex(R"(^\s*(?:export\s+)?import\s+<([^<>]+)>;)");
    std::regex import_module_regex(
        R"(^\s*(?:export\s+)?import\s+((?::[A-Za-z_][A-Za-z0-9_.]*)|(?:[A-Za-z_][A-Za-z0-9_:.]*))\s*;)");
    std::string line;

    while (std::getline(file, line)) {
        // match sys headers
        {
            std::smatch match;
            if (std::regex_search(line, match, import_sys_regex)) {
                std::string library_name = match[1];

                // TODO: maybe don't need to check
                auto it = std::find(std::begin(library_names), std::end(library_names), library_name);
                if (it == std::end(library_names)) {
                    library_names.push_back(library_name);
                }
            }
        }

        // match modules
        {
            std::smatch match;
            if (std::regex_search(line, match, import_module_regex)) {
                std::string module_name = match[1];
                if (is_std_module_name(module_name)) {
                    std_module_names.push_back(module_name);
                } else {
                    module_names.push_back(module_name);
                }
            }
        }
    }

    file.close();
    return { library_names, std_module_names, module_names };
}

auto extract_module_declaration_from_file(std::string_view filename) -> ModuleDeclaration {
    std::ifstream file { std::data(filename) };
    if (!file) {
        std::println("Error: failed to open file '{}'", filename);
        return {};
    }

    std::regex module_regex(R"(^\s*(export\s+)?module\s+([A-Za-z_][A-Za-z0-9_:.]*)\s*;)");
    std::string line;

    while (std::getline(file, line)) {
        std::smatch match;
        if (std::regex_search(line, match, module_regex)) {
            ModuleDeclaration declaration;
            declaration.name = match[2];

            const auto exported = match[1].matched;
            const auto partition = declaration.name.find(':') != std::string::npos;
            if (exported && partition) {
                declaration.kind = ModuleUnitKind::PartitionInterface;
            } else if (exported) {
                declaration.kind = ModuleUnitKind::PrimaryInterface;
            } else if (partition) {
                declaration.kind = ModuleUnitKind::InternalPartition;
            } else {
                declaration.kind = ModuleUnitKind::Implementation;
            }

            return declaration;
        }
    }

    return {};
}

auto primary_module_name(std::string_view module_name) -> std::string {
    const auto partition_separator = module_name.find(':');
    return std::string { module_name.substr(0, partition_separator) };
}

static auto process_units_imports(Context& context, const std::vector<fs::directory_entry>& entries) -> bool {
    context.compile_units.reserve(std::size(entries));

    std::vector<std::string> imports;
    std::vector<std::string> std_module_imports;
    bool success = true;

    for (auto& entry : entries) {
        CompileUnit unit;

        unit.file_path = entry.path();
        std::error_code ec;
        unit.relative_path = fs::relative(unit.file_path, context.source_dir, ec);
        if (ec) {
            unit.relative_path = unit.file_path.filename();
        }
        unit.file_name = unit.relative_path.generic_string();

        auto [library_names, std_module_names, module_names] = extract_import_names_from_file(entry.path().string());
        const auto declaration = extract_module_declaration_from_file(entry.path().string());
        unit.module_name = declaration.name;
        unit.module_kind = declaration.kind;
        if (!library_names.empty()) {
            for (const auto& library_name : library_names) {
                if (std::find(std::begin(imports), std::end(imports), library_name) == std::end(imports)) {
                    imports.push_back(library_name);
                }
            }

            unit.imports.insert(std::begin(library_names), std::end(library_names));
        }

        if (!std_module_names.empty()) {
            std_module_imports.insert(
                std::end(std_module_imports), std::begin(std_module_names), std::end(std_module_names));

            unit.std_module_imports.insert(std::begin(std_module_names), std::end(std_module_names));
        }

        if (!module_names.empty()) {
            for (const auto& module_name : module_names) {
                if (!module_name.empty() && module_name.front() == ':') {
                    if (!unit.declares_module()) {
                        std::println("Error: '{}' imports local partition '{}' outside a module unit", unit.file_name,
                            module_name);
                        success = false;
                        continue;
                    }

                    unit.module_imports.insert(primary_module_name(unit.module_name) + module_name);
                    continue;
                }

                unit.module_imports.insert(module_name);
            }
        }

        context.compile_units.push_back(unit);
    }

    if (std::find(std::begin(std_module_imports), std::end(std_module_imports), StdCompatModuleName)
            != std::end(std_module_imports)
        && std::find(std::begin(std_module_imports), std::end(std_module_imports), StdModuleName)
            == std::end(std_module_imports)) {
        std_module_imports.push_back(StdModuleName);
    }

    std::sort(std::begin(std_module_imports), std::end(std_module_imports), [](const auto& a, const auto& b) {
        if (a == StdModuleName && b == StdCompatModuleName) {
            return true;
        }
        if (a == StdCompatModuleName && b == StdModuleName) {
            return false;
        }
        return a < b;
    });
    std_module_imports.erase(
        std::unique(std::begin(std_module_imports), std::end(std_module_imports)), std::end(std_module_imports));

    imports.insert(std::end(imports), std::begin(context.dependency_import_sys_headers),
        std::end(context.dependency_import_sys_headers));
    std::sort(std::begin(imports), std::end(imports));
    imports.erase(std::unique(std::begin(imports), std::end(imports)), std::end(imports));

    std_module_imports.insert(std::end(std_module_imports), std::begin(context.dependency_import_std_modules),
        std::end(context.dependency_import_std_modules));
    std::sort(std::begin(std_module_imports), std::end(std_module_imports), [](const auto& a, const auto& b) {
        if (a == StdModuleName && b == StdCompatModuleName) {
            return true;
        }
        if (a == StdCompatModuleName && b == StdModuleName) {
            return false;
        }
        return a < b;
    });
    std_module_imports.erase(
        std::unique(std::begin(std_module_imports), std::end(std_module_imports)), std::end(std_module_imports));

    context.import_sys_headers = imports;
    context.import_std_modules = std_module_imports;
    return success;
}

auto sort_units_by_dependency(Context& context) -> bool {
    std::unordered_map<std::string, std::size_t> importable_module_to_unit;
    for (std::size_t i = 0; i < context.compile_units.size(); ++i) {
        const auto& unit = context.compile_units[i];
        if (!unit.is_importable_module_unit()) {
            continue;
        }

        if (importable_module_to_unit.contains(unit.module_name)) {
            std::println("Error: importable module '{}' is defined more than once", unit.module_name);
            return false;
        }

        importable_module_to_unit[unit.module_name] = i;
    }

    enum class VisitState { NotVisited, Visiting, Visited };

    std::vector<VisitState> states(context.compile_units.size(), VisitState::NotVisited);
    std::vector<CompileUnit> sorted_units;
    sorted_units.reserve(context.compile_units.size());
    std::vector<std::size_t> visit_stack;

    auto dependencies_for_unit = [&](const CompileUnit& unit) {
        std::unordered_set<std::string> dependencies = unit.module_imports;
        if (unit.is_primary_implementation_unit()) {
            dependencies.insert(unit.module_name);
        }
        return dependencies;
    };

    std::function<bool(std::size_t)> visit = [&](std::size_t index) {
        if (states[index] == VisitState::Visited) {
            return true;
        }

        if (states[index] == VisitState::Visiting) {
            auto cycle_start = std::find(std::begin(visit_stack), std::end(visit_stack), index);
            std::string cycle;
            for (auto it = cycle_start; it != std::end(visit_stack); ++it) {
                if (!cycle.empty()) {
                    cycle += " -> ";
                }
                cycle += context.compile_units[*it].file_name;
            }
            if (!cycle.empty()) {
                cycle += " -> ";
            }
            cycle += context.compile_units[index].file_name;
            std::println("Error: cyclic module dependency: {}", cycle);
            return false;
        }

        states[index] = VisitState::Visiting;
        visit_stack.push_back(index);

        const auto& unit = context.compile_units[index];
        for (const auto& dependency : dependencies_for_unit(unit)) {
            auto it = importable_module_to_unit.find(dependency);
            if (it != importable_module_to_unit.end() && !visit(it->second)) {
                return false;
            }

            if (it == importable_module_to_unit.end() && unit.is_primary_implementation_unit()
                && dependency == unit.module_name) {
                std::println("Error: '{}' implements module '{}' but no primary interface unit was found",
                    unit.file_name, unit.module_name);
                return false;
            }

            if (it == importable_module_to_unit.end() && unit.declares_module()
                && dependency.find(':') != std::string::npos
                && primary_module_name(dependency) == primary_module_name(unit.module_name)) {
                std::println("Error: '{}' imports missing local partition '{}'", unit.file_name, dependency);
                return false;
            }
        }

        states[index] = VisitState::Visited;
        visit_stack.pop_back();
        sorted_units.push_back(context.compile_units[index]);
        return true;
    };

    for (std::size_t i = 0; i < context.compile_units.size(); ++i) {
        if (!visit(i)) {
            return false;
        }
    }

    context.compile_units = std::move(sorted_units);
    return true;
}

auto object_path(const Context& context, fs::path source_path) -> fs::path {
    std::error_code ec;
    auto relative_path = fs::relative(source_path, context.source_dir, ec);
    if (ec) {
        relative_path = source_path.filename();
    }

    if (relative_path.extension() == ".cppm") {
        return relative_path.string() + context.object_extension;
    }

    return relative_path.replace_extension(context.object_extension);
}

auto target_is_library(const Context& context) -> bool {
    return context.target == Target::Lib || context.target == Target::Shared;
}

auto shared_import_library_name(std::string_view output_name, [[maybe_unused]] Compiler compiler) -> fs::path {
    fs::path import_library { output_name };
#if defined(_WIN32) || defined(_WIN64)
    if (compiler == Compiler::MSVC || compiler == Compiler::Clang) {
        import_library.replace_extension(".lib");
    }
#endif
    return import_library;
}

auto clang_module_pcm_path(std::string_view module_name) -> fs::path {
    std::string artifact_name { module_name };
    std::replace(artifact_name.begin(), artifact_name.end(), ':', '-');
    artifact_name += ".pcm";
    return artifact_name;
}

auto msvc_module_ifc_path(std::string_view module_name) -> fs::path;

auto gcc_module_gcm_path(std::string_view module_name) -> fs::path {
    std::string artifact_name { module_name };
    std::replace(artifact_name.begin(), artifact_name.end(), ':', '-');
    artifact_name += ".gcm";
    return fs::path { "gcm.cache" } / artifact_name;
}

auto module_artifact_path(const Context& context, std::string_view module_name) -> fs::path {
    if (context.compiler == Compiler::Clang) {
        return context.build_dir / clang_module_pcm_path(module_name);
    }
    if (context.compiler == Compiler::MSVC) {
        return context.build_dir / msvc_module_ifc_path(module_name);
    }
    return context.build_dir / gcc_module_gcm_path(module_name);
}

auto ordered_dependency_module_artifacts(const Context& context) -> std::vector<std::pair<std::string, fs::path>> {
    std::vector<std::pair<std::string, fs::path>> artifacts(
        std::begin(context.dependency_module_artifacts), std::end(context.dependency_module_artifacts));
    std::sort(
        std::begin(artifacts), std::end(artifacts), [](const auto& a, const auto& b) { return a.first < b.first; });
    return artifacts;
}

auto append_dependency_clang_module_references(std::vector<std::string>& args, const Context& context) -> void {
    for (const auto& [module_name, artifact_path] : ordered_dependency_module_artifacts(context)) {
        args.push_back(
            quote_arg(std::string { "-fmodule-file=" } + module_name + "=" + artifact_path.generic_string()));
    }
}

auto append_previous_clang_module_references(
    std::vector<std::string>& args, const Context& context, std::size_t unit_index) -> void {
    for (std::size_t i = 0; i < unit_index; ++i) {
        const auto& unit = context.compile_units[i];
        if (!unit.is_importable_module_unit()) {
            continue;
        }

        args.push_back(std::string { "-fmodule-file=" } + unit.module_name + "="
            + clang_module_pcm_path(unit.module_name).generic_string());
    }
}

auto msvc_module_ifc_path(std::string_view module_name) -> fs::path {
    std::string artifact_name { module_name };
    std::replace(artifact_name.begin(), artifact_name.end(), ':', '-');
    artifact_name += ".ifc";
    return artifact_name;
}

auto msvc_header_ifc_path(std::string_view header) -> fs::path {
    fs::path path = fs::path { ".cache" } / fs::path { header };
    return path.replace_extension(".ifc");
}

auto msvc_header_object_path(std::string_view header) -> fs::path {
    fs::path path = fs::path { ".cache" } / fs::path { header };
    return path.replace_extension(".obj");
}

auto std_module_ifc_path(std::string_view module_name) -> fs::path;

auto find_unit_by_module(const Context& context, std::string_view module_name) -> const CompileUnit* {
    for (const auto& unit : context.compile_units) {
        if (unit.is_importable_module_unit() && unit.module_name == module_name) {
            return &unit;
        }
    }

    return nullptr;
}

auto collect_transitive_msvc_dependencies(const Context& context, std::string_view module_name,
    std::unordered_set<std::string>& visited_modules, std::unordered_set<std::string>& headers,
    std::unordered_set<std::string>& std_modules) -> void {
    if (visited_modules.contains(std::string { module_name })) {
        return;
    }

    visited_modules.insert(std::string { module_name });

    const auto* unit = find_unit_by_module(context, module_name);
    if (!unit) {
        return;
    }

    headers.insert(std::begin(unit->imports), std::end(unit->imports));
    std_modules.insert(std::begin(unit->std_module_imports), std::end(unit->std_module_imports));

    for (const auto& imported_module : unit->module_imports) {
        collect_transitive_msvc_dependencies(context, imported_module, visited_modules, headers, std_modules);
    }
}

auto append_msvc_header_unit_args(std::vector<std::string>& args, const std::unordered_set<std::string>& headers)
    -> void {
    for (const auto& header : headers) {
        args.push_back(std::string { "/headerUnit:angle" });
        args.push_back(header + "=" + msvc_header_ifc_path(header).string());
    }
}

auto append_msvc_std_module_reference_args(
    std::vector<std::string>& args, const std::unordered_set<std::string>& std_modules) -> void {
    for (const auto& imported_std_module : ordered_std_module_references(std_modules)) {
        args.push_back(std::string { "/reference" });
        args.push_back(imported_std_module + "=" + std_module_ifc_path(imported_std_module).string());
    }
}

auto append_msvc_module_reference_args(
    std::vector<std::string>& args, const Context& context, const std::unordered_set<std::string>& modules) -> void {
    std::vector<std::string> ordered_modules(std::begin(modules), std::end(modules));
    std::sort(std::begin(ordered_modules), std::end(ordered_modules));
    for (const auto& imported_module : ordered_modules) {
        args.push_back(std::string { "/reference" });
        if (auto artifact = context.dependency_module_artifacts.find(imported_module);
            artifact != std::end(context.dependency_module_artifacts)) {
            args.push_back(quote_arg(imported_module + "=" + artifact->second.generic_string()));
        } else {
            args.push_back(imported_module + "=" + msvc_module_ifc_path(imported_module).string());
        }
    }
}

auto append_msvc_import_args(std::vector<std::string>& args, const Context& context, const CompileUnit& unit) -> void {
    std::unordered_set<std::string> headers = unit.imports;
    std::unordered_set<std::string> std_modules = unit.std_module_imports;
    std::unordered_set<std::string> module_references = unit.module_imports;
    std::unordered_set<std::string> visited_modules;

    for (const auto& imported_module : unit.module_imports) {
        collect_transitive_msvc_dependencies(context, imported_module, visited_modules, headers, std_modules);
        module_references.insert(std::begin(visited_modules), std::end(visited_modules));
    }
    for (const auto& [module_name, _] : ordered_dependency_module_artifacts(context)) {
        module_references.insert(module_name);
    }

    append_msvc_header_unit_args(args, headers);
    append_msvc_std_module_reference_args(args, std_modules);
    append_msvc_module_reference_args(args, context, module_references);
}

auto std_module_artifact_name(std::string_view module_name, std::string_view extension) -> fs::path {
    std::string artifact_name { module_name };
    std::replace(artifact_name.begin(), artifact_name.end(), '.', '-');
    std::replace(artifact_name.begin(), artifact_name.end(), ':', '-');
    artifact_name += extension;
    return fs::path { ".cache" } / artifact_name;
}

auto std_module_pcm_path(std::string_view module_name) -> fs::path {
    return std_module_artifact_name(module_name, ".pcm");
}

auto std_module_ifc_path(std::string_view module_name) -> fs::path {
    return std_module_artifact_name(module_name, ".ifc");
}

auto std_module_object_path(std::string_view module_name) -> fs::path {
    return std_module_artifact_name(module_name, ".obj");
}

auto gcc_std_module_source(std::string_view module_name) -> std::string {
    if (module_name == StdModuleName) {
        return "bits/std.cc";
    }

    return "bits/std.compat.cc";
}

auto msvc_std_module_filename(std::string_view module_name) -> std::string {
    if (module_name == StdModuleName) {
        return "std.ixx";
    }

    return "std.compat.ixx";
}

auto find_msvc_std_module_source(std::string_view module_name) -> fs::path {
    const auto filename = msvc_std_module_filename(module_name);

    if (const char* vctools_dir = std::getenv("VCToolsInstallDir"); vctools_dir && *vctools_dir) {
        fs::path source = fs::path { vctools_dir } / "modules" / filename;
        std::error_code ec;
        if (fs::exists(source, ec)) {
            return source;
        }
    }

#if defined(_WIN32) || defined(_WIN64)
    for (const fs::path& root : {
             fs::path { "C:/Program Files/Microsoft Visual Studio" },
             fs::path { "C:/Program Files (x86)/Microsoft Visual Studio" },
         }) {
        fs::path source = find_file_recursively(root, filename);
        if (!source.empty()) {
            return source;
        }
    }
#endif

    return {};
}

auto find_clang_std_module_source(std::string_view module_name) -> fs::path {
    if (const char* source_path
        = std::getenv(module_name == StdModuleName ? "BSPM_STD_MODULE" : "BSPM_STD_COMPAT_MODULE");
        source_path && *source_path) {
        return source_path;
    }

    return find_msvc_std_module_source(module_name);
}

auto prepare_std_module_imports(Context& context) -> bool {
    if (context.import_std_modules.empty()) {
        return true;
    }

    if (context.compiler == Compiler::Clang || context.compiler == Compiler::MSVC) {
        if (!fs::exists(".cache")) {
            fs::create_directory(".cache");
        }
    }

    for (const auto& module_name : context.import_std_modules) {
        if (context.compiler == Compiler::GCC) {
            if (!execute_command(context, context.cpp_c,
                    std::array { context.cpp_standard, context.cpp_flags, std::string { "-fsearch-include-path" },
                        std::string { "-c" }, gcc_std_module_source(module_name) })) {
                return false;
            }
        } else if (context.compiler == Compiler::Clang) {
            auto source = find_clang_std_module_source(module_name);
            if (source.empty()) {
                std::println("Error: couldn't find source for standard library module '{}'", module_name);
                std::println("Set {} to the module source path.",
                    module_name == StdModuleName ? "BSPM_STD_MODULE" : "BSPM_STD_COMPAT_MODULE");
                return false;
            }

            if (!fs::exists(".cache/clang-module-cache")) {
                fs::create_directories(".cache/clang-module-cache");
            }

            std::vector<std::string> args {
                context.cpp_standard,
                context.cpp_flags,
                std::string { "-Wno-reserved-module-identifier" },
                std::string { "-Wno-include-angled-in-module-purview" },
                std::string { "-fmodules-cache-path=.cache/clang-module-cache" },
            };

            if (module_name == StdCompatModuleName) {
                args.push_back(
                    std::string { "-fmodule-file=std=" } + std_module_pcm_path(StdModuleName).generic_string());
            }

            args.push_back("-x");
            args.push_back("c++-module");
            args.push_back("--precompile");
            args.push_back(path_arg(source));
            args.push_back("-o");
            args.push_back(path_arg(std_module_pcm_path(module_name)));

            if (!execute_command(context, context.cpp_c, args)) {
                return false;
            }
        } else if (context.compiler == Compiler::MSVC) {
            auto source = find_msvc_std_module_source(module_name);
            if (source.empty()) {
                std::println("Error: couldn't find source for standard library module '{}'", module_name);
                return false;
            }

            std::vector<std::string> args {
                context.cpp_standard,
                context.cpp_flags,
                std::string { "/c" },
                std::string { "/TP" },
                std::string { "/interface" },
            };

            if (module_name == StdCompatModuleName) {
                args.push_back(std::string { "/reference" });
                args.push_back(std::string { "std=" } + std_module_ifc_path(StdModuleName).string());
            }

            args.push_back(path_arg(source));
            args.push_back(std::string { "/ifcOutput" });
            args.push_back(std_module_ifc_path(module_name).string());
            args.push_back(std::string { "/Fo" } + std_module_object_path(module_name).string());

            if (!execute_command(context, context.cpp_c, args)) {
                return false;
            }
        }
    }

    return true;
}

auto remove_gcc_header_unit_cache(Context& context) -> bool {
    if (context.compiler != Compiler::GCC || context.import_sys_headers.empty()) {
        return true;
    }

    const auto cache_path = context.build_dir / "gcm.cache";
    std::error_code ec;
    if (!fs::exists(cache_path, ec)) {
        return true;
    }

    for (const auto& entry : fs::directory_iterator(cache_path, fs::directory_options::skip_permission_denied, ec)) {
        if (ec) {
            std::println("Error: couldn't inspect '{}': {}", cache_path.string(), ec.message());
            return false;
        }

        if (!entry.is_directory(ec)) {
            continue;
        }

        fs::remove_all(entry.path(), ec);
        if (ec) {
            std::println("Error: couldn't remove '{}': {}", entry.path().string(), ec.message());
            return false;
        }

        if (context.verbose) {
            std::println("remove stale GCC header unit cache: {}", entry.path().filename().string());
        }
    }

    return true;
}

auto is_ignored_source_directory(const fs::path& path) -> bool {
    const auto name = path.filename().string();
    if (name.empty()) {
        return false;
    }

    return name == "build" || name == "gcm.cache" || name == ".cache" || name == ".git" || name.front() == '.';
}

auto collect_source_entries(const Context& context) -> std::vector<fs::directory_entry> {
    std::vector<fs::directory_entry> entries;
    std::error_code ec;

    for (fs::recursive_directory_iterator it { context.source_dir, fs::directory_options::skip_permission_denied, ec },
        end;
        !ec && it != end; it.increment(ec)) {
        if (it->is_directory(ec)) {
            if (is_ignored_source_directory(it->path())) {
                it.disable_recursion_pending();
            }
            continue;
        }

        if (!it->is_regular_file(ec)) {
            continue;
        }

        const auto extension = it->path().extension().string();
        if (extension != ".cpp" && extension != ".cppm") {
            continue;
        }

        if (context.verbose) {
            std::error_code relative_ec;
            auto relative_path = fs::relative(it->path(), context.source_dir, relative_ec);
            std::println("entry: {}", relative_ec ? it->path().filename().string() : relative_path.generic_string());
        }

        entries.push_back(*it);
    }

    if (ec) {
        std::println("Error: couldn't inspect '{}': {}", context.source_dir.string(), ec.message());
        return {};
    }

    return entries;
}

auto ensure_parent_directory(const fs::path& path) -> bool {
    if (path.parent_path().empty()) {
        return true;
    }

    std::error_code ec;
    fs::create_directories(path.parent_path(), ec);
    if (ec) {
        std::println("Error: couldn't create '{}': {}", path.parent_path().string(), ec.message());
        return false;
    }

    return true;
}

auto prepare_dependency_artifacts(const Context& context) -> bool {
    if (context.dry_run) {
        return true;
    }

    if (context.compiler == Compiler::GCC) {
        const auto destination_cache = context.build_dir / "gcm.cache";
        std::error_code ec;
        for (const auto& source_cache : context.dependency_gcc_cache_dirs) {
            if (!fs::exists(source_cache, ec)) {
                std::println("Error: dependency module cache '{}' does not exist", source_cache.string());
                return false;
            }

            fs::create_directories(destination_cache, ec);
            if (ec) {
                std::println("Error: couldn't create '{}': {}", destination_cache.string(), ec.message());
                return false;
            }

            fs::copy(source_cache, destination_cache,
                fs::copy_options::recursive | fs::copy_options::overwrite_existing, ec);
            if (ec) {
                std::println("Error: couldn't copy dependency module cache from '{}' to '{}': {}",
                    source_cache.string(), destination_cache.string(), ec.message());
                return false;
            }
        }
    }

#if defined(_WIN32) || defined(_WIN64)
    for (const auto& runtime_input : context.dependency_runtime_inputs) {
        std::error_code ec;
        if (!fs::exists(runtime_input, ec)) {
            std::println("Error: dependency runtime file '{}' does not exist", runtime_input.string());
            return false;
        }

        const auto destination = context.build_dir / runtime_input.filename();
        fs::copy_file(runtime_input, destination, fs::copy_options::overwrite_existing, ec);
        if (ec) {
            std::println("Error: couldn't copy dependency runtime file from '{}' to '{}': {}", runtime_input.string(),
                destination.string(), ec.message());
            return false;
        }
    }
#endif

    return true;
}

auto build_command(Context& context, fs::path dir) -> bool {
    dir = !dir.empty() ? dir : ".";

    // if (context.verbose) {
    //     std::println("{} build '{}'", context.name, dir.string());
    //     return execute_command(context, context.cpp_c, std::array { std::string { "-v" } });
    // }

    auto search_path = fs::absolute(dir);
    if (!fs::exists(search_path) || !fs::is_directory(search_path)) {
        std::println("Error: '{}' not exists!", search_path.string());
        return false;
    }

    configure_target_paths(context, search_path);

    auto entries = collect_source_entries(context);

    // Sort sources with .cppm first, then keep the initial order deterministic.
    std::sort(std::begin(entries), std::end(entries), [&context](const auto& a, const auto& b) {
        if (is_cppm(a) != is_cppm(b)) {
            return is_cppm(a);
        }

        std::error_code a_ec;
        std::error_code b_ec;
        auto a_path = fs::relative(a.path(), context.source_dir, a_ec);
        auto b_path = fs::relative(b.path(), context.source_dir, b_ec);
        return (a_ec ? a.path() : a_path).generic_string() < (b_ec ? b.path() : b_path).generic_string();
    });

    if (!process_units_imports(context, entries)) {
        return false;
    }

    if (!sort_units_by_dependency(context)) {
        return false;
    }

    if (!remove_gcc_header_unit_cache(context)) {
        return false;
    }

    std::error_code build_dir_ec;
    fs::create_directories(context.build_dir, build_dir_ec);
    if (build_dir_ec) {
        std::println("Error: couldn't create '{}': {}", context.build_dir.string(), build_dir_ec.message());
        return false;
    }

    if (!prepare_dependency_artifacts(context)) {
        return false;
    }

    ScopedCurrentPath current_path { context.build_dir };

    // build
    if (!prepare_std_module_imports(context)) {
        return false;
    }

    if (context.process_sys_imports) {

        if ((context.compiler == Compiler::Clang || context.compiler == Compiler::MSVC)
            && !context.import_sys_headers.empty()) {
            if (!fs::exists(".cache")) {
                fs::create_directory(".cache");
            }
        }

        for (const auto& header : context.import_sys_headers) {

            if (context.compiler == Compiler::GCC) {
                if (!execute_command(context, context.cpp_c,
                        std::array { context.cpp_standard, context.cpp_flags, std::string { "-xc++-system-header" },
                            std::string { "-c" }, header })) {
                    return false;
                }
            } else if (context.compiler == Compiler::Clang) {
                fs::path header_path { header };

                if (!execute_command(context, context.cpp_c,
                        std::array { context.cpp_standard, context.cpp_flags,
                            std::string { "-xc++-system-header --precompile" }, header, std::string { "-o" },
                            (".cache" / header_path).replace_extension(".pcm").string() })) {
                    return false;
                }
            } else if (context.compiler == Compiler::MSVC) {
                auto ifc_path = msvc_header_ifc_path(header);
                auto obj_path = msvc_header_object_path(header);

                if (!ifc_path.parent_path().empty()) {
                    fs::create_directories(ifc_path.parent_path());
                }

                if (!execute_command(context, context.cpp_c,
                        std::array { context.cpp_standard, context.cpp_flags, std::string { "/c" },
                            std::string { "/exportHeader" }, std::string { "/headerName:angle" }, header,
                            std::string { "/ifcOutput" }, ifc_path.string(),
                            std::string { "/Fo" } + obj_path.string() })) {
                    return false;
                }
            }
        }
    }

    for (std::size_t unit_index = 0; unit_index < context.compile_units.size(); ++unit_index) {
        const auto& unit = context.compile_units[unit_index];
        fs::path entry_path = unit.file_path;
        auto unit_object_path = object_path(context, unit.file_path);
        if (!ensure_parent_directory(unit_object_path)) {
            return false;
        }

        auto extension = entry_path.extension().string();
        if (extension == ".cppm" && !unit.declares_module()) {
            std::println("Error: '{}' uses the .cppm extension but does not declare a module unit", unit.file_name);
            return false;
        }

        if (!unit.is_importable_module_unit()) {
            std::vector<std::string> args;

            args.push_back(context.cpp_standard);
            args.push_back(context.cpp_flags);

            if (context.compiler == Compiler::MSVC) {
                args.push_back(std::string { "/c" });
                args.push_back(std::string { "/TP" });

                append_msvc_import_args(args, context, unit);

                args.push_back(std::string { "/Fo" } + unit_object_path.string());
                args.push_back(path_arg(unit.file_path));
            } else if (context.compiler == Compiler::Clang) {
                args.push_back("-Wno-experimental-header-units");

                for (const auto& imported_module : unit.imports) {
                    std::string module_file;
                    std::format_to(std::back_inserter(module_file), "-fmodule-file=\"{}.pcm\"",
                        (fs::path { ".cache" } / imported_module).string());
                    args.push_back(module_file);
                }

                for (const auto& imported_std_module : ordered_std_module_references(unit.std_module_imports)) {
                    args.push_back(std::string { "-fmodule-file=" } + imported_std_module + "="
                        + std_module_pcm_path(imported_std_module).generic_string());
                }

                append_dependency_clang_module_references(args, context);
                append_previous_clang_module_references(args, context, unit_index);
            }

            if (context.compiler != Compiler::MSVC) {
                if (extension == ".cppm") {
                    args.push_back(std::string { "-xc++" });
                }

                args.push_back(std::string { "-c" });
                args.push_back(path_arg(unit.file_path));
                args.push_back(std::string { "-o" });
                args.push_back(path_arg(unit_object_path));
            }

            if (!execute_command(context, context.cpp_c, args)) {
                return false;
            }
        } else {
            std::vector<std::string> args;

            args.push_back(context.cpp_standard);
            args.push_back(context.cpp_flags);

            if (context.compiler == Compiler::MSVC) {
                args.push_back(std::string { "/c" });
                args.push_back(std::string { "/TP" });
                if (unit.module_kind == ModuleUnitKind::InternalPartition) {
                    args.push_back(std::string { "/internalPartition" });
                } else {
                    args.push_back(std::string { "/interface" });
                }

                append_msvc_import_args(args, context, unit);

                args.push_back(std::string { "/ifcOutput" });
                args.push_back(msvc_module_ifc_path(unit.module_name).string());
                args.push_back(std::string { "/Fo" } + unit_object_path.string());
                args.push_back(path_arg(unit.file_path));
            } else if (context.compiler == Compiler::Clang) {
                args.push_back(std::string { "-xc++" });
                args.push_back(std::string { "-xc++-module" });
                args.push_back(std::string { "--precompile" });
                args.push_back("-Wno-experimental-header-units");

                for (const auto& imported_module : unit.imports) {
                    std::string module_file;
                    std::format_to(std::back_inserter(module_file), "-fmodule-file=\"{}.pcm\"",
                        (fs::path { ".cache" } / imported_module).string());
                    args.push_back(module_file);
                }

                for (const auto& imported_std_module : ordered_std_module_references(unit.std_module_imports)) {
                    args.push_back(std::string { "-fmodule-file=" } + imported_std_module + "="
                        + std_module_pcm_path(imported_std_module).generic_string());
                }

                append_dependency_clang_module_references(args, context);
                append_previous_clang_module_references(args, context, unit_index);

                args.push_back(path_arg(unit.file_path));
                args.push_back(std::string { "-o" });
                args.push_back(clang_module_pcm_path(unit.module_name).generic_string());

                if (!execute_command(context, context.cpp_c, args)) {
                    return false;
                }

                std::vector<std::string> object_args {
                    context.cpp_standard,
                    context.cpp_flags,
                };

                append_dependency_clang_module_references(object_args, context);
                append_previous_clang_module_references(object_args, context, unit_index);

                object_args.push_back(std::string { "-c" });
                object_args.push_back(clang_module_pcm_path(unit.module_name).generic_string());
                object_args.push_back(std::string { "-o" });
                object_args.push_back(path_arg(unit_object_path));

                if (!execute_command(context, context.cpp_c, object_args)) {
                    return false;
                }

                continue;
            } else {
                args.push_back(std::string { "-xc++" });
            }

            if (context.compiler != Compiler::MSVC) {
                args.push_back(std::string { "-c" });
                args.push_back(path_arg(unit.file_path));
                args.push_back(std::string { "-o" });
                args.push_back(path_arg(unit_object_path));
            }

            if (!execute_command(context, context.cpp_c, args)) {
                return false;
            }
        }
    }

    // link
    std::vector<std::string> link_entries;
    for (const auto& unit : context.compile_units) {
        link_entries.push_back(object_path(context, unit.file_path).string());
    }

    if (context.compiler == Compiler::MSVC) {
        for (const auto& header : context.import_sys_headers) {
            link_entries.push_back(msvc_header_object_path(header).string());
        }

        for (const auto& module_name : context.import_std_modules) {
            link_entries.push_back(std_module_object_path(module_name).string());
        }
    }

    if (context.target != Target::Lib) {
        for (const auto& dependency_link_input : context.dependency_link_inputs) {
            link_entries.push_back(path_arg(dependency_link_input));
        }
    }

    if (!ensure_parent_directory(fs::path { context.output_name })) {
        return false;
    }

    std::vector<std::string> link_args;
    std::string link_command = context.cpp_c;

    if (context.target == Target::Lib) {
        if (context.compiler == Compiler::MSVC) {
            link_command = "lib";
            link_args.push_back("/nologo");
            link_args.push_back(std::string { "/OUT:" } + context.output_name);
            link_args.insert(std::end(link_args), std::cbegin(link_entries), std::cend(link_entries));
        } else {
            link_command = "ar";
            link_args.push_back("rcs");
            link_args.push_back(context.output_name);
            link_args.insert(std::end(link_args), std::cbegin(link_entries), std::cend(link_entries));
        }
    } else {
        if (context.target == Target::Shared) {
            if (context.compiler == Compiler::MSVC) {
                link_args.push_back("/LD");
            } else {
                link_args.push_back("-shared");
#if defined(_WIN32) || defined(_WIN64)
                if (context.compiler == Compiler::Clang) {
                    link_args.push_back(std::string { "-Wl,/implib:" }
                        + shared_import_library_name(context.output_name, context.compiler).generic_string());
                }
#endif
            }
        }

        link_args.insert(std::end(link_args), std::cbegin(link_entries), std::cend(link_entries));
        if (!context.ld_flags.empty()) {
            link_args.push_back(context.ld_flags);
        }

        if (context.compiler == Compiler::MSVC) {
            link_args.push_back(std::string { "/Fe" } + context.output_name);
        } else {
            link_args.push_back("-o");
            link_args.push_back(context.output_name);
        }
    }

    if (!execute_command(context, link_command, link_args)) {
        return false;
    }

    return true;
}

static bool is_file_executable(std::string_view filename) {
#if defined(_WIN32) || defined(_WIN64)
    const auto extension = fs::path { filename }.extension().string();
    return extension == ".exe" || extension == ".bat" || extension == ".cmd" || extension == ".com";
#else
    std::filesystem::file_status status = std::filesystem::status(filename);
    return (status.permissions() & std::filesystem::perms::owner_exec) != std::filesystem::perms::none;
#endif
}

static bool is_library_file(const fs::path& path) {
    const auto extension = path.extension().string();
    return extension == ".a" || extension == ".lib" || extension == ".so" || extension == ".dll"
        || extension == ".dylib";
}

static auto newest_executable_in(const fs::path& root, std::string_view preferred_name = {}) -> fs::path {
    std::error_code ec;
    if (!fs::exists(root, ec)) {
        return {};
    }

    fs::path newest_path;
    fs::file_time_type newest_time {};

    for (fs::recursive_directory_iterator it { root, fs::directory_options::skip_permission_denied, ec }, end;
        !ec && it != end; it.increment(ec)) {
        std::error_code entry_ec;
        if (!it->is_regular_file(entry_ec)) {
            continue;
        }

        if (!preferred_name.empty() && it->path().filename().string() != preferred_name) {
            continue;
        }

        if (!is_file_executable(it->path().string())) {
            continue;
        }

        auto modified_at = it->last_write_time(entry_ec);
        if (entry_ec) {
            continue;
        }

        if (newest_path.empty() || modified_at > newest_time) {
            newest_path = it->path();
            newest_time = modified_at;
        }
    }

    return newest_path;
}

static auto newest_library_in(const fs::path& root) -> fs::path {
    std::error_code ec;
    if (!fs::exists(root, ec)) {
        return {};
    }

    fs::path newest_path;
    fs::file_time_type newest_time {};

    for (fs::recursive_directory_iterator it { root, fs::directory_options::skip_permission_denied, ec }, end;
        !ec && it != end; it.increment(ec)) {
        std::error_code entry_ec;
        if (!it->is_regular_file(entry_ec) || !is_library_file(it->path())) {
            continue;
        }

        auto modified_at = it->last_write_time(entry_ec);
        if (entry_ec) {
            continue;
        }

        if (newest_path.empty() || modified_at > newest_time) {
            newest_path = it->path();
            newest_time = modified_at;
        }
    }

    return newest_path;
}

static auto find_app_file(const Context& context) -> fs::path {
    auto output_path = context.build_dir / context.output_name;
    if (fs::is_regular_file(output_path) && is_file_executable(output_path.string())) {
        return output_path;
    }

    const auto build_root = context.source_dir / "build";
    auto matching_output = newest_executable_in(build_root, context.output_name);
    if (!matching_output.empty()) {
        return matching_output;
    }

    return newest_executable_in(build_root);
}

auto run_command(Context& context, fs::path dir) -> bool {
    dir = !dir.empty() ? dir : ".";

    auto search_path = fs::absolute(dir);
    if (!fs::exists(search_path) || !fs::is_directory(search_path)) {
        std::println("Error: '{}' not exists!", search_path.string());
        return false;
    }

    configure_target_paths(context, search_path);

    if (target_is_library(context)) {
        std::println("Error: '{}' produces a library, not a runnable executable", dir.string());
        return false;
    }

    auto app_file = find_app_file(context);
    if (app_file.empty() || !fs::exists(app_file)) {
        const auto library_file = newest_library_in(context.source_dir / "build");
        if (!library_file.empty()) {
            std::println("Error: '{}' produces a library, not a runnable executable", dir.string());
            return false;
        }

        std::println("Error: couldn't run from '{}'", dir.string());
        return false;
    }

    if (context.verbose) {
        std::println("{} running '{}'", context.name, app_file.string());
        std::fflush(stdout);
    }

    auto app_command = path_arg(app_file);
    return std::system(std::data(app_command)) == 0;
}

auto clean_command(Context& context, fs::path dir) -> bool {
    dir = !dir.empty() ? dir : ".";

    auto search_path = fs::absolute(dir);
    if (!fs::exists(search_path) || !fs::is_directory(search_path)) {
        std::println("Error: '{}' not exists!", search_path.string());
        return false;
    }

    configure_target_paths(context, search_path);

    const auto build_root = context.source_dir / "build";
    const auto normalized_source = fs::absolute(context.source_dir).lexically_normal();
    const auto normalized_build_root = fs::absolute(build_root).lexically_normal();
    if (normalized_build_root.parent_path() != normalized_source || normalized_build_root.filename() != "build") {
        std::println("Error: refusing to remove unexpected build directory '{}'", normalized_build_root.string());
        return false;
    }

    std::error_code ec;
    if (fs::exists(normalized_build_root, ec)) {
        fs::remove_all(normalized_build_root, ec);
        if (ec) {
            std::println("Error: couldn't remove '{}': {}", normalized_build_root.string(), ec.message());
            return false;
        }

        if (context.verbose) {
            std::println("remove entry: {}", normalized_build_root.string());
        }
    }

    return true;
}

auto project_name_for(const fs::path& dir) -> std::string {
    auto normalized_dir = fs::absolute(dir).lexically_normal();
    auto project_name = normalized_dir.filename().string();
    return project_name.empty() ? "app" : project_name;
}

auto init_command(Context& context, fs::path dir) -> void {
    if (!fs::exists(dir)) {
        fs::create_directories(dir);
    }

    // Create packages dependency file
    {
        fs::path fullpath = dir / ".dependencies";
        if (!fs::exists(fullpath)) {
            std::ofstream f { fullpath };
            if (f.good()) {
                f.close();
            }
        }
    }

    if (context.project_init) {
        const auto project_name = project_name_for(dir);

        fs::path config_path = dir / "bspm.build";
        if (!fs::exists(config_path)) {
            std::ofstream f { config_path };
            if (f.good()) {
                f << "project " << project_name << "\n";
                f << "default app\n\n";
                f << "target app app --bin -o " << project_name << "\n";
                f.close();
            }
        }

        fs::create_directories(dir / "app");
        constexpr char main_file[] = "main.cpp";
        fs::path fullpath = dir / "app" / main_file;

        if (!fs::exists(fullpath)) {
            std::ofstream f { fullpath };
            if (f.good()) {
                f.write(DefaultMain, sizeof(DefaultMain) - 1);
                f.close();
            }
        }

        if (!fs::exists(fullpath)) {
            std::println("Error: couldn't create '{}'", fullpath.string());
        }

        return;
    }

    if (context.target == Target::Bin) {
        constexpr char main_file[] = "main.cpp";
        fs::path fullpath = dir / main_file;

        if (!fs::exists(fullpath)) {
            std::ofstream f { fullpath };
            if (f.good()) {
                f.write(DefaultMain, sizeof(DefaultMain) - 1);
                f.close();
            }
        }

        if (!fs::exists(fullpath)) {
            std::println("Error: couldn't create '{}'", fullpath.string());
        }
    }
}

struct InitConfiguration {
    int32_t argc { 0 };
    int32_t index { 0 };
    char** argv { nullptr };
};

static auto check_option(std::span<const std::string_view> opts, std::string_view opt) -> bool {
    for (auto o : opts) {
        if (o == opt) {
            return true;
        }
    }

    return false;
}

auto init_gcc_compiler(Context& context) -> void {
    context.compiler = Compiler::GCC;
    context.cpp_c = GPPCompilerTag;
    context.cc = GCCCompilerTag;
    context.cpp_standard = "-std=c++23";
    context.cpp_flags = "-fmodules";
    context.ld_flags = "-lstdc++exp";
    context.object_extension = ".o";
    context.process_sys_imports = true;
}

auto init_clang_compiler(Context& context) -> void {
    context.compiler = Compiler::Clang;
    context.cc = "clang";
    context.cpp_c = "clang++";
    context.cpp_standard = "-std=c++23";
    context.cpp_flags = "-fmodules";
    context.ld_flags.clear();
    context.object_extension = ".o";
    context.process_sys_imports = true;
}

auto init_msvc_compiler(Context& context) -> void {
    context.compiler = Compiler::MSVC;
    context.cc = CLCompilerTag;
    context.cpp_c = CLCompilerTag;
    context.cpp_standard = "/std:c++latest";
    context.cpp_flags = "/EHsc /nologo";
    context.ld_flags.clear();
    context.object_extension = ".obj";
    context.msvc_dev_cmd = find_vs_dev_cmd();
    context.process_sys_imports = true;
}

auto compiler_build_name(const Context& context) -> std::string_view {
    switch (context.compiler) {
    case Compiler::GCC:
        return "gcc";
    case Compiler::Clang:
        return "clang";
    case Compiler::MSVC:
        return "msvc";
    }

    return "unknown";
}

auto build_mode_name(const Context& context) -> std::string_view {
    return context.debug ? "debug" : "release";
}

auto configure_target_paths(Context& context, const fs::path& source_dir) -> void {
    context.source_dir = fs::absolute(source_dir);
    context.build_dir = context.source_dir / "build"
        / (std::string { compiler_build_name(context) } + "-" + std::string { build_mode_name(context) });
}

auto append_build_mode_flags(Context& context) -> void {
    if (context.compiler == Compiler::MSVC) {
        if (context.debug) {
            context.cpp_flags += " /Zi";
        } else {
            context.cpp_flags += " /O2 /DNDEBUG";
        }
    } else {
        if (context.debug) {
            context.cpp_flags += " -g";
        } else {
            context.cpp_flags += " -O2 -DNDEBUG";
        }
    }
}

auto append_target_compile_flags(Context& context) -> void {
#if !defined(_WIN32) && !defined(_WIN64)
    if (context.target == Target::Shared && context.compiler != Compiler::MSVC) {
        context.cpp_flags += " -fPIC";
    }
#else
    (void)context;
#endif
}

auto output_has_library_prefix(const fs::path& path) -> bool {
    return path.filename().string().starts_with("lib");
}

auto prefix_output_filename(fs::path path, std::string_view prefix) -> fs::path {
    auto filename = path.filename().string();
    path.replace_filename(std::string { prefix } + filename);
    return path;
}

auto normalize_output_name(Context& context) -> void {
    if (!context.output_name_configured && target_is_library(context)) {
        context.output_name = "a";
    }

    fs::path output_path { context.output_name };
    const bool has_extension = !output_path.extension().empty();

    if (context.target == Target::Bin) {
#if defined(_WIN32) || defined(_WIN64)
        if (!has_extension) {
            context.output_name += ".exe";
        }
#endif
        return;
    }

    if (context.target == Target::Lib) {
        if (context.compiler == Compiler::MSVC) {
            if (!has_extension) {
                context.output_name += ".lib";
            }
            return;
        }

        if (!has_extension && !output_has_library_prefix(output_path)) {
            output_path = prefix_output_filename(output_path, "lib");
        }
        if (!has_extension) {
            output_path += ".a";
        }
        context.output_name = output_path.string();
        return;
    }

    if (context.target == Target::Shared) {
#if defined(_WIN32) || defined(_WIN64)
        if (!has_extension) {
            context.output_name += ".dll";
        }
#else
        if (!has_extension && !output_has_library_prefix(output_path)) {
            output_path = prefix_output_filename(output_path, "lib");
        }
        if (!has_extension) {
            output_path += ".so";
        }
        context.output_name = output_path.string();
#endif
    }
}

auto is_option_argument(std::string_view argument) -> bool {
    return !argument.empty() && argument.front() == '-';
}

auto tokenize_config_line(std::string_view line, std::size_t line_number, bool& ok) -> std::vector<std::string> {
    std::vector<std::string> tokens;
    std::string token;
    bool in_quotes = false;
    bool escaped = false;

    for (char ch : line) {
        if (escaped) {
            token.push_back(ch);
            escaped = false;
            continue;
        }

        if (in_quotes && ch == '\\') {
            escaped = true;
            continue;
        }

        if (ch == '"') {
            in_quotes = !in_quotes;
            continue;
        }

        if (!in_quotes && ch == '#') {
            break;
        }

        if (!in_quotes && std::isspace(static_cast<unsigned char>(ch))) {
            if (!token.empty()) {
                tokens.push_back(std::move(token));
                token.clear();
            }
            continue;
        }

        token.push_back(ch);
    }

    if (escaped || in_quotes) {
        std::println("Error: invalid quoted string in bspm.build line {}", line_number);
        ok = false;
        return {};
    }

    if (!token.empty()) {
        tokens.push_back(std::move(token));
    }

    return tokens;
}

auto apply_build_options(Context& context, std::span<const std::string_view> opts, std::string_view source) -> bool {
    for (std::size_t idx = 0; idx < opts.size(); ++idx) {
        const auto opt = opts[idx];

        if (opt == VerboseOptTag) {
            context.verbose = true;
            continue;
        }

        if (!check_option(BuildOpts, opt)) {
            std::println("Error: Invalid option '{}' in {}", opt, source);
            return false;
        }

        if (opt == BinaryOptTag) {
            context.target = Target::Bin;
        } else if (opt == LibraryOptTag) {
            context.target = Target::Lib;
        } else if (opt == SharedOptTag) {
            context.target = Target::Shared;
        } else if (opt == DebugOptTag) {
            context.debug = true;
        } else if (opt == ReleaseOptTag) {
            context.debug = false;
        } else if (opt == DryRunOptTag) {
            context.dry_run = true;
        } else if (opt == CompilerOptTag) {
            if (idx + 1 >= opts.size() || is_option_argument(opts[idx + 1])) {
                std::println("Error: Invalid option '{}' in {}, option didn't have parameter", opt, source);
                return false;
            }

            const auto compiler = opts[++idx];
            if (compiler == GPPCompilerTag || compiler == GCCCompilerTag) {
                init_gcc_compiler(context);
            } else if (compiler == ClangPPCompilerTag || compiler == ClangCompilerTag) {
                init_clang_compiler(context);
            } else if (compiler == MSVCCompilerTag || compiler == CLCompilerTag || compiler == CLExeCompilerTag) {
                init_msvc_compiler(context);
            } else {
                std::println("Error: Unknown compiler parameter {} in {}", compiler, source);
                return false;
            }
        } else if (opt == OutputOptTag || opt == LongOutputOptTag) {
            if (idx + 1 >= opts.size() || is_option_argument(opts[idx + 1])) {
                std::println("Error: Invalid option '{}' in {}, option didn't have parameter", opt, source);
                return false;
            }

            context.output_name = opts[++idx];
            context.output_name_configured = true;
        }
    }

    return true;
}

auto finalize_build_context(Context& context) -> void {
    append_build_mode_flags(context);
    append_target_compile_flags(context);
    normalize_output_name(context);
}

auto parse_project_config(const fs::path& config_path, ProjectConfig& project) -> bool {
    std::ifstream file { config_path };
    if (!file) {
        std::println("Error: failed to open '{}'", config_path.string());
        return false;
    }

    project.root = config_path.parent_path();

    std::string line;
    std::size_t line_number = 0;
    while (std::getline(file, line)) {
        ++line_number;

        bool ok = true;
        auto tokens = tokenize_config_line(line, line_number, ok);
        if (!ok) {
            return false;
        }
        if (tokens.empty()) {
            continue;
        }

        if (tokens[0] == "project") {
            if (tokens.size() != 2) {
                std::println("Error: bspm.build line {} expects 'project <name>'", line_number);
                return false;
            }
            project.name = tokens[1];
            continue;
        }

        if (tokens[0] == "default") {
            if (tokens.size() != 2) {
                std::println("Error: bspm.build line {} expects 'default <target>'", line_number);
                return false;
            }
            project.default_target = tokens[1];
            continue;
        }

        if (tokens[0] != "target") {
            std::println("Error: unknown bspm.build directive '{}' on line {}", tokens[0], line_number);
            return false;
        }

        if (tokens.size() < 3) {
            std::println("Error: bspm.build line {} expects 'target <name> <path> [options]'", line_number);
            return false;
        }

        TargetConfig target;
        target.name = tokens[1];
        target.path = tokens[2];

        for (std::size_t idx = 3; idx < tokens.size(); ++idx) {
            if (tokens[idx] == DependsOptTag) {
                if (idx + 1 >= tokens.size() || is_option_argument(tokens[idx + 1])) {
                    std::println("Error: invalid '{}' on bspm.build line {}", DependsOptTag, line_number);
                    return false;
                }
                target.dependencies.push_back(tokens[++idx]);
                continue;
            }

            target.build_options.push_back(tokens[idx]);
        }

        if (std::any_of(project.targets.begin(), project.targets.end(),
                [&](const auto& existing) { return existing.name == target.name; })) {
            std::println("Error: target '{}' is defined more than once", target.name);
            return false;
        }

        Context validation_context;
        std::vector<std::string_view> option_views;
        option_views.reserve(target.build_options.size());
        for (const auto& option : target.build_options) {
            option_views.push_back(option);
        }
        if (!apply_build_options(validation_context, option_views, "bspm.build target options")) {
            return false;
        }

        project.targets.push_back(std::move(target));
    }

    if (project.targets.empty()) {
        std::println("Error: '{}' does not define any targets", config_path.string());
        return false;
    }

    if (project.default_target.empty() && project.targets.size() == 1) {
        project.default_target = project.targets.front().name;
    }

    if (!project.default_target.empty()) {
        const bool default_exists = std::any_of(project.targets.begin(), project.targets.end(),
            [&](const auto& target) { return target.name == project.default_target; });
        if (!default_exists) {
            std::println("Error: default target '{}' is not defined", project.default_target);
            return false;
        }
    }

    for (const auto& target : project.targets) {
        for (const auto& dependency : target.dependencies) {
            const bool dependency_exists = std::any_of(project.targets.begin(), project.targets.end(),
                [&](const auto& candidate) { return candidate.name == dependency; });
            if (!dependency_exists) {
                std::println("Error: target '{}' depends on unknown target '{}'", target.name, dependency);
                return false;
            }
        }
    }

    return true;
}

auto init_context(Context& context, std::string_view command, const InitConfiguration& conf) -> bool {
#if defined(_WIN32) || defined(_WIN64)
    context.output_name = "a.exe";
#else
    context.output_name = "a.out";
#endif

    if (conf.argv && conf.index < conf.argc) {
        int32_t idx = conf.index;

        while (idx < conf.argc) {
            std::string_view opt { conf.argv[idx] };

            if (opt == VerboseOptTag) {
                context.verbose = true;
                idx++;
                continue;
            }

            if (command != BuildTag) {
                if (command != InitTag || !check_option(InitOpts, opt)) {
                    std::println("Error: Invalid option '{}' for {} command", opt, command);
                    return false;
                }

                if (opt == ProjectOptTag) {
                    context.project_init = true;
                }

                idx++;
                continue;
            }

            if (command == BuildTag) {
                if (!check_option(BuildOpts, opt)) {
                    std::println("Error: Invalid option '{}' for {} command", opt, command);
                    return false;
                }

                if (opt == BinaryOptTag) {
                    context.target = Target::Bin;
                } else if (opt == LibraryOptTag) {
                    context.target = Target::Lib;
                } else if (opt == SharedOptTag) {
                    context.target = Target::Shared;
                } else if (opt == DebugOptTag) {
                    context.debug = true;
                } else if (opt == ReleaseOptTag) {
                    context.debug = false;
                } else if (opt == DryRunOptTag) {
                    context.dry_run = true;
                }

                if (opt == CompilerOptTag) {
                    if (idx + 1 < conf.argc) {
                        idx++;
                        std::string_view compiler { conf.argv[idx] };

                        if (is_option_argument(compiler)) {
                            std::println("Error: Invalid option '{}' for {} command, option didn't have {}", opt,
                                command, "parameter");
                            return false;
                        }

                        if (compiler == GPPCompilerTag || compiler == GCCCompilerTag) {
                            init_gcc_compiler(context);
                        } else if (compiler == ClangPPCompilerTag || compiler == ClangCompilerTag) {
                            init_clang_compiler(context);
                        } else if (compiler == MSVCCompilerTag || compiler == CLCompilerTag
                            || compiler == CLExeCompilerTag) {
                            init_msvc_compiler(context);
                        } else {
                            std::println("Error: Unknown compiler parameter {} for {} command", compiler, command);
                            return false;
                        }

                    } else {
                        std::println("Error: Invalid option '{}' for {} command, option didn't have {}", opt, command,
                            "parameter");
                        return false;
                    }
                } else if (opt == OutputOptTag || opt == LongOutputOptTag) {
                    if (idx + 1 < conf.argc) {
                        idx++;
                        std::string_view output_name { conf.argv[idx] };

                        if (is_option_argument(output_name)) {
                            std::println("Error: Invalid option '{}' for {} command, option didn't have {}", opt,
                                command, "parameter");
                            return false;
                        }

                        context.output_name = output_name;
                        context.output_name_configured = true;
                    } else {
                        std::println("Error: Invalid option '{}' for {} command, option didn't have {}", opt, command,
                            "parameter");
                        return false;
                    }
                }
            }

            idx++;
        }
    }

    if (command == BuildTag) {
        finalize_build_context(context);
    }

    return true;
}

auto find_target(const ProjectConfig& project, std::string_view name) -> const TargetConfig* {
    for (const auto& target : project.targets) {
        if (target.name == name) {
            return &target;
        }
    }

    return nullptr;
}

auto build_option_views(const std::vector<std::string>& options) -> std::vector<std::string_view> {
    std::vector<std::string_view> views;
    views.reserve(options.size());
    for (const auto& option : options) {
        views.push_back(option);
    }
    return views;
}

auto create_target_context(const TargetConfig& target, std::span<const std::string_view> cli_options, Context& context)
    -> bool {
    auto config_options = build_option_views(target.build_options);
    if (!apply_build_options(context, config_options, "bspm.build target options")) {
        return false;
    }
    if (!apply_build_options(context, cli_options, "command line")) {
        return false;
    }

    finalize_build_context(context);
    return true;
}

auto collect_built_target_artifacts(const Context& context) -> BuiltTargetArtifacts {
    BuiltTargetArtifacts artifacts;
    artifacts.compiler = context.compiler;
    artifacts.debug = context.debug;
    artifacts.target = context.target;
    artifacts.build_dir = context.build_dir;
    artifacts.output_path = context.build_dir / context.output_name;
    artifacts.import_sys_headers = context.import_sys_headers;
    artifacts.import_std_modules = context.import_std_modules;

    for (const auto& unit : context.compile_units) {
        if (unit.is_importable_module_unit()) {
            artifacts.module_artifacts[unit.module_name] = module_artifact_path(context, unit.module_name);
        }
    }

    return artifacts;
}

auto shared_link_input_path(const BuiltTargetArtifacts& artifacts) -> fs::path {
#if defined(_WIN32) || defined(_WIN64)
    if (artifacts.compiler == Compiler::MSVC || artifacts.compiler == Compiler::Clang) {
        return artifacts.build_dir
            / shared_import_library_name(artifacts.output_path.filename().string(), artifacts.compiler);
    }
#endif
    return artifacts.output_path;
}

auto append_unique_path(std::vector<fs::path>& paths, const fs::path& path) -> void {
    if (std::find(std::begin(paths), std::end(paths), path) == std::end(paths)) {
        paths.push_back(path);
    }
}

auto merge_dependency_artifacts(Context& context, const BuiltTargetArtifacts& artifacts) -> bool {
    if (artifacts.compiler != context.compiler || artifacts.debug != context.debug) {
        std::println("Error: dependency artifacts were built with a different compiler profile");
        return false;
    }

    for (const auto& [module_name, artifact_path] : artifacts.module_artifacts) {
        if (auto existing = context.dependency_module_artifacts.find(module_name);
            existing != std::end(context.dependency_module_artifacts) && existing->second != artifact_path) {
            std::println("Error: dependency module '{}' is provided by more than one target", module_name);
            return false;
        }
        context.dependency_module_artifacts[module_name] = artifact_path;
    }

    context.dependency_import_sys_headers.insert(std::end(context.dependency_import_sys_headers),
        std::begin(artifacts.import_sys_headers), std::end(artifacts.import_sys_headers));
    context.dependency_import_std_modules.insert(std::end(context.dependency_import_std_modules),
        std::begin(artifacts.import_std_modules), std::end(artifacts.import_std_modules));

    if (context.compiler == Compiler::GCC && !artifacts.module_artifacts.empty()) {
        append_unique_path(context.dependency_gcc_cache_dirs, artifacts.build_dir / "gcm.cache");
    }

    if (artifacts.target == Target::Lib) {
        append_unique_path(context.dependency_link_inputs, artifacts.output_path);
    } else if (artifacts.target == Target::Shared) {
        append_unique_path(context.dependency_link_inputs, shared_link_input_path(artifacts));
        append_unique_path(context.dependency_runtime_inputs, artifacts.output_path);
    }

    return true;
}

auto collect_target_dependency_artifacts(const ProjectConfig& project, const TargetConfig& target,
    const std::unordered_map<std::string, BuiltTargetArtifacts>& built_artifacts, Context& context) -> bool {
    std::unordered_set<std::string> visited_targets;

    std::function<bool(const TargetConfig&)> visit = [&](const TargetConfig& dependency) {
        if (!visited_targets.insert(dependency.name).second) {
            return true;
        }

        auto artifacts = built_artifacts.find(dependency.name);
        if (artifacts == std::end(built_artifacts)) {
            std::println("Error: dependency target '{}' has not been built yet", dependency.name);
            return false;
        }

        if (!merge_dependency_artifacts(context, artifacts->second)) {
            return false;
        }

        for (const auto& nested_dependency_name : dependency.dependencies) {
            const auto* nested_dependency = find_target(project, nested_dependency_name);
            if (!nested_dependency || !visit(*nested_dependency)) {
                return false;
            }
        }

        return true;
    };

    for (const auto& dependency_name : target.dependencies) {
        const auto* dependency = find_target(project, dependency_name);
        if (!dependency || !visit(*dependency)) {
            return false;
        }
    }

    return true;
}

auto collect_project_build_order(const ProjectConfig& project, std::span<const std::string_view> requested_targets,
    std::vector<const TargetConfig*>& order) -> bool {
    enum class VisitState { Visiting, Visited };
    std::unordered_map<std::string, VisitState> states;

    std::function<bool(const TargetConfig&)> visit = [&](const TargetConfig& target) {
        if (auto it = states.find(target.name); it != states.end()) {
            if (it->second == VisitState::Visiting) {
                std::println("Error: cyclic target dependency involving '{}'", target.name);
                return false;
            }
            return true;
        }

        states[target.name] = VisitState::Visiting;
        for (const auto& dependency_name : target.dependencies) {
            const auto* dependency = find_target(project, dependency_name);
            if (!dependency || !visit(*dependency)) {
                return false;
            }
        }

        states[target.name] = VisitState::Visited;
        order.push_back(&target);
        return true;
    };

    for (const auto& target_name : requested_targets) {
        const auto* target = find_target(project, target_name);
        if (!target) {
            std::println("Error: unknown target '{}'", target_name);
            return false;
        }
        if (!visit(*target)) {
            return false;
        }
    }

    return true;
}

auto build_project(const ProjectConfig& project, std::span<const std::string_view> requested_targets,
    std::span<const std::string_view> cli_options) -> bool {
    std::vector<const TargetConfig*> build_order;
    if (!collect_project_build_order(project, requested_targets, build_order)) {
        return false;
    }

    std::unordered_map<std::string, BuiltTargetArtifacts> built_artifacts;

    for (const auto* target : build_order) {
        Context target_context;
        if (!create_target_context(*target, cli_options, target_context)) {
            return false;
        }
        if (!collect_target_dependency_artifacts(project, *target, built_artifacts, target_context)) {
            return false;
        }
        if (!build_command(target_context, project.root / target->path)) {
            return false;
        }
        built_artifacts[target->name] = collect_built_target_artifacts(target_context);
    }

    return true;
}

auto create_configured_target_context(const TargetConfig& target, bool verbose, Context& context) -> bool {
    if (!create_target_context(target, {}, context)) {
        return false;
    }
    context.verbose = verbose;
    return true;
}

auto run_project_target(const ProjectConfig& project, std::string_view target_name, bool verbose) -> bool {
    const auto* target = find_target(project, target_name);
    if (!target) {
        std::println("Error: unknown target '{}'", target_name);
        return false;
    }

    Context target_context;
    if (!create_configured_target_context(*target, verbose, target_context)) {
        return false;
    }

    return run_command(target_context, project.root / target->path);
}

auto clean_project_targets(const ProjectConfig& project, std::span<const std::string_view> target_names, bool verbose)
    -> bool {
    for (const auto& target_name : target_names) {
        const auto* target = find_target(project, target_name);
        if (!target) {
            std::println("Error: unknown target '{}'", target_name);
            return false;
        }

        Context target_context;
        if (!create_configured_target_context(*target, verbose, target_context)) {
            return false;
        }
        if (!clean_command(target_context, project.root / target->path)) {
            return false;
        }
    }

    return true;
}

auto load_project_config_if_present(ProjectConfig& project, bool& present, fs::path root = fs::current_path()) -> bool {
    const auto config_path = fs::absolute(root) / "bspm.build";
    std::error_code ec;
    if (!fs::is_regular_file(config_path, ec) || fs::file_size(config_path, ec) == 0) {
        present = false;
        return false;
    }

    present = true;
    return parse_project_config(config_path, project);
}

auto project_target_names(const ProjectConfig& project) -> std::vector<std::string_view> {
    std::vector<std::string_view> names;
    names.reserve(project.targets.size());
    for (const auto& target : project.targets) {
        names.push_back(target.name);
    }
    return names;
}

auto has_project_target(const ProjectConfig& project, std::string_view name) -> bool {
    return find_target(project, name) != nullptr;
}

auto contains_option(std::span<const std::string_view> options, std::string_view option) -> bool {
    return std::find(std::begin(options), std::end(options), option) != std::end(options);
}

int main(int argc, char* argv[]) {

    Context context;

    if (argc < 2) {
        init_context(context, HelpTag, {});
        help_command(context, {});
        return 0;
    }

    std::string_view command { argv[1] };

    if (command == HelpTag) {
        init_context(context, HelpTag, {});
        help_command(context, argc > 2 ? std::string_view { argv[2] } : std::string_view {});
        return 0;
    }

    if (command == VersionTag) {
        init_context(context, VersionTag, {});
        version_command(context);
        return 0;
    }

    ProjectConfig project;
    bool project_present = false;
    if (!load_project_config_if_present(project, project_present) && project_present) {
        return 1;
    }

    if (command == BuildTag) {
        fs::path dir;
        int32_t option_index = 2;
        if (argc > 2 && !is_option_argument(argv[2])) {
            dir = argv[2];
            option_index = 3;
        }

        std::vector<std::string_view> cli_options;
        for (int32_t idx = option_index; idx < argc; ++idx) {
            cli_options.push_back(argv[idx]);
        }

        const bool force_project = contains_option(cli_options, ProjectOptTag);
        if (force_project) {
            ProjectConfig explicit_project;
            bool explicit_project_present = false;
            const auto project_root = dir.empty() ? fs::current_path() : dir;
            if (!load_project_config_if_present(explicit_project, explicit_project_present, project_root)) {
                if (!explicit_project_present) {
                    std::println(
                        "Error: '{}' does not contain a non-empty bspm.build", fs::absolute(project_root).string());
                }
                return 1;
            }

            project = std::move(explicit_project);
            project_present = true;
        }

        const auto subject = dir.generic_string();
        const bool use_project = force_project
            || (project_present && (dir.empty() || subject == "all" || has_project_target(project, subject)));
        if (use_project) {
            std::vector<std::string_view> requested_targets;
            if (force_project) {
                if (!project.default_target.empty()) {
                    requested_targets.push_back(project.default_target);
                } else {
                    std::println("Error: project does not define a default target");
                    return 1;
                }
            } else if (subject == "all") {
                requested_targets = project_target_names(project);
            } else if (!dir.empty()) {
                requested_targets.push_back(subject);
            } else if (!project.default_target.empty()) {
                requested_targets.push_back(project.default_target);
            } else {
                std::println("Error: project does not define a default target");
                return 1;
            }

            return build_project(project, requested_targets, cli_options) ? 0 : 1;
        }

        if (!init_context(context, BuildTag, { .argc = argc, .index = option_index, .argv = argv })) {
            std::println("Type '{} help {}' for more description.", context.name, BuildTag);
            return 1;
        }
        return build_command(context, dir) ? 0 : 1;
    }

    if (command == RunTag) {
        fs::path dir;
        int32_t option_index = 2;
        if (argc > 2 && !is_option_argument(argv[2])) {
            dir = argv[2];
            option_index = 3;
        }

        if (!init_context(context, RunTag, { .argc = argc, .index = option_index, .argv = argv })) {
            std::println("Type '{} help {}' for more description.", context.name, RunTag);
            return 1;
        }

        const auto subject = dir.generic_string();
        const bool use_project = project_present && (dir.empty() || has_project_target(project, subject));
        if (use_project) {
            std::string_view target_name;
            if (!dir.empty()) {
                target_name = subject;
            } else if (!project.default_target.empty()) {
                target_name = project.default_target;
            } else {
                std::println("Error: project does not define a default target");
                return 1;
            }

            return run_project_target(project, target_name, context.verbose) ? 0 : 1;
        }

        return run_command(context, dir) ? 0 : 1;
    }

    if (command == CleanTag) {
        fs::path dir;
        int32_t option_index = 2;
        if (argc > 2 && !is_option_argument(argv[2])) {
            dir = argv[2];
            option_index = 3;
        }

        if (!init_context(context, CleanTag, { .argc = argc, .index = option_index, .argv = argv })) {
            std::println("Type '{} help {}' for more description.", context.name, CleanTag);
            return 1;
        }

        const auto subject = dir.generic_string();
        const bool use_project
            = project_present && (dir.empty() || subject == "all" || has_project_target(project, subject));
        if (use_project) {
            std::vector<std::string_view> target_names;
            if (subject == "all") {
                target_names = project_target_names(project);
            } else if (!dir.empty()) {
                target_names.push_back(subject);
            } else if (!project.default_target.empty()) {
                target_names.push_back(project.default_target);
            } else {
                std::println("Error: project does not define a default target");
                return 1;
            }

            return clean_project_targets(project, target_names, context.verbose) ? 0 : 1;
        }

        return clean_command(context, dir) ? 0 : 1;
    }

    if (command == InitTag) {
        fs::path dir;
        int32_t option_index = 2;
        if (argc > 2 && !is_option_argument(argv[2])) {
            dir = argv[2];
            option_index = 3;
        }

        if (!init_context(context, InitTag, { .argc = argc, .index = option_index, .argv = argv })) {
            std::println("Type '{} help {}' for more description.", context.name, InitTag);
            return 1;
        }
        init_command(context, dir);
        return 0;
    }

    std::println("Error: '{}' unknown command!", command);
    std::println("Type '{} help' for command list.", context.name);

    return 1;
}
