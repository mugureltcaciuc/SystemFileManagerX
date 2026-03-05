#ifndef OSUTILS_H
#define OSUTILS_H

#include <string>

// Enum for supported operating systems
enum class OS {
    Windows,
    Linux,
    Unknown
};

// Returns true if the current OS is Windows
bool isWindowsOs();

// Returns the current OS type as an enum
OS getOsType();

// Returns the OS name as a string (optional helper)
std::string getOsName();

#endif // OSUTILS_H
