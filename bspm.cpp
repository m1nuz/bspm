#define _CRT_SECURE_NO_WARNINGS

#include <array>
#include <chrono>
#include <cstdint>
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
#include <unordered_map>
#include <unordered_set>
#include <variant>
#include <vector>

namespace fs = std::filesystem;

enum class Target { Bin, Lib, Shared };
enum class Compiler { GCC, Clang, MSCL };

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
    std::unordered_set<std::string> modules;

    auto get_dependencies() const -> std::unordered_set<std::string> {
        std::unordered_set<std::string> all_deps = imports;
        all_deps.insert(modules.begin(), modules.end());
        return all_deps;
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
    std::string output_name;

    std::vector<std::string> import_sys_headers;
    bool process_sys_imports { true };

    std::vector<CompileUnit> compile_units;

    Target target { Target::Bin };

    bool verbose { false };
    bool debug { true };
};

auto execute_command(Context& context, std::string_view command, std::span<const std::string> args) -> void {
    std::string full_command { command };
    for (const auto& arg : args) {
        full_command += " " + arg;
    }

    if (context.verbose) {
        std::println("command: {}", full_command);
    }

    std::system(std::data(full_command));
}

auto help_command([[maybe_unused]] Context& context, std::string_view command) -> void {
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
        return;
    }
}

auto version_command(Context& context) -> void {
    std::println("{} {}", context.name, context.version);
}

inline auto is_cppm(const fs::directory_entry& entry) -> bool {
    return entry.path().extension() == ".cppm";
}

auto extract_import_names_from_file(std::string_view filename)
    -> std::tuple<std::vector<std::string>, std::vector<std::string>> {
    std::vector<std::string> library_names;
    std::vector<std::string> module_names;

    std::ifstream file { std::data(filename) };
    if (!file) {
        std::println("Error: failed to open file '{}'", filename);
        return {};
    }

    std::regex import_sys_regex(R"(import\s+<([^<>]+)>;)");
    std::regex import_module_regex(R"(import\s+([^<>]+);)");
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
                module_names.push_back(match[1]);
            }
        }
    }

    file.close();
    return { library_names, module_names };
}

static auto process_units_imports(Context& context, const std::vector<fs::directory_entry>& entries) -> void {
    context.compile_units.reserve(std::size(entries));

    std::vector<std::string> imports;

    for (auto& entry : entries) {
        CompileUnit unit;

        unit.file_path = entry.path();
        unit.file_name = unit.file_path.filename().string();

        auto [library_names, module_names] = extract_import_names_from_file(entry.path().string());
        if (!library_names.empty()) {
            imports.insert(std::end(imports), std::begin(library_names), std::end(library_names));

            unit.imports.insert(std::begin(library_names), std::end(library_names));
        }

        if (!module_names.empty()) {
            unit.modules.insert(std::begin(module_names), std::end(module_names));
        }

        context.compile_units.push_back(unit);
    }

    std::sort(std::begin(imports), std::end(imports));
    imports.erase(std::unique(std::begin(imports), std::end(imports)), std::end(imports));

    context.import_sys_headers = imports;
}

std::string get_file_name(const std::string& file_path) {
    fs::path path(file_path);
    return path.filename().string();
}

std::string get_module_name(const std::string& file_path) {
    std::ifstream input(file_path);
    std::string line;

    while (std::getline(input, line)) {
        // Assuming each module statement is in the format "export module a;"
        if (line.find("export module") != std::string::npos) {
            std::string module_name = line.substr(line.find("module") + 7);
            module_name.erase(module_name.find(';'));
            return module_name;
        }
    }

    return {};
}

std::vector<std::string> extract_dependencies(const std::string& file_path) {
    std::vector<std::string> dependencies;
    std::ifstream input(file_path);
    std::string line;

    while (std::getline(input, line)) {
        // Assuming each import statement is in the format "import a;"
        if (line.find("import") != std::string::npos) {
            std::string dependency = line.substr(line.find("import") + 7);
            auto end_pos = dependency.find(';');
            if (end_pos != std::string::npos) {
                dependency.erase(end_pos);
                dependencies.push_back(dependency);
            }
        }
    }

    return dependencies;
}

