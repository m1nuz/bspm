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

constexpr char VersionTag[] = "version";
constexpr char HelpTag[] = "help";
constexpr char BuildTag[] = "build";
constexpr char RunTag[] = "run";
constexpr char CleanTag[] = "clean";
constexpr char InitTag[] = "init";

std::string_view commands[] { HelpTag, InitTag, BuildTag, RunTag, CleanTag, VersionTag };

constexpr char DefaultMain[] = R"(
import <print>;

int main(int argc, char** argv) {
    std::println("Hello, world!");
    return 0;
}

)";

struct Context {

    static constexpr std::string_view version { "0.0.2" };
    static constexpr std::string_view name { "bspm" };

    using Value = std::variant<uint64_t, double, std::string_view>;
    using Options = std::unordered_map<std::string_view, Value>;

    std::string cc { "gcc" };
    std::string cpp_c { "g++" };
    std::string cpp_standard { "-std=c++23" };
    std::string cpp_flags { "-fmodules-ts -MD" };
    std::string ld_flags { "-lstdc++exp" };
    std::string output_name;

    std::vector<std::string> import_sys_headers;

    Target target { Target::Bin };

    bool verbose { true };
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
    }
}

auto version_command(Context& context) -> void {
    std::println("{} {}", context.name, context.version);
}

inline auto is_cppm(const fs::directory_entry& entry) -> bool {
    return entry.path().extension() == ".cppm";
}

std::vector<std::string> extract_library_names_from_file(std::string_view filename) {
    std::vector<std::string> library_names;

    std::ifstream file { std::data(filename) };
    if (!file) {
        std::println("Error: failed to open file '{}'", filename);
        return library_names;
    }

    std::regex import_regex(R"(import\s+<([^<>]+)>;)");
    std::string line;

    while (std::getline(file, line)) {
        std::smatch match;
        if (std::regex_search(line, match, import_regex)) {
            std::string library_name = match[1];

            auto it = std::find(std::begin(library_names), std::end(library_names), library_name);
            if (it == std::end(library_names)) {
                library_names.push_back(library_name);
            }
        }
    }

    file.close();
    return library_names;
}

static auto process_cpp_headers_imports(Context& context, const std::vector<fs::directory_entry>& entries) -> void {

    std::vector<std::string> imports;

    for (auto& entry : entries) {
        auto library_names = extract_library_names_from_file(entry.path().string());
        if (!library_names.empty()) {
            imports.insert(std::end(imports), std::begin(library_names), std::end(library_names));
        }
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
            dependency.erase(dependency.find(';'));
            dependencies.push_back(dependency);
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

auto build_command(Context& context, fs::path dir) -> void {
    dir = !dir.empty() ? dir : ".";

    if (context.verbose) {
        std::println("{} build '{}'", context.name, dir.string());
        execute_command(context, context.cpp_c, std::array { std::string { "-v" } });
    }

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

    process_cpp_headers_imports(context, entries);

    std::vector<std::string> file_entries;
    for (const auto& e : entries) {
        file_entries.push_back(e.path().string());
    }

    auto oredered_entries = sort_files_by_dependency(file_entries);

    // build
    for (const auto& header : context.import_sys_headers) {
        execute_command(context, context.cpp_c,
            std::array { context.cpp_standard, context.cpp_flags, std::string { "-xc++-system-header" },
                std::string { "-c" }, header });
    }

    for (const auto& entry : oredered_entries) {
        fs::path entry_path { entry };
        auto extension = entry_path.extension().string();
        if (extension == ".cpp") {
            execute_command(context, context.cpp_c,
                std::array { context.cpp_standard, context.cpp_flags, std::string { "-c" }, entry });
        } else if (extension == ".cppm") {
            execute_command(context, context.cc,
                std::array {
                    std::string { "-xc++" }, context.cpp_standard, context.cpp_flags, std::string { "-c" }, entry });
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

    fs::remove_all("gcm.cache");

    // Restore previous working directory
    fs::current_path(previous_path);
}

auto init_command(Context& context, fs::path dir) -> void {
    if (!fs::exists(dir)) {
        fs::create_directory(dir);
    }

    if (context.target == Target::Bin) {
        constexpr char main_file[] = "main.cpp";
        fs::path fullpath = dir / main_file;

        if (!fs::exists(fullpath)) {
            auto f = fopen(fullpath.string().c_str(), "w");
            if (f) {
                std::print(f, "{}", DefaultMain);
                fclose(f);
            }
        }

        if (!fs::exists(fullpath)) {
            std::println("Error: couldn't create '{}'", fullpath.string());
        }
    }
}

struct InitConfiguration { };

auto init_context(Context& context, const InitConfiguration&) -> void {
#if defined(_WIN32) || defined(_WIN64)
    context.output_name = "a.exe";
#else
    context.output_name = "a.out";
#endif
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
            init_context(context, {});
            help_command(context, arg_idx + 1 < argc ? argv[arg_idx + 1] : std::string_view {});
            return 0;
        }

        if (std::strncmp(argv[arg_idx], VersionTag, sizeof(VersionTag)) == 0) {
            init_context(context, {});
            version_command(context);
            return 0;
        }

        if (std::strncmp(argv[arg_idx], BuildTag, sizeof(BuildTag)) == 0) {
            init_context(context, {});
            build_command(context, arg_idx + 1 < argc ? argv[arg_idx + 1] : std::string_view {});
            return 0;
        }

        if (std::strncmp(argv[arg_idx], RunTag, sizeof(RunTag)) == 0) {
            init_context(context, {});
            run_command(context, arg_idx + 1 < argc ? argv[arg_idx + 1] : std::string_view {});
            return 0;
        }

        if (std::strncmp(argv[arg_idx], CleanTag, sizeof(CleanTag)) == 0) {
            init_context(context, {});
            clean_command(context, arg_idx + 1 < argc ? argv[arg_idx + 1] : std::string_view {});
            return 0;
        }

        if (std::strncmp(argv[arg_idx], InitTag, sizeof(InitTag)) == 0) {
            init_context(context, {});
            init_command(context, arg_idx + 1 < argc ? argv[arg_idx + 1] : std::string_view {});
            return 0;
        }

        arg_idx++;
    }

    std::println("Error: '{}' unknown command!", argv[1]);

    return 0;
}