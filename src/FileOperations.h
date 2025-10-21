#pragma once

#include "IFileSystem.h"

#include <array>
#include <fstream>
#include <system_error>

namespace file_operations {
constexpr std::size_t copy_buffer_size = 1024 * 1024;

inline void copy_regular_file(const std::filesystem::path &from,
                              const std::filesystem::path &to,
                              std::uintmax_t &completed, std::uintmax_t total,
                              const FileOperationProgress &progress) {
  std::ifstream input(from, std::ios::binary);
  if (!input)
    throw std::filesystem::filesystem_error(
        "Failed to read source file", from,
        std::make_error_code(std::errc::io_error));

  std::ofstream output(to, std::ios::binary | std::ios::trunc);
  if (!output)
    throw std::filesystem::filesystem_error(
        "Failed to create destination file", to,
        std::make_error_code(std::errc::io_error));

  std::array<char, copy_buffer_size> buffer{};
  while (input) {
    input.read(buffer.data(), static_cast<std::streamsize>(buffer.size()));
    const auto count = input.gcount();
    if (count > 0) {
      output.write(buffer.data(), count);
      if (!output)
        throw std::filesystem::filesystem_error(
            "Failed to write destination file", to,
            std::make_error_code(std::errc::io_error));
      completed += static_cast<std::uintmax_t>(count);
      if (progress)
        progress(completed, total);
    }
  }
  if (!input.eof())
    throw std::filesystem::filesystem_error(
        "Failed while reading source file", from,
        std::make_error_code(std::errc::io_error));
}

inline bool copy_path(const std::filesystem::path &from,
                      const std::filesystem::path &to, bool overwrite,
                      const FileOperationProgress &progress) {
  namespace fs = std::filesystem;
  const fs::file_status sourceStatus = fs::symlink_status(from);
  if (!fs::exists(sourceStatus))
    throw fs::filesystem_error(
        "Source does not exist", from,
        std::make_error_code(std::errc::no_such_file_or_directory));
  if (fs::is_directory(sourceStatus)) {
    const fs::path sourceAbsolute = fs::weakly_canonical(from);
    const fs::path targetAbsolute = fs::weakly_canonical(to);
    auto sourcePart = sourceAbsolute.begin();
    auto targetPart = targetAbsolute.begin();
    while (sourcePart != sourceAbsolute.end() &&
           targetPart != targetAbsolute.end() && *sourcePart == *targetPart) {
      ++sourcePart;
      ++targetPart;
    }
    if (sourcePart == sourceAbsolute.end())
      throw fs::filesystem_error(
          "Cannot copy a directory into itself", from, to,
          std::make_error_code(std::errc::invalid_argument));
  } else if (fs::weakly_canonical(from) == fs::weakly_canonical(to)) {
    throw fs::filesystem_error(
        "Source and destination are the same file", from, to,
        std::make_error_code(std::errc::invalid_argument));
  }

  const fs::file_status targetStatus = fs::symlink_status(to);
  bool targetExists = fs::exists(targetStatus);
  if (targetExists && !overwrite)
    return false;
  if (targetExists && fs::is_symlink(targetStatus)) {
    fs::remove_all(to);
    targetExists = false;
  }

  if (fs::is_symlink(sourceStatus)) {
    if (targetExists)
      fs::remove_all(to);
    fs::copy_symlink(from, to);
    if (progress)
      progress(1, 1);
    return true;
  }

  const bool sourceIsDirectory = fs::is_directory(sourceStatus);
  if (targetExists && sourceIsDirectory != fs::is_directory(targetStatus))
    throw fs::filesystem_error("Source and destination types differ", from, to,
                               std::make_error_code(std::errc::file_exists));

  std::uintmax_t total = 0;
  if (sourceIsDirectory) {
    for (const auto &entry : fs::recursive_directory_iterator(from)) {
      std::error_code error;
      if (fs::is_regular_file(entry.symlink_status()))
        total += entry.file_size(error);
      if (error)
        throw fs::filesystem_error("Failed to inspect source file",
                                   entry.path(), error);
    }
  } else {
    total = fs::file_size(from);
  }

  std::uintmax_t completed = 0;
  if (sourceIsDirectory) {
    fs::create_directories(to);
    for (const auto &entry : fs::recursive_directory_iterator(from)) {
      const fs::path target = to / entry.path().lexically_relative(from);
      const fs::file_status status = entry.symlink_status();
      std::error_code targetError;
      const fs::file_status existingStatus =
          fs::symlink_status(target, targetError);
      const bool entryExists = !targetError && fs::exists(existingStatus);
      if (fs::is_symlink(status)) {
        if (entryExists)
          fs::remove_all(target);
        fs::copy_symlink(entry.path(), target);
      } else if (fs::is_directory(status)) {
        if (entryExists && !fs::is_directory(existingStatus))
          fs::remove_all(target);
        fs::create_directories(target);
      } else if (fs::is_regular_file(status)) {
        if (entryExists && (!fs::is_regular_file(existingStatus) ||
                            fs::is_symlink(existingStatus)))
          fs::remove_all(target);
        std::error_code error;
        fs::create_directories(target.parent_path(), error);
        if (error)
          throw fs::filesystem_error("Failed to create destination directory",
                                     target.parent_path(), error);
        copy_regular_file(entry.path(), target, completed, total, progress);
      }
    }
  } else {
    std::error_code error;
    fs::create_directories(to.parent_path(), error);
    if (error)
      throw fs::filesystem_error("Failed to create destination directory",
                                 to.parent_path(), error);
    copy_regular_file(from, to, completed, total, progress);
  }

  if (progress && total == 0)
    progress(0, 0);
  return true;
}

inline bool move_path(const std::filesystem::path &from,
                      const std::filesystem::path &to, bool overwrite,
                      const FileOperationProgress &progress) {
  namespace fs = std::filesystem;
  std::error_code error;
  const bool targetExists = fs::exists(fs::symlink_status(to, error));
  if (error == std::errc::no_such_file_or_directory)
    error.clear();
  else if (error)
    throw fs::filesystem_error("Failed to inspect destination", to, error);
  if (targetExists && !overwrite)
    return false;

  if (!targetExists) {
    fs::rename(from, to, error);
    if (!error)
      return true;
    if (error != std::errc::cross_device_link)
      throw fs::filesystem_error("Failed to move item", from, to, error);
  }

  if (!copy_path(from, to, overwrite, progress))
    return false;
  fs::remove_all(from);
  return true;
}
} // namespace file_operations
