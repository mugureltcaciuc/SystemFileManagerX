#include <algorithm>
#include <atomic>
#include <chrono>

#include <cstdlib>
#include <ctime>
#include <filesystem>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

#include "FileManager.h"
#include "Globals.h"
#include "OsUtils.h"

#ifdef _WIN32
#include "WindowsFileSystem.h"
#include <conio.h>
#include <windows.h>

#pragma comment(lib, "User32.lib")
#else
#include "LinuxFileSystem.h"
#include <fcntl.h>
#include <termios.h>
#include <unistd.h>
#endif

using namespace std;

const int panelWidth = 50;
const int separatorWidth = 2;
const std::string panelSeparator = " || ";

#ifdef __linux__
constexpr int KEY_UP = 'A';   // 65
constexpr int KEY_DOWN = 'B'; // 66;

// F2–F4 (ESC O X form)
constexpr int KEY_F2 = 'Q';
constexpr int KEY_F3 = 'R';
constexpr int KEY_F4 = 'S';

// F5–F8 use ESC [ number ~ form
constexpr int KEY_F5_SEQ1 = '1'; // 15~
constexpr int KEY_F5_SEQ2 = '5';

constexpr int KEY_F6_SEQ2 = '7'; // 17~
constexpr int KEY_F7_SEQ2 = '8'; // 18~
constexpr int KEY_F8_SEQ2 = '9'; // 19~

constexpr int KEY_ESC = 27;
constexpr int KEY_ENTER = 10;      // '\n'
constexpr int KEY_BACKSPACE = 127; // sometimes 8 depending on terminal
constexpr int KEY_TAB = 9;
#else // windows
constexpr int KEY_EXT_PREFIX1 = 0;
constexpr int KEY_EXT_PREFIX2 = 224;

constexpr int KEY_UP = 72;
constexpr int KEY_DOWN = 80;

constexpr int KEY_F2 = 60;
constexpr int KEY_F3 = 61;
constexpr int KEY_F4 = 62;
constexpr int KEY_F5 = 63;
constexpr int KEY_F6 = 64;
constexpr int KEY_F7 = 65;
constexpr int KEY_F8 = 66;

constexpr int KEY_ESC = 27;
constexpr int KEY_ENTER = 13; // '\r'
constexpr int KEY_BACKSPACE = 8;
constexpr int KEY_TAB = 9;
#endif

#ifdef __linux__
char _getch() {
  char c;
  if (read(STDIN_FILENO, &c, 1) == 1) {
    // std::cout << "_getch (int): " << static_cast<int>(c) << std::endl;
    return c;
  }
  return 0;
}

int _kbhit() {
  timeval tv{};
  tv.tv_sec = 0;
  tv.tv_usec = 0;

  fd_set fds;
  FD_ZERO(&fds);
  FD_SET(STDIN_FILENO, &fds);

  select(STDIN_FILENO + 1, &fds, nullptr, nullptr, &tv);

  return FD_ISSET(STDIN_FILENO, &fds);
}
#endif

FileManager::FileManager(std::unique_ptr<IFileSystem> fileSystem,
                         const std::string &path)
    : fileSystem(std::move(fileSystem)), left_path(path),
      right_path(std::filesystem::path(path).parent_path().string()) {
  refresh_files();
}
void FileManager::refresh_files() {
  left_files.clear();
  right_files.clear();

  left_files = fileSystem->list_files(left_path);
  right_files = fileSystem->list_files(right_path);
}