std::vector<std::string> sort_files_by_dependency(const std::vector<std::string>& file_paths) {
    std::unordered_map<std::string, std::unordered_set<std::string>> dependencies;
    std::unordered_map<std::string, std::string> module_names;
    std::unordered_set<std::string> visited;
    std::vector<std::string> sorted_files;

    // Extract the dependencies and module names from each file
    for (const std::string& file_path : file_paths) {
        auto deps = extract_dependencies(file_path);
        std::string module_name = get_module_name(file_path);
        std::string file_name = get_file_name(file_path);

        if (!module_name.empty()) {
            module_names[file_name] = module_name;
            dependencies[file_name] = std::unordered_set<std::string>(deps.begin(), deps.end());
        }
    }

    // Perform topological sorting
    std::function<void(const std::string&)> visit = [&](const std::string& file_name) {
        visited.insert(file_name);

        for (const std::string& dependency : dependencies[file_name]) {
            auto it = std::find_if(
                module_names.begin(), module_names.end(), [&](const auto& pair) { return pair.second == dependency; });

            if (it != module_names.end() && visited.find(it->first) == visited.end()) {
                visit(it->first);
            }
        }

        sorted_files.push_back(file_name);
    };

    for (const std::string& file_path : file_paths) {
        std::string file_name = get_file_name(file_path);
        if (visited.find(file_name) == visited.end()) {
            visit(file_name);
        }
    }

    // Prepend the file paths to the sorted file names
    std::vector<std::string> sorted_file_paths;
    for (const std::string& file_name : sorted_files) {
        auto it = std::find_if(file_paths.begin(), file_paths.end(),
            [&](const std::string& file_path) { return get_file_name(file_path) == file_name; });

        if (it != file_paths.end()) {
            sorted_file_paths.push_back(*it);
        }
    }

    return sorted_file_paths;
}

auto sort_units_by_dependency(Context& context) -> void {
    std::unordered_map<std::string, std::unordered_set<std::string>> dependencies;
    std::unordered_map<std::string, std::string> module_names;
    std::unordered_set<std::string> visited;
    //  std::vector<std::string> sorted_files;
    std::vector<CompileUnit> sorted_units;

    // Extract the dependencies and module names from each file
    for (const auto& unit : context.compile_units) {
        auto file_path = unit.file_path.string();
        auto deps = extract_dependencies(file_path);
        std::string module_name = get_module_name(file_path);
        std::string file_name = get_file_name(file_path);

        if (!module_name.empty()) {
            module_names[file_name] = module_name;
            dependencies[file_name] = std::unordered_set<std::string>(deps.begin(), deps.end());
        }
    }

    // Perform topological sorting
    // auto visit = [&](const auto& unit) {
    //     auto file_name = unit.file_name;

    //     visited.insert(file_name);

    //     for (const auto& dependency : dependencies[file_name]) {
    //         auto it = std::find_if(
    //             module_names.begin(), module_names.end(), [&](const auto& pair) { return pair.second == dependency;
    //             });

    //         if (it != module_names.end() && visited.find(it->first) == visited.end()) {
    //             visit(it->first);
    //         }
    //     }

    //     sorted_units.push_back(unit);
    // };

    // for (const auto& unit : context.compile_units) {
    //     auto file_name = unit.file_name;
    //     if (visited.find(file_name) == visited.end()) {
    //         visit(file_name);
    //     }
    // }

    // // Perform topological sorting
    // std::function<void(const std::string&)> visit = [&](const std::string& file_name) {
    //     visited.insert(file_name);

    //     for (const std::string& dependency : dependencies[file_name]) {
    //         auto it = std::find_if(
    //             module_names.begin(), module_names.end(), [&](const auto& pair) { return pair.second == dependency;
    //             });

    //         if (it != module_names.end() && visited.find(it->first) == visited.end()) {
    //             visit(it->first);
    //         }
    //     }

    //     sorted_files.push_back(file_name);
    // };

    // for (const std::string& file_path : file_paths) {
    //     std::string file_name = get_file_name(file_path);
    //     if (visited.find(file_name) == visited.end()) {
    //         visit(file_name);
    //     }
    // }

    // // Prepend the file paths to the sorted file names
    // std::vector<std::string> sorted_file_paths;
    // for (const std::string& file_name : sorted_files) {
    //     auto it = std::find_if(file_paths.begin(), file_paths.end(),
    //         [&](const std::string& file_path) { return get_file_name(file_path) == file_name; });

    //     if (it != file_paths.end()) {
    //         sorted_file_paths.push_back(*it);
    //     }
    // }

    // return sorted_file_paths;
}

