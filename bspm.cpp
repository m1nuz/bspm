#define _CRT_SECURE_NO_WARNINGS

#include <algorithm>
#include <array>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
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

std::array BuildOpts {
    BinaryOptTag,
    LibraryOptTag,
    SharedOptTag,
    VerboseOptTag,
    CompilerOptTag,
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
    std::unordered_set<std::string> imports;
    std::unordered_set<std::string> std_module_imports;
    std::unordered_set<std::string> module_imports;
    std::string module_name;

    auto get_dependencies() const -> std::unordered_set<std::string> {
        std::unordered_set<std::string> all_deps = imports;
        all_deps.insert(module_imports.begin(), module_imports.end());
        return all_deps;
    }
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

    static constexpr std::string_view version { "0.0.2" };
    static constexpr std::string_view name { "bspm" };

    using Value = std::variant<uint64_t, double, std::string_view>;
    using Options = std::unordered_map<std::string_view, Value>;

    Compiler compiler { Compiler::GCC };
    std::string cc { "gcc" };
    std::string cpp_c { "g++" };
    std::string cpp_standard { "-std=c++23" };
    std::string cpp_flags { "-fmodules-ts -MD" };
    std::string ld_flags { "-lstdc++exp" };
    std::string object_extension { ".o" };
    std::string output_name;
    fs::path msvc_dev_cmd;

    std::vector<std::string> import_sys_headers;
    std::vector<std::string> import_std_modules;
    bool process_sys_imports { true };

    std::vector<CompileUnit> compile_units;

    Target target { Target::Bin };

    bool verbose { false };
    bool debug { true };
};

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

    const int status = std::system(std::data(full_command));
    if (status != 0) {
        std::println("Error: command failed with status {}", status);
        return false;
    }

    return true;
}

auto help_command(Context& context, std::string_view command) -> void {
    if (command.empty()) {
        for (auto cmd : commands) {
            std::println("\t{}", cmd);
        }

        return;
    }

    if (command == VersionTag) {
        std::println("\tShow {} current version", context.name);
        return;
    }

    if (command == InitTag) {
        std::println("\t{} create <dir> and init configuration for build", context.name);
        return;
    }

    if (command == BuildTag) {
        std::println("\t{} will initiate build in <dir>", context.name);
        std::println("\tUse -c <g++|clang++|msvc> to choose compiler");
        return;
    }
}

auto version_command(Context& context) -> void {
    std::println("{} {}", context.name, context.version);
}

inline auto is_cppm(const fs::directory_entry& entry) -> bool {
    return entry.path().extension() == ".cppm";
}

auto is_std_module_name(std::string_view module_name) -> bool {
    return module_name == StdModuleName || module_name == "std.compat";
}

