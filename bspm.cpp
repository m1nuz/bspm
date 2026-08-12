#define _CRT_SECURE_NO_WARNINGS

#include <algorithm>
#include <array>
#include <cctype>
#include <cerrno>
#include <charconv>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <format>
#include <future>
#include <fstream>
#include <functional>
#include <iterator>
#include <mutex>
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

#if defined(_WIN32) || defined(_WIN64)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#else
#include <sys/wait.h>
#include <unistd.h>
#endif

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
constexpr char DoctorTag[] = "doctor";
constexpr char GraphTag[] = "graph";

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
    DoctorTag,
    GraphTag,
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
constexpr std::string_view ExplainOptTag { "--explain" };
constexpr std::string_view DependsOptTag { "--depends" };
constexpr std::string_view ProjectOptTag { "--project" };
constexpr std::string_view JobsOptTag { "-j" };
constexpr std::string_view LongJobsOptTag { "--jobs" };
constexpr std::string_view CxxFlagOptTag { "--cxxflag" };
constexpr std::string_view LdFlagOptTag { "--ldflag" };
constexpr std::string_view DefineOptTag { "--define" };
constexpr std::string_view IncludeOptTag { "--include" };
constexpr std::string_view SourceOptTag { "--source" };
constexpr std::string_view ExcludeOptTag { "--exclude" };

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
    ExplainOptTag,
    ProjectOptTag,
    JobsOptTag,
    LongJobsOptTag,
    CxxFlagOptTag,
    LdFlagOptTag,
    DefineOptTag,
    IncludeOptTag,
    SourceOptTag,
    ExcludeOptTag,
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