// Reads input immediately, returns on Enter or Escape
std::string FileManager::read_user_command() {
  std::string buffer;

  while (!esc_pressed.load()) {
    if (_kbhit()) {
      int ch = _getch();

#ifdef __linux__
      // Handle extended keys (e.g., function keys)
      if (ch == KEY_ESC) {
        if (!_kbhit())
          return "__ESC__";

        int second = _getch();

        // Arrow keys: ESC [ A/B
        if (second == '[') {
          int third = _getch();

          switch (third) {
          case KEY_UP:
            return "__UP__";
          case KEY_DOWN:
            return "__DOWN__";
          }

          // F5–F8: ESC [ 15~ etc.
          if (third == '1') {
            int fourth = _getch(); // 5,7,8,9
            int fifth = _getch();  // ~

            if (fifth == '~') {
              switch (fourth) {
              case KEY_F5_SEQ2:
                return "__F5__";
              case KEY_F6_SEQ2:
                return "__F6__";
              case KEY_F7_SEQ2:
                return "__F7__";
              case KEY_F8_SEQ2:
                return "__F8__";
              }
            }
          }
        }

        // F2–F4: ESC O Q/R/S
        else if (second == 'O') {
          int third = _getch();

          switch (third) {
          case KEY_F2:
            return "__F2__";
          case KEY_F3:
            return "__F3__";
          case KEY_F4:
            return "__F4__";
          }
        }
      }
#else
      if (ch == KEY_EXT_PREFIX1 || ch == KEY_EXT_PREFIX2) {
        int ext = _getch();

        switch (ext) {
        case KEY_F2:
          return "__F2__";
        case KEY_F3:
          return "__F3__";
        case KEY_F4:
          return "__F4__";
        case KEY_F5:
          return "__F5__";
        case KEY_F6:
          return "__F6__";
        case KEY_F7:
          return "__F7__";
        case KEY_F8:
          return "__F8__";
        case KEY_UP:
          return "__UP__";
        case KEY_DOWN:
          return "__DOWN__";
        default:
          break;
        }
      }
#endif
      std::cout << " FileManager::read_user_command() ch: " << ch << std::endl;
      switch (ch) {
      case KEY_ESC: // ESC
        std::cout << "\nEscape pressed. Exiting...\n";
        return "__ESC__";

      case KEY_ENTER: // Enter
        return "__ENTER__";

      case KEY_BACKSPACE: // Backspace
        return "__BACKSPACE__";
        // case KEY_ENTER: // Enter
        //   std::cout << "\n";
        //   return buffer;

        // case KEY_BACKSPACE: // Backspace
        //   if (!buffer.empty()) {
        //     buffer.pop_back();
        //     std::cout << "\b \b";
        //   }
        //   break;

      case KEY_TAB: // TAB
        tab_pressed.store(true);
        return "__TAB__";

      default:
        if (ch >= 32 && ch <= 126) // printable ASCII only
        {
          buffer.push_back(static_cast<char>(ch));
          std::cout << static_cast<char>(ch);
        }
        break;
      }
    }
  }

  return buffer;
}

void FileManager::run() {
  clear_screen();
  refresh_files();
  drawPanel(left_path, right_path);

  std::cout << "Read user command: " << std::endl;
  std::string input = read_user_command();
  std::cout << "User command: " << input << std::endl;

  if (process_navigation_input(input)) {
    return;
  }

  process_file_selection_input(input);
  refresh_files();
}

// minimal helper: truncate path to fit width
static std::string truncatePath(const std::string &path, size_t maxLen) {
  if (path.length() <= maxLen)
    return path;
  if (maxLen <= 3)
    return std::string(maxLen, '.');
  return "..." + path.substr(path.length() - (maxLen - 3));
}

void FileManager::drawMenuBar() {
  std::vector<std::string> menuItems = {
      "F2 Rename",   "F3 View",      "F4 Edit",   "F5 Copy",
      "F6 Move",     "F7 NewFolder", "F8 Delete", "TAB SwitchPanel",
      "BS FolderUp", "ESC Quit"};
  const int totalWidth = 144;
  const int innerWidth =
      totalWidth + 2; // matches the top/bottom border width you use
  const int colWidth =
      13; // you print "|  " + std::setw(8) -> 10 chars per item

#ifndef _WIN32
  // Linux ANSI: bright gray background
  std::cout << "\033[30;47m";
#endif

  // Bottom border before menu
  std::cout << std::endl
            << "+" << std::string(totalWidth + 2, '-') << "+" << std::endl;

  int printed = 0;
  for (const auto &item : menuItems) {
    std::cout << "|  " << std::setw(8) << std::left << item;
    printed += colWidth;
  }

  // pad remaining space (if any) and print final closing '|' and newline
  int pad = innerWidth - printed;
  if (pad < 0)
    pad = 0;
  std::cout << std::string(pad + 5, ' ') << "|\n";

  std::cout << "+" << std::string(totalWidth + 2, '=') << "+" << std::endl;
#ifndef _WIN32
  std::cout << "\033[0m"; // reset colors
#endif
}

