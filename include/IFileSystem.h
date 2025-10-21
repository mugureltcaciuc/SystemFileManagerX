#pragma once
#include <filesystem>
#include <functional>
#include <cstdint>
#include <string>
#include <vector>

using FileOperationProgress =
    std::function<void(std::uintmax_t completed, std::uintmax_t total)>;

class IFileSystem
{
public:
    IFileSystem() = default;
    virtual ~IFileSystem() = default;
    IFileSystem(const IFileSystem &) = delete;
    IFileSystem &operator=(const IFileSystem &) = delete;
    IFileSystem(IFileSystem &&) = delete;
    IFileSystem &operator=(IFileSystem &&) = delete;

    virtual std::string &get_left_path() = 0;
    virtual std::string &get_right_path() = 0;
    virtual std::string go_back(const std::string &current_path) = 0;
    virtual bool open(const std::filesystem::path &selected_path) = 0;
    virtual std::vector<std::filesystem::directory_entry> list_files(const std::string &path) const = 0;

    virtual bool rename_file(const std::filesystem::path& original_path, const std::string& new_name) = 0;
    virtual bool view_file_content(const std::filesystem::path& file_path) = 0;
    virtual bool edit_file_content(const std::filesystem::path& file_path) = 0;
    virtual bool copy_file(const std::filesystem::path& from,
                           const std::filesystem::path& to,
                           bool overwrite = false,
                           const FileOperationProgress& progress = {}) = 0;
    virtual bool move_file(const std::filesystem::path& from,
                           const std::filesystem::path& to,
                           bool overwrite = false,
                           const FileOperationProgress& progress = {}) = 0;
    virtual bool delete_file(const std::filesystem::path& target) = 0;
    virtual bool create_folder(const std::filesystem::path& parent, const std::string& name) = 0;
};