struct BuildState {
    fs::path path;
    std::unordered_map<std::string, std::string> signatures;
    bool dirty { false };
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
    std::vector<std::string> user_cxx_flags;
    std::vector<std::string> user_ld_flags;
    std::vector<std::string> defines;
    std::vector<fs::path> include_dirs;
    std::vector<fs::path> source_filters;
    std::vector<fs::path> exclude_filters;

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
    bool explain { false };
    bool output_name_configured { false };
    bool project_init { false };
    std::size_t jobs { 1 };
    BuildState build_state;
    std::mutex build_state_mutex;
    std::mutex output_mutex;
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
    std::vector<std::string> common_build_options;
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
auto build_option_views(const std::vector<std::string>& options) -> std::vector<std::string_view>;
auto init_gcc_compiler(Context& context) -> void;
auto init_clang_compiler(Context& context) -> void;
auto init_msvc_compiler(Context& context) -> void;
auto is_option_argument(std::string_view argument) -> bool;

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

struct ProcessInvocation {
    std::string executable;
    std::vector<std::string> args;
    std::string display_command;
};

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

auto environment_path_entries() -> std::vector<fs::path> {
    std::vector<fs::path> entries;
    const char* raw_path = std::getenv("PATH");
    if (!raw_path || !*raw_path) {
        return entries;
    }

    std::string_view path { raw_path };
#if defined(_WIN32) || defined(_WIN64)
    constexpr char separator = ';';
#else
    constexpr char separator = ':';
#endif

    std::size_t start = 0;
    while (start <= path.size()) {
        const auto end = path.find(separator, start);
        const auto part = path.substr(start, end == std::string_view::npos ? std::string_view::npos : end - start);
        if (!part.empty()) {
            entries.emplace_back(part);
        }
        if (end == std::string_view::npos) {
            break;
        }
        start = end + 1;
    }

    return entries;
}

auto executable_candidates(std::string_view executable) -> std::vector<fs::path> {
    fs::path requested { executable };
#if defined(_WIN32) || defined(_WIN64)
    if (!requested.extension().empty()) {
        return { requested };
    }

    std::vector<fs::path> candidates;
    if (const char* raw_extensions = std::getenv("PATHEXT"); raw_extensions && *raw_extensions) {
        std::string_view extensions { raw_extensions };
        std::size_t start = 0;
        while (start <= extensions.size()) {
            const auto end = extensions.find(';', start);
            const auto part
                = extensions.substr(start, end == std::string_view::npos ? std::string_view::npos : end - start);
            if (!part.empty()) {
                candidates.emplace_back(std::string { executable } + std::string { part });
            }
            if (end == std::string_view::npos) {
                break;
            }
            start = end + 1;
        }
    }

    candidates.push_back(requested);
    candidates.emplace_back(std::string { executable } + ".exe");
    return candidates;
#else
    return { requested };
#endif
}

auto find_executable_on_path(std::string_view executable) -> fs::path {
    const fs::path requested { executable };
    std::error_code ec;
    if (requested.has_parent_path() && fs::exists(requested, ec)) {
        return requested;
    }

    for (const auto& directory : environment_path_entries()) {
        for (const auto& candidate_name : executable_candidates(executable)) {
            auto candidate = directory / candidate_name;
            if (fs::exists(candidate, ec) && fs::is_regular_file(candidate, ec)) {
                return candidate;
            }
        }
    }

    return {};
}

auto compiler_label(Compiler compiler) -> std::string_view {
    switch (compiler) {
    case Compiler::GCC:
        return "gcc";
    case Compiler::Clang:
        return "clang";
    case Compiler::MSVC:
        return "msvc";
    }

    return "unknown";
}

auto select_compiler(Context& context, std::string_view compiler, std::string_view source) -> bool {
    if (compiler == GPPCompilerTag || compiler == GCCCompilerTag) {
        init_gcc_compiler(context);
        return true;
    }
    if (compiler == ClangPPCompilerTag || compiler == ClangCompilerTag) {
        init_clang_compiler(context);
        return true;
    }
    if (compiler == MSVCCompilerTag || compiler == CLCompilerTag || compiler == CLExeCompilerTag) {
        init_msvc_compiler(context);
        return true;
    }

    std::println("Error: Unknown compiler parameter {} in {}", compiler, source);
    return false;
}

auto print_doctor_check(std::string_view status, std::string_view label, std::string_view detail = {}) -> void {
    if (detail.empty()) {
        std::println("[{}] {}", status, label);
    } else {
        std::println("[{}] {}: {}", status, label, detail);
    }
}

auto doctor_command(Context& context) -> bool {
    std::println("{} doctor", context.name);
    std::println("profile: {}", compiler_label(context.compiler));

    bool ok = true;
    auto check_tool = [&](std::string_view label, std::string_view executable, bool required = true) {
        auto path = find_executable_on_path(executable);
        if (path.empty()) {
            print_doctor_check(required ? "fail" : "warn", label, std::string { executable } + " was not found on PATH");
            ok = ok && !required;
            return;
        }

        print_doctor_check("ok", label, path.string());
    };

    if (context.compiler == Compiler::GCC) {
        check_tool("C++ compiler", context.cpp_c);
        check_tool("C compiler", context.cc);
        check_tool("static library archiver", "ar");
        print_doctor_check("note", "modules", "GCC C++ module support is enabled with -fmodules");
        if (context.ld_flags.find("-lstdc++exp") != std::string::npos) {
            print_doctor_check("note", "standard library", "import std may need libstdc++exp");
        }
    } else if (context.compiler == Compiler::Clang) {
        check_tool("C++ compiler", context.cpp_c);
        check_tool("static library archiver", "ar");
        print_doctor_check("note", "modules", "Clang C++ module support is enabled with -fprebuilt-module-path");
    } else {
        const auto cl_path = find_executable_on_path("cl.exe");
        if (!cl_path.empty()) {
            print_doctor_check("ok", "C++ compiler", cl_path.string());
        } else if (!context.msvc_dev_cmd.empty()) {
            print_doctor_check("ok", "C++ compiler", "cl.exe will be loaded through VsDevCmd.bat");
        } else {
            print_doctor_check("fail", "C++ compiler", "cl.exe was not found on PATH");
            ok = false;
        }

        const auto lib_path = find_executable_on_path("lib.exe");
        if (!lib_path.empty()) {
            print_doctor_check("ok", "static library archiver", lib_path.string());
        } else if (!context.msvc_dev_cmd.empty()) {
            print_doctor_check("ok", "static library archiver", "lib.exe will be loaded through VsDevCmd.bat");
        } else {
            print_doctor_check("fail", "static library archiver", "lib.exe was not found on PATH");
            ok = false;
        }

        if (!context.msvc_dev_cmd.empty()) {
            print_doctor_check("ok", "Visual Studio developer environment", context.msvc_dev_cmd.string());
        } else {
            print_doctor_check("warn", "Visual Studio developer environment", "VsDevCmd.bat was not found");
        }
        print_doctor_check("note", "modules", "MSVC module support is enabled with /interface and /ifcSearchDir");
    }

    print_doctor_check("note", "build directory", "generated files are written under <target>/build/<profile>");
    return ok;
}

auto init_doctor_context(Context& context, int argc, char* argv[], int32_t index) -> bool {
    for (int32_t idx = index; idx < argc; ++idx) {
        std::string_view opt { argv[idx] };

        if (opt == VerboseOptTag) {
            context.verbose = true;
            continue;
        }

        if (opt != CompilerOptTag) {
            std::println("Error: Invalid option '{}' for {} command", opt, DoctorTag);
            return false;
        }

        if (idx + 1 >= argc || is_option_argument(argv[idx + 1])) {
            std::println("Error: Invalid option '{}' for {} command, option didn't have parameter", opt, DoctorTag);
            return false;
        }

        if (!select_compiler(context, argv[++idx], "doctor command")) {
            return false;
        }
    }

    return true;
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

auto split_argument_tokens(std::string_view value, std::vector<std::string>& tokens) -> bool {
    std::string token;
    bool in_quotes = false;
    bool escaped = false;

    auto flush_token = [&] {
        if (!token.empty()) {
            tokens.push_back(std::move(token));
            token.clear();
        }
    };

    for (char ch : value) {
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

        if (!in_quotes && std::isspace(static_cast<unsigned char>(ch))) {
            flush_token();
            continue;
        }

        token.push_back(ch);
    }

    if (escaped || in_quotes) {
        return false;
    }

    flush_token();
    return true;
}

auto normalize_process_args(std::span<const std::string> args, std::vector<std::string>& normalized) -> bool {
    for (const auto& arg : args) {
        if (!split_argument_tokens(arg, normalized)) {
            std::println("Error: invalid quoted command argument '{}'", arg);
            return false;
        }
    }
    return true;
}

auto render_command(std::string_view command, std::span<const std::string> args) -> std::string {
    std::string rendered { quote_arg(command) };
    for (const auto& arg : args) {
        rendered += " ";
        rendered += quote_arg(arg);
    }
    return rendered;
}

auto make_process_invocation(
    const Context& context, std::string_view command, std::span<const std::string> args) -> ProcessInvocation {
    ProcessInvocation invocation;
    invocation.executable = command;
    if (!normalize_process_args(args, invocation.args)) {
        return {};
    }

    invocation.display_command = render_command(command, invocation.args);
    if (context.compiler == Compiler::MSVC && !context.msvc_dev_cmd.empty()) {
        invocation.executable = "cmd.exe";
        invocation.args = { "/D", "/S", "/C", wrap_msvc_command(context, invocation.display_command) };
        invocation.display_command = invocation.args.back();
    }
    return invocation;
}

auto quote_windows_process_arg(std::string_view arg) -> std::string {
    const bool needs_quotes
        = arg.empty() || arg.find_first_of(" \t\n\v\"") != std::string_view::npos;
    if (!needs_quotes) {
        return std::string { arg };
    }

    std::string quoted;
    quoted.push_back('"');
    std::size_t backslashes = 0;

    for (char ch : arg) {
        if (ch == '\\') {
            ++backslashes;
            continue;
        }

        if (ch == '"') {
            quoted.append(backslashes * 2 + 1, '\\');
            quoted.push_back('"');
            backslashes = 0;
            continue;
        }

        quoted.append(backslashes, '\\');
        backslashes = 0;
        quoted.push_back(ch);
    }

    quoted.append(backslashes * 2, '\\');
    quoted.push_back('"');
    return quoted;
}

auto spawn_process(const std::string& executable, std::span<const std::string> args) -> int {
    std::vector<std::string> argv_storage;
    argv_storage.reserve(args.size() + 1);
    argv_storage.push_back(executable);
    argv_storage.insert(std::end(argv_storage), std::begin(args), std::end(args));

#if defined(_WIN32) || defined(_WIN64)
    std::string command_line;
    for (const auto& arg : argv_storage) {
        if (!command_line.empty()) {
            command_line.push_back(' ');
        }
        command_line += quote_windows_process_arg(arg);
    }

    STARTUPINFOA startup_info {};
    PROCESS_INFORMATION process_info {};
    startup_info.cb = sizeof(startup_info);

    const BOOL created = CreateProcessA(nullptr, command_line.data(), nullptr, nullptr, TRUE, 0, nullptr, nullptr,
        &startup_info, &process_info);
    if (!created) {
        return static_cast<int>(GetLastError());
    }

    WaitForSingleObject(process_info.hProcess, INFINITE);

    DWORD exit_code = 1;
    if (!GetExitCodeProcess(process_info.hProcess, &exit_code)) {
        exit_code = GetLastError();
    }

    CloseHandle(process_info.hThread);
    CloseHandle(process_info.hProcess);
    return static_cast<int>(exit_code);
#else
    std::vector<char*> argv;
    argv.reserve(argv_storage.size() + 1);
    for (auto& arg : argv_storage) {
        argv.push_back(arg.data());
    }
    argv.push_back(nullptr);

    const pid_t pid = fork();
    if (pid == -1) {
        return errno == 0 ? 1 : errno;
    }

    if (pid == 0) {
        execvp(executable.c_str(), argv.data());
        _exit(127);
    }

    int status = 0;
    if (waitpid(pid, &status, 0) == -1) {
        return errno == 0 ? 1 : errno;
    }
    if (WIFEXITED(status)) {
        return WEXITSTATUS(status);
    }
    if (WIFSIGNALED(status)) {
        return 128 + WTERMSIG(status);
    }
    return status;
#endif
}

auto execute_process_invocation(Context& context, const ProcessInvocation& invocation) -> bool {
    if (invocation.executable.empty()) {
        return false;
    }

    if (context.verbose) {
        std::scoped_lock lock { context.output_mutex };
        std::println("command: {}", invocation.display_command);
        std::fflush(stdout);
    }

    if (context.dry_run) {
        if (!context.verbose) {
            std::scoped_lock lock { context.output_mutex };
            std::println("command: {}", invocation.display_command);
        }
        return true;
    }

    const int status = spawn_process(invocation.executable, invocation.args);
    if (status != 0) {
        std::println("Error: command failed with status {}", status);
        return false;
    }

    return true;
}

auto execute_command(Context& context, std::string_view command, std::span<const std::string> args) -> bool {
    const auto invocation = make_process_invocation(context, command, args);
    return execute_process_invocation(context, invocation);
}

auto encode_state_field(std::string_view value) -> std::string {
    std::string encoded;
    encoded.reserve(value.size());
    for (char ch : value) {
        if (ch == '\\') {
            encoded += "\\\\";
        } else if (ch == '\t') {
            encoded += "\\t";
        } else if (ch == '\n') {
            encoded += "\\n";
        } else if (ch == '\r') {
            encoded += "\\r";
        } else {
            encoded.push_back(ch);
        }
    }
    return encoded;
}

auto decode_state_field(std::string_view value, bool& ok) -> std::string {
    std::string decoded;
    decoded.reserve(value.size());
    for (std::size_t index = 0; index < value.size(); ++index) {
        const char ch = value[index];
        if (ch != '\\') {
            decoded.push_back(ch);
            continue;
        }

        if (index + 1 >= value.size()) {
            ok = false;
            return {};
        }

        const char escaped = value[++index];
        if (escaped == '\\') {
            decoded.push_back('\\');
        } else if (escaped == 't') {
            decoded.push_back('\t');
        } else if (escaped == 'n') {
            decoded.push_back('\n');
        } else if (escaped == 'r') {
            decoded.push_back('\r');
        } else {
            ok = false;
            return {};
        }
    }
    return decoded;
}

auto state_key_for_path(const fs::path& path) -> std::string {
    return path.lexically_normal().generic_string();
}

auto load_build_state(Context& context) -> bool {
    context.build_state.path = context.build_dir / ".bspm-state";
    context.build_state.signatures.clear();
    context.build_state.dirty = false;

    std::ifstream file { context.build_state.path };
    if (!file) {
        return true;
    }

    std::string line;
    if (!std::getline(file, line) || line != "bspm-state-v1") {
        return true;
    }

    while (std::getline(file, line)) {
        const auto separator = line.find('\t');
        if (separator == std::string::npos) {
            continue;
        }

        bool ok = true;
        auto key = decode_state_field(std::string_view { line }.substr(0, separator), ok);
        if (!ok) {
            continue;
        }

        auto signature = decode_state_field(std::string_view { line }.substr(separator + 1), ok);
        if (!ok) {
            continue;
        }

        context.build_state.signatures[std::move(key)] = std::move(signature);
    }

    return true;
}

auto save_build_state(Context& context) -> bool {
    if (context.dry_run || !context.build_state.dirty) {
        return true;
    }

    std::ofstream file { context.build_state.path };
    if (!file) {
        std::println("Error: failed to write '{}'", context.build_state.path.string());
        return false;
    }

    std::vector<std::string> keys;
    keys.reserve(context.build_state.signatures.size());
    for (const auto& [key, _] : context.build_state.signatures) {
        keys.push_back(key);
    }
    std::sort(std::begin(keys), std::end(keys));

    file << "bspm-state-v1\n";
    for (const auto& key : keys) {
        file << encode_state_field(key) << '\t' << encode_state_field(context.build_state.signatures[key]) << '\n';
    }

    return true;
}

auto oldest_output_time(std::span<const fs::path> outputs, fs::file_time_type& oldest_time) -> bool {
    bool found = false;
    for (const auto& output : outputs) {
        std::error_code ec;
        if (!fs::is_regular_file(output, ec)) {
            return false;
        }

        auto modified_at = fs::last_write_time(output, ec);
        if (ec) {
            return false;
        }

        if (!found || modified_at < oldest_time) {
            oldest_time = modified_at;
            found = true;
        }
    }

    return found;
}

auto inputs_are_not_newer_than(const fs::file_time_type& output_time, std::span<const fs::path> inputs) -> bool {
    for (const auto& input : inputs) {
        if (input.empty()) {
            continue;
        }

        std::error_code ec;
        if (!fs::exists(input, ec)) {
            return false;
        }

        auto modified_at = fs::last_write_time(input, ec);
        if (ec || modified_at > output_time) {
            return false;
        }
    }

    return true;
}

auto build_step_signature(
    const std::string& command, std::span<const fs::path> outputs, std::span<const fs::path> inputs) -> std::string {
    std::string signature = command;
    signature += "\noutputs:";
    for (const auto& output : outputs) {
        signature += "\n";
        signature += output.lexically_normal().generic_string();
    }
    signature += "\ninputs:";
    for (const auto& input : inputs) {
        signature += "\n";
        signature += input.lexically_normal().generic_string();
    }
    return signature;
}

auto step_is_up_to_date(Context& context, std::span<const fs::path> outputs, std::span<const fs::path> inputs,
    const std::string& signature) -> bool {
    if (outputs.empty()) {
        return false;
    }

    const auto key = state_key_for_path(outputs.front());
    std::scoped_lock lock { context.build_state_mutex };
    auto it = context.build_state.signatures.find(key);
    if (it == std::end(context.build_state.signatures) || it->second != signature) {
        return false;
    }

    fs::file_time_type output_time {};
    return oldest_output_time(outputs, output_time) && inputs_are_not_newer_than(output_time, inputs);
}

auto record_build_step(Context& context, std::span<const fs::path> outputs, const std::string& signature) -> void {
    if (outputs.empty()) {
        return;
    }

    std::scoped_lock lock { context.build_state_mutex };
    context.build_state.signatures[state_key_for_path(outputs.front())] = signature;
    context.build_state.dirty = true;
}

auto execute_incremental_command(Context& context, std::string_view command, std::span<const std::string> args,
    std::span<const fs::path> outputs, std::span<const fs::path> inputs, std::string_view label) -> bool {
    const auto invocation = make_process_invocation(context, command, args);
    const auto signature = build_step_signature(invocation.display_command, outputs, inputs);

    if (!context.dry_run && step_is_up_to_date(context, outputs, inputs, signature)) {
        if (context.verbose) {
            std::scoped_lock lock { context.output_mutex };
            std::println("up to date: {}", label);
        }
        return true;
    }

    if (!execute_process_invocation(context, invocation)) {
        return false;
    }

    if (!context.dry_run) {
        record_build_step(context, outputs, signature);
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
        std::println("\t--explain\t\tPrint discovered graph before building");
        std::println("\t--project\t\tTreat [dir] as a project root containing bspm.build");
        std::println("\t-j, --jobs <count>\tCompile independent source files in parallel");
        std::println("\t--cxxflag <flag>\tAdd a compiler flag");
        std::println("\t--ldflag <flag>\t\tAdd a linker flag");
        std::println("\t--define <name[=value]>\tAdd a preprocessor definition");
        std::println("\t--include <dir>\t\tAdd an include directory");
        std::println("\t--source <path>\t\tRestrict source discovery to a file or directory");
        std::println("\t--exclude <path>\tExclude a file or directory from source discovery");
        std::println("\t-v\t\t\tPrint commands while building");
        return;
    }

    if (command == DoctorTag) {
        std::println("Usage:");
        std::println("\t{} {} [-c <g++|clang++|msvc>] [-v]", context.name, DoctorTag);
        std::println("");
        std::println("Check local compiler and linker tools used by {}.", context.name);
        return;
    }

    if (command == GraphTag) {
        std::println("Usage:");
        std::println("\t{} {} [dir|target|all] [options]", context.name, GraphTag);
        std::println("");
        std::println("Print discovered project targets, source units, module declarations, and imports.");
        std::println("Accepts the same build selection options as '{} {}'.", context.name, BuildTag);
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
            // GCC chooses implementation-specific paths under gcm.cache for standard modules.
            // Keep this step eager until those artifacts can be identified portably.
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

            std::vector<fs::path> outputs { std_module_pcm_path(module_name) };
            std::vector<fs::path> inputs { source };
            if (module_name == StdCompatModuleName) {
                inputs.push_back(std_module_pcm_path(StdModuleName));
            }

            if (!execute_incremental_command(context, context.cpp_c, args, outputs, inputs,
                    std::string { "standard module " } + module_name)) {
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

            std::vector<fs::path> outputs { std_module_ifc_path(module_name), std_module_object_path(module_name) };
            std::vector<fs::path> inputs { source };
            if (module_name == StdCompatModuleName) {
                inputs.push_back(std_module_ifc_path(StdModuleName));
            }

            if (!execute_incremental_command(context, context.cpp_c, args, outputs, inputs,
                    std::string { "standard module " } + module_name)) {
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

auto is_source_file(const fs::path& path) -> bool {
    const auto extension = path.extension().string();
    return extension == ".cpp" || extension == ".cppm";
}

auto source_filter_root(const Context& context, const fs::path& filter) -> fs::path {
    return filter.is_absolute() ? filter : context.source_dir / filter;
}

auto target_relative_path(const Context& context, const fs::path& path) -> fs::path {
    return path.is_absolute() ? path : context.source_dir / path;
}

auto relative_filter_path(const Context& context, const fs::path& path) -> fs::path {
    const auto absolute_path = path.is_absolute() ? path : context.source_dir / path;
    std::error_code ec;
    auto relative_path = fs::relative(absolute_path, context.source_dir, ec);
    return ec ? path.lexically_normal() : relative_path.lexically_normal();
}

auto path_is_within_or_equal(const fs::path& path, const fs::path& parent) -> bool {
    auto path_string = path.lexically_normal().generic_string();
    auto parent_string = parent.lexically_normal().generic_string();
    if (parent_string == ".") {
        return true;
    }
    if (path_string == parent_string) {
        return true;
    }
    if (!parent_string.ends_with('/')) {
        parent_string.push_back('/');
    }
    return path_string.starts_with(parent_string);
}

auto is_excluded_source_path(const Context& context, const fs::path& path) -> bool {
    if (context.exclude_filters.empty()) {
        return false;
    }

    const auto relative_path = relative_filter_path(context, path);
    for (const auto& filter : context.exclude_filters) {
        if (path_is_within_or_equal(relative_path, relative_filter_path(context, filter))) {
            return true;
        }
    }
    return false;
}

auto append_source_entry(const Context& context, const fs::directory_entry& entry,
    std::vector<fs::directory_entry>& entries) -> void {
    if (!is_source_file(entry.path()) || is_excluded_source_path(context, entry.path())) {
        return;
    }

    if (context.verbose) {
        std::error_code relative_ec;
        auto relative_path = fs::relative(entry.path(), context.source_dir, relative_ec);
        std::println("entry: {}", relative_ec ? entry.path().filename().string() : relative_path.generic_string());
    }

    if (std::none_of(std::begin(entries), std::end(entries),
            [&](const auto& existing) { return existing.path() == entry.path(); })) {
        entries.push_back(entry);
    }
}

auto append_source_entries_from_root(
    const Context& context, const fs::path& root, std::vector<fs::directory_entry>& entries, std::error_code& ec)
    -> bool {
    if (is_excluded_source_path(context, root)) {
        return true;
    }

    if (fs::is_regular_file(root, ec)) {
        append_source_entry(context, fs::directory_entry { root }, entries);
        return !ec;
    }

    if (!fs::is_directory(root, ec)) {
        std::println("Error: source path '{}' is not a file or directory", root.string());
        return false;
    }

    for (fs::recursive_directory_iterator it { root, fs::directory_options::skip_permission_denied, ec }, end;
        !ec && it != end; it.increment(ec)) {
        if (it->is_directory(ec)) {
            if (is_ignored_source_directory(it->path()) || is_excluded_source_path(context, it->path())) {
                it.disable_recursion_pending();
            }
            continue;
        }

        if (!it->is_regular_file(ec)) {
            continue;
        }

        append_source_entry(context, *it, entries);
    }

    return !ec;
}

auto collect_source_entries(const Context& context) -> std::vector<fs::directory_entry> {
    std::vector<fs::directory_entry> entries;
    std::error_code ec;

    if (context.source_filters.empty()) {
        append_source_entries_from_root(context, context.source_dir, entries, ec);
    } else {
        for (const auto& filter : context.source_filters) {
            if (!append_source_entries_from_root(context, source_filter_root(context, filter), entries, ec)) {
                break;
            }
        }
    }

    if (ec) {
        std::println("Error: couldn't inspect '{}': {}", context.source_dir.string(), ec.message());
        return {};
    }

    return entries;
}

auto module_unit_kind_name(ModuleUnitKind kind) -> std::string_view {
    switch (kind) {
    case ModuleUnitKind::None:
        return "source";
    case ModuleUnitKind::PrimaryInterface:
        return "primary interface";
    case ModuleUnitKind::Implementation:
        return "implementation";
    case ModuleUnitKind::PartitionInterface:
        return "partition interface";
    case ModuleUnitKind::InternalPartition:
        return "internal partition";
    }

    return "unknown";
}

auto target_kind_name(Target target) -> std::string_view {
    switch (target) {
    case Target::Bin:
        return "bin";
    case Target::Lib:
        return "lib";
    case Target::Shared:
        return "shared";
    }

    return "unknown";
}

auto sorted_strings(const std::unordered_set<std::string>& values) -> std::vector<std::string> {
    std::vector<std::string> sorted { std::begin(values), std::end(values) };
    std::sort(std::begin(sorted), std::end(sorted));
    return sorted;
}

auto print_named_list(std::string_view label, const std::vector<std::string>& values) -> void {
    if (values.empty()) {
        return;
    }

    std::print("    {}:", label);
    for (const auto& value : values) {
        std::print(" {}", value);
    }
    std::println("");
}

auto prepare_build_plan(Context& context, fs::path dir) -> bool {
    dir = !dir.empty() ? dir : ".";

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

    return sort_units_by_dependency(context);
}

auto print_build_graph(const Context& context, std::string_view target_name = {}) -> void {
    if (!target_name.empty()) {
        std::println("target: {}", target_name);
    }
    std::println("source: {}", context.source_dir.string());
    std::println("build: {}", context.build_dir.string());
    std::println("output: {}", context.output_name);
    std::println("type: {}", target_kind_name(context.target));
    std::println("compiler: {}", context.cpp_c);

    print_named_list("header units", context.import_sys_headers);
    print_named_list("std modules", context.import_std_modules);

    if (context.compile_units.empty()) {
        std::println("units: none");
        return;
    }

    std::println("units:");
    for (std::size_t index = 0; index < context.compile_units.size(); ++index) {
        const auto& unit = context.compile_units[index];
        std::println("  {}. {} [{}]", index + 1, unit.file_name, module_unit_kind_name(unit.module_kind));
        if (!unit.module_name.empty()) {
            std::println("    provides: {}", unit.module_name);
        }
        print_named_list("imports", sorted_strings(unit.imports));
        print_named_list("std imports", sorted_strings(unit.std_module_imports));
        print_named_list("module imports", sorted_strings(unit.module_imports));
    }
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

auto append_previous_module_artifact_inputs(
    std::vector<fs::path>& inputs, const Context& context, std::size_t unit_index) -> void {
    for (std::size_t i = 0; i < unit_index; ++i) {
        const auto& dependency_unit = context.compile_units[i];
        if (dependency_unit.is_importable_module_unit()) {
            inputs.push_back(module_artifact_path(context, dependency_unit.module_name));
        }
    }
}

auto append_dependency_artifact_inputs(std::vector<fs::path>& inputs, const Context& context) -> void {
    for (const auto& [_, artifact_path] : ordered_dependency_module_artifacts(context)) {
        inputs.push_back(artifact_path);
    }
}

auto append_header_unit_inputs(std::vector<fs::path>& inputs, const Context& context, const CompileUnit& unit) -> void {
    if (context.compiler == Compiler::Clang) {
        for (const auto& header : unit.imports) {
            fs::path header_path { header };
            inputs.push_back((fs::path { ".cache" } / header_path).replace_extension(".pcm"));
        }
    } else if (context.compiler == Compiler::MSVC) {
        for (const auto& header : unit.imports) {
            inputs.push_back(msvc_header_ifc_path(header));
        }
    }
}

auto append_std_module_inputs(std::vector<fs::path>& inputs, const Context& context, const CompileUnit& unit) -> void {
    if (context.compiler == Compiler::Clang) {
        for (const auto& module_name : unit.std_module_imports) {
            inputs.push_back(std_module_pcm_path(module_name));
        }
    } else if (context.compiler == Compiler::MSVC) {
        for (const auto& module_name : unit.std_module_imports) {
            inputs.push_back(std_module_ifc_path(module_name));
        }
    }
}

auto compile_unit_inputs(const Context& context, const CompileUnit& unit, std::size_t unit_index)
    -> std::vector<fs::path> {
    std::vector<fs::path> inputs { unit.file_path };
    append_header_unit_inputs(inputs, context, unit);
    append_std_module_inputs(inputs, context, unit);
    append_previous_module_artifact_inputs(inputs, context, unit_index);
    append_dependency_artifact_inputs(inputs, context);
    return inputs;
}

auto compile_unit_outputs(const Context& context, const CompileUnit& unit, const fs::path& unit_object_path)
    -> std::vector<fs::path> {
    std::vector<fs::path> outputs { unit_object_path };
    if (unit.is_importable_module_unit()) {
        outputs.push_back(module_artifact_path(context, unit.module_name));
    }
    return outputs;
}

auto link_outputs(const Context& context) -> std::vector<fs::path> {
    std::vector<fs::path> outputs { context.output_name };
#if defined(_WIN32) || defined(_WIN64)
    if (context.target == Target::Shared && (context.compiler == Compiler::MSVC || context.compiler == Compiler::Clang)) {
        const auto import_library = shared_import_library_name(context.output_name, context.compiler);
        if (import_library != context.output_name) {
            outputs.push_back(import_library);
        }
    }
#endif
    return outputs;
}

auto append_clang_unit_import_args(
    std::vector<std::string>& args, const Context& context, const CompileUnit& unit, std::size_t unit_index) -> void {
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

auto append_non_msvc_source_compile_args(std::vector<std::string>& args, const CompileUnit& unit,
    const fs::path& unit_object_path, std::string_view extension) -> void {
    if (extension == ".cppm") {
        args.push_back(std::string { "-xc++" });
    }

    args.push_back(std::string { "-c" });
    args.push_back(path_arg(unit.file_path));
    args.push_back(std::string { "-o" });
    args.push_back(path_arg(unit_object_path));
}

auto append_user_compile_args(std::vector<std::string>& args, const Context& context) -> void {
    args.insert(std::end(args), std::begin(context.user_cxx_flags), std::end(context.user_cxx_flags));

    for (const auto& define : context.defines) {
        if (context.compiler == Compiler::MSVC) {
            args.push_back(std::string { "/D" } + define);
        } else {
            args.push_back(std::string { "-D" } + define);
        }
    }

    for (const auto& include_dir : context.include_dirs) {
        if (context.compiler == Compiler::MSVC) {
            args.push_back(std::string { "/I" });
        } else {
            args.push_back(std::string { "-I" });
        }
        args.push_back(path_arg(target_relative_path(context, include_dir)));
    }
}

auto append_user_link_args(std::vector<std::string>& args, const Context& context) -> void {
    args.insert(std::end(args), std::begin(context.user_ld_flags), std::end(context.user_ld_flags));
}

auto compile_object_only_unit(Context& context, std::size_t unit_index, const CompileUnit& unit,
    const fs::path& unit_object_path, std::string_view extension) -> bool {
    std::vector<std::string> args {
        context.cpp_standard,
        context.cpp_flags,
    };
    append_user_compile_args(args, context);

    if (context.compiler == Compiler::MSVC) {
        args.push_back(std::string { "/c" });
        args.push_back(std::string { "/TP" });

        append_msvc_import_args(args, context, unit);

        args.push_back(std::string { "/Fo" } + unit_object_path.string());
        args.push_back(path_arg(unit.file_path));
    } else if (context.compiler == Compiler::Clang) {
        args.push_back("-Wno-experimental-header-units");
        append_clang_unit_import_args(args, context, unit, unit_index);
    }

    if (context.compiler != Compiler::MSVC) {
        append_non_msvc_source_compile_args(args, unit, unit_object_path, extension);
    }

    auto inputs = compile_unit_inputs(context, unit, unit_index);
    std::vector<fs::path> outputs { unit_object_path };
    return execute_incremental_command(context, context.cpp_c, args, outputs, inputs, unit.file_name);
}

auto compile_clang_importable_unit(
    Context& context, std::size_t unit_index, const CompileUnit& unit, const fs::path& unit_object_path) -> bool {
    std::vector<std::string> module_args {
        context.cpp_standard,
        context.cpp_flags,
        std::string { "-xc++" },
        std::string { "-xc++-module" },
        std::string { "--precompile" },
        std::string { "-Wno-experimental-header-units" },
    };
    append_user_compile_args(module_args, context);

    append_clang_unit_import_args(module_args, context, unit, unit_index);

    module_args.push_back(path_arg(unit.file_path));
    module_args.push_back(std::string { "-o" });
    module_args.push_back(clang_module_pcm_path(unit.module_name).generic_string());

    auto inputs = compile_unit_inputs(context, unit, unit_index);
    std::vector<fs::path> outputs { clang_module_pcm_path(unit.module_name) };
    if (!execute_incremental_command(context, context.cpp_c, module_args, outputs, inputs, unit.file_name + " module")) {
        return false;
    }

    std::vector<std::string> object_args {
        context.cpp_standard,
        context.cpp_flags,
    };
    append_user_compile_args(object_args, context);

    append_dependency_clang_module_references(object_args, context);
    append_previous_clang_module_references(object_args, context, unit_index);

    object_args.push_back(std::string { "-c" });
    object_args.push_back(clang_module_pcm_path(unit.module_name).generic_string());
    object_args.push_back(std::string { "-o" });
    object_args.push_back(path_arg(unit_object_path));

    std::vector<fs::path> object_inputs = compile_unit_inputs(context, unit, unit_index);
    object_inputs.push_back(clang_module_pcm_path(unit.module_name));
    std::vector<fs::path> object_outputs { unit_object_path };
    return execute_incremental_command(
        context, context.cpp_c, object_args, object_outputs, object_inputs, unit.file_name + " object");
}

auto compile_importable_unit(Context& context, std::size_t unit_index, const CompileUnit& unit,
    const fs::path& unit_object_path) -> bool {
    if (context.compiler == Compiler::Clang) {
        return compile_clang_importable_unit(context, unit_index, unit, unit_object_path);
    }

    std::vector<std::string> args {
        context.cpp_standard,
        context.cpp_flags,
    };
    append_user_compile_args(args, context);

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
    } else {
        args.push_back(std::string { "-xc++" });
        args.push_back(std::string { "-c" });
        args.push_back(path_arg(unit.file_path));
        args.push_back(std::string { "-o" });
        args.push_back(path_arg(unit_object_path));
    }

    auto inputs = compile_unit_inputs(context, unit, unit_index);
    auto outputs = compile_unit_outputs(context, unit, unit_object_path);
    return execute_incremental_command(context, context.cpp_c, args, outputs, inputs, unit.file_name);
}

auto compile_unit(Context& context, std::size_t unit_index) -> bool {
    const auto& unit = context.compile_units[unit_index];
    const auto extension = unit.file_path.extension().string();
    auto unit_object_path = object_path(context, unit.file_path);
    if (!ensure_parent_directory(unit_object_path)) {
        return false;
    }

    if (extension == ".cppm" && !unit.declares_module()) {
        std::println("Error: '{}' uses the .cppm extension but does not declare a module unit", unit.file_name);
        return false;
    }

    if (unit.is_importable_module_unit()) {
        return compile_importable_unit(context, unit_index, unit, unit_object_path);
    }

    return compile_object_only_unit(context, unit_index, unit, unit_object_path, extension);
}

auto compile_units(Context& context) -> bool {
    if (context.jobs <= 1 || context.dry_run) {
        for (std::size_t unit_index = 0; unit_index < context.compile_units.size(); ++unit_index) {
            if (!compile_unit(context, unit_index)) {
                return false;
            }
        }
        return true;
    }

    std::vector<std::size_t> parallel_units;
    parallel_units.reserve(context.compile_units.size());

    for (std::size_t unit_index = 0; unit_index < context.compile_units.size(); ++unit_index) {
        if (context.compile_units[unit_index].is_importable_module_unit()) {
            if (!compile_unit(context, unit_index)) {
                return false;
            }
        } else {
            parallel_units.push_back(unit_index);
        }
    }

    std::vector<std::future<bool>> pending;
    pending.reserve(std::min(context.jobs, parallel_units.size()));

    auto wait_for_oldest_job = [&]() -> bool {
        auto result = pending.front().get();
        pending.erase(std::begin(pending));
        return result;
    };

    bool success = true;
    for (const auto unit_index : parallel_units) {
        if (!success) {
            break;
        }

        pending.push_back(std::async(std::launch::async, [&context, unit_index] {
            return compile_unit(context, unit_index);
        }));
        if (pending.size() >= context.jobs && !wait_for_oldest_job()) {
            success = false;
        }
    }

    while (!pending.empty()) {
        if (!wait_for_oldest_job()) {
            success = false;
        }
    }

    return success;
}

auto build_command(Context& context, fs::path dir) -> bool {
    if (!prepare_build_plan(context, dir)) {
        return false;
    }

    if (context.explain) {
        print_build_graph(context);
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

    if (!load_build_state(context)) {
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
                std::vector<std::string> args {
                    context.cpp_standard,
                    context.cpp_flags,
                };
                append_user_compile_args(args, context);
                args.push_back(std::string { "-xc++-system-header" });
                args.push_back(std::string { "-c" });
                args.push_back(header);

                if (!execute_command(context, context.cpp_c, args)) {
                    return false;
                }
            } else if (context.compiler == Compiler::Clang) {
                fs::path header_path { header };
                auto output_path = (fs::path { ".cache" } / header_path).replace_extension(".pcm");
                if (!ensure_parent_directory(output_path)) {
                    return false;
                }

                std::vector<std::string> args {
                    context.cpp_standard,
                    context.cpp_flags,
                };
                append_user_compile_args(args, context);
                args.push_back(std::string { "-xc++-system-header --precompile" });
                args.push_back(header);
                args.push_back(std::string { "-o" });
                args.push_back(output_path.string());
                std::array outputs { output_path };
                std::array<fs::path, 0> inputs {};

                if (!execute_incremental_command(
                        context, context.cpp_c, args, outputs, inputs, std::string { "header unit <" } + header + ">")) {
                    return false;
                }
            } else if (context.compiler == Compiler::MSVC) {
                auto ifc_path = msvc_header_ifc_path(header);
                auto obj_path = msvc_header_object_path(header);

                if (!ifc_path.parent_path().empty()) {
                    fs::create_directories(ifc_path.parent_path());
                }

                std::vector<std::string> args {
                    context.cpp_standard,
                    context.cpp_flags,
                };
                append_user_compile_args(args, context);
                args.push_back(std::string { "/c" });
                args.push_back(std::string { "/exportHeader" });
                args.push_back(std::string { "/headerName:angle" });
                args.push_back(header);
                args.push_back(std::string { "/ifcOutput" });
                args.push_back(ifc_path.string());
                args.push_back(std::string { "/Fo" } + obj_path.string());
                std::array outputs { ifc_path, obj_path };
                std::array<fs::path, 0> inputs {};

                if (!execute_incremental_command(
                        context, context.cpp_c, args, outputs, inputs, std::string { "header unit <" } + header + ">")) {
                    return false;
                }
            }
        }
    }

    if (!compile_units(context)) {
        return false;
    }

    // link
    std::vector<std::string> link_entries;
    std::vector<fs::path> link_input_paths;
    for (const auto& unit : context.compile_units) {
        auto path = object_path(context, unit.file_path);
        link_entries.push_back(path.string());
        link_input_paths.push_back(path);
    }

    if (context.compiler == Compiler::MSVC) {
        for (const auto& header : context.import_sys_headers) {
            auto path = msvc_header_object_path(header);
            link_entries.push_back(path.string());
            link_input_paths.push_back(path);
        }

        for (const auto& module_name : context.import_std_modules) {
            auto path = std_module_object_path(module_name);
            link_entries.push_back(path.string());
            link_input_paths.push_back(path);
        }
    }

    if (context.target != Target::Lib) {
        for (const auto& dependency_link_input : context.dependency_link_inputs) {
            link_entries.push_back(path_arg(dependency_link_input));
            link_input_paths.push_back(dependency_link_input);
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
        append_user_link_args(link_args, context);

        if (context.compiler == Compiler::MSVC) {
            link_args.push_back(std::string { "/Fe" } + context.output_name);
        } else {
            link_args.push_back("-o");
            link_args.push_back(context.output_name);
        }
    }

    auto outputs = link_outputs(context);
    if (!execute_incremental_command(context, link_command, link_args, outputs, link_input_paths, context.output_name)) {
        return false;
    }

    return save_build_state(context);
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

    auto display_command = quote_arg(app_file.string());
    if (context.verbose) {
        std::println("command: {}", display_command);
    }

    const int status = spawn_process(app_file.string(), {});
    if (status != 0) {
        std::println("Error: command failed with status {}", status);
        return false;
    }

    return true;
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

auto parse_jobs_count(std::string_view value, std::size_t& jobs) -> bool {
    uint64_t parsed = 0;
    const auto* begin = value.data();
    const auto* end = value.data() + value.size();
    auto [ptr, ec] = std::from_chars(begin, end, parsed);
    if (ec != std::errc {} || ptr != end || parsed == 0) {
        return false;
    }

    jobs = static_cast<std::size_t>(parsed);
    return true;
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
        } else if (opt == ExplainOptTag) {
            context.explain = true;
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
        } else if (opt == JobsOptTag || opt == LongJobsOptTag) {
            if (idx + 1 >= opts.size() || is_option_argument(opts[idx + 1])) {
                std::println("Error: Invalid option '{}' in {}, option didn't have parameter", opt, source);
                return false;
            }

            std::size_t jobs = 1;
            const auto value = opts[++idx];
            if (!parse_jobs_count(value, jobs)) {
                std::println("Error: Invalid jobs count '{}' in {}", value, source);
                return false;
            }
            context.jobs = jobs;
        } else if (opt == CxxFlagOptTag || opt == LdFlagOptTag || opt == DefineOptTag || opt == IncludeOptTag
            || opt == SourceOptTag || opt == ExcludeOptTag) {
            if (idx + 1 >= opts.size()) {
                std::println("Error: Invalid option '{}' in {}, option didn't have parameter", opt, source);
                return false;
            }

            const auto value = opts[++idx];
            if (opt == CxxFlagOptTag) {
                context.user_cxx_flags.emplace_back(value);
            } else if (opt == LdFlagOptTag) {
                context.user_ld_flags.emplace_back(value);
            } else if (opt == DefineOptTag) {
                context.defines.emplace_back(value);
            } else if (opt == IncludeOptTag) {
                context.include_dirs.emplace_back(value);
            } else if (opt == SourceOptTag) {
                context.source_filters.emplace_back(value);
            } else {
                context.exclude_filters.emplace_back(value);
            }
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

        if (tokens[0] == "common") {
            if (tokens.size() < 2) {
                std::println("Error: bspm.build line {} expects 'common [options]'", line_number);
                return false;
            }

            project.common_build_options.insert(std::end(project.common_build_options), std::next(std::begin(tokens)),
                std::end(tokens));
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

        project.targets.push_back(std::move(target));
    }

    Context common_validation_context;
    auto common_option_views = build_option_views(project.common_build_options);
    if (!apply_build_options(common_validation_context, common_option_views, "bspm.build common options")) {
        return false;
    }

    for (const auto& target : project.targets) {
        Context validation_context;
        auto common_views = build_option_views(project.common_build_options);
        auto target_views = build_option_views(target.build_options);
        if (!apply_build_options(validation_context, common_views, "bspm.build common options")) {
            return false;
        }
        if (!apply_build_options(validation_context, target_views, "bspm.build target options")) {
            return false;
        }
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

    const bool build_like_command = command == BuildTag || command == GraphTag;

    if (conf.argv && conf.index < conf.argc) {
        int32_t idx = conf.index;

        while (idx < conf.argc) {
            std::string_view opt { conf.argv[idx] };

            if (opt == VerboseOptTag) {
                context.verbose = true;
                idx++;
                continue;
            }

            if (!build_like_command) {
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

            if (build_like_command) {
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
                } else if (opt == ExplainOptTag) {
                    context.explain = true;
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
                } else if (opt == JobsOptTag || opt == LongJobsOptTag) {
                    if (idx + 1 < conf.argc) {
                        idx++;
                        std::string_view jobs_value { conf.argv[idx] };

                        if (is_option_argument(jobs_value)) {
                            std::println("Error: Invalid option '{}' for {} command, option didn't have {}", opt,
                                command, "parameter");
                            return false;
                        }

                        std::size_t jobs = 1;
                        if (!parse_jobs_count(jobs_value, jobs)) {
                            std::println("Error: Invalid jobs count '{}' for {} command", jobs_value, command);
                            return false;
                        }
                        context.jobs = jobs;
                    } else {
                        std::println("Error: Invalid option '{}' for {} command, option didn't have {}", opt, command,
                            "parameter");
                        return false;
                    }
                } else if (opt == CxxFlagOptTag || opt == LdFlagOptTag || opt == DefineOptTag || opt == IncludeOptTag
                    || opt == SourceOptTag || opt == ExcludeOptTag) {
                    if (idx + 1 < conf.argc) {
                        idx++;
                        std::string_view value { conf.argv[idx] };

                        if (opt == CxxFlagOptTag) {
                            context.user_cxx_flags.emplace_back(value);
                        } else if (opt == LdFlagOptTag) {
                            context.user_ld_flags.emplace_back(value);
                        } else if (opt == DefineOptTag) {
                            context.defines.emplace_back(value);
                        } else if (opt == IncludeOptTag) {
                            context.include_dirs.emplace_back(value);
                        } else if (opt == SourceOptTag) {
                            context.source_filters.emplace_back(value);
                        } else {
                            context.exclude_filters.emplace_back(value);
                        }
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

    if (build_like_command) {
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

auto create_target_context(const ProjectConfig& project, const TargetConfig& target,
    std::span<const std::string_view> cli_options, Context& context) -> bool {
    auto common_options = build_option_views(project.common_build_options);
    auto config_options = build_option_views(target.build_options);
    if (!apply_build_options(context, common_options, "bspm.build common options")) {
        return false;
    }
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
        if (!create_target_context(project, *target, cli_options, target_context)) {
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

auto graph_command(Context& context, fs::path dir, std::string_view target_name = {}) -> bool {
    if (!prepare_build_plan(context, dir)) {
        return false;
    }

    print_build_graph(context, target_name);
    return true;
}

auto graph_project(const ProjectConfig& project, std::span<const std::string_view> requested_targets,
    std::span<const std::string_view> cli_options) -> bool {
    std::vector<const TargetConfig*> build_order;
    if (!collect_project_build_order(project, requested_targets, build_order)) {
        return false;
    }

    std::println("project: {}", project.name.empty() ? project.root.filename().string() : project.name);
    std::println("root: {}", project.root.string());
    std::print("target order:");
    for (const auto* target : build_order) {
        std::print(" {}", target->name);
    }
    std::println("");

    for (const auto* target : build_order) {
        Context target_context;
        if (!create_target_context(project, *target, cli_options, target_context)) {
            return false;
        }

        if (!target->dependencies.empty()) {
            std::print("depends({}):", target->name);
            for (const auto& dependency : target->dependencies) {
                std::print(" {}", dependency);
            }
            std::println("");
        }

        if (!graph_command(target_context, project.root / target->path, target->name)) {
            return false;
        }
    }

    return true;
}

auto create_configured_target_context(
    const ProjectConfig& project, const TargetConfig& target, bool verbose, Context& context) -> bool {
    if (!create_target_context(project, target, {}, context)) {
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
    if (!create_configured_target_context(project, *target, verbose, target_context)) {
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
        if (!create_configured_target_context(project, *target, verbose, target_context)) {
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

    if (command == DoctorTag) {
        if (!init_doctor_context(context, argc, argv, 2)) {
            std::println("Type '{} help {}' for more description.", context.name, DoctorTag);
            return 1;
        }
        return doctor_command(context) ? 0 : 1;
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

    if (command == GraphTag) {
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

            return graph_project(project, requested_targets, cli_options) ? 0 : 1;
        }

        if (!init_context(context, GraphTag, { .argc = argc, .index = option_index, .argv = argv })) {
            std::println("Type '{} help {}' for more description.", context.name, GraphTag);
            return 1;
        }
        return graph_command(context, dir) ? 0 : 1;
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
