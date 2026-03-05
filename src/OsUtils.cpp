
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
        case OS::Windows: return "Windows";
        case OS::Linux:   return "Linux";
        default:          return "Unknown";
    }
}
