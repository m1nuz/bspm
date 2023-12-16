#include <cxxopts.hpp>
#include <fmt/chrono.h>
#include <fmt/color.h>
#include <fmt/core.h>
#include <fmt/ranges.h>
#include <toml.hpp>

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <regex>
#include <string>
#include <vector>

namespace fs = std::filesystem;

constexpr char DefaultMain[] = R"(int main(int, char**) {
    return 0;
}
)";

namespace application {

enum class Target { Bin, Lib, Shared };

struct Instance {
    std::string name = "bspm";
    std::string compiller = "gcc";
    std::string cpp_standart = "-std=c++23";
    std::string cpp_flags = "-fmodules-ts";
    std::string debug_flag = "-g";
    std::string optimization_flags = "-O2";
    std::string debug_optimization_flags = "-Og";
    std::string ld_flags = "-lstdc++";
    std::vector<std::string> imports;
    Target target = Target::Bin;
    bool verbose = false;
    bool debug = false;
    bool release = false;
};

} // namespace application

namespace sys {

std::string execute_command(const std::vector<std::string>& args) {
    std::string command;
    for (const auto& arg : args) {
        command += arg + " ";
    }

    std::string result;
    FILE* pipe = popen(command.c_str(), "r");
    if (pipe) {
        constexpr int buffer_size = 128;
        char buffer[buffer_size];

        while (!feof(pipe)) {
            if (fgets(buffer, buffer_size, pipe) != nullptr) {
                result += buffer;
            }
        }

        pclose(pipe);
    }

    return result;
}

} // namespace sys

