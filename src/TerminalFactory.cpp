#include "TerminalFactory.h"

#include <memory>

#include "ITerminal.h"

#ifdef _WIN32
#include "WindowsTerminal.h"
#else
#include "LinuxTerminal.h"
#endif

ITerminal &terminal() {
  static std::unique_ptr<ITerminal> instance = [] {
#ifdef _WIN32
    return std::make_unique<WindowsTerminal>();
#else
    return std::make_unique<LinuxTerminal>();
#endif
  }();
  return *instance;
}