std::string FileManager::formatEntry(size_t index, const std::string &name,
                                     const std::string &type,
                                     const std::string &sizeStr,
                                     const std::string &timeStr,
                                     bool isSelected) {
  std::ostringstream line;
  std::string indexStr = isSelected ? "[* " + std::to_string(index) + "]"
                                    : "[" + std::to_string(index) + "]";
  line << std::left << std::setw(6) << indexStr << std::setw(25) << name
       << std::setw(7) << type << std::setw(10) << sizeStr << std::setw(17)
       << timeStr;
  return line.str();
}

void FileManager::print_line_with_dual_selection(const std::string &line,
                                                 bool selected1, bool selected2,
                                                 size_t width) {
#ifdef _WIN32
  HANDLE h = GetStdHandle(STD_OUTPUT_HANDLE);
  CONSOLE_SCREEN_BUFFER_INFO csbi;
  WORD originalAttr = 7; // light gray on black default
  if (GetConsoleScreenBufferInfo(h, &csbi))
    originalAttr = csbi.wAttributes;

  size_t halfWidth = width / 2;
  std::string leftPart = line.substr(0, halfWidth);
  std::string rightPart = line.substr(halfWidth);

  if (selected1) {
    WORD leftAttr = BACKGROUND_BLUE | FOREGROUND_RED | FOREGROUND_GREEN |
                    FOREGROUND_BLUE | FOREGROUND_INTENSITY;
    SetConsoleTextAttribute(h, leftAttr);
    std::cout << leftPart;
    SetConsoleTextAttribute(h, originalAttr);
  } else
    std::cout << leftPart;

  if (selected2) {
    WORD rightAttr = BACKGROUND_GREEN | FOREGROUND_RED | FOREGROUND_GREEN |
                     FOREGROUND_BLUE | FOREGROUND_INTENSITY;
    SetConsoleTextAttribute(h, rightAttr);
    std::cout << rightPart;
    SetConsoleTextAttribute(h, originalAttr);
  } else
    std::cout << rightPart;

#else
  // Linux ANSI: bright gray background
  std::cout << "\033[30;47m";

  if (selected1 && selected2)
    std::cout << "\033[1;33m"; // yellow
  else if (selected1)
    std::cout << "\033[1;35m"; // green
  else if (selected2)
    std::cout << "\033[1;34m"; // blue

  std::cout << line;

  std::cout << "\033[0m"; // reset colors
#endif
}

