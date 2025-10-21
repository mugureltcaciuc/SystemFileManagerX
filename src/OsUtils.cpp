
#include "OsUtilsConstants.h"
#include "OsUtils.h"

bool isWindowsOs() {
#ifdef _WIN32
    return true;
#else
    return false;
#endif
}

OS getOsType() {
#ifdef _WIN32
    return OS::Windows;
#elif __linux__
    return OS::Linux;
#else
    return OS::Unknown;
#endif
}

std::string getOsName() {
    switch (getOsType()) {
        case OS::Windows:
            return std::string(os_utils_constants::windows_name);
        case OS::Linux:
            return std::string(os_utils_constants::linux_name);
        default:
            return std::string(os_utils_constants::unknown_name);
    }
}