namespace commands {

auto init(application::Instance& inst, const fs::path& path) -> void {
    fmt::println("{} {} Init {}", std::chrono::system_clock::now(), inst.name, path.string());

    if (!fs::exists(path)) {
        fs::create_directory(path);
    }

    // Create packages dependency file
    {
        std::string fullpath { path };
        fullpath += fs::path::preferred_separator;
        fullpath += "packages.conf";

        if (!fs::exists(fullpath)) {
            if (inst.verbose) {
                std::cout << "Create packages.conf: " << fullpath << std::endl;
            }

            std::ofstream o { fullpath };
        }
    }

    // Create manifest file
    {
        std::string fullpath { path };
        fullpath += fs::path::preferred_separator;
        fullpath += "manifest.conf";

        if (!fs::exists(fullpath)) {
            if (inst.verbose) {
                std::cout << "Create manifest.conf: " << fullpath << std::endl;
            }

            std::ofstream o { fullpath };
        }
    }

    if (inst.target == application::Target::Bin) {

        std::string fullpath { path };
        fullpath += fs::path::preferred_separator;
        fullpath += "main.cpp";

        if (!fs::exists(fullpath)) {
            if (inst.verbose) {
                std::cout << "Create file: " << fullpath << std::endl;
            }

            std::ofstream o { fullpath };
            o << DefaultMain;
        }
    }
}

bool is_cppm(const fs::directory_entry& entry) {
    return entry.path().extension() == ".cppm";
}

std::vector<std::string> extract_library_names_from_file(const std::string& filename) {
    std::vector<std::string> library_names;

    std::ifstream file(filename);
    if (!file) {
        std::cerr << "Failed to open file: " << filename << std::endl;
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

static auto process_imports([[maybe_unused]] application::Instance& inst, const std::vector<fs::directory_entry>& entries) -> void {
    std::vector<std::string> imports;

    for (auto& entry : entries) {
        auto library_names = extract_library_names_from_file(entry.path().string());
        if (!library_names.empty()) {
            imports.insert(std::end(imports), std::begin(library_names), std::end(library_names));
        }
    }

    std::sort(std::begin(imports), std::end(imports));
    imports.erase(std::unique(std::begin(imports), std::end(imports)), std::end(imports));

    if (inst.verbose) {
        for (const auto& i : imports) {
            std::cout << "Import: " << i << std::endl;
        }
    }

    inst.imports = imports;
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

    return "";
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
            auto it = std::find_if(module_names.begin(), module_names.end(), [&](const auto& pair) { return pair.second == dependency; });

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
        auto it = std::find_if(
            file_paths.begin(), file_paths.end(), [&](const std::string& file_path) { return get_file_name(file_path) == file_name; });

        if (it != file_paths.end()) {
            sorted_file_paths.push_back(*it);
        }
    }

    return sorted_file_paths;
}
auto build(application::Instance& inst, fs::path path) {
    fmt::println("{} {} Build {} workingDir '{}'", std::chrono::system_clock::now(), inst.name, path.string(), fs::current_path().string());

    // Set working directory
    fs::path previous_path = fs::current_path();
    fs::current_path(path);

    std::vector<fs::directory_entry> entries;

    auto search_path = previous_path / path;
    if (!fs::exists(search_path)) {
        fmt::println("{} {} ERROR: '{}' not exists!", std::chrono::system_clock::now(), inst.name, search_path.string());
        exit(0);
    }

    for (const auto& entry : fs::directory_iterator(search_path)) {
        if (entry.is_regular_file()) {
            std::string extension = entry.path().extension().string();
            if (extension == ".cpp" || extension == ".cppm") {
                if (inst.verbose) {
                    std::cout << "File: " << entry.path().filename().string() << std::endl;
                }

                entries.push_back(entry);
            }
        }
    }

    // Sort sources with .cppm first
    std::sort(std::begin(entries), std::end(entries),
        [](const fs::directory_entry& a, const fs::directory_entry& b) { return is_cppm(a) && !is_cppm(b); });

    process_imports(inst, entries);

    std::vector<std::string> file_entries;
    for (const auto& e : entries) {
        file_entries.push_back(e.path().string());
    }

    auto oredered_files = sort_files_by_dependency(file_entries);

    std::vector<std::string> cmds;
    cmds.push_back(inst.compiller);
    cmds.push_back(inst.cpp_standart);
    cmds.push_back(inst.cpp_flags);

    if (inst.debug) {
        cmds.push_back(inst.debug_flag);
    }

    if (inst.release) {
        cmds.push_back(inst.debug ? inst.debug_optimization_flags : inst.optimization_flags);
    }

    for (const auto& i : inst.imports) {
        cmds.push_back("-x c++-system-header");
        cmds.push_back(i);
    }

    bool added = false;
    for (const auto& f : oredered_files) {
        fs::path entry { f };
        if (((entry.extension().string() == ".cppm") || (entry.extension().string() == ".cppm")) && !added) {
            cmds.push_back("-x c++");
            added = true;
        }

        cmds.push_back(f);
    }

    if (auto it = std::find(std::begin(inst.imports), std::end(inst.imports), "cmath"); it != std::end(inst.imports)) {
        cmds.push_back("-lm");
    }

    cmds.push_back(inst.ld_flags);

    cmds.push_back("-o a.out");

    if (inst.verbose) {
        for (const auto& c : cmds) {
            std::cout << "Command: " << c << '\n';
        }
    }

    sys::execute_command(cmds);

    // Restore previous working directory
    fs::current_path(previous_path);
}

static bool is_file_executable(const std::string& filename) {
    std::filesystem::file_status status = std::filesystem::status(filename);
    return (status.permissions() & std::filesystem::perms::owner_exec) != std::filesystem::perms::none;
}

static auto find_app_file(application::Instance& inst, const fs::path& search_path) {
    std::string app_file;

    for (const auto& entry : fs::directory_iterator(search_path)) {
        if (entry.is_regular_file()) {
            if (is_file_executable(entry.path().string())) {
                if (inst.verbose) {
                    std::cout << "Exec: " << entry.path().filename().string() << std::endl;
                }

                app_file = entry.path().string();
                break;
            }
        }
    }

    return app_file;
}

auto run(application::Instance& inst, fs::path path) {
    fmt::println("{} {} Run {}", std::chrono::system_clock::now(), inst.name, path.string());

    // Set working directory
    fs::path previous_path = fs::current_path();
    fs::current_path(path);

    auto search_path = previous_path / path;
    if (!fs::exists(search_path)) {
        fmt::println("{} {} ERROR: '{}' not exists!", std::chrono::system_clock::now(), inst.name, search_path.string());
        exit(0);
    }

    auto app_file = find_app_file(inst, search_path);
    if (app_file.empty()) {
        fmt::println("{} {} ERROR: app file not found in '{}'", std::chrono::system_clock::now(), inst.name, search_path.string());
        return;
    }

    auto result = sys::execute_command({ app_file });
    if (!result.empty()) {
        std::cout << result;
    }

    // Restore previous working directory
    fs::current_path(previous_path);
}

auto clean(application::Instance& inst, fs::path path) {
    fmt::println("{} {} Clean {}", std::chrono::system_clock::now(), inst.name, path.string());

    // Remove packages dependency file
    {
        std::string fullpath { path };
        fullpath += fs::path::preferred_separator;
        fullpath += "packages.conf";

        if (fs::exists(fullpath)) {
            if (inst.verbose) {
                fmt::println("Remove {}", fullpath);
            }

            fs::remove(fullpath);
        }
    }

    // Remove manifest file
    {
        std::string fullpath { path };
        fullpath += fs::path::preferred_separator;
        fullpath += "manifest.conf";

        if (fs::exists(fullpath)) {
            if (inst.verbose) {
                fmt::println("Remove {}", fullpath);
            }

            fs::remove(fullpath);
        }
    }

    auto app_file = find_app_file(inst, path);
    if (!app_file.empty() && fs::exists(app_file)) {
        if (inst.verbose) {
            fmt::println("Remove {}", app_file);
        }

        fs::remove(app_file);
    }
}

} // namespace commands

int main(int argc, char* argv[]) {
    cxxopts::Options options("bspm", "C++ build system and package manager");

    options.add_options("General")("v,verbose", "Enable verbose output")("h,help", "Print help")(
        "path", "Directory path", cxxopts::value<std::string>())("command", "Command to execute", cxxopts::value<std::string>());

    options.add_options("init")("bin", "Create a package with a binary target")("lib", "Create a package with a library target")(
        "shared", "Create a package with a  shared library target");
    options.add_options("build")("d,debug", "Build with debug information")(
        "r,release", "Build optimized artifacts with the release profile");

    options.positional_help("<command> <path>");
    options.parse_positional({ "command", "path" });

    application::Instance inst;
    fs::path directory = fs::current_path().string();

    try {
        auto result = options.parse(argc, argv);

        if (result.count("help")) {
            std::cout << options.help() << std::endl;
            return 0;
        }

        if (result.count("verbose")) {
            inst.verbose = true;
        }

        if (inst.verbose) {
            std::cout << "Verbose mode: " << std::boolalpha << inst.verbose << std::endl;
        }

        if (result.count("path")) {
            directory = result["path"].as<std::string>();
        }

        if (inst.verbose) {
            std::cout << "Directory path: " << directory << std::endl;
        }

        if (result.count("command")) {
            if (result["command"].as<std::string>() == "init") {
                commands::init(inst, directory);
            } else if (result["command"].as<std::string>() == "build") {
                if (result.count("debug")) {
                    inst.debug = true;
                }

                if (result.count("release")) {
                    inst.release = true;
                    inst.debug = false;
                }

                if (result.count("debug")) {
                    inst.debug = true;
                }

                commands::build(inst, directory);
            } else if (result["command"].as<std::string>() == "run") {
                commands::run(inst, directory);
            } else if (result["command"].as<std::string>() == "clean") {
                commands::clean(inst, directory);
            } else {
                std::cout << "Unknown command." << std::endl;
            }
        } else {
            std::cout << "Invalid command." << std::endl;
        }
    } catch (const cxxopts::exceptions::exception& e) {
        std::cerr << "Error parsing command line options: " << e.what() << std::endl;
        return 1;
    }

    return 0;
}