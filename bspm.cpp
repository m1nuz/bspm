
#include "cxxopts.hpp"
#include "json.hpp"

using json = nlohmann::json;

#include "Logger.hpp"

#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <regex>
#include <vector>

namespace fs = std::filesystem;

namespace application {

struct Instance {
    std::string compiller = "gcc";
    std::string cpp_standart = "-std=c++20";
    std::string cpp_flags = "-fmodules-ts";
    std::string ld_flags = "-lstdc++";
    std::vector<std::string> imports;
    bool verbose = false;
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

auto init([[maybe_unused]] application::Instance& inst, std::string_view path) -> void {
    Logger::message("bspm", "Init {}", path);

    if (!fs::exists(path)) {
        fs::create_directory(path);
    }

    std::string fullpath { path };
    fullpath += fs::path::preferred_separator;
    fullpath += "packages.conf";
    if (!fs::exists(fullpath)) {
        std::ofstream o { fullpath };
        // json j {};
        // o << std::setw(4) << j << std::endl;
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
            library_names.push_back(library_name);
        }
    }

    file.close();
    return library_names;
}

static auto process_imports([[maybe_unused]] application::Instance& inst, [[maybe_unused]] std::vector<fs::directory_entry> entries)
    -> void {
    std::vector<std::string> imports;

    for (auto& entry : entries) {
        auto library_names = extract_library_names_from_file(entry.path().string());
        if (!library_names.empty()) {

            imports.insert(std::end(imports), std::begin(library_names), std::end(library_names));
        }
    }

    if (inst.verbose) {
        for (const auto& i : imports) {
            std::cout << "Import: " << i << std::endl;
        }
    }

    inst.imports = imports;
}

auto build(application::Instance& inst, fs::path path) {
    Logger::message("bspm", "Build {} working_dir={}", path.string(), fs::current_path().string());

    // Set working directory
    fs::path previous_path = fs::current_path();
    fs::current_path(path);

    std::vector<fs::directory_entry> entries;

    auto search_path = previous_path / path;
    if (!fs::exists(search_path)) {
        Logger::critical("bspm", "{} not exists!", search_path.string());
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
    std::sort(entries.begin(), entries.end(),
        [](const fs::directory_entry& a, const fs::directory_entry& b) { return is_cppm(a) && !is_cppm(b); });

    process_imports(inst, entries);

    std::vector<std::string> cmds;
    cmds.push_back(inst.compiller);
    cmds.push_back(inst.cpp_standart);
    cmds.push_back(inst.cpp_flags);

    for (const auto& i : inst.imports) {
        cmds.push_back("-x c++-system-header");
        cmds.push_back(i);
    }

    bool added = false;
    for (const auto& entry : entries) {
        if (((entry.path().extension().string() == ".cppm") || (entry.path().extension().string() == ".cpp")) && !added) {
            cmds.push_back("-x c++");
            added = true;
        }
        cmds.push_back(entry.path().string());
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

auto run([[maybe_unused]] application::Instance& inst, [[maybe_unused]] fs::path path) {
    Logger::message("bspm", "Run {}", path.string());

    // Set working directory
    fs::path previous_path = fs::current_path();
    fs::current_path(path);

    auto search_path = previous_path / path;
    if (!fs::exists(search_path)) {
        Logger::critical("bspm", "{} not exists!", search_path.string());
        exit(0);
    }

    std::string exec_file;

    for (const auto& entry : fs::directory_iterator(search_path)) {
        if (entry.is_regular_file()) {
            if (is_file_executable(entry.path().string())) {
                if (inst.verbose) {
                    std::cout << "Exec: " << entry.path().filename().string() << std::endl;
                }

                exec_file = entry.path().string();
                break;
            }
        }
    }

    auto result = sys::execute_command({ exec_file });
    if (!result.empty()) {
        std::cout << result;
    }

    // Restore previous working directory
    fs::current_path(previous_path);
}

} // namespace commands

int main(int argc, char* argv[]) {
    cxxopts::Options options("bspm", "C++ build system and package manager");

    options.add_options()("v,verbose", "Enable verbose output")("h,help", "Print help")(
        "path", "Directory path", cxxopts::value<std::string>())("command", "Command to execute", cxxopts::value<std::string>());

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
                commands::init(inst, directory.string());
            } else if (result["command"].as<std::string>() == "build") {
                commands::build(inst, directory.string());
            } else if (result["command"].as<std::string>() == "run") {
                commands::run(inst, directory.string());
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