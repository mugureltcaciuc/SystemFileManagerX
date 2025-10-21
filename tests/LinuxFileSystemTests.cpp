#include <gtest/gtest.h>
#include "LinuxFileSystem.h"
#include <filesystem>
#include <fstream>
#include <memory>

class LinuxFileSystemTest : public ::testing::Test {
protected:
    std::unique_ptr<LinuxFileSystem> fs;

    std::filesystem::path test_dir = "test_dir";
    std::filesystem::path test_file = test_dir / "file.txt";
    std::filesystem::path copy_file = test_dir / "copy.txt";

    void SetUp() override {
        std::filesystem::create_directory(test_dir);
        std::ofstream(test_file) << "Hello, Linux!";
        std::string dummy_path = test_dir.string();
        fs = std::make_unique<LinuxFileSystem>(dummy_path, dummy_path);
    }

    void TearDown() override {
        std::filesystem::remove_all(test_dir);
        fs.reset();
    }
};

TEST_F(LinuxFileSystemTest, CopyFileSuccess) {
    EXPECT_TRUE(fs->copy_file(test_file, copy_file));
    EXPECT_TRUE(std::filesystem::exists(copy_file));
}

TEST_F(LinuxFileSystemTest, CopyDoesNotOverwriteWithoutApproval) {
    std::ofstream(copy_file) << "existing";
    EXPECT_FALSE(fs->copy_file(test_file, copy_file));
    std::ifstream input(copy_file);
    std::string contents;
    input >> contents;
    EXPECT_EQ(contents, "existing");
}

TEST_F(LinuxFileSystemTest, CopyOverwriteReportsProgress) {
    std::uintmax_t completed = 0;
    std::uintmax_t total = 0;
    std::ofstream(test_file, std::ios::trunc) << "replacement content";
    std::ofstream(copy_file) << "existing";

    EXPECT_TRUE(fs->copy_file(
        test_file, copy_file, true,
        [&](std::uintmax_t copied, std::uintmax_t bytes) {
            completed = copied;
            total = bytes;
        }));
    EXPECT_EQ(completed, total);
    EXPECT_EQ(total, std::filesystem::file_size(test_file));
}

TEST_F(LinuxFileSystemTest, CopyDirectoryRecursively) {
    const auto source = test_dir / "source";
    std::filesystem::create_directories(source / "nested");
    std::ofstream(source / "nested" / "file.txt") << "nested";
    const auto destination = test_dir / "destination";

    EXPECT_TRUE(fs->copy_file(source, destination));
    EXPECT_TRUE(std::filesystem::exists(destination / "nested" / "file.txt"));
}

TEST_F(LinuxFileSystemTest, CopyDirectoryMergesAfterOverwriteApproval) {
    const auto source = test_dir / "source";
    const auto destination = test_dir / "destination";
    std::filesystem::create_directories(source / "nested");
    std::filesystem::create_directories(destination / "nested");
    std::ofstream(source / "nested" / "replace.txt") << "new";
    std::ofstream(destination / "nested" / "replace.txt") << "old";
    std::ofstream(destination / "keep.txt") << "keep";

    EXPECT_TRUE(fs->copy_file(source, destination, true));
    std::ifstream replaced(destination / "nested" / "replace.txt");
    std::string contents;
    replaced >> contents;
    EXPECT_EQ(contents, "new");
    EXPECT_TRUE(std::filesystem::exists(destination / "keep.txt"));
}

TEST_F(LinuxFileSystemTest, MoveFileSuccess) {
    EXPECT_TRUE(fs->move_file(test_file, copy_file));
    EXPECT_TRUE(std::filesystem::exists(copy_file));
    EXPECT_FALSE(std::filesystem::exists(test_file));
}

TEST_F(LinuxFileSystemTest, DeleteFileSuccess) {
    EXPECT_TRUE(fs->delete_file(test_file));
    EXPECT_FALSE(std::filesystem::exists(test_file));
}

TEST_F(LinuxFileSystemTest, DeleteDirectoryRecursively) {
    const auto directory = test_dir / "directory";
    std::filesystem::create_directories(directory / "nested");
    std::ofstream(directory / "nested" / "file.txt") << "nested";

    EXPECT_TRUE(fs->delete_file(directory));
    EXPECT_FALSE(std::filesystem::exists(directory));
}

TEST_F(LinuxFileSystemTest, CreateFolderSuccess) {
    std::string folder_name = "new_folder";
    EXPECT_TRUE(fs->create_folder(test_dir, folder_name));
    EXPECT_TRUE(std::filesystem::exists(test_dir / folder_name));
}