auto ordered_std_module_references(const std::unordered_set<std::string>& imports) -> std::vector<std::string> {
    std::vector<std::string> references;
    if (imports.contains(StdModuleName) || imports.contains(StdCompatModuleName)) {
        references.push_back(StdModuleName);
    }
    if (imports.contains("std.compat")) {
        references.push_back("std.compat");
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
    std::regex import_module_regex(R"(^\s*(?:export\s+)?import\s+([A-Za-z_][A-Za-z0-9_:.]*)\s*;)");
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

std::string extract_module_name_from_file(std::string_view filename) {
    std::ifstream file { std::data(filename) };
    if (!file) {
        std::println("Error: failed to open file '{}'", filename);
        return {};
    }

    std::regex module_regex(R"(^\s*export\s+module\s+([A-Za-z_][A-Za-z0-9_:.]*)\s*;)");
    std::string line;

    while (std::getline(file, line)) {
        std::smatch match;
        if (std::regex_search(line, match, module_regex)) {
            return match[1];
        }
    }

    return {};
}

static auto process_units_imports(Context& context, const std::vector<fs::directory_entry>& entries) -> void {
    context.compile_units.reserve(std::size(entries));

    std::vector<std::string> imports;
    std::vector<std::string> std_module_imports;

    for (auto& entry : entries) {
        CompileUnit unit;

        unit.file_path = entry.path();
        unit.file_name = unit.file_path.filename().string();

        auto [library_names, std_module_names, module_names] = extract_import_names_from_file(entry.path().string());
        unit.module_name = extract_module_name_from_file(entry.path().string());
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
            unit.module_imports.insert(std::begin(module_names), std::end(module_names));
        }

        context.compile_units.push_back(unit);
    }

    if (std::find(std::begin(std_module_imports), std::end(std_module_imports), "std.compat")
            != std::end(std_module_imports)
        && std::find(std::begin(std_module_imports), std::end(std_module_imports), StdModuleName)
            == std::end(std_module_imports)) {
        std_module_imports.push_back(StdModuleName);
    }

    std::sort(std::begin(std_module_imports), std::end(std_module_imports), [](const auto& a, const auto& b) {
        if (a == StdModuleName && b == "std.compat") {
            return true;
        }
        if (a == "std.compat" && b == StdModuleName) {
            return false;
        }
        return a < b;
    });
    std_module_imports.erase(
        std::unique(std::begin(std_module_imports), std::end(std_module_imports)), std::end(std_module_imports));

    context.import_sys_headers = imports;
    context.import_std_modules = std_module_imports;
}

auto sort_units_by_dependency(Context& context) -> bool {
    std::unordered_map<std::string, std::size_t> module_to_unit;
    for (std::size_t i = 0; i < context.compile_units.size(); ++i) {
        const auto& module_name = context.compile_units[i].module_name;
        if (module_name.empty()) {
            continue;
        }

        if (module_to_unit.contains(module_name)) {
            std::println("Error: module '{}' is defined more than once", module_name);
            return false;
        }

        module_to_unit[module_name] = i;
    }

    enum class VisitState { NotVisited, Visiting, Visited };

    std::vector<VisitState> states(context.compile_units.size(), VisitState::NotVisited);
    std::vector<CompileUnit> sorted_units;
    sorted_units.reserve(context.compile_units.size());

    std::function<bool(std::size_t)> visit = [&](std::size_t index) {
        if (states[index] == VisitState::Visited) {
            return true;
        }

        if (states[index] == VisitState::Visiting) {
            std::println("Error: cyclic module dependency involving '{}'", context.compile_units[index].file_name);
            return false;
        }

        states[index] = VisitState::Visiting;

        for (const auto& dependency : context.compile_units[index].module_imports) {
            auto it = module_to_unit.find(dependency);
            if (it != module_to_unit.end() && !visit(it->second)) {
                return false;
            }
        }

        states[index] = VisitState::Visited;
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
    return source_path.replace_extension(context.object_extension);
}

auto clang_module_pcm_path(std::string_view module_name) -> fs::path {
    std::string artifact_name { module_name };
    std::replace(artifact_name.begin(), artifact_name.end(), ':', '-');
    artifact_name += ".pcm";
    return artifact_name;
}

auto append_previous_clang_module_references(
    std::vector<std::string>& args, const Context& context, std::size_t unit_index) -> void {
    for (std::size_t i = 0; i < unit_index; ++i) {
        const auto& module_name = context.compile_units[i].module_name;
        if (module_name.empty()) {
            continue;
        }

        args.push_back(
            std::string { "-fmodule-file=" } + module_name + "=" + clang_module_pcm_path(module_name).generic_string());
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
        if (unit.module_name == module_name) {
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

auto append_msvc_module_reference_args(std::vector<std::string>& args, const std::unordered_set<std::string>& modules)
    -> void {
    for (const auto& imported_module : modules) {
        args.push_back(std::string { "/reference" });
        args.push_back(imported_module + "=" + msvc_module_ifc_path(imported_module).string());
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

    append_msvc_header_unit_args(args, headers);
    append_msvc_std_module_reference_args(args, std_modules);
    append_msvc_module_reference_args(args, module_references);
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

            if (module_name == "std.compat") {
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

            if (module_name == "std.compat") {
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

auto remove_gcc_header_unit_cache(Context& context, const fs::path& search_path) -> bool {
    if (context.compiler != Compiler::GCC || context.import_sys_headers.empty()) {
        return true;
    }

    const auto cache_path = search_path / "gcm.cache";
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

    ScopedCurrentPath current_path { search_path };

    std::vector<fs::directory_entry> entries;

    for (const auto& entry : fs::directory_iterator(search_path)) {
        if (entry.is_regular_file()) {
            auto extension = entry.path().extension().string();
            if (extension == ".cpp" || extension == ".cppm") {
                if (context.verbose) {
                    std::println("entry: {}", entry.path().filename().string());
                }

                entries.push_back(entry);
            }
        }
    }

    // Sort sources with .cppm first, then keep the initial order deterministic.
    std::sort(std::begin(entries), std::end(entries), [](const auto& a, const auto& b) {
        if (is_cppm(a) != is_cppm(b)) {
            return is_cppm(a);
        }

        return a.path().filename().string() < b.path().filename().string();
    });

    process_units_imports(context, entries);

    if (!sort_units_by_dependency(context)) {
        return false;
    }

    if (!remove_gcc_header_unit_cache(context, search_path)) {
        return false;
    }

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
        auto extension = entry_path.extension().string();
        if (extension == ".cpp") {
            std::vector<std::string> args;

            args.push_back(context.cpp_standard);
            args.push_back(context.cpp_flags);

            if (context.compiler == Compiler::MSVC) {
                args.push_back(std::string { "/c" });
                args.push_back(std::string { "/TP" });

                append_msvc_import_args(args, context, unit);

                args.push_back(std::string { "/Fo" } + object_path(context, unit.file_path).filename().string());
                args.push_back(unit.file_name);
            } else if (context.compiler == Compiler::Clang) {
                args.push_back("-Wno-experimental-header-units");

                for (const auto& imported_module : unit.imports) {
                    std::string module_file;
                    std::format_to(std::back_inserter(module_file), "-fmodule-file=\"{}.pcm\"",
                        (search_path / ".cache" / imported_module).string());
                    args.push_back(module_file);
                }

                for (const auto& imported_std_module : ordered_std_module_references(unit.std_module_imports)) {
                    args.push_back(std::string { "-fmodule-file=" } + imported_std_module + "="
                        + std_module_pcm_path(imported_std_module).generic_string());
                }

                append_previous_clang_module_references(args, context, unit_index);
            }

            if (context.compiler != Compiler::MSVC) {
                args.push_back(std::string { "-c" });
                args.push_back(unit.file_name);
            }

            if (!execute_command(context, context.cpp_c, args)) {
                return false;
            }
        } else if (extension == ".cppm") {
            std::vector<std::string> args;

            args.push_back(context.cpp_standard);
            args.push_back(context.cpp_flags);

            if (context.compiler == Compiler::MSVC) {
                if (unit.module_name.empty()) {
                    std::println("Error: '{}' does not declare an exported module", unit.file_name);
                    return false;
                }

                args.push_back(std::string { "/c" });
                args.push_back(std::string { "/TP" });
                args.push_back(std::string { "/interface" });

                append_msvc_import_args(args, context, unit);

                args.push_back(std::string { "/ifcOutput" });
                args.push_back(msvc_module_ifc_path(unit.module_name).string());
                args.push_back(std::string { "/Fo" } + object_path(context, unit.file_path).filename().string());
                args.push_back(unit.file_name);
            } else if (context.compiler == Compiler::Clang) {
                if (unit.module_name.empty()) {
                    std::println("Error: '{}' does not declare an exported module", unit.file_name);
                    return false;
                }

                args.push_back(std::string { "-xc++" });
                args.push_back(std::string { "-xc++-module" });
                args.push_back(std::string { "--precompile" });
                args.push_back("-Wno-experimental-header-units");

                for (const auto& imported_module : unit.imports) {
                    std::string module_file;
                    std::format_to(std::back_inserter(module_file), "-fmodule-file=\"{}.pcm\"",
                        (search_path / ".cache" / imported_module).string());
                    args.push_back(module_file);
                }

                for (const auto& imported_std_module : ordered_std_module_references(unit.std_module_imports)) {
                    args.push_back(std::string { "-fmodule-file=" } + imported_std_module + "="
                        + std_module_pcm_path(imported_std_module).generic_string());
                }

                append_previous_clang_module_references(args, context, unit_index);

                args.push_back(unit.file_name);
                args.push_back(std::string { "-o" });
                args.push_back(clang_module_pcm_path(unit.module_name).generic_string());

                if (!execute_command(context, context.cpp_c, args)) {
                    return false;
                }

                std::vector<std::string> object_args {
                    context.cpp_standard,
                    context.cpp_flags,
                };

                append_previous_clang_module_references(object_args, context, unit_index);

                object_args.push_back(std::string { "-c" });
                object_args.push_back(clang_module_pcm_path(unit.module_name).generic_string());
                object_args.push_back(std::string { "-o" });
                object_args.push_back(object_path(context, unit.file_path).filename().string());

                if (!execute_command(context, context.cpp_c, object_args)) {
                    return false;
                }

                continue;
            } else {
                args.push_back(std::string { "-xc++" });
            }

            if (context.compiler != Compiler::MSVC) {
                args.push_back(std::string { "-c" });
                args.push_back(unit.file_name);
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

    std::vector<std::string> link_args;
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

    if (!execute_command(context, context.cpp_c, link_args)) {
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

static auto find_app_file(const Context& context, const fs::path& search_path) {
    auto output_path = search_path / context.output_name;
    if (fs::is_regular_file(output_path)) {
        return output_path.string();
    }

    std::string app_file;

    for (const auto& entry : fs::directory_iterator(search_path)) {
        if (entry.is_regular_file()) {
            if (is_file_executable(entry.path().string())) {
                app_file = entry.path().string();
                break;
            }
        }
    }

    return app_file;
}

auto run_command(Context& context, fs::path dir) -> bool {
    dir = !dir.empty() ? dir : ".";

    auto search_path = fs::absolute(dir);
    if (!fs::exists(search_path) || !fs::is_directory(search_path)) {
        std::println("Error: '{}' not exists!", search_path.string());
        return false;
    }

    ScopedCurrentPath current_path { search_path };

    auto app_file = find_app_file(context, search_path);
    if (app_file.empty() || !fs::exists(app_file)) {
        std::println("Error: couldn't run from '{}'", dir.string());
        return false;
    }

    if (context.verbose) {
        std::println("{} running '{}'", context.name, app_file);
        std::fflush(stdout);
    }

    return std::system(std::data(app_file)) == 0;
}

auto clean_command(Context& context, fs::path dir) -> bool {
    dir = !dir.empty() ? dir : ".";

    auto search_path = fs::absolute(dir);
    if (!fs::exists(search_path) || !fs::is_directory(search_path)) {
        std::println("Error: '{}' not exists!", search_path.string());
        return false;
    }

    ScopedCurrentPath current_path { search_path };

    auto app_file = find_app_file(context, search_path);
    if (!app_file.empty()) {
        std::error_code ec;
        fs::remove(app_file, ec);
        if (ec) {
            std::println("Error: couldn't remove '{}': {}", app_file, ec.message());
            return false;
        }
    }

    for (const auto& entry : fs::directory_iterator(search_path)) {
        if (entry.is_regular_file()) {
            auto extension = entry.path().extension().string();
            if (extension == ".o" || extension == ".d" || extension == ".obj" || extension == ".ifc") {
                if (context.verbose) {
                    std::println("remove entry: {}", entry.path().filename().string());
                }

                std::error_code ec;
                fs::remove(entry.path(), ec);
                if (ec) {
                    std::println("Error: couldn't remove '{}': {}", entry.path().string(), ec.message());
                    return false;
                }
            }
        }
    }

    for (const auto& cache_dir : { fs::path { "gcm.cache" }, fs::path { ".cache" } }) {
        const auto cache_path = search_path / cache_dir;
        std::error_code ec;
        if (fs::exists(cache_path, ec)) {
            fs::remove_all(cache_path, ec);
            if (ec) {
                std::println("Error: couldn't remove '{}': {}", cache_path.string(), ec.message());
                return false;
            }

            if (context.verbose) {
                std::println("remove entry: {}", cache_dir.string());
            }
        }
    }

    return true;
}

auto init_command(Context& context, fs::path dir) -> void {
    if (!fs::exists(dir)) {
        fs::create_directory(dir);
    }

    // Create build configuration file
    {
        fs::path fullpath = dir / ".build";
        if (!fs::exists(fullpath)) {
            std::ofstream f { fullpath };
            if (f.good()) {
                f.close();
            }
        }
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

auto init_context(Context& context, std::string_view command, const InitConfiguration& conf) -> bool {
#if defined(_WIN32) || defined(_WIN64)
    context.output_name = "a.exe";
#else
    context.output_name = "a.out";
#endif

    if (conf.argv && conf.index < conf.argc) {
        int32_t idx = conf.index;

        while (idx < conf.argc) {
            if (std::strncmp(conf.argv[idx], std::data(VerboseOptTag), std::size(VerboseOptTag)) == 0) {
                context.verbose = true;
            }

            if (command == BuildTag) {
                if (!check_option(BuildOpts, conf.argv[idx])) {
                    std::println("Error: Invalid option '{}' for {} command", conf.argv[idx], command);
                    return false;
                }

                if (std::strncmp(conf.argv[idx], std::data(BinaryOptTag), std::size(BinaryOptTag)) == 0) {
                    context.target = Target::Bin;
                } else if (std::strncmp(conf.argv[idx], std::data(LibraryOptTag), std::size(LibraryOptTag)) == 0) {
                    context.target = Target::Lib;
                } else if (std::strncmp(conf.argv[idx], std::data(SharedOptTag), std::size(SharedOptTag)) == 0) {
                    context.target = Target::Shared;
                }

                if (std::strncmp(conf.argv[idx], std::data(CompilerOptTag), std::size(CompilerOptTag)) == 0) {
                    if (idx + 1 < conf.argc) {
                        idx++;

                        if (std::strncmp(conf.argv[idx], GPPCompilerTag, sizeof(GPPCompilerTag)) == 0
                            || std::strncmp(conf.argv[idx], GCCCompilerTag, sizeof(GCCCompilerTag)) == 0) {
                            init_gcc_compiler(context);
                        } else if (std::strncmp(conf.argv[idx], ClangPPCompilerTag, sizeof(ClangPPCompilerTag)) == 0
                            || std::strncmp(conf.argv[idx], ClangCompilerTag, sizeof(ClangCompilerTag)) == 0) {
                            init_clang_compiler(context);
                        } else if (std::strncmp(conf.argv[idx], MSVCCompilerTag, sizeof(MSVCCompilerTag)) == 0
                            || std::strncmp(conf.argv[idx], CLCompilerTag, sizeof(CLCompilerTag)) == 0
                            || std::strncmp(conf.argv[idx], CLExeCompilerTag, sizeof(CLExeCompilerTag)) == 0) {
                            init_msvc_compiler(context);
                        } else {
                            std::println(
                                "Error: Unknown compiler parameter {} for {} command", conf.argv[idx], command);
                            return false;
                        }

                    } else {
                        std::println("Error: Invalid option '{}' for {} command, option didn't have {}", conf.argv[idx],
                            command, "parameter");
                        return false;
                    }
                }
            }

            idx++;
        }
    }

    return true;
}

int main(int argc, char* argv[]) {

    if (argc < 2) {
        std::print("{} <command> <dir> <options>", "bspm");
        return 0;
    }

    Context context;

    int32_t arg_idx = 0;
    while (arg_idx < argc) {

        if (std::strncmp(argv[arg_idx], HelpTag, sizeof(HelpTag)) == 0) {
            init_context(context, HelpTag, {});
            help_command(context, arg_idx + 1 < argc ? argv[arg_idx + 1] : std::string_view {});
            return 0;
        }

        if (std::strncmp(argv[arg_idx], VersionTag, sizeof(VersionTag)) == 0) {
            init_context(context, VersionTag, {});
            version_command(context);
            return 0;
        }

        if (std::strncmp(argv[arg_idx], BuildTag, sizeof(BuildTag)) == 0) {
            if (!init_context(context, BuildTag, { .argc = argc, .index = arg_idx + 2, .argv = argv })) {
                std::println("Type '{} help {}' for more description.", context.name, BuildTag);
                return 1;
            }
            return build_command(context, arg_idx + 1 < argc ? argv[arg_idx + 1] : std::string_view {}) ? 0 : 1;
        }

        if (std::strncmp(argv[arg_idx], RunTag, sizeof(RunTag)) == 0) {
            init_context(context, RunTag, { .argc = argc, .index = arg_idx + 2, .argv = argv });
            return run_command(context, arg_idx + 1 < argc ? argv[arg_idx + 1] : std::string_view {}) ? 0 : 1;
        }

        if (std::strncmp(argv[arg_idx], CleanTag, sizeof(CleanTag)) == 0) {
            init_context(context, CleanTag, { .argc = argc, .index = arg_idx + 2, .argv = argv });
            return clean_command(context, arg_idx + 1 < argc ? argv[arg_idx + 1] : std::string_view {}) ? 0 : 1;
        }

        if (std::strncmp(argv[arg_idx], InitTag, sizeof(InitTag)) == 0) {
            init_context(context, InitTag, {});
            init_command(context, arg_idx + 1 < argc ? argv[arg_idx + 1] : std::string_view {});
            return 0;
        }

        arg_idx++;
    }

    std::println("Error: '{}' unknown command!", argv[1]);

    return 1;
}
