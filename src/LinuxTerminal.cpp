#include "LinuxTerminal.h"

#include "FileManagerConstants.h"

#ifndef _WIN32
#include <iostream>
#include <sys/ioctl.h>
#include <unistd.h>

#include "Constants.h"
#endif

std::pair<int, int> LinuxTerminal::dimensions() const {
#ifndef _WIN32
  struct winsize size {};
  if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &size) == 0 &&
      size.ws_col > app_constants::terminal::no_dimensions &&
      size.ws_row > app_constants::terminal::no_dimensions)
    return {size.ws_col, size.ws_row};
#endif
  return {app_constants::terminal::fallback_columns,
          app_constants::terminal::fallback_rows};
}

void LinuxTerminal::clear() const {
#ifndef _WIN32
  std::cout << app_constants::terminal::ansi_reset
            << app_constants::terminal::ansi_clear_and_home << std::flush;
#endif
}