void FileManager::drawLeftRightPanels(const std::filesystem::path &leftPath,
                                      const std::filesystem::path &rightPath,
                                      std::vector<std::string> &buffer) {
  // Constants for formatting
  constexpr int indexWidth = 6; // width for file index "[ 0]"
  constexpr int nameWidth = 30; // width for file/folder name
  constexpr int typeWidth = 6;  // width for type "[DIR]"
  constexpr int sizeWidth = 12; // width for size string "0.00 MB"
  constexpr int timeWidth =
      16; // width for last modified time "YYYY-MM-DD HH:MM"
  const std::string panelSeparator = " || ";
  const int panelWidth =
      indexWidth + nameWidth + typeWidth + sizeWidth + timeWidth;

  // Panel name row with >>> marker for active panel
  std::string leftPanelName =
      isLeftPanelActive ? ">>> LEFT PANEL <<<" : "Left Panel";
  std::string rightPanelName =
      !isLeftPanelActive ? ">>> RIGHT PANEL <<<" : "Right Panel";

  auto padToWidth = [panelWidth](const std::string &s) -> std::string {
    if ((int)s.size() < panelWidth)
      return s + std::string(panelWidth - s.size(), ' ');
    return s.substr(0, panelWidth);
  };

  // Push the panel name line to buffer
  buffer.push_back(padToWidth(leftPanelName) + panelSeparator +
                   padToWidth(rightPanelName));

  // Lambda to format each entry
  auto formatEntry = [&](const std::filesystem::directory_entry &entry,
                         size_t idx, bool panelActive,
                         size_t selectedIdx) -> std::string {
    std::ostringstream oss;

    // Index with selection marker
    std::string idxStr = (panelActive && idx == selectedIdx)
                             ? "[* " + std::to_string(idx) + "]"
                             : "[  " + std::to_string(idx) + "]";
    oss << std::left << std::setw(indexWidth) << idxStr;

    // Name, truncated if necessary
    std::string name = entry.path().filename().string();
    if ((int)name.size() > nameWidth - 1)
      name = name.substr(0, nameWidth - 2) + "~";
    oss << std::left << std::setw(nameWidth) << name;

    // Type
    std::string type = entry.is_directory() ? "[DIR]" : "";
    oss << std::left << std::setw(typeWidth) << type;

    // Size in MB with 3 decimals, "-" if not file
    std::string sizeStr = "-";
    if (entry.is_regular_file()) {
      double sz = static_cast<double>(entry.file_size()) / (1024.0 * 1024.0);
      std::ostringstream sizeOSS;
      sizeOSS.precision(3);
      sizeOSS << std::fixed << sz << " MB";
      sizeStr = sizeOSS.str();
    }
    oss << std::left << std::setw(sizeWidth) << sizeStr;

    // Last modified time
    std::string timeStr = "-";
    if (entry.is_regular_file()) {
      auto ftime = entry.last_write_time();
      auto sctp =
          std::chrono::time_point_cast<std::chrono::system_clock::duration>(
              ftime - decltype(ftime)::clock::now() +
              std::chrono::system_clock::now());
      std::time_t cftime = std::chrono::system_clock::to_time_t(sctp);
      std::tm tm{};
      localtime_r(&cftime, &tm);
      char buf[20];
      std::strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M", &tm);
      timeStr = buf;
    }
    oss << std::left << std::setw(timeWidth) << timeStr;

    return oss.str();
  };

  // Collect left and right entries
  std::vector<std::string> leftEntries, rightEntries;
  if (std::filesystem::exists(leftPath) &&
      std::filesystem::is_directory(leftPath)) {
    size_t idx = 0;
    for (const auto &entry : std::filesystem::directory_iterator(leftPath)) {
      leftEntries.push_back(
          formatEntry(entry, idx++, isLeftPanelActive, selectedLeftIndex));
      if (leftEntries.size() >= 20)
        break; // limit
    }
  } else {
    leftEntries.push_back(padToWidth("[Invalid path]"));
  }

  if (std::filesystem::exists(rightPath) &&
      std::filesystem::is_directory(rightPath)) {
    size_t idx = 0;
    for (const auto &entry : std::filesystem::directory_iterator(rightPath)) {
      rightEntries.push_back(
          formatEntry(entry, idx++, !isLeftPanelActive, selectedRightIndex));
      if (rightEntries.size() >= 20)
        break; // limit
    }
  } else {
    rightEntries.push_back(padToWidth("[Invalid path]"));
  }

  // Merge left/right entries line by line
  size_t maxLines = std::max(leftEntries.size(), rightEntries.size());
  for (size_t i = 0; i < maxLines; ++i) {
    std::string left =
        i < leftEntries.size() ? leftEntries[i] : std::string(panelWidth, ' ');
    std::string right = i < rightEntries.size() ? rightEntries[i]
                                                : std::string(panelWidth, ' ');
    buffer.push_back(left + panelSeparator + right);
  }
}

