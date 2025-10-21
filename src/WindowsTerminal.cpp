#include "WindowsTerminal.h"

#include "FileManagerConstants.h"

#ifdef _WIN32
#include <cstdlib>
#include <string>
#include <windows.h>

#include "Constants.h"
#endif

std::pair<int, int> WindowsTerminal::dimensions() const {
#ifdef _WIN32
  CONSOLE_SCREEN_BUFFER_INFO info{};
  if (GetConsoleScreenBufferInfo(GetStdHandle(STD_OUTPUT_HANDLE), &info))
    return {info.srWindow.Right - info.srWindow.Left + 1,
            info.srWindow.Bottom - info.srWindow.Top + 1};
#endif
  return {app_constants::ui::windows_content_width,
          app_constants::ui::windows_visible_file_rows +
              app_constants::terminal::screen_rows_reserved};
}

void WindowsTerminal::clear() const {
#ifdef _WIN32
  std::system(
      std::string(app_constants::filesystem::windows_clear_command).c_str());
#endif
}
