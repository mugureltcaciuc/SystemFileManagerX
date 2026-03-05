#include <filesystem>
#include <iostream>
#include <fstream>
#include <cstdlib>

#include "LinuxFileSystem.h"

using namespace std;

LinuxFileSystem::LinuxFileSystem(std::string& left_path, std::string& right_path)
    : _left_path(left_path), _right_path(right_path), _initialized(false) {}

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

std::string LinuxFileSystem::open(const std::string& selected_path, int index)
{
    auto files = LinuxFileSystem::list_files(selected_path);
    const auto& selected = files[index];
    auto current_path = selected.path().string();

    if (!selected.is_directory())
    {
        std::string command = "xdg-open \"" + selected.path().string() + "\"";
        system(command.c_str());
        current_path = std::filesystem::path(selected).parent_path().parent_path().string();
    }
    else
    {
        system("clear");
        files.clear();

        for (const auto& entry : std::filesystem::directory_iterator(current_path,
                  std::filesystem::directory_options::skip_permission_denied))
        {
            files.emplace_back(entry);
        }
    }
    return current_path;
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
        cerr << "Error reading directory: " << e.what() << endl;
    }
    return files;
}

std::string LinuxFileSystem::go_back(const std::string& selected_path)
{
    auto absolute_path = std::filesystem::absolute(selected_path);
    auto current_absolute_path = std::filesystem::absolute(absolute_path.parent_path());
    auto parent_absolute_path = std::filesystem::absolute(current_absolute_path.parent_path());
    auto current_path = parent_absolute_path.string();
    return current_path;
}

bool LinuxFileSystem::rename_file(const std::filesystem::path& original_path, const std::string& new_name)
{
    // Basic check for illegal characters in Linux
    if (new_name.empty() || new_name.find('/') != std::string::npos) {
        std::cerr << "Rename failed: name cannot be empty or contain '/'.\n";
        return false;
    }

    try {
        std::filesystem::path new_path = original_path.parent_path() / new_name;
        std::filesystem::rename(original_path, new_path);
        std::cout << "Renamed to: " << new_path.filename() << "\n";
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
        std::cerr << "Failed to open file for viewing: " << file_path << "\n";
        return false;
    }

    std::cout << "\n--- Viewing: " << file_path.filename() << " ---\n";
    std::string line;
    while (std::getline(file, line)) {
        std::cout << line << '\n';
    }

    std::cout << "\n--- End of file ---\n";
    std::cout << "Press Enter to go back...\n";
    std::cin.get();

    file.close();
    return true;
}

bool LinuxFileSystem::edit_file_content(const std::filesystem::path& file_path)
{
    if (!std::filesystem::exists(file_path)) {
        std::cerr << "File does not exist: " << file_path << "\n";
        return false;
    }

    std::string command = "nano \"" + file_path.string() + "\"";
    int result = std::system(command.c_str());
    return result == 0;
}

bool LinuxFileSystem::copy_file(const std::filesystem::path& from, const std::filesystem::path& to) {
    try {
        std::filesystem::copy_file(from, to, std::filesystem::copy_options::overwrite_existing);
        return true;
    } catch (const std::filesystem::filesystem_error& e) {
        std::cerr << "Copy failed: " << e.what() << '\n';
        return false;
    }
}

bool LinuxFileSystem::move_file(const std::filesystem::path& from, const std::filesystem::path& to) {
    try {
        std::filesystem::rename(from, to);
        return true;
    } catch (const std::filesystem::filesystem_error& e) {
        std::cerr << "Move failed: " << e.what() << '\n';
        return false;
    }
}

bool LinuxFileSystem::delete_file(const std::filesystem::path& target) {
    try {
        return std::filesystem::remove(target);
    } catch (const std::filesystem::filesystem_error& e) {
        std::cerr << "Delete failed: " << e.what() << '\n';
        return false;
    }
}

bool LinuxFileSystem::create_folder(const std::filesystem::path& parent, const std::string& name) {
    try {
        std::filesystem::path newFolder = parent / name;
        return std::filesystem::create_directory(newFolder);
    } catch (const std::filesystem::filesystem_error& e) {
        std::cerr << "Create folder failed: " << e.what() << '\n';
        return false;
    }
}

