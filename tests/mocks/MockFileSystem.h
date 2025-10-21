#pragma once
#include "IFileSystem.h"
#include <gmock/gmock.h>

class MockFileSystem : public IFileSystem {
public:
    MOCK_METHOD(std::string &, get_left_path, (), (override));
    MOCK_METHOD(std::string &, get_right_path, (), (override));
    MOCK_METHOD(std::string, go_back, (const std::string &), (override));
    MOCK_METHOD(bool, open, (const std::filesystem::path &), (override));
    MOCK_METHOD(std::vector<std::filesystem::directory_entry>, list_files,
                (const std::string &), (const, override));
    MOCK_METHOD(bool, rename_file,
                (const std::filesystem::path &, const std::string &), (override));
    MOCK_METHOD(bool, view_file_content, (const std::filesystem::path &), (override));
    MOCK_METHOD(bool, edit_file_content, (const std::filesystem::path &), (override));
    MOCK_METHOD(bool, copy_file,
                (const std::filesystem::path &, const std::filesystem::path &,
                 bool, const FileOperationProgress &), (override));
    MOCK_METHOD(bool, move_file,
                (const std::filesystem::path &, const std::filesystem::path &,
                 bool, const FileOperationProgress &), (override));
    MOCK_METHOD(bool, delete_file, (const std::filesystem::path&), (override));
    MOCK_METHOD(bool, create_folder, (const std::filesystem::path&, const std::string&), (override));
};