void FileManager::drawPanel(const std::filesystem::path &leftPath,
                            const std::filesystem::path &rightPath) {
  // Apply theme for entire panel: black text on light gray background
#ifdef _WIN32
  HANDLE hConsole = GetStdHandle(STD_OUTPUT_HANDLE);
  SetConsoleTextAttribute(hConsole, BACKGROUND_RED | BACKGROUND_GREEN |
                                        BACKGROUND_BLUE | BACKGROUND_INTENSITY);
#else
  // ANSI escape: black text (30) on bright gray background (47)
  std::cout << "\033[30;47m";
#endif

  // Constants for formatting
  const int totalWidth = 144;
  const int sepWidth = 4; // " || "
  const int halfWidth = (totalWidth - sepWidth) / 2;
  const std::string leftLabel = "LEFT: ";
  const std::string rightLabel = "RIGHT: ";

  // Prepare header strings, truncated if too long
  std::string leftStr =
      leftLabel + truncatePath(leftPath.string() + "\\*.*",
                               halfWidth - (int)leftLabel.size());
  std::string rightStr =
      rightLabel + truncatePath(rightPath.string() + "\\*.*",
                                halfWidth - (int)rightLabel.size());

  // Build header line
  std::ostringstream hdr;
  hdr << std::left << std::setw(halfWidth) << leftStr << panelSeparator
      << std::left << std::setw(halfWidth) << rightStr;
  std::string headerLine = hdr.str();

  // Draw top border and header
  std::cout << "+" << std::string(totalWidth + 2, '=') << "+" << std::endl;
  std::cout << "| " << headerLine << " |" << std::endl;
  std::cout << "+" << std::string(totalWidth + 2, '-') << "+";

  // Fix selected index if out of range
  if (isLeftPanelActive && selectedLeftIndex >= left_files.size())
    selectedLeftIndex = left_files.empty() ? 0 : left_files.size() - 1;
  if (!isLeftPanelActive && selectedRightIndex >= right_files.size())
    selectedRightIndex = right_files.empty() ? 0 : right_files.size() - 1;

  // Draw entries into buffer
  std::vector<std::string> buffer;
  drawLeftRightPanels(leftPath, rightPath, buffer);

  // Number of header lines (panel names)
  const size_t headerLines = 1;

  // Print each buffer line
  for (size_t i = 0; i < buffer.size(); ++i) {
    // Compute file index for selection highlighting
    size_t fileIndex = (i >= headerLines) ? i - headerLines : 0;

    // Determine which panel is selected
    bool isLeftSelected =
        isLeftPanelActive && i >= headerLines && fileIndex == selectedLeftIndex;
    bool isRightSelected = !isLeftPanelActive && i >= headerLines &&
                           fileIndex == selectedRightIndex;

    // Ensure the line fills the total width
    std::string line = buffer[i];
    if ((int)line.size() < totalWidth)
      line += std::string(totalWidth - line.size(), ' ');

#ifndef _WIN32
    // Linux ANSI: bright gray background
    std::cout << "\033[30;47m";
#endif

    // Print with | at start, use dual selection highlighting
    std::cout << std::endl << "| ";
    print_line_with_dual_selection(line, isLeftSelected, isRightSelected,
                                   totalWidth);

#ifndef _WIN32
    // Linux ANSI: bright gray background
    std::cout << "\033[30;47m";
#endif
    // Ensure | at end of line
    std::cout << " |";
  }

  // Draw menu bar
  drawMenuBar();

  // Keep console attributes (do not reset) as requested
}

void FileManager::clear_screen() {
#ifdef _WIN32
  system("cls");
#else
  system("clear");
#endif
}

