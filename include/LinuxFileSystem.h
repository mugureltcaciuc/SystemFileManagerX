#pragma once
#include "IFileSystem.h"

class LinuxFileSystem : public IFileSystem
{
public:
    LinuxFileSystem(std::string &left_path, std::string &right_path);
    void initialize();

    std::string &get_left_path() override;
    std::string &get_right_path() override;
    std::string go_back(const std::string &current_path) override;
    bool open(const std::filesystem::path& selected_path) override;
    std::vector<std::filesystem::directory_entry> list_files(const std::string &path) const override;

    bool rename_file(const std::filesystem::path& original_path, const std::string& new_name) override;
    bool view_file_content(const std::filesystem::path& file_path) override;
    bool edit_file_content(const std::filesystem::path& file_path) override;
    bool copy_file(const std::filesystem::path& from,
                   const std::filesystem::path& to, bool overwrite = false,
                   const FileOperationProgress& progress = {}) override;
    bool move_file(const std::filesystem::path& from,
                   const std::filesystem::path& to, bool overwrite = false,
                   const FileOperationProgress& progress = {}) override;
    bool delete_file(const std::filesystem::path& target) override;
    bool create_folder(const std::filesystem::path& parent, const std::string& name) override;

private:
    std::string _left_path;
    std::string _right_path;
    std::vector<std::filesystem::directory_entry> _left_files;
    std::vector<std::filesystem::directory_entry> _right_files;

    bool _initialized = false;
};