auto build_command(Context& context, fs::path dir) -> void {
    dir = !dir.empty() ? dir : ".";

    // if (context.verbose) {
    //     std::println("{} build '{}'", context.name, dir.string());
    //     execute_command(context, context.cpp_c, std::array { std::string { "-v" } });
    // }

    // Set working directory
    fs::path previous_path = fs::current_path();
    fs::current_path(dir);

    auto search_path = previous_path / dir;

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

    // Sort sources with .cppm first
    std::sort(
        std::begin(entries), std::end(entries), [](const auto& a, const auto& b) { return is_cppm(a) && !is_cppm(b); });

    process_units_imports(context, entries);

    // std::vector<std::string> file_entries;
    // for (const auto& e : entries) {
    //     file_entries.push_back(e.path().string());
    // }

    // auto oredered_entries = sort_files_by_dependency(file_entries);
    sort_units_by_dependency(context);

    // build
    if (context.process_sys_imports) {

        if (context.compiler == Compiler::Clang) {
            if (!fs::exists(".cache")) {
                fs::create_directory(".cache");
            }
        }

        for (const auto& header : context.import_sys_headers) {

            if (context.compiler == Compiler::GCC) {
                execute_command(context, context.cpp_c,
                    std::array { context.cpp_standard, context.cpp_flags, std::string { "-xc++-system-header" },
                        std::string { "-c" }, header });
            } else if (context.compiler == Compiler::Clang) {
                fs::path header_path { header };

                execute_command(context, context.cpp_c,
                    std::array { context.cpp_standard, context.cpp_flags,
                        std::string { "-xc++-system-header --precompile" }, std::string { "-c" }, header,
                        std::string { "-o" }, (".cache" / header_path).replace_extension(".pcm").string() });
            }
        }
    }

    for (const auto& unit : context.compile_units) {
        fs::path entry_path = unit.file_path;
        auto extension = entry_path.extension().string();
        if (extension == ".cpp") {
            std::vector<std::string> args;

            args.push_back(context.cpp_standard);
            args.push_back(context.cpp_flags);

            if (context.compiler == Compiler::Clang) {
                // args.push_back(std::string { "-fprebuilt-module-path=." });
                args.push_back("-Wno-experimental-header-units");

                for (const auto& imported_module : unit.imports) {
                    std::string module_file;
                    std::format_to(std::back_inserter(module_file), "-fmodule-file=\"{}.pcm\"",
                        (search_path / ".cache" / imported_module).string());
                    args.push_back(module_file);
                }

                for (const auto& imported_module : unit.modules) {
                    std::string module_file;
                    std::format_to(std::back_inserter(module_file), "-fmodule-file=\"{}.pcm\"",
                        (search_path / imported_module).string());
                    args.push_back(module_file);
                }
            }

            args.push_back(std::string { "-c" });
            args.push_back(unit.file_name);

            execute_command(context, context.cpp_c, args);
        } else if (extension == ".cppm") {
            std::vector<std::string> args;

            args.push_back(std::string { "-xc++" });
            args.push_back(context.cpp_standard);
            args.push_back(context.cpp_flags);

            if (context.compiler == Compiler::Clang) {
                args.push_back(std::string { "-xc++-module" });
                // args.push_back(std::string { "--precompile" });
                // args.push_back(std::string { "-fprebuilt-module-path=." });

                args.push_back("-Wno-experimental-header-units");

                for (const auto& imported_module : unit.imports) {
                    std::string module_file;
                    std::format_to(std::back_inserter(module_file), "-fmodule-file=\"{}.pcm\"",
                        (search_path / ".cache" / imported_module).string());
                    args.push_back(module_file);
                }
            }

            args.push_back(std::string { "-c" });
            args.push_back(unit.file_name);

            execute_command(context, context.cpp_c, args);
        }
    }

    // link
    std::vector<std::string> link_entries;
    for (const auto& entry : entries) {
        auto p = entry.path();
        link_entries.push_back(p.replace_extension(".o").string());
    }

    std::vector<std::string> link_args;
    link_args.insert(std::end(link_args), std::cbegin(link_entries), std::cend(link_entries));
    link_args.push_back(context.ld_flags);
    link_args.push_back("-o");
    link_args.push_back(context.output_name);
    execute_command(context, context.cpp_c, link_args);

    // Restore previous working directory
    fs::current_path(previous_path);
}

