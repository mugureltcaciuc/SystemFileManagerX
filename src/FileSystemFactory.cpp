#include "FileSystemFactory.h"
#include "IFileSystem.h"
#include "Globals.h"

// Include platform-specific file system implementations
#ifdef _WIN32
#include "WindowsFileSystem.h"
#else
#include "LinuxFileSystem.h"
#endif

std::unique_ptr<IFileSystem> FileSystemFactory::create(std::string &left_path, std::string &right_path)
{
#ifdef _WIN32
    return std::make_unique<WindowsFileSystem>(left_path, right_path);
#else
    return std::make_unique<LinuxFileSystem>(left_path, right_path);
#endif
}
