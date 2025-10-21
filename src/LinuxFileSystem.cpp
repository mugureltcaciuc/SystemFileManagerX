#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>

#include "Constants.h"
#include "FileOperations.h"
#include "LinuxFileSystem.h"
#include "LinuxFileSystemConstants.h"

using namespace std;

LinuxFileSystem::LinuxFileSystem(std::string& left_path, std::string& right_path)
    : _left_path(left_path), _right_path(right_path) {}

void LinuxFileSystem::initialize()
{
    if (!_initialized)
    {
        _right_files = list_files(_right_path);
        _left_files = list_files(_left_path);
        _initialized = true;
    }
}

std::string& LinuxFileSystem::get_left_path()
{
    return _left_path;
}

std::string& LinuxFileSystem::get_right_path()
{
    return _right_path;
}

bool LinuxFileSystem::open(const std::filesystem::path& selected_path)
{
    std::string command =
        std::string(linux_file_system_constants::open_command_prefix) +
        selected_path.string() +
        std::string(linux_file_system_constants::command_quote);
    return system(command.c_str()) == 0;
}

std::vector<std::filesystem::directory_entry> LinuxFileSystem::list_files(const std::string& path) const
{
    vector<std::filesystem::directory_entry> files;
    try
    {
        for (const auto& entry : std::filesystem::directory_iterator(path))
            files.emplace_back(entry);
    }
    catch (const std::exception& e)
    {
        cerr << app_constants::filesystem::read_directory_failed << e.what()
             << endl;
    }
    return files;
}

std::string LinuxFileSystem::go_back(const std::string& current_path)
{
    const auto absolute_path = std::filesystem::absolute(current_path);
    const auto parent_path = absolute_path.parent_path();
    return (parent_path.empty() ? absolute_path : parent_path).string();
}

bool LinuxFileSystem::rename_file(const std::filesystem::path& original_path, const std::string& new_name)
{
    // Basic check for illegal characters in Linux
    if (new_name.empty() ||
        new_name.find(linux_file_system_constants::path_separator) !=
            std::string::npos) {
        std::cerr << linux_file_system_constants::invalid_rename_name;
        return false;
    }

    try {
        std::filesystem::path new_path = original_path.parent_path() / new_name;
        std::filesystem::rename(original_path, new_path);
        std::cout << app_constants::filesystem::rename_success
                  << new_path.filename() << '\n';
        return true;
    } catch (const std::filesystem::filesystem_error& e) {
        std::cerr << "Rename failed: " << e.what() << "\n";
        return false;
    }
}

bool LinuxFileSystem::view_file_content(const std::filesystem::path& file_path)
{
    std::ifstream file(file_path);
    if (!file.is_open()) {
        std::cerr << app_constants::filesystem::view_file_failed << file_path
                  << '\n';
        return false;
    }

    std::cout << linux_file_system_constants::view_header_prefix
              << file_path.filename()
              << linux_file_system_constants::view_header_suffix;
    std::string line;
    while (std::getline(file, line)) {
        std::cout << line << '\n';
    }

    std::cout << linux_file_system_constants::view_footer
              << linux_file_system_constants::view_continue;
    std::cin.get();

    file.close();
    return true;
}

bool LinuxFileSystem::edit_file_content(const std::filesystem::path& file_path)
{
    if (!std::filesystem::exists(file_path)) {
        std::cerr << app_constants::filesystem::file_missing << file_path
                  << '\n';
        return false;
    }

    std::string command =
        std::string(linux_file_system_constants::editor_command_prefix) +
        file_path.string() +
        std::string(linux_file_system_constants::command_quote);
    int result = std::system(command.c_str());
    return result == 0;
}

bool LinuxFileSystem::copy_file(
    const std::filesystem::path& from, const std::filesystem::path& to,
    bool overwrite, const FileOperationProgress& progress) {
    try {
        return file_operations::copy_path(from, to, overwrite, progress);
    } catch (const std::filesystem::filesystem_error& e) {
        std::cerr << app_constants::filesystem::copy_failed << e.what()
                  << '\n';
        return false;
    }
}

bool LinuxFileSystem::move_file(
    const std::filesystem::path& from, const std::filesystem::path& to,
    bool overwrite, const FileOperationProgress& progress) {
    try {
        return file_operations::move_path(from, to, overwrite, progress);
    } catch (const std::filesystem::filesystem_error& e) {
        std::cerr << app_constants::filesystem::move_failed << e.what()
                  << '\n';
        return false;
    }
}

bool LinuxFileSystem::delete_file(const std::filesystem::path& target) {
    try {
        return std::filesystem::remove_all(target) != 0;
    } catch (const std::filesystem::filesystem_error& e) {
        std::cerr << app_constants::filesystem::delete_failed << e.what()
                  << '\n';
        return false;
    }
}

bool LinuxFileSystem::create_folder(const std::filesystem::path& parent, const std::string& name) {
    try {
        std::filesystem::path newFolder = parent / name;
        return std::filesystem::create_directory(newFolder);
    } catch (const std::filesystem::filesystem_error& e) {
        std::cerr << app_constants::filesystem::create_folder_failed
                  << e.what() << '\n';
        return false;
    }
}