static bool is_file_executable(std::string_view filename) {
    std::filesystem::file_status status = std::filesystem::status(filename);
    return (status.permissions() & std::filesystem::perms::owner_exec) != std::filesystem::perms::none;
}

static auto find_app_file(const fs::path& search_path) {
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

auto run_command(Context& context, fs::path dir) -> void {
    dir = !dir.empty() ? dir : ".";

    // Set working directory
    fs::path previous_path = fs::current_path();
    fs::current_path(dir);

    auto search_path = previous_path / dir;
    if (!fs::exists(search_path)) {
        std::println("Error: '{}' not exists!", search_path.string());
        return;
    }

    auto app_file = find_app_file(search_path);
    if (app_file.empty() || !fs::exists(app_file)) {
        std::println("Error: couldn't run from '{}'", dir.string());
        return;
    }

    if (context.verbose) {
        std::println("{} running '{}'", context.name, app_file);
    }

    std::system(std::data(app_file));

    // Restore previous working directory
    fs::current_path(previous_path);
}

auto clean_command(Context& context, fs::path dir) -> void {
    dir = !dir.empty() ? dir : ".";

    // Set working directory
    fs::path previous_path = fs::current_path();
    fs::current_path(dir);

    auto search_path = previous_path / dir;
    if (!fs::exists(search_path)) {
        std::println("Error: '{}' not exists!", search_path.string());
        return;
    }

    auto app_file = find_app_file(search_path);
    if (!app_file.empty()) {
        fs::remove(app_file);
    }

    for (const auto& entry : fs::directory_iterator(search_path)) {
        if (entry.is_regular_file()) {
            auto extension = entry.path().extension().string();
            if (extension == ".o" || extension == ".d") {
                if (context.verbose) {
                    std::println("remove entry: {}", entry.path().filename().string());
                }

                fs::remove(entry.path());
            }
        }
    }

    if (fs::exists(search_path / "gcm.cache")) {
        if (auto res = fs::remove_all(search_path / "gcm.cache"); res != static_cast<std::uintmax_t>(-1) || res > 1) {
            if (context.verbose) {
                std::println("remove entry: {}", "gcm.cache");
            }
        }
    }

    if (fs::exists(search_path / ".cache")) {
        if (auto res = fs::remove_all(search_path / ".cache"); res != static_cast<std::uintmax_t>(-1) || res > 1) {
            if (context.verbose) {
                std::println("remove entry: {}", ".cache");
            }
        }
    }

    // Restore previous working directory
    fs::current_path(previous_path);
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
    context.cpp_flags = "-fmodules-ts -MD";
    context.ld_flags = "-lstdc++exp";
    context.process_sys_imports = true;
}

auto init_clang_compiler(Context& context) -> void {
    context.compiler = Compiler::Clang;
    context.cc = "clang";
    context.cpp_c = "clang++";
    context.cpp_standard = "-std=c++23";
    context.cpp_flags = "-fmodules -MD";
    context.ld_flags.clear();
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
                return 0;
            }
            build_command(context, arg_idx + 1 < argc ? argv[arg_idx + 1] : std::string_view {});
            return 0;
        }

        if (std::strncmp(argv[arg_idx], RunTag, sizeof(RunTag)) == 0) {
            init_context(context, RunTag, { .argc = argc, .index = arg_idx + 2, .argv = argv });
            run_command(context, arg_idx + 1 < argc ? argv[arg_idx + 1] : std::string_view {});
            return 0;
        }

        if (std::strncmp(argv[arg_idx], CleanTag, sizeof(CleanTag)) == 0) {
            init_context(context, CleanTag, { .argc = argc, .index = arg_idx + 2, .argv = argv });
            clean_command(context, arg_idx + 1 < argc ? argv[arg_idx + 1] : std::string_view {});
            return 0;
        }

        if (std::strncmp(argv[arg_idx], InitTag, sizeof(InitTag)) == 0) {
            init_context(context, InitTag, {});
            init_command(context, arg_idx + 1 < argc ? argv[arg_idx + 1] : std::string_view {});
            return 0;
        }

        arg_idx++;
    }

    std::println("Error: '{}' unknown command!", argv[1]);

    return 0;
}