bool FileManager::process_navigation_input(const std::string &input) {
  auto &activeFiles = isLeftPanelActive ? left_files : right_files;
  auto &activePath = isLeftPanelActive ? left_path : right_path;

  std::string cmd = input;

#ifndef _WIN32
  // On Linux, use atomic flags from main.cpp to emulate special keys
  if (up_pressed.load()) {
    cmd = "__UP__";
    up_pressed.store(false); // reset flag
  } else if (down_pressed.load()) {
    cmd = "__DOWN__";
    down_pressed.store(false); // reset flag
  } else if (tab_pressed.load()) {
    cmd = "__TAB__";
    tab_pressed.store(false); // reset flag
  } else if (esc_pressed.load()) {
    cmd = "__ESC__";
    esc_pressed.store(false); // reset flag
  }
#endif

  // -------------------------------
  // Handle commands
  // -------------------------------

if (cmd == "__BACKSPACE__") {
    if (!activeFiles.empty()) {
      activePath = fileSystem->go_back(activeFiles[0].path().string());
      refresh_files();
    }
    return true;
  }

  if (cmd == "__ESC__") {
    std::cout << "\nExiting program...\n";
#ifdef _WIN32
    std::exit(0);
#else
    // Restore terminal settings if necessary
    tcsetattr(STDIN_FILENO, TCSANOW, &g_orig_termios); // restore canonical mode
    std::cout << "\033[0m\033[?25h" << std::flush; // reset colors, show cursor
    std::_Exit(0); // immediately exit without flushing buffers
#endif
  }

  if (cmd == "__TAB__") {
    isLeftPanelActive = !isLeftPanelActive;
    return false;
  }

  if (cmd == "__UP__") {
    if (isLeftPanelActive) {
      if (selectedLeftIndex > 0)
        selectedLeftIndex--;
    } else {
      if (selectedRightIndex > 0)
        selectedRightIndex--;
    }
    return true;
  }

  if (cmd == "__DOWN__") {
    if (isLeftPanelActive) {
      if (selectedLeftIndex + 1 < activeFiles.size())
        selectedLeftIndex++;
    } else {
      if (selectedRightIndex + 1 < activeFiles.size())
        selectedRightIndex++;
    }
    return true;
  }

  if (cmd == "__ENTER__") { // Enter pressed
    if (!activeFiles.empty()) {
      auto &selectedFile = activeFiles[isLeftPanelActive ? selectedLeftIndex
                                                         : selectedRightIndex];
      std::cout << "Enter pressed" << std::endl;                                                         
      if (std::filesystem::is_directory(selectedFile.path())) {
        // Go into the directory
        activePath = selectedFile.path().string();
      } else {
        // It's a file: optionally do nothing or open
        // e.g., fileSystem->view_file_content(selectedFile.path());
      }
      refresh_files();
    }
    return true;
  }

  // Windows-specific keys or other input handling can remain unchanged
  if (cmd == "__F2__") // Rename
  {
    if (!activeFiles.empty()) {
      std::filesystem::path original = activeFiles[selectedIndex()].path();
      std::cout << "\nEnter new name for " << original.filename() << ": ";

      std::string new_name;
      std::getline(std::cin, new_name);

      if (!new_name.empty()) {
        fileSystem->rename_file(original, new_name);
        refresh_files();
      }
    }
    return true;
  }

  if (cmd == "__F3__") // View
  {
    if (!activeFiles.empty())
      fileSystem->view_file_content(activeFiles[selectedIndex()].path());
    return true;
  }

  if (cmd == "__F4__") // Edit
  {
    if (!activeFiles.empty())
      fileSystem->edit_file_content(activeFiles[selectedIndex()].path());
    return true;
  }

  if (cmd == "__F5__") // Copy
  {
    if (activeFiles.empty())
      return true;

    std::filesystem::path source = activeFiles[selectedIndex()].path();
    std::filesystem::path targetDir =
        isLeftPanelActive ? right_path : left_path;

    fileSystem->copy_file(source, targetDir / source.filename());
    refresh_files();
    return true;
  }

  if (cmd == "__F6__") // Move
  {
    if (activeFiles.empty())
      return true;

    std::filesystem::path source = activeFiles[selectedIndex()].path();
    std::filesystem::path targetDir =
        isLeftPanelActive ? right_path : left_path;

    fileSystem->move_file(source, targetDir / source.filename());
    refresh_files();
    return true;
  }

  if (cmd == "__F7__") // New Folder
  {
    std::cout << "\nEnter new folder name: ";
    std::string folderName;
    std::getline(std::cin, folderName);

    if (!folderName.empty())
      fileSystem->create_folder(activePath, folderName);

    refresh_files();
    return true;
  }

  if (cmd == "__F8__") // Delete
  {
    if (activeFiles.empty())
      return true;

    std::filesystem::path target = activeFiles[selectedIndex()].path();
    std::cout << "\nDelete " << target.filename() << "? [y/N]: ";

    char confirm;
    std::cin >> confirm;
    std::cin.ignore(std::numeric_limits<std::streamsize>::max(), '\n');

    if (confirm == 'y' || confirm == 'Y') {
      try {
        std::uintmax_t count = std::filesystem::remove_all(target);
        std::cout << "Deleted " << count << " item(s).\n";
      } catch (const std::filesystem::filesystem_error &e) {
        std::cerr << "Error deleting: " << e.what() << '\n';
      }
    }

    refresh_files();
    return true;
  }

  return false;
}

int FileManager::selectedIndex() const {
  return isLeftPanelActive ? selectedLeftIndex : selectedRightIndex;
}

int FileManager::process_file_selection_input(const std::string &input) {
  int index = 0;
  try {
    index = std::stoi(input);

    if (isLeftPanelActive) {
      if (index >= 0 && index < static_cast<int>(left_files.size())) {
        std::filesystem::path directoryPath =
            left_files[0].path().parent_path();
        left_path = fileSystem->open(directoryPath.string(), index);
      } else {
        // Do nothing;
      }
    } else {
      if (index >= 0 && index < static_cast<int>(right_files.size())) {
        std::filesystem::path directoryPath =
            right_files[0].path().parent_path();
        right_path = fileSystem->open(directoryPath.string(), index);
      } else {
        // Do nothing;
      }
    }
  } catch (...) {
    // done.store(true);
    // esc_pressed.store(true);
    // Do nothing;
  }
  return index;
}
