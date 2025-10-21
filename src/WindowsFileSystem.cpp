#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>

#include "Constants.h"

#ifdef _WIN32
#include <conio.h>

#include "FileOperations.h"
#include "WindowsFileSystem.h"

using namespace std;

namespace windows_file_system_constants {
constexpr std::string_view open_command_prefix = "\"";
constexpr std::string_view editor_command_prefix = "notepad \"";
constexpr std::string_view command_quote = "\"";
constexpr std::string_view clear_command = "cls";
constexpr std::string_view illegal_rename_characters = "<>:\"/\\|?*";
constexpr std::string_view rename_empty =
    "Rename failed: name cannot be empty.\n";
constexpr std::string_view rename_invalid =
    "Rename failed: name contains illegal characters.\n";
constexpr std::string_view rename_hint =
    "Avoid using: < > : \" / \\ | ? *\n";
constexpr std::string_view view_header_prefix = "\n--- Viewing: ";
constexpr std::string_view view_header_suffix = " ---\n";
constexpr std::string_view view_footer = "\n--- End of file ---\n";
constexpr std::string_view view_continue = "Press any key to go back...\n";
} // namespace windows_file_system_constants

WindowsFileSystem::WindowsFileSystem(std::string& left_path, std::string& right_path)
    : _left_path(left_path), _right_path(right_path) {}

void WindowsFileSystem::initialize()
{
    if (!_initialized)
    {
        _right_files = list_files(_right_path);
        _left_files = list_files(_left_path);
        _initialized = true;
    }
}

std::string &WindowsFileSystem::get_left_path()
{
    return _left_path;
}

std::string &WindowsFileSystem::get_right_path()
{
    return _right_path;
}

bool WindowsFileSystem::open(const std::filesystem::path &selected_path)
{
    std::string command =
        std::string(windows_file_system_constants::open_command_prefix) +
        selected_path.string() +
        std::string(windows_file_system_constants::command_quote);
    return system(command.c_str()) == 0;
}

std::vector<std::filesystem::directory_entry> WindowsFileSystem::list_files(const std::string &path) const
{
    vector<std::filesystem::directory_entry> files;
    try
    {
        for (const auto &entry : std::filesystem::directory_iterator(path))
            files.emplace_back(entry);
    }
    catch (const std::exception &e)
    {
        cerr << app_constants::filesystem::read_directory_failed << e.what()
             << endl;
    }
    return files;
}

std::string WindowsFileSystem::go_back(const std::string &current_path)
{
    const auto absolute_path = std::filesystem::absolute(current_path);
    const auto parent_path = absolute_path.parent_path();
    return (parent_path.empty() ? absolute_path : parent_path).string();
}

bool WindowsFileSystem::rename_file(const std::filesystem::path& original_path, const std::string& new_name)
{
    // Check for illegal characters
    if (new_name.empty()) {
        std::cerr << windows_file_system_constants::rename_empty;
        return false;
    }

    if (new_name.find_first_of(
            windows_file_system_constants::illegal_rename_characters) !=
        std::string::npos) {
        std::cerr << windows_file_system_constants::rename_invalid;
        std::cerr << windows_file_system_constants::rename_hint;
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

bool WindowsFileSystem::view_file_content(const std::filesystem::path& file_path)
{
    std::ifstream file(file_path);
    if (!file.is_open()) {
        std::cerr << app_constants::filesystem::view_file_failed << file_path
                  << '\n';
        return false;
    }

    std::cout << windows_file_system_constants::view_header_prefix
              << file_path.filename()
              << windows_file_system_constants::view_header_suffix;
    std::string line;
    while (std::getline(file, line)) {
        std::cout << line << '\n';
    }

    std::cout << windows_file_system_constants::view_footer
              << windows_file_system_constants::view_continue;
    _getch();  // Wait for user input

    file.close();
    return true;
}

bool WindowsFileSystem::edit_file_content(const std::filesystem::path& file_path)
{
    if (!std::filesystem::exists(file_path)) {
        std::cerr << app_constants::filesystem::file_missing << file_path
                  << '\n';
        return false;
    }

    std::string command =
        std::string(windows_file_system_constants::editor_command_prefix) +
        file_path.string() +
        std::string(windows_file_system_constants::command_quote);
    int result = std::system(command.c_str());
    return result == 0;
}


bool WindowsFileSystem::copy_file(
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

bool WindowsFileSystem::move_file(
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

bool WindowsFileSystem::delete_file(const std::filesystem::path& target) {
    try {
        return std::filesystem::remove_all(target) != 0;
    } catch (const std::filesystem::filesystem_error& e) {
        std::cerr << app_constants::filesystem::delete_failed << e.what()
                  << '\n';
        return false;
    }
}

bool WindowsFileSystem::create_folder(const std::filesystem::path& parent, const std::string& name) {
    try {
        std::filesystem::path newFolder = parent / name;
        return std::filesystem::create_directory(newFolder);
    } catch (const std::filesystem::filesystem_error& e) {
        std::cerr << app_constants::filesystem::create_folder_failed
                  << e.what() << '\n';
        return false;
    }
}
#endif
