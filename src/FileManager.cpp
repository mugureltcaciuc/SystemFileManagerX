#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cctype>
#include <csignal>
#include <condition_variable>
#include <cstdio>
#include <cstdlib>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <iterator>
#include <limits>
#include <mutex>
#include <optional>
#include <sstream>
#include <string>
#include <thread>
#include <utility>
#include <vector>

#include "Constants.h"
#include "FileManager.h"
#include "FileManagerConstants.h"
#include "Globals.h"
#include "ITerminal.h"
#include "OsUtils.h"
#include "TerminalFactory.h"

#ifdef _WIN32
#include "WindowsFileSystem.h"

#include <conio.h>
#include <windows.h>

#pragma comment(lib, "User32.lib")

namespace {
class WindowsWorkModeKeepAlive {
public:
  WindowsWorkModeKeepAlive() {
    SetThreadExecutionState(ES_CONTINUOUS | ES_SYSTEM_REQUIRED);
    worker_ = std::thread([this] { send_periodic_input(); });
  }

  ~WindowsWorkModeKeepAlive() {
    {
      std::lock_guard<std::mutex> lock(mutex_);
      stopped_ = true;
    }
    condition_.notify_one();
    if (worker_.joinable())
      worker_.join();
    SetThreadExecutionState(ES_CONTINUOUS);
  }

  WindowsWorkModeKeepAlive(const WindowsWorkModeKeepAlive &) = delete;
  WindowsWorkModeKeepAlive &
  operator=(const WindowsWorkModeKeepAlive &) = delete;

private:
  void send_periodic_input() {
    std::unique_lock<std::mutex> lock(mutex_);
    while (!condition_.wait_for(lock, std::chrono::seconds(60),
                                [this] { return stopped_; })) {
      lock.unlock();
      INPUT movement[2]{};
      movement[0].type = INPUT_MOUSE;
      movement[0].mi.dx = 1;
      movement[0].mi.dwFlags = MOUSEEVENTF_MOVE;
      movement[1] = movement[0];
      movement[1].mi.dx = -1;
      SendInput(2, movement, sizeof(INPUT));
      lock.lock();
    }
  }

  std::condition_variable condition_;
  std::mutex mutex_;
  bool stopped_ = false;
  std::thread worker_;
};
} // namespace
#else
#include "LinuxFileSystem.h"

#include <fcntl.h>
#include <sys/select.h>
#include <sys/wait.h>
#include <termios.h>
#include <unistd.h>
#endif

using namespace std;

const std::string panelSeparator(app_constants::terminal::panel_separator);

constexpr int closeButtonWidth = 3;

static bool is_close_button_column(int column, int rightColumn) {
  return column >= rightColumn - closeButtonWidth + 1 &&
         column <= rightColumn;
}

static void print_close_button() {
#ifdef _WIN32
  std::cout.flush();
  HANDLE console = GetStdHandle(STD_OUTPUT_HANDLE);
  DWORD mode = 0;
  if (GetConsoleMode(console, &mode)) {
    constexpr wchar_t symbol[] = L"[\u2716]";
    DWORD written = 0;
    if (!WriteConsoleW(console, symbol, closeButtonWidth, &written, nullptr) ||
        written != static_cast<DWORD>(closeButtonWidth))
      std::cerr << "Unable to draw the close button.\n";
    return;
  }
#endif
  std::cout << "[\xe2\x9c\x96]";
}

#ifndef _WIN32
namespace {
volatile std::sig_atomic_t resize_requested = 0;

void handle_terminal_resize(int) { resize_requested = 1; }
} // namespace
#endif

namespace {
struct GitRepositoryLookup {
  std::optional<std::filesystem::path> root;
  std::string error;
};

GitRepositoryLookup find_git_root(const std::filesystem::path &directory) {
  std::error_code error;
  const auto original_directory = std::filesystem::current_path(error);
  if (error)
    return {{}, error.message()};

  std::filesystem::current_path(directory, error);
  if (error)
    return {{}, error.message()};

#ifdef _WIN32
  constexpr auto command = app_constants::ui::git_rev_parse_command_windows;
  FILE *pipe = _popen(std::string(command).c_str(), "r");
#else
  constexpr auto command = app_constants::ui::git_rev_parse_command;
  FILE *pipe = popen(std::string(command).c_str(), "r");
#endif
  if (pipe == nullptr) {
    std::filesystem::current_path(original_directory, error);
    return {{}, "Unable to start Git."};
  }

  std::string output;
  std::array<char, app_constants::ui::git_output_buffer_size> buffer{};
  while (std::fgets(buffer.data(), static_cast<int>(buffer.size()), pipe) !=
         nullptr)
    output += buffer.data();

#ifdef _WIN32
  const int status = _pclose(pipe);
#else
  const int status = pclose(pipe);
#endif
  std::error_code restore_error;
  std::filesystem::current_path(original_directory, restore_error);
  if (restore_error)
    return {{}, restore_error.message()};

#ifdef _WIN32
  if (status != 0)
#else
  if (status == -1 || !WIFEXITED(status) || WEXITSTATUS(status) != 0)
#endif
    return {};

  while (!output.empty() &&
         (output.back() == '\n' || output.back() == '\r'))
    output.pop_back();
  if (output.empty())
    return {};
  return {std::filesystem::path(output), {}};
}

void suspend_terminal_for_git() {
#ifndef _WIN32
  tcsetattr(STDIN_FILENO, TCSANOW, &g_orig_termios);
  std::cout << app_constants::terminal::ansi_reset
            << app_constants::terminal::ansi_show_cursor
            << app_constants::terminal::ansi_leave_fullscreen << std::flush;
#endif
}

void resume_terminal_after_git() {
#ifndef _WIN32
  struct termios raw_termios = g_orig_termios;
  raw_termios.c_lflag &= ~(ICANON | ECHO);
  tcsetattr(STDIN_FILENO, TCSANOW, &raw_termios);
  std::cout << app_constants::terminal::ansi_enter_fullscreen
            << app_constants::terminal::ansi_hide_cursor
            << app_constants::terminal::ansi_clear_and_home << std::flush;
#endif
}

void run_git_command(const std::filesystem::path &repository_root,
                     std::string_view command,
                     const std::string *standard_input = nullptr) {
  std::error_code error;
  const auto original_directory = std::filesystem::current_path(error);
  if (error) {
    std::cout << error.message() << '\n'
              << app_constants::ui::git_press_enter << std::flush;
    std::cin.get();
    return;
  }

  std::filesystem::current_path(repository_root, error);
  if (error) {
    std::cout << error.message() << '\n'
              << app_constants::ui::git_press_enter << std::flush;
    std::cin.get();
    return;
  }

  suspend_terminal_for_git();
  int status = -1;
  if (standard_input == nullptr) {
    status = std::system(std::string(command).c_str());
  } else {
#ifdef _WIN32
    FILE *pipe = _popen(std::string(command).c_str(), "w");
#else
    FILE *pipe = popen(std::string(command).c_str(), "w");
#endif
    if (pipe != nullptr) {
      const size_t bytes_written =
          std::fwrite(standard_input->data(), sizeof(char),
                      standard_input->size(), pipe);
#ifdef _WIN32
      const int process_status = _pclose(pipe);
#else
      const int process_status = pclose(pipe);
#endif
      status = bytes_written == standard_input->size() ? process_status : -1;
    }
  }
  std::error_code restore_error;
  std::filesystem::current_path(original_directory, restore_error);

  if (status == -1) {
    std::cout << app_constants::ui::git_command_failed << status << ").\n";
  } else {
#ifdef _WIN32
    const int exit_code = status;
#else
    const int exit_code =
        WIFEXITED(status) ? WEXITSTATUS(status) : status;
#endif
    if (exit_code == 0)
      std::cout << app_constants::ui::git_command_succeeded << '\n';
    else
      std::cout << app_constants::ui::git_command_failed << exit_code
                << ").\n";
  }
  if (restore_error)
    std::cout << restore_error.message() << '\n';
  std::cout << app_constants::ui::git_press_enter << std::flush;
  std::string ignored;
  std::getline(std::cin, ignored);
  resume_terminal_after_git();
}
} // namespace

using app_constants::input::KEY_BACKSPACE;
using app_constants::input::KEY_DOWN;
using app_constants::input::KEY_ENTER;
using app_constants::input::KEY_ESC;
using app_constants::input::KEY_F2;
using app_constants::input::KEY_F3;
using app_constants::input::KEY_F4;
using app_constants::input::KEY_TAB;
using app_constants::input::KEY_UP;
#ifndef __linux__
using app_constants::input::KEY_EXT_PREFIX1;
using app_constants::input::KEY_EXT_PREFIX2;
using app_constants::input::KEY_F5;
using app_constants::input::KEY_F6;
using app_constants::input::KEY_F7;
using app_constants::input::KEY_F8;
using app_constants::input::KEY_F9;
using app_constants::input::windows_alt_d;
using app_constants::input::windows_alt_f;
using app_constants::input::windows_alt_left;
using app_constants::input::windows_alt_right;
using app_constants::input::windows_alt_x;
using app_constants::input::windows_end;
using app_constants::input::windows_f1;
using app_constants::input::windows_home;
using app_constants::input::windows_page_down;
using app_constants::input::windows_page_up;
#endif

#ifdef __linux__
char _getch() {
  char c{};
  if (read(STDIN_FILENO, &c,
           app_constants::terminal::bytes_per_character) ==
      app_constants::terminal::bytes_per_character) {
    return c;
  }
  return app_constants::input::no_data;
}

int _kbhit() {
  timeval tv{};
  tv.tv_sec = app_constants::terminal::poll_timeout_ms;
  tv.tv_usec = app_constants::terminal::poll_timeout_ms;

  fd_set fds;
  FD_ZERO(&fds);
  FD_SET(STDIN_FILENO, &fds);

  const int ready = select(STDIN_FILENO + 1, &fds, nullptr, nullptr, &tv);
  return ready > 0 && FD_ISSET(STDIN_FILENO, &fds);
}

int _getch_timeout(int timeout_ms) {
  timeval tv{};
  tv.tv_usec = timeout_ms * app_constants::terminal::microseconds_per_millisecond;

  fd_set fds;
  FD_ZERO(&fds);
  FD_SET(STDIN_FILENO, &fds);

  const int ready = select(STDIN_FILENO + 1, &fds, nullptr, nullptr, &tv);
  if (ready <= app_constants::terminal::select_no_ready ||
      !FD_ISSET(STDIN_FILENO, &fds))
    return app_constants::input::invalid_input;

  char c{};
  return read(STDIN_FILENO, &c,
              app_constants::terminal::bytes_per_character) ==
                 app_constants::terminal::bytes_per_character
             ? static_cast<unsigned char>(c)
             : app_constants::input::invalid_input;
}

void discard_pending_input() {
  while (_getch_timeout(app_constants::terminal::pending_input_timeout_ms) >
         app_constants::input::invalid_input) {
  }
}
#else
void discard_pending_input() {}
#endif

#ifdef __linux__
struct MouseReportingGuard {
  MouseReportingGuard() {
    std::cout << app_constants::terminal::ansi_enable_mouse_reporting
              << std::flush;
  }
  ~MouseReportingGuard() {
    std::cout << app_constants::terminal::ansi_disable_mouse_reporting
              << std::flush;
  }
};

struct SgrMouseEvent {
  int button;
  int column;
  int row;
  int terminator;
};

static std::optional<SgrMouseEvent> read_sgr_mouse_event() {
  std::array<int, 3> values{};
  size_t field = 0;
  int terminator = app_constants::input::invalid_input;
  while (field < values.size()) {
    const int value =
        _getch_timeout(app_constants::terminal::resize_input_timeout_ms);
    if (value < 0)
      return {};
    if (value == 'M' || value == 'm') {
      terminator = value;
      break;
    }
    if (value == ';') {
      if (field + 1 >= values.size())
        return {};
      ++field;
    } else if (value >= '0' && value <= '9') {
      const int digit = value - '0';
      if (values[field] >
          (std::numeric_limits<int>::max() - digit) / 10)
        return {};
      values[field] = values[field] * 10 + digit;
    } else {
      return {};
    }
  }
  if (terminator == app_constants::input::invalid_input || field != 2)
    return {};
  return SgrMouseEvent{values[0], values[1], values[2], terminator};
}

static void wait_for_window_close(int closeColumn, int closeRow) {
  MouseReportingGuard mouseReportingGuard;
  while (true) {
    const int key = _getch();
    if (key != KEY_ESC)
      return;
    const int second =
        _getch_timeout(app_constants::terminal::resize_input_timeout_ms);
    if (second == app_constants::input::linux_sequence_csi) {
      const int third =
          _getch_timeout(app_constants::terminal::resize_input_timeout_ms);
      if (third == '<') {
        const auto event = read_sgr_mouse_event();
        if (event && event->terminator == 'M' &&
            (event->button & 3) == 0 && (event->button & 32) == 0 &&
            is_close_button_column(event->column, closeColumn) &&
            event->row == closeRow)
          return;
        continue;
      }
    }
    return;
  }
}
#endif

#ifdef _WIN32
static void wait_for_window_close(int closeColumn, int closeRow) {
  const HANDLE input = GetStdHandle(STD_INPUT_HANDLE);
  DWORD originalMode{};
  const bool hasOriginalMode = GetConsoleMode(input, &originalMode);
  struct ConsoleModeRestorer {
    HANDLE handle;
    DWORD mode;
    bool restore;
    ~ConsoleModeRestorer() {
      if (restore)
        SetConsoleMode(handle, mode);
    }
  } modeRestorer{input, originalMode, hasOriginalMode};
  if (hasOriginalMode) {
    const DWORD inputMode =
        (originalMode | ENABLE_EXTENDED_FLAGS | ENABLE_MOUSE_INPUT) &
        ~ENABLE_QUICK_EDIT_MODE;
    SetConsoleMode(input, inputMode);
  }

  while (true) {
    INPUT_RECORD record{};
    DWORD recordsRead = 0;
    if (!ReadConsoleInputW(input, &record, 1, &recordsRead) ||
        recordsRead == 0)
      return;
    if (record.EventType == KEY_EVENT && record.Event.KeyEvent.bKeyDown)
      return;
    if (record.EventType != MOUSE_EVENT)
      continue;

    const MOUSE_EVENT_RECORD &mouse = record.Event.MouseEvent;
    if ((mouse.dwEventFlags != 0 && mouse.dwEventFlags != DOUBLE_CLICK) ||
        (mouse.dwButtonState & FROM_LEFT_1ST_BUTTON_PRESSED) == 0)
      continue;
    int column = mouse.dwMousePosition.X + 1;
    int row = mouse.dwMousePosition.Y + 1;
    CONSOLE_SCREEN_BUFFER_INFO info{};
    if (GetConsoleScreenBufferInfo(GetStdHandle(STD_OUTPUT_HANDLE), &info)) {
      column -= info.srWindow.Left;
      row -= info.srWindow.Top;
    }
    if (is_close_button_column(column, closeColumn) && row == closeRow)
      return;
  }
}
#endif

static std::string read_line_input() {
  std::string input;
  while (true) {
    const int key = _getch();
    if (key == KEY_ENTER ||
        key == app_constants::input::carriage_return)
      break;
    if ((key == KEY_BACKSPACE ||
         key == app_constants::input::delete_backspace) &&
        !input.empty()) {
      input.pop_back();
      std::cout << app_constants::terminal::terminal_erase_character
                << std::flush;
    } else if (key >= app_constants::input::printable_first &&
               key <= app_constants::input::printable_last) {
      input.push_back(static_cast<char>(key));
      std::cout << static_cast<char>(key) << std::flush;
    }
  }
  std::cout << '\n';
  return input;
}

namespace {
std::string ascii_lowercase(std::string value) {
  for (char &character : value) {
    if (character >= app_constants::ui::ascii_uppercase_first &&
        character <= app_constants::ui::ascii_uppercase_last)
      character += app_constants::ui::ascii_case_offset;
  }
  return value;
}
} // namespace

FileManager::FileManager(std::unique_ptr<IFileSystem> fileSystem,
                         const std::string &path)
    : left_path(path),
      right_path(std::filesystem::path(path).parent_path().string()),
      fileSystem(std::move(fileSystem)) {
  left_directory_history.push_back(left_path);
  right_directory_history.push_back(right_path);
#ifndef _WIN32
  std::signal(SIGWINCH, handle_terminal_resize);
#endif
  refresh_files();
}

void FileManager::setActivePanelLeft(bool isLeft) {
  isLeftPanelActive = isLeft;
}

void FileManager::moveActiveSelection(int offset) {
  auto &files = isLeftPanelActive ? left_files : right_files;
  auto &selectedIndex =
      isLeftPanelActive ? selectedLeftIndex : selectedRightIndex;
  if (files.empty()) {
    selectedIndex = app_constants::ui::first_selected_index;
    return;
  }

  const auto maxIndex = static_cast<int>(std::min(
      files.size() - 1,
      static_cast<size_t>(std::numeric_limits<int>::max())));
  const auto nextIndex = std::clamp(
      static_cast<long long>(selectedIndex) + offset, 0LL,
      static_cast<long long>(maxIndex));
  selectedIndex = static_cast<int>(nextIndex);
}

static bool is_docker_cache_path(const std::filesystem::path &cache_root,
                                const std::filesystem::path &path);
static std::string container_path_for(
    const std::filesystem::path &cache_root,
    const std::filesystem::path &local_path);
static std::vector<std::filesystem::directory_entry>
list_docker_directory(const std::string &container_id,
                      const std::filesystem::path &cache_root,
                      const std::filesystem::path &local_directory);

void FileManager::refresh_files() {
  refresh_panel(true);
  refresh_panel(false);
}

void FileManager::refresh_panel(bool is_left_panel) {
  const auto loadVisibleFiles = [this, is_left_panel](
                                    const std::string &path,
                                    const std::string &filter) {
    const bool search_active =
        is_left_panel ? left_search_active : right_search_active;
    auto files = search_active
                     ? (is_left_panel ? left_search_results
                                      : right_search_results)
                     : !docker_cache_root.empty() &&
                         is_docker_cache_path(docker_cache_root, path)
                     ? list_docker_directory(docker_container_id,
                                             docker_cache_root, path)
                     : fileSystem->list_files(path);
    std::sort(files.begin(), files.end(),
              [](const auto &left, const auto &right) {
                std::error_code left_error;
                std::error_code right_error;
                const bool left_is_directory =
                    left.is_directory(left_error) && !left_error;
                const bool right_is_directory =
                    right.is_directory(right_error) && !right_error;
                if (left_is_directory != right_is_directory)
                  return left_is_directory;

                const std::string left_name =
                    left.path().filename().string();
                const std::string right_name =
                    right.path().filename().string();
                const std::string left_folded = ascii_lowercase(left_name);
                const std::string right_folded = ascii_lowercase(right_name);
                return left_folded == right_folded
                           ? left_name < right_name
                           : left_folded < right_folded;
              });

    const std::string folded_filter = ascii_lowercase(filter);
    files.erase(
        std::remove_if(files.begin(), files.end(),
                       [this, &folded_filter](const auto &entry) {
                         const std::string name =
                             entry.path().filename().string();
                         if (!show_hidden_files &&
                             !name.empty() &&
                             name.front() ==
                                 app_constants::ui::hidden_filename_prefix)
                           return true;
                         return !folded_filter.empty() &&
                                ascii_lowercase(name).find(folded_filter) ==
                                    std::string::npos;
                       }),
        files.end());
    return files;
  };

  auto &files = is_left_panel ? left_files : right_files;
  const auto &path = is_left_panel ? left_path : right_path;
  const auto &filter = is_left_panel ? left_filter : right_filter;
  const bool searchActive =
      is_left_panel ? left_search_active : right_search_active;
  files = loadVisibleFiles(path, filter);
  if (!searchActive)
    files.insert(files.begin(),
                 std::filesystem::directory_entry(
                     std::filesystem::path(path) / ".."));
}

void FileManager::cleanup_docker_cache() {
  if (docker_cache_root.empty())
    return;
  std::error_code error;
  std::filesystem::remove_all(docker_cache_root, error);
  if (error)
    std::cerr << "Unable to clean Docker browse cache: " << error.message()
              << '\n';
  docker_cache_root.clear();
  docker_container_id.clear();
}

static std::vector<std::filesystem::directory_entry>
search_host_files(const std::filesystem::path &root,
                  const std::string &pattern);
static bool search_docker_files(
    const std::string &container_id,
    const std::filesystem::path &cache_root,
    const std::filesystem::path &search_root, const std::string &pattern,
    std::vector<std::filesystem::directory_entry> &results);
static std::string read_search_pattern_dialog(
    bool is_left_panel, const std::string &search_location);

void FileManager::search_files() {
  auto &activePath = isLeftPanelActive ? left_path : right_path;
  auto &searchActive =
      isLeftPanelActive ? left_search_active : right_search_active;
  auto &searchResults =
      isLeftPanelActive ? left_search_results : right_search_results;
  auto &searchRoot =
      isLeftPanelActive ? left_search_root : right_search_root;
  auto &searchPattern =
      isLeftPanelActive ? left_search_pattern : right_search_pattern;
  auto &activeSelectedIndex =
      isLeftPanelActive ? selectedLeftIndex : selectedRightIndex;

  std::string searchLocation = activePath;
  if (!docker_cache_root.empty() &&
      is_docker_cache_path(docker_cache_root, activePath))
    searchLocation = docker_container_id + ":" +
                     container_path_for(docker_cache_root, activePath);
  const std::string pattern =
      read_search_pattern_dialog(isLeftPanelActive, searchLocation);
  if (pattern.empty())
    return;

  const std::filesystem::path root(activePath);
  std::vector<std::filesystem::directory_entry> results;
  if (!docker_cache_root.empty() &&
      is_docker_cache_path(docker_cache_root, root)) {
    if (!search_docker_files(docker_container_id, docker_cache_root, root,
                             pattern, results)) {
      std::cerr << "Docker file search failed.\n";
      return;
    }
  } else {
    results = search_host_files(root, pattern);
  }

  searchRoot = root;
  searchPattern = pattern;
  searchResults = std::move(results);
  searchActive = true;
  activeSelectedIndex = app_constants::ui::first_selected_index;
  refresh_files();
}


static std::string shell_quote(const std::string &value) {
  std::string quoted = "'";
  for (const char character : value) {
    if (character == '\'')
      quoted += "'\\''";
    else
      quoted += character;
  }
  quoted += '\'';
  return quoted;
}

static bool is_docker_cache_path(const std::filesystem::path &cache_root,
                                 const std::filesystem::path &path) {
  const auto relative = path.lexically_normal().lexically_relative(
      cache_root.lexically_normal());
  return !relative.empty() && *relative.begin() != "..";
}

static std::string container_path_for(
    const std::filesystem::path &cache_root,
    const std::filesystem::path &local_path) {
  const auto relative = local_path.lexically_normal().lexically_relative(
      cache_root.lexically_normal());
  if (relative.empty() || *relative.begin() == "..")
    return {};
  const std::string suffix = relative.generic_string();
  return suffix == "." ? "/" : "/" + suffix;
}

static std::vector<std::filesystem::directory_entry>
list_docker_directory(const std::string &container_id,
                      const std::filesystem::path &cache_root,
                      const std::filesystem::path &local_directory) {
  const std::string remote_directory =
      container_path_for(cache_root, local_directory);
  if (remote_directory.empty())
    return {};

  static constexpr std::string_view listing_script =
      "dir=$1; for path in \"$dir\"/* \"$dir\"/.[!.]* \"$dir\"/..?*; do "
      "[ -e \"$path\" ] || [ -L \"$path\" ] || continue; "
      "name=${path##*/}; "
      "if [ -d \"$path\" ] && [ ! -L \"$path\" ]; then type=d; "
      "else type=f; fi; printf '%s\\0%s\\0' \"$name\" \"$type\"; done";
  const std::string command =
      "docker exec " + container_id + " sh -c " +
      shell_quote(std::string(listing_script)) + " sh " +
      shell_quote(remote_directory);
#ifdef _WIN32
  FILE *pipe = _popen(command.c_str(), "rb");
#else
  FILE *pipe = popen(command.c_str(), "r");
#endif
  if (pipe == nullptr) {
    std::cerr << "Unable to start Docker directory listing.\n";
    return {};
  }

  std::string output;
  std::array<char, 4096> buffer{};
  while (const size_t count =
             std::fread(buffer.data(), 1, buffer.size(), pipe))
    output.append(buffer.data(), count);
#ifdef _WIN32
  const int status = _pclose(pipe);
#else
  const int status = pclose(pipe);
#endif
  if (status != 0) {
    std::cerr << "Docker could not list " << remote_directory
              << " in container " << container_id << ".\n";
    return {};
  }

  std::error_code error;
  std::filesystem::create_directories(local_directory, error);
  if (error)
    return {};
  for (const auto &entry : std::filesystem::directory_iterator(local_directory,
                                                                error)) {
    std::filesystem::remove_all(entry.path(), error);
    if (error)
      return {};
  }

  std::vector<std::filesystem::directory_entry> entries;
  size_t position = 0;
  while (position < output.size()) {
    const size_t name_end = output.find('\0', position);
    if (name_end == std::string::npos)
      break;
    const std::string name = output.substr(position, name_end - position);
    position = name_end + 1;
    const size_t type_end = output.find('\0', position);
    if (type_end == std::string::npos)
      break;
    const bool is_directory =
        output.substr(position, type_end - position) == "d";
    position = type_end + 1;
    const auto local_entry = local_directory / name;
    if (is_directory)
      std::filesystem::create_directory(local_entry, error);
    else {
      std::ofstream placeholder(local_entry);
      if (!placeholder)
        continue;
    }
    if (!error)
      entries.emplace_back(local_entry);
    error.clear();
  }
  return entries;
}

static bool fetch_docker_file(const std::string &container_id,
                              const std::filesystem::path &cache_root,
                              const std::filesystem::path &local_path) {
  const std::string remote_path =
      container_path_for(cache_root, local_path);
  if (remote_path.empty())
    return false;
  const std::string command =
      "docker cp " + shell_quote(container_id + ":" + remote_path) + " " +
      shell_quote(local_path.string());
  return std::system(command.c_str()) == 0;
}

static bool wildcard_match(std::string_view pattern, std::string_view name) {
  const std::string folded_pattern = ascii_lowercase(std::string(pattern));
  const std::string folded_name = ascii_lowercase(std::string(name));
  size_t pattern_index = 0;
  size_t name_index = 0;
  size_t last_star = std::string::npos;
  size_t star_name_index = 0;
  while (name_index < folded_name.size()) {
    if (pattern_index < folded_pattern.size() &&
        (folded_pattern[pattern_index] == '?' ||
         folded_pattern[pattern_index] == folded_name[name_index])) {
      ++pattern_index;
      ++name_index;
    } else if (pattern_index < folded_pattern.size() &&
               folded_pattern[pattern_index] == '*') {
      last_star = pattern_index++;
      star_name_index = name_index;
    } else if (last_star != std::string::npos) {
      pattern_index = last_star + 1;
      name_index = ++star_name_index;
    } else {
      return false;
    }
  }
  while (pattern_index < folded_pattern.size() &&
         folded_pattern[pattern_index] == '*')
    ++pattern_index;
  return pattern_index == folded_pattern.size();
}

static std::vector<std::filesystem::directory_entry>
search_host_files(const std::filesystem::path &root,
                  const std::string &pattern) {
  std::vector<std::filesystem::directory_entry> results;
  std::error_code error;
  std::filesystem::recursive_directory_iterator iterator(
      root, std::filesystem::directory_options::skip_permission_denied, error);
  if (error) {
    std::cerr << "Unable to search " << root << ": " << error.message()
              << '\n';
    return results;
  }
  const std::filesystem::recursive_directory_iterator end;
  while (iterator != end) {
    std::error_code entry_error;
    if (iterator->is_regular_file(entry_error) && !entry_error &&
        wildcard_match(pattern, iterator->path().filename().string()))
      results.emplace_back(*iterator);
    iterator.increment(error);
    if (error)
      error.clear();
  }
  std::sort(results.begin(), results.end(),
            [&root](const auto &left, const auto &right) {
              return left.path().lexically_relative(root).generic_string() <
                     right.path().lexically_relative(root).generic_string();
            });
  return results;
}

static bool search_docker_files(
    const std::string &container_id,
    const std::filesystem::path &cache_root,
    const std::filesystem::path &search_root, const std::string &pattern,
    std::vector<std::filesystem::directory_entry> &results) {
  const std::string remote_root =
      container_path_for(cache_root, search_root);
  if (remote_root.empty())
    return false;
  const std::string command =
      "docker exec " + container_id + " find " +
      shell_quote(remote_root) + " -type f -iname " +
      shell_quote(pattern) + " -print0 2>/dev/null";
#ifdef _WIN32
  FILE *pipe = _popen(command.c_str(), "rb");
#else
  FILE *pipe = popen(command.c_str(), "r");
#endif
  if (pipe == nullptr)
    return false;

  std::string output;
  std::array<char, 4096> buffer{};
  while (const size_t count =
             std::fread(buffer.data(), 1, buffer.size(), pipe))
    output.append(buffer.data(), count);
#ifdef _WIN32
  const int status = _pclose(pipe);
#else
  const int status = pclose(pipe);
#endif
  if (status != 0 && output.empty())
    return false;

  size_t position = 0;
  while (position < output.size()) {
    const size_t path_end = output.find('\0', position);
    if (path_end == std::string::npos)
      break;
    const std::filesystem::path remote_path(
        output.substr(position, path_end - position));
    position = path_end + 1;
    const auto relative = remote_path.lexically_relative(remote_root);
    if (relative.empty() || *relative.begin() == "..")
      continue;
    const auto local_path = search_root / relative;
    std::error_code error;
    std::filesystem::create_directories(local_path.parent_path(), error);
    if (error)
      continue;
    std::ofstream placeholder(local_path);
    if (placeholder)
      results.emplace_back(local_path);
  }
  return true;
}

struct PanelDialogGeometry {
  int left;
  int top;
  int width;
  int screenHeight;
#ifdef _WIN32
  int windowLeft;
  int windowTop;
#endif
};

static PanelDialogGeometry panel_dialog_geometry(bool is_left_panel) {
  int terminalWidth = app_constants::ui::windows_content_width;
  int terminalHeight =
      app_constants::ui::windows_visible_file_rows +
      app_constants::terminal::screen_rows_reserved;
#ifdef _WIN32
  int windowLeft = 0;
  int windowTop = 0;
  HANDLE console = GetStdHandle(STD_OUTPUT_HANDLE);
  CONSOLE_SCREEN_BUFFER_INFO info{};
  if (GetConsoleScreenBufferInfo(console, &info)) {
    terminalWidth = info.srWindow.Right - info.srWindow.Left + 1;
    terminalHeight = info.srWindow.Bottom - info.srWindow.Top + 1;
    windowLeft = info.srWindow.Left;
    windowTop = info.srWindow.Top;
  }
  const int panelContentWidth = std::max(
      app_constants::terminal::minimum_panel_content_width,
      terminalWidth - app_constants::terminal::panel_row_horizontal_padding);
#else
  const auto [width, height] = terminal().dimensions();
  terminalWidth = width;
  terminalHeight = height;
  const int panelContentWidth = std::max(
      app_constants::terminal::minimum_panel_content_width,
      terminalWidth - app_constants::terminal::panel_row_horizontal_padding);
#endif
  constexpr int separatorWidth =
      app_constants::terminal::panel_separator_width;
  const int leftPanelWidth =
      (panelContentWidth - separatorWidth) /
      app_constants::ui::panel_split_divisor;
  const int rowContentWidth =
      panelContentWidth -
      app_constants::ui::right_panel_extra_trailing_space;
  const int rightPanelWidth =
      rowContentWidth - separatorWidth - leftPanelWidth;
  return {
      is_left_panel ? 1 : 1 + leftPanelWidth + separatorWidth,
      4,
      is_left_panel ? leftPanelWidth : rightPanelWidth,
      terminalHeight,
#ifdef _WIN32
      windowLeft,
      windowTop
#endif
  };
}

static std::string fit_dialog_content(std::string text, int contentWidth) {
  if (text.size() > static_cast<size_t>(contentWidth))
    text.resize(static_cast<size_t>(contentWidth));
  return text + std::string(static_cast<size_t>(contentWidth) - text.size(),
                            ' ');
}

static void draw_panel_dialog(const PanelDialogGeometry &geometry,
                              const std::vector<std::string> &rows) {
  const int contentWidth = geometry.width - 2;
  std::string topBorder =
      "+" + std::string(static_cast<size_t>(contentWidth - 2), '-');
  const std::string bottomBorder =
      "+" + std::string(static_cast<size_t>(contentWidth), '-') + "+";
  auto putAt = [&](int row, const std::string &text, bool closeButton = false) {
#ifdef _WIN32
    HANDLE console = GetStdHandle(STD_OUTPUT_HANDLE);
    COORD position{
        static_cast<SHORT>(geometry.windowLeft + geometry.left),
        static_cast<SHORT>(geometry.windowTop + row - 1)};
    SetConsoleCursorPosition(console, position);
    SetConsoleTextAttribute(console, BACKGROUND_RED | BACKGROUND_GREEN |
                                         BACKGROUND_BLUE |
                                         BACKGROUND_INTENSITY);
    std::cout << text;
#else
    std::cout << app_constants::terminal::ansi_selected_row << "\033[" << row
              << ';' << geometry.left + 1 << 'H' << text;
#endif
    if (closeButton)
      print_close_button();
    std::cout << std::flush;
  };

  putAt(geometry.top, topBorder, true);
  for (size_t index = 0; index < rows.size(); ++index) {
    const std::string line =
        "|" + fit_dialog_content(rows[index], contentWidth) + "|";
    putAt(geometry.top + 1 + static_cast<int>(index), line);
  }
  putAt(geometry.top + 1 + static_cast<int>(rows.size()), bottomBorder);
}

static void set_panel_dialog_cursor(const PanelDialogGeometry &geometry,
                                   int row, size_t promptLength) {
#ifdef _WIN32
  HANDLE console = GetStdHandle(STD_OUTPUT_HANDLE);
  COORD position{
      static_cast<SHORT>(geometry.windowLeft + geometry.left + 1 +
                         static_cast<int>(promptLength)),
      static_cast<SHORT>(geometry.windowTop + row - 1)};
  SetConsoleCursorPosition(console, position);
  CONSOLE_CURSOR_INFO cursorInfo{};
  if (GetConsoleCursorInfo(console, &cursorInfo)) {
    cursorInfo.bVisible = TRUE;
    SetConsoleCursorInfo(console, &cursorInfo);
  }
#else
  std::cout << app_constants::terminal::ansi_show_cursor << "\033[" << row
            << ';' << geometry.left + 2 + static_cast<int>(promptLength)
            << 'H' << std::flush;
#endif
}

static std::optional<std::string>
read_panel_dialog_input(size_t maximumLength,
                        const PanelDialogGeometry &geometry,
                        bool anyKeyCloses = false) {
#ifdef __linux__
  MouseReportingGuard mouseReportingGuard;
#endif
#ifdef _WIN32
  const HANDLE inputHandle = GetStdHandle(STD_INPUT_HANDLE);
  DWORD originalMode{};
  const bool hasOriginalMode = GetConsoleMode(inputHandle, &originalMode);
  struct ConsoleModeRestorer {
    HANDLE handle;
    DWORD mode;
    bool restore;
    ~ConsoleModeRestorer() {
      if (restore)
        SetConsoleMode(handle, mode);
    }
  } modeRestorer{inputHandle, originalMode, hasOriginalMode};
  if (hasOriginalMode) {
    const DWORD inputMode =
        (originalMode | ENABLE_EXTENDED_FLAGS | ENABLE_MOUSE_INPUT) &
        ~ENABLE_QUICK_EDIT_MODE;
    SetConsoleMode(inputHandle, inputMode);
  }
#endif
  std::string input;
  while (true) {
#ifdef _WIN32
    int key;
    while (true) {
      INPUT_RECORD record{};
      DWORD recordsRead = 0;
      if (!ReadConsoleInputW(inputHandle, &record, 1, &recordsRead) ||
          recordsRead == 0)
        return std::nullopt;
      if (record.EventType == MOUSE_EVENT) {
        const MOUSE_EVENT_RECORD &mouse = record.Event.MouseEvent;
        if ((mouse.dwEventFlags != 0 &&
             mouse.dwEventFlags != DOUBLE_CLICK) ||
            (mouse.dwButtonState & FROM_LEFT_1ST_BUTTON_PRESSED) == 0)
          continue;
        int column = mouse.dwMousePosition.X + 1;
        int row = mouse.dwMousePosition.Y + 1;
        CONSOLE_SCREEN_BUFFER_INFO info{};
        if (GetConsoleScreenBufferInfo(GetStdHandle(STD_OUTPUT_HANDLE), &info)) {
          column -= info.srWindow.Left;
          row -= info.srWindow.Top;
        }
        if (is_close_button_column(column,
                                  geometry.left + geometry.width) &&
            row == geometry.top)
          return std::nullopt;
        continue;
      }
      if (record.EventType != KEY_EVENT || !record.Event.KeyEvent.bKeyDown)
        continue;
      const KEY_EVENT_RECORD &event = record.Event.KeyEvent;
      if (event.wVirtualKeyCode == VK_ESCAPE) {
        key = KEY_ESC;
      } else if (event.wVirtualKeyCode == VK_RETURN) {
        key = KEY_ENTER;
      } else if (event.wVirtualKeyCode == VK_BACK) {
        key = KEY_BACKSPACE;
      } else if (event.uChar.AsciiChar != 0) {
        key = static_cast<unsigned char>(event.uChar.AsciiChar);
      } else {
        continue;
      }
      break;
    }
#else
    const int key = _getch();
#endif
    if (key == KEY_ESC) {
#ifdef __linux__
      const int second =
          _getch_timeout(app_constants::terminal::resize_input_timeout_ms);
      if (second == app_constants::input::linux_sequence_csi) {
        const int third =
            _getch_timeout(app_constants::terminal::resize_input_timeout_ms);
        if (third == '<') {
          const auto event = read_sgr_mouse_event();
          if (event && event->terminator == 'M' &&
              (event->button & 3) == 0 && (event->button & 32) == 0 &&
              is_close_button_column(event->column,
                         geometry.left + geometry.width) &&
              event->row == geometry.top)
            return std::nullopt;
          continue;
        }
      }
#endif
      return std::nullopt;
    }
    if (anyKeyCloses)
      return std::string{};
    if (key == KEY_ENTER ||
        key == app_constants::input::carriage_return)
      return input;
    if ((key == KEY_BACKSPACE ||
         key == app_constants::input::delete_backspace) &&
        !input.empty()) {
      input.pop_back();
      std::cout << "\b \b" << std::flush;
    } else if (key >= app_constants::input::printable_first &&
               key <= app_constants::input::printable_last &&
               input.size() < maximumLength) {
      input.push_back(static_cast<char>(key));
      std::cout << static_cast<char>(key) << std::flush;
    }
  }
}

static std::optional<std::string>
read_panel_dialog_prompt(bool is_left_panel, std::vector<std::string> rows,
                         const std::string &prompt) {
  const PanelDialogGeometry geometry =
      panel_dialog_geometry(is_left_panel);
  const int contentWidth = geometry.width - 2;
  rows.push_back(prompt);
  draw_panel_dialog(geometry, rows);
  const int inputRow = geometry.top + static_cast<int>(rows.size());
  set_panel_dialog_cursor(geometry, inputRow, prompt.size());
  const auto input = read_panel_dialog_input(static_cast<size_t>(
      std::max(0, contentWidth - static_cast<int>(prompt.size()))),
      geometry);
  std::cout << '\n';
#ifdef _WIN32
  HANDLE console = GetStdHandle(STD_OUTPUT_HANDLE);
  CONSOLE_CURSOR_INFO cursorInfo{};
  if (GetConsoleCursorInfo(console, &cursorInfo)) {
    cursorInfo.bVisible = FALSE;
    SetConsoleCursorInfo(console, &cursorInfo);
  }
#else
  std::cout << app_constants::terminal::ansi_hide_cursor << std::flush;
#endif
  return input;
}

static void show_panel_dialog_message(bool is_left_panel,
                                      std::vector<std::string> rows) {
  const PanelDialogGeometry geometry =
      panel_dialog_geometry(is_left_panel);
  draw_panel_dialog(geometry, rows);
  read_panel_dialog_input(0, geometry, true);
}

static std::string read_search_pattern_dialog(
    bool is_left_panel, const std::string &search_location) {
  const PanelDialogGeometry geometry =
      panel_dialog_geometry(is_left_panel);
  const int contentWidth = geometry.width - 2;
  const std::string prompt = "Search for: ";
  const std::string locationPrefix = "Search in: ";
  const size_t locationWidth = static_cast<size_t>(
      std::max(0, contentWidth - static_cast<int>(locationPrefix.size())));
  std::string visibleLocation = search_location;
  if (visibleLocation.size() > locationWidth) {
    if (locationWidth > 3)
      visibleLocation =
          "..." + visibleLocation.substr(visibleLocation.size() -
                                         (locationWidth - 3));
    else
      visibleLocation.resize(locationWidth);
  }
  const std::vector<std::string> rows = {
      " FIND FILES ", prompt, locationPrefix + visibleLocation};
  draw_panel_dialog(geometry, rows);
  const int inputRow = geometry.top + 2;
  set_panel_dialog_cursor(geometry, inputRow, prompt.size());
  const auto input = read_panel_dialog_input(
      static_cast<size_t>(std::max(0, contentWidth -
                                          static_cast<int>(prompt.size()))),
      geometry);
  std::cout << '\n';
#ifdef _WIN32
  HANDLE console = GetStdHandle(STD_OUTPUT_HANDLE);
  CONSOLE_CURSOR_INFO cursorInfo{};
  if (GetConsoleCursorInfo(console, &cursorInfo)) {
    cursorInfo.bVisible = FALSE;
    SetConsoleCursorInfo(console, &cursorInfo);
  }
#else
  std::cout << app_constants::terminal::ansi_hide_cursor << std::flush;
#endif
  return input.value_or(std::string{});
}

static bool capture_command_output(const std::string &command,
                                   std::string &output) {
#ifdef _WIN32
  FILE *pipe = _popen(command.c_str(), "r");
#else
  FILE *pipe = popen(command.c_str(), "r");
#endif
  if (pipe == nullptr)
    return false;
  std::array<char, 4096> buffer{};
  while (const size_t count =
             std::fread(buffer.data(), 1, buffer.size(), pipe))
    output.append(buffer.data(), count);
#ifdef _WIN32
  const int status = _pclose(pipe);
#else
  const int status = pclose(pipe);
#endif
  return status == 0;
}

static std::string quote_shell_path(const std::filesystem::path &path) {
  const std::string value = path.string();
#ifdef _WIN32
  std::string quoted = "\"";
  for (const char character : value) {
    if (character == '"')
      quoted += '\\';
    quoted += character;
  }
  quoted += '"';
  return quoted;
#else
  std::string quoted = "'";
  for (const char character : value) {
    if (character == '\'')
      quoted += "'\\''";
    else
      quoted += character;
  }
  quoted += '\'';
  return quoted;
#endif
}

static bool capture_git_command(const std::filesystem::path &repository_root,
                               std::string_view command,
                               const std::string *standard_input,
                               std::string &output) {
  std::error_code error;
  const auto original_directory = std::filesystem::current_path(error);
  if (error) {
    output = error.message();
    return false;
  }
  std::filesystem::current_path(repository_root, error);
  if (error) {
    output = error.message();
    return false;
  }

  int status = -1;
  if (standard_input == nullptr) {
    FILE *pipe = nullptr;
#ifdef _WIN32
    pipe = _popen((std::string(command) + " 2>&1").c_str(), "r");
#else
    pipe = popen((std::string(command) + " 2>&1").c_str(), "r");
#endif
    if (pipe != nullptr) {
      std::array<char, 4096> buffer{};
      while (const size_t count = std::fread(buffer.data(), 1, buffer.size(),
                                             pipe))
        output.append(buffer.data(), count);
#ifdef _WIN32
      status = _pclose(pipe);
#else
      status = pclose(pipe);
#endif
    }
  } else {
    const auto output_path =
        std::filesystem::temp_directory_path() /
        ("SystemFileManagerX-git-" +
         std::to_string(
             std::chrono::steady_clock::now().time_since_epoch().count()));
    {
      std::ofstream output_file(output_path, std::ios::binary);
      if (!output_file) {
        output = "Unable to create temporary Git output file.";
      } else {
        output_file.close();
        const std::string redirected_command =
            std::string(command) + " > " + quote_shell_path(output_path) +
            " 2>&1";
        FILE *pipe = nullptr;
#ifdef _WIN32
        pipe = _popen(redirected_command.c_str(), "w");
#else
        pipe = popen(redirected_command.c_str(), "w");
#endif
        if (pipe != nullptr) {
          const size_t bytes_written =
              std::fwrite(standard_input->data(), sizeof(char),
                          standard_input->size(), pipe);
#ifdef _WIN32
          const int process_status = _pclose(pipe);
#else
          const int process_status = pclose(pipe);
#endif
          if (bytes_written == standard_input->size())
            status = process_status;
        }
        std::ifstream output_file_read(output_path, std::ios::binary);
        if (output_file_read) {
          output.assign(std::istreambuf_iterator<char>(output_file_read),
                        std::istreambuf_iterator<char>());
        }
        std::filesystem::remove(output_path, error);
        if (error)
          std::cerr << "Unable to remove temporary Git output file: "
                    << error.message() << '\n';
      }
    }
  }

  std::error_code restore_error;
  std::filesystem::current_path(original_directory, restore_error);
  if (restore_error) {
    if (!output.empty())
      output.push_back('\n');
    output += "Unable to restore working directory: " +
              restore_error.message();
    return false;
  }

  int exit_code = status;
#ifndef _WIN32
  if (status != -1)
    exit_code = WIFEXITED(status) ? WEXITSTATUS(status) : status;
#endif
  if (status == -1 || exit_code != 0) {
    if (!output.empty() && output.back() != '\n')
      output.push_back('\n');
    output += "Git command failed (exit code " +
              std::to_string(exit_code) + ").";
    return false;
  }
  if (output.empty())
    output = "(no output)";
  return true;
}

static std::string show_docker_menu(bool is_left_panel) {
  const PanelDialogGeometry geometry =
      panel_dialog_geometry(is_left_panel);
  const int contentWidth = geometry.width - 2;
  std::string containerOutput;
  std::string defaultOutput;
  const bool dockerAvailable =
      capture_command_output(
          "docker ps --format \"{{.ID}}\\t{{.Image}}\\t{{.Names}}\"",
          containerOutput) &&
      capture_command_output("docker ps -q | head -n 1", defaultOutput);
  if (!dockerAvailable) {
    draw_panel_dialog(geometry, {" DOCKER CONTAINERS ",
                                 "Unable to run docker ps.",
                                 "Press any key to close."});
    read_panel_dialog_input(0, geometry, true);
    return {};
  }

  std::vector<std::string> containers;
  std::istringstream lines(containerOutput);
  std::string line;
  while (std::getline(lines, line)) {
    if (line.empty())
      continue;
    std::replace(line.begin(), line.end(), '\t', ' ');
    containers.push_back(std::move(line));
  }
  while (!defaultOutput.empty() &&
         (defaultOutput.back() == '\n' || defaultOutput.back() == '\r'))
    defaultOutput.pop_back();
  if (defaultOutput.empty()) {
    draw_panel_dialog(geometry, {" DOCKER CONTAINERS ",
                                 "No running containers.",
                                 "Press any key to close."});
    read_panel_dialog_input(0, geometry, true);
    return {};
  }

  const int availableRows =
      std::max(0, geometry.screenHeight - geometry.top - 4);
  const bool truncated =
      containers.size() > static_cast<size_t>(availableRows);
  size_t visibleRows =
      std::min(containers.size(), static_cast<size_t>(availableRows));
  if (truncated && visibleRows > 0)
    --visibleRows;

  std::vector<std::string> rows = {" DOCKER CONTAINERS ",
                                   "ID  IMAGE  NAME"};
  for (size_t index = 0; index < visibleRows; ++index)
    rows.push_back(containers[index]);
  if (truncated)
    rows.push_back("More containers omitted; enter an ID");

  const std::string prompt = "Container ID [" + defaultOutput + "]: ";
  rows.push_back(prompt);
  draw_panel_dialog(geometry, rows);
  const int inputRow = geometry.top + static_cast<int>(rows.size());
  set_panel_dialog_cursor(geometry, inputRow, prompt.size());
  const auto input = read_panel_dialog_input(static_cast<size_t>(
      std::max(0, contentWidth - static_cast<int>(prompt.size()))),
      geometry);
  std::cout << '\n';
#ifdef _WIN32
  HANDLE console = GetStdHandle(STD_OUTPUT_HANDLE);
  CONSOLE_CURSOR_INFO cursorInfo{};
  if (GetConsoleCursorInfo(console, &cursorInfo)) {
    cursorInfo.bVisible = FALSE;
    SetConsoleCursorInfo(console, &cursorInfo);
  }
#else
  std::cout << app_constants::terminal::ansi_hide_cursor << std::flush;
#endif
  if (!input)
    return {};
  const std::string id = input->empty() ? defaultOutput : *input;
  if (id.empty() ||
      !std::all_of(id.begin(), id.end(), [](unsigned char character) {
        return std::isxdigit(character);
      }))
    return {};
  return id;
}

void FileManager::show_git_output(std::string_view title,
                                  const std::string &output) {
  const PanelDialogGeometry geometry =
      panel_dialog_geometry(isLeftPanelActive);
  const int contentWidth = std::max(0, geometry.width - 2);
  const int panelRows = std::max(
      1, geometry.screenHeight - app_constants::terminal::screen_rows_reserved);
  const int outputRows = std::max(1, panelRows - 1);
  std::vector<std::string> lines;
  std::istringstream input(output);
  std::string line;
  while (std::getline(input, line)) {
    if (!line.empty() && line.back() == '\r')
      line.pop_back();
    lines.push_back(std::move(line));
  }
  if (lines.empty())
    lines.emplace_back("(no output)");
  size_t topLine = 0;
  const size_t maximumTop =
      lines.size() > static_cast<size_t>(outputRows)
          ? lines.size() - static_cast<size_t>(outputRows)
          : 0;

  auto draw = [&]() {
    for (int row = 0; row < panelRows; ++row) {
      std::string text;
      if (row == 0) {
        text = " GIT " + std::string(title) +
               " | Esc return | Up/Down scroll | PgUp/PgDn";
      } else {
        const size_t lineIndex =
            topLine + static_cast<size_t>(row - 1);
        if (lineIndex < lines.size())
          text = lines[lineIndex];
      }
      const bool closeButton = row == 0 && contentWidth >= closeButtonWidth;
      const size_t textWidth =
          static_cast<size_t>(contentWidth -
                              (closeButton ? closeButtonWidth : 0));
      if (text.size() > textWidth)
        text.resize(textWidth);
      text.append(textWidth - text.size(), ' ');
#ifdef _WIN32
      HANDLE console = GetStdHandle(STD_OUTPUT_HANDLE);
      COORD position{
          static_cast<SHORT>(geometry.windowLeft + geometry.left + 1),
          static_cast<SHORT>(geometry.windowTop + geometry.top + row - 1)};
      SetConsoleCursorPosition(console, position);
      SetConsoleTextAttribute(console, BACKGROUND_RED | BACKGROUND_GREEN |
                                           BACKGROUND_BLUE |
                                           BACKGROUND_INTENSITY);
      std::cout << text;
#else
      std::cout << "\033[" << geometry.top + row << ';'
                << geometry.left + 2 << "H\033[30;47m" << text;
#endif
      if (closeButton)
        print_close_button();
    }
#ifndef _WIN32
    std::cout << app_constants::terminal::ansi_reset;
#endif
    std::cout << std::flush;
  };

  while (true) {
    draw();
    const std::string_view command = read_user_command();
    if ((command == app_constants::input::command_mouse_click ||
         command == app_constants::input::command_mouse_double_click) &&
      is_close_button_column(lastMouseColumn,
                   geometry.left + geometry.width - 1) &&
        lastMouseRow == geometry.top)
      return;
    if (command == app_constants::input::command_escape ||
        command == app_constants::input::command_view_quit)
      return;
    if (command == app_constants::input::command_up && topLine > 0) {
      --topLine;
    } else if (command == app_constants::input::command_down &&
               topLine < maximumTop) {
      ++topLine;
    } else if (command == app_constants::input::command_page_up) {
      topLine = topLine > static_cast<size_t>(outputRows)
                    ? topLine - static_cast<size_t>(outputRows)
                    : 0;
    } else if (command == app_constants::input::command_page_down) {
      topLine = std::min(maximumTop,
                         topLine + static_cast<size_t>(outputRows));
    } else if (command == app_constants::input::command_home) {
      topLine = 0;
    } else if (command == app_constants::input::command_end) {
      topLine = maximumTop;
    }
  }
}

void FileManager::show_git_menu(const std::filesystem::path &active_path) {
  const PanelDialogGeometry geometry =
      panel_dialog_geometry(isLeftPanelActive);
  const GitRepositoryLookup repository = find_git_root(active_path);
  if (!repository.error.empty() || !repository.root) {
    draw_panel_dialog(
        geometry,
        {" GIT ", repository.error.empty()
                     ? std::string(app_constants::ui::git_repository_invalid)
                     : repository.error,
         "Press any key to close."});
    read_panel_dialog_input(0, geometry, true);
    return;
  }

  const int contentWidth = geometry.width - 2;
  const std::string prompt(app_constants::ui::git_choice_prompt);
  const std::string repositoryPrefix = "Repository: ";
  std::string repositoryPath =
      repositoryPrefix + repository.root->string();
  if (repositoryPath.size() > static_cast<size_t>(contentWidth))
    repositoryPath.resize(static_cast<size_t>(contentWidth));
  const std::vector<std::string> rows = {
      " GIT ", repositoryPath,
      std::string(app_constants::ui::git_menu_options), prompt};
  while (true) {
    draw_panel_dialog(geometry, rows);
    const int inputRow = geometry.top + static_cast<int>(rows.size());
    set_panel_dialog_cursor(geometry, inputRow, prompt.size());
    const auto input = read_panel_dialog_input(static_cast<size_t>(
        std::max(0, contentWidth - static_cast<int>(prompt.size()))),
        geometry);
    std::cout << '\n';
#ifdef _WIN32
    HANDLE console = GetStdHandle(STD_OUTPUT_HANDLE);
    CONSOLE_CURSOR_INFO cursorInfo{};
    if (GetConsoleCursorInfo(console, &cursorInfo)) {
      cursorInfo.bVisible = FALSE;
      SetConsoleCursorInfo(console, &cursorInfo);
    }
#else
    std::cout << app_constants::terminal::ansi_hide_cursor << std::flush;
#endif
    if (!input || input->empty() || *input == "0")
      return;

    std::string output;
    if (*input == "1") {
      capture_git_command(*repository.root,
                          app_constants::ui::git_status_command, nullptr,
                          output);
      show_git_output("STATUS", output);
    } else if (*input == "2") {
      capture_git_command(*repository.root, app_constants::ui::git_diff_command,
                          nullptr, output);
      show_git_output("DIFF", output);
    } else if (*input == "3") {
      run_git_command(*repository.root, app_constants::ui::git_gui_command);
    } else if (*input == "4") {
      capture_git_command(*repository.root, app_constants::ui::git_log_command,
                          nullptr, output);
      show_git_output("LOG", output);
    } else if (*input == "5") {
      std::string commit_message;
      bool has_line = false;
      bool cancelled = false;
      while (true) {
        const auto line = read_panel_dialog_prompt(
            isLeftPanelActive,
            {" GIT COMMIT ",
             std::string(app_constants::ui::git_commit_instructions)},
            std::string(app_constants::ui::git_commit_prompt));
        if (!line) {
          cancelled = true;
          break;
        }
        if (*line == ".")
          break;
        if (has_line)
          commit_message.push_back('\n');
        commit_message += *line;
        has_line = true;
      }
      if (!cancelled &&
          commit_message.find_first_not_of(" \t\n\r") != std::string::npos) {
        commit_message.push_back('\n');
        capture_git_command(*repository.root,
                            app_constants::ui::git_commit_stdin_command,
                            &commit_message, output);
        show_git_output("COMMIT", output);
      } else {
        show_panel_dialog_message(
            isLeftPanelActive,
            {" GIT COMMIT ", std::string(app_constants::ui::git_commit_cancelled),
             "Press any key to return."});
      }
    } else if (*input == "6") {
      capture_git_command(*repository.root, app_constants::ui::git_fetch_command,
                          nullptr, output);
      show_git_output("FETCH", output);
    } else if (*input == "7") {
      capture_git_command(*repository.root, app_constants::ui::git_pull_command,
                          nullptr, output);
      show_git_output("PULL", output);
      refresh_files();
    } else if (*input == "8") {
      const auto confirmation = read_panel_dialog_prompt(
          isLeftPanelActive, {" GIT PUSH ", "Push to the configured remote?"},
          "Push? [y/N]: ");
      if (confirmation &&
          (*confirmation == "y" || *confirmation == "Y")) {
        capture_git_command(*repository.root,
                            app_constants::ui::git_push_command, nullptr,
                            output);
        show_git_output("PUSH", output);
      } else {
        show_panel_dialog_message(
            isLeftPanelActive,
            {" GIT PUSH ", std::string(app_constants::ui::git_push_cancelled),
             "Press any key to return."});
      }
    } else {
      draw_panel_dialog(geometry,
                        {" GIT ", std::string(app_constants::ui::git_invalid_option),
                         "Press any key to return."});
      read_panel_dialog_input(0, geometry, true);
    }

    drawPanel(left_path, right_path);
  }
}

// Reads input immediately, returns on Enter or Escape
std::string_view FileManager::read_user_command(
    std::optional<std::chrono::steady_clock::time_point> deadline) {
#ifdef _WIN32
  const HANDLE inputHandle = GetStdHandle(STD_INPUT_HANDLE);
  DWORD originalMode{};
  const bool hasOriginalMode = GetConsoleMode(inputHandle, &originalMode);
  struct ConsoleModeRestorer {
    HANDLE handle;
    DWORD mode;
    bool restore;
    ~ConsoleModeRestorer() {
      if (restore)
        SetConsoleMode(handle, mode);
    }
  } modeRestorer{inputHandle, originalMode, hasOriginalMode};
  if (hasOriginalMode) {
    const DWORD inputMode =
        (originalMode | ENABLE_EXTENDED_FLAGS | ENABLE_MOUSE_INPUT |
         ENABLE_WINDOW_INPUT) &
        ~ENABLE_QUICK_EDIT_MODE;
    SetConsoleMode(inputHandle, inputMode);
  }

  while (true) {
    if (deadline) {
      const auto now = std::chrono::steady_clock::now();
      if (now >= *deadline)
        return app_constants::input::command_work_tick;
      const auto remaining =
          std::chrono::duration_cast<std::chrono::milliseconds>(*deadline - now)
              .count();
      const DWORD waitMilliseconds = static_cast<DWORD>(
          std::clamp<std::chrono::milliseconds::rep>(remaining, 1, 20));
      if (WaitForSingleObject(inputHandle, waitMilliseconds) == WAIT_TIMEOUT)
        continue;
    }
    INPUT_RECORD record{};
    DWORD recordsRead = 0;
    if (!ReadConsoleInputW(inputHandle, &record, 1, &recordsRead) ||
        recordsRead == 0)
      return app_constants::input::command_unknown;

    if (record.EventType == WINDOW_BUFFER_SIZE_EVENT) {
      previousClickTime = {};
      previouslyClickedEntry.reset();
      return app_constants::input::command_resize;
    }

    if (record.EventType == MOUSE_EVENT) {
      const MOUSE_EVENT_RECORD &mouse = record.Event.MouseEvent;
      if ((mouse.dwEventFlags != 0 &&
           mouse.dwEventFlags != DOUBLE_CLICK) ||
          (mouse.dwButtonState & FROM_LEFT_1ST_BUTTON_PRESSED) == 0)
        continue;

      lastMouseColumn = mouse.dwMousePosition.X + 1;
      lastMouseRow = mouse.dwMousePosition.Y + 1;
      CONSOLE_SCREEN_BUFFER_INFO screenInfo{};
      if (GetConsoleScreenBufferInfo(GetStdHandle(STD_OUTPUT_HANDLE),
                                     &screenInfo)) {
        lastMouseColumn -= screenInfo.srWindow.Left;
        lastMouseRow -= screenInfo.srWindow.Top;
      }
      const auto clickTime = std::chrono::steady_clock::now();
      const bool doubleClick =
          mouse.dwEventFlags == DOUBLE_CLICK ||
          (previousClickTime.time_since_epoch().count() != 0 &&
           clickTime - previousClickTime < std::chrono::milliseconds(500) &&
           lastMouseColumn == previousClickColumn &&
           lastMouseRow == previousClickRow);
      previousClickTime = doubleClick
                              ? std::chrono::steady_clock::time_point{}
                              : clickTime;
      previousClickColumn = lastMouseColumn;
      previousClickRow = lastMouseRow;
      return doubleClick
                 ? app_constants::input::command_mouse_double_click
                 : app_constants::input::command_mouse_click;
    }

    if (record.EventType != KEY_EVENT || !record.Event.KeyEvent.bKeyDown)
      continue;

    const KEY_EVENT_RECORD &key = record.Event.KeyEvent;
    const WORD virtualKey = key.wVirtualKeyCode;
    const DWORD modifiers = key.dwControlKeyState;
    const bool altPressed =
      (modifiers & (LEFT_ALT_PRESSED | RIGHT_ALT_PRESSED)) != 0 ||
      (GetAsyncKeyState(VK_MENU) & 0x8000) != 0;
    if (altPressed) {
      switch (virtualKey) {
      case 'X':
        return app_constants::input::command_exit;
      case 'D':
        return app_constants::input::command_docker;
      case 'F':
        return app_constants::input::command_search_files;
      case 'W':
        return app_constants::input::command_work;
      case VK_LEFT:
        return app_constants::input::command_history_back;
      case VK_RIGHT:
        return app_constants::input::command_history_forward;
      default:
        if (key.uChar.UnicodeChar == 'w' ||
            key.uChar.UnicodeChar == 'W')
          return app_constants::input::command_work;
        continue;
      }
    }
    if ((modifiers & (LEFT_CTRL_PRESSED | RIGHT_CTRL_PRESSED)) != 0 &&
        virtualKey == 'A')
      return app_constants::input::command_select_all;

    switch (virtualKey) {
    case VK_F1:
      return app_constants::input::command_f1;
    case VK_F2:
      return app_constants::input::command_f2;
    case VK_F3:
      return app_constants::input::command_f3;
    case VK_F4:
      return app_constants::input::command_f4;
    case VK_F5:
      return app_constants::input::command_f5;
    case VK_F6:
      return app_constants::input::command_f6;
    case VK_F7:
      return app_constants::input::command_f7;
    case VK_F8:
      return app_constants::input::command_f8;
    case VK_F9:
      return app_constants::input::command_f9;
    case VK_UP:
      return app_constants::input::command_up;
    case VK_DOWN:
      return app_constants::input::command_down;
    case VK_HOME:
      return app_constants::input::command_home;
    case VK_END:
      return app_constants::input::command_end;
    case VK_PRIOR:
      return app_constants::input::command_page_up;
    case VK_NEXT:
      return app_constants::input::command_page_down;
    case VK_ESCAPE:
      return app_constants::input::command_escape;
    case VK_RETURN:
      return app_constants::input::command_enter;
    case VK_BACK:
      return app_constants::input::command_backspace;
    case VK_TAB:
      return app_constants::input::command_tab;
    case VK_SPACE:
      return app_constants::input::command_mark;
    default:
      break;
    }

    const wchar_t character = key.uChar.UnicodeChar;
    if (character == app_constants::input::toggle_hidden_key)
      return app_constants::input::command_toggle_hidden;
    if (character == app_constants::input::filter_key)
      return app_constants::input::command_filter;
    if (character == '|')
      return app_constants::input::command_view_filter;
    if (character == 'q' || character == 'Q')
      return app_constants::input::command_view_quit;
    if (character == 'h' || character == 'H')
      return app_constants::input::command_view_hex;
    if (character == 'n' || character == 'N')
      return app_constants::input::command_view_next;
    if (character == 'w' || character == 'W')
      return app_constants::input::command_view_wrap;
    if (!deadline && character >= app_constants::input::printable_first &&
        character <= app_constants::input::printable_last)
      std::cout << static_cast<char>(character);
  }
#else
#ifdef __linux__
  MouseReportingGuard mouseReportingGuard;
#endif
  while (true) {
#ifndef _WIN32
    if (resize_requested) {
      resize_requested = app_constants::terminal::state_not_requested;
      previousClickTime = {};
      return app_constants::input::command_resize;
    }
#endif
    if (_kbhit()) {
      int ch = _getch();

#ifdef __linux__
      if (ch != KEY_ESC)
        previousClickTime = {};
      if (ch == KEY_ESC) {
        const int second =
            _getch_timeout(app_constants::terminal::resize_input_timeout_ms);
        if (second != app_constants::input::linux_sequence_csi)
          previousClickTime = {};
        if (second < 0)
          return app_constants::input::command_escape;

        if (second == 'x' || second == 'X')
          return app_constants::input::command_exit;
        if (second == 'd' || second == 'D')
          return app_constants::input::command_docker;
        if (second == 'f' || second == 'F')
          return app_constants::input::command_search_files;
        if (second == 'w' || second == 'W')
          return app_constants::input::command_work;

        if (second == app_constants::input::linux_sequence_csi) {
          const int third =
              _getch_timeout(app_constants::terminal::resize_input_timeout_ms);
          if (third != '<')
            previousClickTime = {};
          if (third < 0)
            return app_constants::input::command_escape;

          if (third == '<') {
            const auto event = read_sgr_mouse_event();
            if (!event || event->button != 0 || event->column < 1 ||
                event->row < 1) {
              previousClickTime = {};
              return app_constants::input::command_unknown;
            }
            if (event->terminator != 'M')
              return app_constants::input::command_unknown;

            lastMouseColumn = event->column;
            lastMouseRow = event->row;
            const auto clickTime = std::chrono::steady_clock::now();
            const bool doubleClick =
                previousClickTime.time_since_epoch().count() != 0 &&
                clickTime - previousClickTime <
                    std::chrono::milliseconds(500) &&
                lastMouseColumn == previousClickColumn &&
                lastMouseRow == previousClickRow;
            previousClickTime = doubleClick
                                    ? std::chrono::steady_clock::time_point{}
                                    : clickTime;
            previousClickColumn = lastMouseColumn;
            previousClickRow = lastMouseRow;
            return doubleClick
                       ? app_constants::input::command_mouse_double_click
                       : app_constants::input::command_mouse_click;
          }

          // Arrow keys
          if (third == app_constants::input::linux_arrow_up)
            return app_constants::input::command_up;
          if (third == app_constants::input::linux_arrow_down)
            return app_constants::input::command_down;
          if (third == 'H')
            return app_constants::input::command_home;
          if (third == 'F')
            return app_constants::input::command_end;
          if (third == '4' || third == '5' || third == '6') {
            const int fourth = _getch_timeout(
                app_constants::terminal::resize_input_timeout_ms);
            if (fourth != app_constants::input::linux_sequence_terminator)
              return app_constants::input::command_unknown;
            if (third == '4')
              return app_constants::input::command_end;
            return third == '5' ? app_constants::input::command_page_up
                                : app_constants::input::command_page_down;
          }
          if (third == '1') {
            const int fourth = _getch_timeout(
                app_constants::terminal::resize_input_timeout_ms);
            if (fourth == app_constants::input::linux_sequence_terminator)
              return app_constants::input::command_home;
            if (fourth == ';') {
              const int fifth = _getch_timeout(
                  app_constants::terminal::resize_input_timeout_ms);
              const int sixth = _getch_timeout(
                  app_constants::terminal::resize_input_timeout_ms);
              if (fifth == '3' && sixth == 'D')
                return app_constants::input::command_history_back;
              if (fifth == '3' && sixth == 'C')
                return app_constants::input::command_history_forward;
            } else {
              const int fifth = _getch_timeout(
                  app_constants::terminal::resize_input_timeout_ms);
              if (fifth == app_constants::input::linux_sequence_terminator) {
                if (fourth == '1')
                  return app_constants::input::command_f1;
                if (fourth == '2')
                  return app_constants::input::command_f2;
                if (fourth == '3')
                  return app_constants::input::command_f3;
                if (fourth == '4')
                  return app_constants::input::command_f4;
                if (fourth == '5')
                  return app_constants::input::command_f5;
                if (fourth == '7')
                  return app_constants::input::command_f6;
                if (fourth == '8')
                  return app_constants::input::command_f7;
                if (fourth == '9')
                  return app_constants::input::command_f8;
              }
            }
          }
          if (third == '2') {
            const int fourth =
                _getch_timeout(app_constants::terminal::resize_input_timeout_ms);
            const int fifth =
                _getch_timeout(app_constants::terminal::resize_input_timeout_ms);
            if (fifth == app_constants::input::linux_sequence_terminator) {
              if (fourth == '0')
                return app_constants::input::command_f9;
            }
          }

          // Function keys: ESC [ 13~ through 19~ (xterm-compatible terminals)
          if (third == app_constants::input::linux_sequence_function_prefix) {
            const int fourth =
                _getch_timeout(app_constants::terminal::resize_input_timeout_ms);
            const int fifth =
                _getch_timeout(app_constants::terminal::resize_input_timeout_ms);
            if (fifth == app_constants::input::linux_sequence_terminator) {
              if (fourth == app_constants::input::linux_function_key_2)
                return app_constants::input::command_f2;
              if (fourth == app_constants::input::linux_function_key_3)
                return app_constants::input::command_f3;
              if (fourth == app_constants::input::linux_function_key_4)
                return app_constants::input::command_f4;
              if (fourth == app_constants::input::linux_function_key_5)
                return app_constants::input::command_f5;
              if (fourth == app_constants::input::linux_function_key_6)
                return app_constants::input::command_f6;
              if (fourth == app_constants::input::linux_function_key_7)
                return app_constants::input::command_f7;
              if (fourth == app_constants::input::linux_function_key_8)
                return app_constants::input::command_f8;
            }
          }
          return app_constants::input::command_unknown;

        } else if (second == app_constants::input::linux_sequence_ss3) {
          // F2–F4: ESC O Q/R/S
          int third = _getch();
          switch (third) {
          case app_constants::input::linux_f2_ss3:
            return app_constants::input::command_f2;
          case app_constants::input::linux_f3_ss3:
            return app_constants::input::command_f3;
          case app_constants::input::linux_f4_ss3:
            return app_constants::input::command_f4;
          case app_constants::input::linux_f1_ss3:
            return app_constants::input::command_f1;
          }
          return app_constants::input::command_unknown;
        } else {
          return app_constants::input::command_escape;
        }
      }
#else
      if (ch == KEY_EXT_PREFIX1 || ch == KEY_EXT_PREFIX2) {
        int ext = _getch();

        switch (ext) {
        case windows_f1:
          return app_constants::input::command_f1;
        case KEY_F2:
          return app_constants::input::command_f2;
        case KEY_F3:
          return app_constants::input::command_f3;
        case KEY_F4:
          return app_constants::input::command_f4;
        case KEY_F5:
          return app_constants::input::command_f5;
        case KEY_F6:
          return app_constants::input::command_f6;
        case KEY_F7:
          return app_constants::input::command_f7;
        case KEY_F8:
          return app_constants::input::command_f8;
        case KEY_F9:
          return app_constants::input::command_f9;
        case windows_alt_x:
          return app_constants::input::command_exit;
        case windows_alt_d:
          return app_constants::input::command_docker;
        case windows_alt_f:
          return app_constants::input::command_search_files;
        case windows_alt_w:
          return app_constants::input::command_work;
        case KEY_UP:
          return app_constants::input::command_up;
        case KEY_DOWN:
          return app_constants::input::command_down;
        case windows_home:
          return app_constants::input::command_home;
        case windows_end:
          return app_constants::input::command_end;
        case windows_page_up:
          return app_constants::input::command_page_up;
        case windows_page_down:
          return app_constants::input::command_page_down;
        case windows_alt_left:
          return app_constants::input::command_history_back;
        case windows_alt_right:
          return app_constants::input::command_history_forward;
        default:
          break;
        }
      }
#endif
#ifdef __linux__
      if (ch == app_constants::input::carriage_return)
        return app_constants::input::command_enter;
#endif
      if (ch == app_constants::input::toggle_hidden_key)
        return app_constants::input::command_toggle_hidden;
      if (ch == app_constants::input::filter_key)
        return app_constants::input::command_filter;
      if (ch == '|')
        return app_constants::input::command_view_filter;
      if (ch == app_constants::input::mark_key)
        return app_constants::input::command_mark;
      if (ch == app_constants::input::select_all_key)
        return app_constants::input::command_select_all;
      if (ch == 'q' || ch == 'Q')
        return app_constants::input::command_view_quit;
      if (ch == 'h' || ch == 'H')
        return app_constants::input::command_view_hex;
      if (ch == 'n' || ch == 'N')
        return app_constants::input::command_view_next;
      if (ch == 'w' || ch == 'W')
        return app_constants::input::command_view_wrap;
      switch (ch) {
      case KEY_ESC: // ESC
        return app_constants::input::command_escape;

      case KEY_ENTER: // Enter
        return app_constants::input::command_enter;

      case KEY_BACKSPACE: // Backspace
        return app_constants::input::command_backspace;
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
        return app_constants::input::command_tab;

      default:
        if (!deadline && ch >= app_constants::input::printable_first &&
            ch <= app_constants::input::printable_last)
        {
          std::cout << static_cast<char>(ch);
        }
        break;
      }
    } else if (deadline) {
      if (std::chrono::steady_clock::now() >= *deadline)
        return app_constants::input::command_work_tick;
      std::this_thread::sleep_for(std::chrono::milliseconds(20));
    }
  }

  return {};
#endif
}

void FileManager::run() {
  clear_screen();
  refresh_files();
  drawPanel(display_path(left_path), display_path(right_path));

  const std::string_view input = read_user_command();

  const bool handled = process_navigation_input(input);
  if (input != app_constants::input::command_mouse_click &&
      input != app_constants::input::command_unknown) {
    previousClickTime = {};
    previouslyClickedEntry.reset();
  }
  if (handled) {
    return;
  }

  process_file_selection_input(input);
  refresh_files();
}

std::filesystem::path FileManager::display_path(const std::string &path) const {
  if (!docker_cache_root.empty() &&
      is_docker_cache_path(docker_cache_root, path))
    return std::filesystem::path(docker_container_id + ":" +
                                 container_path_for(docker_cache_root, path));
  return std::filesystem::path(path);
}

// minimal helper: truncate path to fit width
static std::string truncatePath(const std::string &path, size_t maxLen) {
  if (path.length() <= maxLen)
    return path;
  if (maxLen <=
      static_cast<size_t>(app_constants::terminal::path_ellipsis_length))
    return std::string(maxLen, app_constants::terminal::ellipsis);
  return std::string(app_constants::ui::path_truncation) +
         path.substr(path.length() -
                     (maxLen - app_constants::terminal::path_ellipsis_length));
}

void FileManager::drawMenuBar(int contentWidth) {
#ifndef _WIN32
  std::cout << app_constants::terminal::ansi_panel_colors;
#endif

  const int footerContentWidth =
      contentWidth - app_constants::terminal::row_border_padding;
  auto fitToWidth = [footerContentWidth](std::string text) {
    if (text.size() > static_cast<size_t>(footerContentWidth))
      text.resize(footerContentWidth);
    return text;
  };
  auto printFooterLine = [footerContentWidth](const std::string &line) {
    std::cout << app_constants::terminal::border_vertical
              << app_constants::terminal::padding << std::left
              << std::setw(footerContentWidth) << line
              << std::string(app_constants::ui::file_row_right_border_padding,
                             app_constants::terminal::padding)
              << app_constants::terminal::border_vertical << '\n';
  };

  std::cout << std::endl
            << app_constants::terminal::border_corner
            << std::string(
                   contentWidth +
                       app_constants::terminal::panel_border_horizontal_padding,
                   app_constants::terminal::border_middle)
            << app_constants::terminal::border_corner << std::endl;
  const std::string fullMenu(app_constants::ui::menu_footer);
  if (fullMenu.size() <= static_cast<size_t>(footerContentWidth)) {
    printFooterLine(fullMenu);
  } else {
    printFooterLine(fitToWidth(
        std::string(app_constants::ui::menu_footer_first_line)));
    printFooterLine(fitToWidth(
        std::string(app_constants::ui::menu_footer_second_line)));
  }

  std::cout << app_constants::terminal::border_corner
            << std::string(
                   contentWidth +
                       app_constants::terminal::panel_border_horizontal_padding,
                   app_constants::terminal::border_bottom)
            << app_constants::terminal::border_corner;
#ifndef _WIN32
  std::cout << app_constants::terminal::ansi_reset;
#endif
  std::cout << std::flush;
}

void FileManager::print_line_with_dual_selection(const std::string &line,
                                                 bool selected1, bool selected2,
                                                 size_t width) {
#ifdef _WIN32
  HANDLE h = GetStdHandle(STD_OUTPUT_HANDLE);
  CONSOLE_SCREEN_BUFFER_INFO csbi;
  WORD originalAttr =
      app_constants::ui::windows_default_console_attributes;
  if (GetConsoleScreenBufferInfo(h, &csbi))
    originalAttr = csbi.wAttributes;

  const size_t leftWidth =
      (width - app_constants::terminal::panel_separator_width) /
      app_constants::ui::panel_split_divisor;
  const size_t rightStart =
      leftWidth + app_constants::terminal::panel_separator_width;
  std::string leftPart = line.substr(0, leftWidth);
  std::string separator =
      line.substr(leftWidth, rightStart - leftWidth);
  std::string rightPart = line.substr(rightStart);

  if (selected1) {
    WORD leftAttr = BACKGROUND_RED | BACKGROUND_GREEN | BACKGROUND_BLUE |
                    BACKGROUND_INTENSITY;
    SetConsoleTextAttribute(h, leftAttr);
    std::cout << leftPart;
    SetConsoleTextAttribute(h, originalAttr);
  } else
    std::cout << leftPart;

  std::cout << separator;

  if (selected2) {
    WORD rightAttr = BACKGROUND_RED | BACKGROUND_GREEN | BACKGROUND_BLUE |
                     BACKGROUND_INTENSITY;
    SetConsoleTextAttribute(h, rightAttr);
    std::cout << rightPart;
    SetConsoleTextAttribute(h, originalAttr);
  } else
    std::cout << rightPart;

#else
  std::cout << app_constants::terminal::ansi_panel_colors;
  const size_t leftWidth =
      (width - app_constants::terminal::panel_separator_width) /
      app_constants::ui::panel_split_divisor;
  const size_t rightStart =
      leftWidth + app_constants::terminal::panel_separator_width;
  if (selected1)
    std::cout << app_constants::terminal::ansi_selected_row;
  std::cout << line.substr(0, leftWidth)
            << app_constants::terminal::ansi_panel_colors
            << line.substr(leftWidth,
                           app_constants::terminal::panel_separator_width);
  if (selected2)
    std::cout << app_constants::terminal::ansi_selected_row;
  std::cout << line.substr(rightStart);
  std::cout << app_constants::terminal::ansi_reset;
#endif
}

namespace {
template <size_t Size>
bool has_extension(const std::string &extension,
                   const std::array<std::string_view, Size> &extensions) {
  return std::find(extensions.begin(), extensions.end(), extension) !=
         extensions.end();
}

std::string_view icon_for_entry(
    const std::filesystem::directory_entry &entry) {
  std::error_code error;
  if (entry.is_symlink(error) && !error)
    return app_constants::ui::symlink_icon;
  if (entry.is_directory(error) && !error)
    return app_constants::ui::directory_icon;
  if (!entry.is_regular_file(error) || error)
    return app_constants::ui::generic_file_icon;

  std::string extension = entry.path().extension().string();
  std::transform(extension.begin(), extension.end(), extension.begin(),
                 [](unsigned char character) {
                   return static_cast<char>(std::tolower(character));
                 });

  if (has_extension(extension, app_constants::ui::image_extensions))
    return app_constants::ui::image_icon;
  if (has_extension(extension, app_constants::ui::audio_extensions))
    return app_constants::ui::audio_icon;
  if (has_extension(extension, app_constants::ui::video_extensions))
    return app_constants::ui::video_icon;
  if (has_extension(extension, app_constants::ui::archive_extensions))
    return app_constants::ui::archive_icon;
  if (has_extension(extension, app_constants::ui::code_extensions))
    return app_constants::ui::code_icon;
  if (extension == ".pdf")
    return app_constants::ui::pdf_icon;
  if (has_extension(extension, app_constants::ui::text_extensions))
    return app_constants::ui::text_icon;
  return app_constants::ui::generic_file_icon;
}
} // namespace

void FileManager::drawLeftRightPanels(std::vector<std::string> &buffer,
                                      int leftPanelWidth, int rightPanelWidth,
                                      int visibleRows) {
  constexpr int indexWidth = app_constants::ui::index_column_width;
  constexpr int iconWidth = app_constants::ui::icon_column_width;
  struct PanelLayout {
    int width;
    int sizeWidth;
    int timeWidth;
    int sizeColumnSpace;
    int timeColumnSpace;
    int maximumNameWidth;
  };
  const auto getPanelLayout = [indexWidth, iconWidth](int width) {
    const int sizeWidth =
        width >= app_constants::ui::size_column_minimum_panel_width
            ? app_constants::ui::size_column_width
            : app_constants::ui::hidden_column_width;
    const int timeWidth =
        width >= app_constants::ui::timestamp_column_minimum_panel_width
            ? app_constants::ui::timestamp_column_width
            : app_constants::ui::hidden_column_width;
    const int sizeColumnSpace =
        sizeWidth > app_constants::ui::hidden_column_width
            ? sizeWidth + app_constants::ui::filename_size_gap_width
            : app_constants::ui::hidden_column_width;
    const int timeColumnSpace =
        timeWidth > app_constants::ui::hidden_column_width
            ? timeWidth + app_constants::ui::metadata_column_separator_width
            : app_constants::ui::hidden_column_width;
    const int maximumNameWidth = std::max(
        app_constants::ui::name_minimum_width,
        width - indexWidth - iconWidth - sizeColumnSpace - timeColumnSpace);
    return PanelLayout{width, sizeWidth, timeWidth, sizeColumnSpace,
                       timeColumnSpace, maximumNameWidth};
  };
  const PanelLayout leftLayout = getPanelLayout(leftPanelWidth);
  const PanelLayout rightLayout = getPanelLayout(rightPanelWidth);
  const auto displayName = [this](const auto &entry, bool is_left_panel) {
    const bool searching =
        is_left_panel ? left_search_active : right_search_active;
    if (!searching)
      return entry.path().filename().string();
    const auto &root =
        is_left_panel ? left_search_root : right_search_root;
    return entry.path().lexically_relative(root).generic_string();
  };
  auto getNameWidth = [&displayName](const auto &files, int maximumWidth,
                                     bool is_left_panel) {
    size_t longestName =
        static_cast<size_t>(app_constants::ui::name_minimum_width);
    for (const auto &entry : files)
      longestName =
          std::max(longestName, displayName(entry, is_left_panel).size());
    return static_cast<int>(
        std::min(longestName, static_cast<size_t>(maximumWidth)));
  };
  const int leftNameWidth =
      getNameWidth(left_files, leftLayout.maximumNameWidth, true);
  const int rightNameWidth =
      getNameWidth(right_files, rightLayout.maximumNameWidth, false);
  const std::string panelSeparator(app_constants::terminal::panel_separator);

  std::string leftPanelName(
      isLeftPanelActive ? app_constants::ui::active_left_panel
                        : app_constants::ui::inactive_left_panel);
  std::string rightPanelName(
      !isLeftPanelActive ? app_constants::ui::active_right_panel
                         : app_constants::ui::inactive_right_panel);
  const auto appendPanelState = [this](std::string &panelName,
                                       const std::string &filter,
                                       bool is_left_panel) {
    if (show_hidden_files)
      panelName += app_constants::ui::hidden_files_indicator;
    if (!filter.empty()) {
      panelName += app_constants::ui::filter_indicator_prefix;
      panelName += filter;
      panelName += app_constants::ui::filter_indicator_suffix;
    }
    const bool searching =
        is_left_panel ? left_search_active : right_search_active;
    if (searching) {
      const auto &pattern =
          is_left_panel ? left_search_pattern : right_search_pattern;
      panelName += app_constants::ui::search_indicator_prefix;
      panelName += pattern;
      panelName += app_constants::ui::search_indicator_suffix;
    }
  };
  appendPanelState(leftPanelName, left_filter, true);
  appendPanelState(rightPanelName, right_filter, false);

  auto padToWidth = [](const std::string &s, int width) -> std::string {
    if (static_cast<int>(s.size()) < width)
      return s + std::string(width - s.size(),
                             app_constants::terminal::padding);
    return s.substr(0, width);
  };

  buffer.push_back(padToWidth(leftPanelName, leftPanelWidth) + panelSeparator +
                   padToWidth(rightPanelName, rightPanelWidth));

  const auto &leftMarkedFiles = left_marked_files;
  const auto &rightMarkedFiles = right_marked_files;
  auto formatEntry = [&](const std::filesystem::directory_entry &entry,
                         size_t idx, size_t selectedIdx,
                         bool panelActive,
                         const std::vector<std::filesystem::path> &markedFiles,
                         const std::string &display_name,
                         int nameWidth,
                         const PanelLayout &layout) -> std::string {
    std::ostringstream oss;

    std::string idxStr =
        std::string(panelActive && idx == selectedIdx
                        ? app_constants::ui::index_prefix_selected
                        : app_constants::ui::index_prefix_unselected) +
        std::to_string(idx) + std::string(app_constants::ui::index_suffix);
    oss << std::left << std::setw(indexWidth) << idxStr;

    const bool marked =
        std::find(markedFiles.begin(), markedFiles.end(), entry.path()) !=
        markedFiles.end();
    const int filenameWidth = std::max(
        app_constants::ui::name_minimum_width,
        nameWidth - (marked ? 2 : 0));
    std::string filename = display_name;
    if (static_cast<int>(filename.size()) > filenameWidth) {
      filename =
          filenameWidth > app_constants::ui::name_truncation_suffix_length
              ? filename.substr(
                    0, filenameWidth -
                           app_constants::ui::name_truncation_suffix_length) +
                    std::string(app_constants::ui::filename_truncation)
              : filename.substr(app_constants::ui::first_selected_index,
                                app_constants::ui::name_minimum_width);
    }
    std::string name = (marked ? "* " : "") + filename;
    oss << icon_for_entry(entry)
        << std::string(
               iconWidth - app_constants::ui::icon_terminal_width,
               app_constants::terminal::padding)
        << std::left << std::setw(nameWidth) << name;

    if (layout.sizeWidth > app_constants::ui::hidden_column_width) {
      std::string sizeStr(app_constants::ui::no_file_size);
      if (entry.is_regular_file()) {
        std::error_code error;
        const auto size = entry.file_size(error);
        if (!error) {
          std::ostringstream sizeOSS;
          const bool showKilobytes =
              size < app_constants::ui::bytes_per_megabyte;
          const int unitBytes =
              showKilobytes ? app_constants::ui::bytes_per_kilobyte
                            : app_constants::ui::bytes_per_megabyte;
          sizeOSS << std::fixed
                  << std::setprecision(app_constants::ui::file_size_precision)
                  << (static_cast<double>(size) / unitBytes)
                  << (showKilobytes ? " KB"
                                    : app_constants::ui::megabyte_suffix);
          sizeStr = sizeOSS.str();
        }
      }
      oss << std::string(app_constants::ui::filename_size_gap_width,
                         app_constants::terminal::padding)
          << std::left
          << std::setw(layout.sizeWidth) << sizeStr;
    }

    if (layout.timeWidth > app_constants::ui::hidden_column_width) {
      std::string timeStr(app_constants::ui::unavailable_value);
      if (entry.is_regular_file()) {
        std::error_code error;
        auto ftime = entry.last_write_time(error);
        if (!error) {
          auto sctp =
              std::chrono::time_point_cast<std::chrono::system_clock::duration>(
                  ftime - decltype(ftime)::clock::now() +
                  std::chrono::system_clock::now());
          std::time_t cftime = std::chrono::system_clock::to_time_t(sctp);
          std::tm tm{};
#ifdef _WIN32
          localtime_s(&tm, &cftime);
#else
          localtime_r(&cftime, &tm);
#endif
          std::array<char, app_constants::ui::timestamp_buffer_size> buf{};
          std::strftime(buf.data(), buf.size(),
                        app_constants::ui::timestamp_format.data(), &tm);
          timeStr = buf.data();
        }
      }
      oss << std::string(app_constants::ui::metadata_column_separator_width,
                         app_constants::terminal::padding)
          << std::left
          << std::setw(layout.timeWidth) << timeStr;
    }

    const int formattedWidth =
        indexWidth + iconWidth + nameWidth + layout.sizeColumnSpace +
        layout.timeColumnSpace;
    if (formattedWidth < layout.width)
      oss << std::string(layout.width - formattedWidth,
                         app_constants::terminal::padding);

    return oss.str();
  };

  auto updateStart = [visibleRows](const auto &files, size_t selected,
                                   size_t &start) {
    const size_t rows = static_cast<size_t>(visibleRows);
    start = std::min(start, files.size() > rows ? files.size() - rows : 0);
    if (selected < start)
      start = selected;
    else if (selected >= start + rows)
      start = selected - rows + 1;
  };
  updateStart(left_files, selectedLeftIndex, leftPanelStart);
  updateStart(right_files, selectedRightIndex, rightPanelStart);

  for (int row = 0; row < visibleRows; ++row) {
    const size_t leftIndex = leftPanelStart + static_cast<size_t>(row);
    const size_t rightIndex = rightPanelStart + static_cast<size_t>(row);
    std::string left = leftIndex < left_files.size()
                           ? formatEntry(left_files[leftIndex], leftIndex,
                                         selectedLeftIndex, isLeftPanelActive,
                                         leftMarkedFiles,
                                         displayName(left_files[leftIndex],
                                                     true),
                                         leftNameWidth, leftLayout)
                           : std::string(leftPanelWidth,
                                         app_constants::terminal::padding);
    std::string right = rightIndex < right_files.size()
                            ? formatEntry(right_files[rightIndex], rightIndex,
                                          selectedRightIndex,
                                          !isLeftPanelActive, rightMarkedFiles,
                                          displayName(right_files[rightIndex],
                                                      false),
                                          rightNameWidth,
                                          rightLayout)
                            : std::string(rightPanelWidth,
                                          app_constants::terminal::padding);
    buffer.push_back(left + panelSeparator + right);
  }
}

void FileManager::drawPanel(const std::filesystem::path &leftPath,
                            const std::filesystem::path &rightPath) {
  // Apply theme for entire panel: black text on light gray background
#ifdef _WIN32
  HANDLE hConsole = GetStdHandle(STD_OUTPUT_HANDLE);
  SetConsoleTextAttribute(hConsole,
                          BACKGROUND_RED | BACKGROUND_GREEN | BACKGROUND_BLUE);
#else
  std::cout << app_constants::terminal::ansi_panel_colors;
#endif
  const auto [terminalWidth, terminalHeight] = terminal().dimensions();
  const int totalWidth = std::max(
      app_constants::terminal::minimum_panel_content_width,
      terminalWidth - app_constants::terminal::panel_row_horizontal_padding);
  const int visibleRows = std::max(
      app_constants::terminal::minimum_visible_file_rows,
      terminalHeight - app_constants::terminal::screen_rows_reserved);

  constexpr int sepWidth = app_constants::terminal::panel_separator_width;
  const int halfWidth =
      (totalWidth - sepWidth) / app_constants::ui::panel_split_divisor;
  const int rowContentWidth =
      totalWidth - app_constants::ui::right_panel_extra_trailing_space;
  const int rightPanelWidth = rowContentWidth - sepWidth - halfWidth;
  const std::string leftLabel(app_constants::ui::left_panel_label);
  const std::string rightLabel(app_constants::ui::right_panel_label);

  std::string leftStr =
      leftLabel + truncatePath(leftPath.string(),
                               halfWidth - (int)leftLabel.size());
  std::string rightStr =
      rightLabel + truncatePath(rightPath.string(),
                                rightPanelWidth - (int)rightLabel.size());

  std::ostringstream hdr;
  hdr << std::left << std::setw(halfWidth) << leftStr << panelSeparator
      << std::left << std::setw(rightPanelWidth) << rightStr;
  std::string headerLine = hdr.str();
  if (headerLine.size() < static_cast<size_t>(rowContentWidth))
    headerLine += std::string(rowContentWidth - headerLine.size(),
                              app_constants::terminal::padding);

  std::string topBorder =
      std::string(1, app_constants::terminal::border_corner) +
      std::string(
          totalWidth +
              app_constants::terminal::panel_border_horizontal_padding -
              (closeButtonWidth - 1),
          app_constants::terminal::border_top);
  std::cout << topBorder;
  print_close_button();
  std::cout << std::endl;
  std::cout << app_constants::terminal::border_vertical
            << app_constants::terminal::padding << headerLine
            << std::string(app_constants::ui::file_row_right_border_padding,
                           app_constants::terminal::padding)
            << app_constants::terminal::border_vertical << std::endl;
  std::cout << app_constants::terminal::border_corner
            << std::string(
                   totalWidth +
                       app_constants::terminal::panel_border_horizontal_padding,
                   app_constants::terminal::border_middle)
            << app_constants::terminal::border_corner;

  if (selectedLeftIndex >= left_files.size())
    selectedLeftIndex = left_files.empty()
                            ? app_constants::ui::first_selected_index
                            : static_cast<int>(std::min(
                                  left_files.size() - 1,
                                  static_cast<size_t>(
                                      std::numeric_limits<int>::max())));
  if (selectedRightIndex >= right_files.size())
    selectedRightIndex = right_files.empty()
                             ? app_constants::ui::first_selected_index
                             : static_cast<int>(std::min(
                                   right_files.size() - 1,
                                   static_cast<size_t>(
                                       std::numeric_limits<int>::max())));

  std::vector<std::string> buffer;
  drawLeftRightPanels(buffer, halfWidth, rightPanelWidth, visibleRows);

  for (size_t i = 0; i < buffer.size(); ++i) {
    const size_t row = i == 0 ? 0 : i - 1;
    const bool isLeftSelected =
        i > 0 && isLeftPanelActive &&
        leftPanelStart + row == static_cast<size_t>(selectedLeftIndex);
    const bool isRightSelected =
        i > 0 && !isLeftPanelActive &&
        rightPanelStart + row == static_cast<size_t>(selectedRightIndex);

    std::string line = buffer[i];
    if (static_cast<int>(line.size()) < rowContentWidth)
      line += std::string(rowContentWidth - line.size(),
                          app_constants::terminal::padding);

#ifndef _WIN32
    std::cout << app_constants::terminal::ansi_panel_colors;
#endif

    std::cout << std::endl << app_constants::terminal::border_vertical
              << app_constants::terminal::padding;
    print_line_with_dual_selection(line, isLeftSelected, isRightSelected,
                                   totalWidth);

#ifndef _WIN32
    std::cout << app_constants::terminal::ansi_panel_colors;
#endif
    std::cout << std::string(
                                 app_constants::ui::file_row_right_border_padding,
                             app_constants::terminal::padding)
              << app_constants::terminal::border_vertical;
  }

  drawMenuBar(totalWidth);

#ifndef _WIN32
  std::cout << app_constants::terminal::ansi_reset << std::flush;
#endif
}

void FileManager::clear_screen() {
  if (screen_initialized) {
#ifdef _WIN32
    COORD origin{};
    SetConsoleCursorPosition(GetStdHandle(STD_OUTPUT_HANDLE), origin);
#else
    std::cout << "\033[H" << std::flush;
#endif
    return;
  }
  terminal().clear();
  screen_initialized = true;
}

void FileManager::show_help_screen() {
  constexpr int help_box_horizontal_padding = 4;
  constexpr int help_box_vertical_padding = 2;
  const std::vector<std::string> helpLines = {
      "SystemFileManagerX - Help",
      "",
      "F1 Help  F2 Rename  F3 View  F4 Edit",
      "F5 Copy  F6 Move  F7 New Folder  F8 Delete",
      "F9 Git menu  Alt+D Docker  Alt+F Find files  Alt+X Exit",
      "",
      "Viewer: arrows/PgUp/PgDn scroll  / search  n next",
      "h hex  w word wrap  | grep-filter  q or F3 returns",
      "Up/Down move  Home/End jump to first/last",
      "Page Up/Down move by a page",
      "Enter opens a directory or launches a file",
      "Space marks/unmarks; Ctrl+A select/clear all",
      "TAB switches panels  Backspace goes to parent",
      "Alt+Left/Right browse directory history",
      ". toggles hidden files  / filters the panel",
      "",
      "F5/F6/F8 use marked items or selection",
      "",
      "Press any key to return to the file panels"};
  const size_t contentWidth = std::max_element(
      helpLines.begin(), helpLines.end(),
      [](const auto &left, const auto &right) {
        return left.size() < right.size();
      })->size();
  int terminalWidth = app_constants::ui::windows_content_width;
  int terminalHeight =
      app_constants::ui::windows_visible_file_rows +
      app_constants::terminal::screen_rows_reserved;
  int windowLeft = 0;
  int windowTop = 0;
#ifdef _WIN32
  HANDLE console = GetStdHandle(STD_OUTPUT_HANDLE);
  CONSOLE_SCREEN_BUFFER_INFO info{};
  WORD originalAttributes =
      app_constants::ui::windows_default_console_attributes;
  if (GetConsoleScreenBufferInfo(console, &info)) {
    terminalWidth = info.srWindow.Right - info.srWindow.Left + 1;
    terminalHeight = info.srWindow.Bottom - info.srWindow.Top + 1;
    windowLeft = info.srWindow.Left;
    windowTop = info.srWindow.Top;
    originalAttributes = info.wAttributes;
  }
#else
  const auto [width, height] = terminal().dimensions();
  terminalWidth = width;
  terminalHeight = height;
#endif

  const int boxWidth = std::max(
      closeButtonWidth + 1,
      std::min(terminalWidth - 2,
               static_cast<int>(contentWidth) + help_box_horizontal_padding));
  const int boxHeight = static_cast<int>(helpLines.size()) +
                        help_box_vertical_padding;
  const int left = std::max(0, (terminalWidth - boxWidth) / 2);
  const int top = std::max(0, (terminalHeight - boxHeight) / 2);
#ifdef _WIN32
  SetConsoleTextAttribute(console, BACKGROUND_RED | BACKGROUND_GREEN |
                                       BACKGROUND_BLUE |
                                       BACKGROUND_INTENSITY);
#else
  std::cout << app_constants::terminal::ansi_selected_row;
#endif
  for (int row = 0; row < boxHeight; ++row) {
#ifdef _WIN32
    const COORD position{static_cast<SHORT>(windowLeft + left),
                         static_cast<SHORT>(windowTop + top + row)};
    SetConsoleCursorPosition(console, position);
#else
    std::cout << "\033[" << top + row + 1 << ';' << left + 1 << 'H';
#endif
    if (row == 0) {
      std::string border(static_cast<size_t>(boxWidth - closeButtonWidth), '-');
      border.front() = '+';
      std::cout << border;
      print_close_button();
      continue;
    }
    if (row == boxHeight - 1) {
      std::cout << '+' << std::string(boxWidth - 2, '-') << '+';
      continue;
    }

    const std::string &text = helpLines[static_cast<size_t>(row - 1)];
    std::cout << '|' << ' ' << text.substr(0, boxWidth - 4)
              << std::string(
                     static_cast<size_t>(std::max(
                         0, boxWidth - 4 - static_cast<int>(text.size()))),
                     ' ')
              << ' ' << '|';
  }
  std::cout << std::flush;
#if defined(__linux__) || defined(_WIN32)
  wait_for_window_close(left + boxWidth, top + 1);
#else
  _getch();
  discard_pending_input();
#endif
#ifdef _WIN32
  SetConsoleTextAttribute(console, originalAttributes);
#else
  std::cout << app_constants::terminal::ansi_reset;
#endif
}

bool FileManager::show_file_viewer(const std::filesystem::path &filePath,
                                   bool automatic, bool initialHexMode) {
  std::ifstream file(filePath, std::ios::binary);
  if (!file) {
    std::cerr << app_constants::filesystem::view_file_failed << filePath
              << '\n';
    if (!automatic) {
      std::cout << "Press any key to return..." << std::flush;
      _getch();
      discard_pending_input();
    }
    return true;
  }

  std::error_code fileSizeError;
  const std::uintmax_t measuredFileSize =
      std::filesystem::file_size(filePath, fileSizeError);
  const std::uintmax_t fileSize = fileSizeError ? 0 : measuredFileSize;
  std::vector<std::streamoff> lineOffsets{0};
  bool lineIndexComplete = false;
  size_t topLine = 0;
  std::uintmax_t hexOffset = 0;
  std::uintmax_t searchHit = std::numeric_limits<std::uintmax_t>::max();
  std::uintmax_t nextSearchOffset = 0;
  std::string searchText;
  struct FilteredLine {
    std::streamoff offset;
    size_t line_number;
  };
  std::vector<FilteredLine> filteredLines;
  std::string filterText;
  bool filterMode = false;
  std::string status;
  bool hexMode = initialHexMode;
  bool wrapMode = false;
  size_t topWrapOffset = 0;

  auto ensureLineOffset = [&](size_t lineIndex) {
    while (lineOffsets.size() <= lineIndex && !lineIndexComplete) {
      file.clear();
      file.seekg(lineOffsets.back());
      std::string line;
      if (!std::getline(file, line)) {
        lineIndexComplete = true;
        break;
      }
      const std::streamoff nextOffset = file.tellg();
      if (nextOffset < 0 ||
          (!fileSizeError &&
           static_cast<std::uintmax_t>(nextOffset) >= fileSize)) {
        lineIndexComplete = true;
      } else {
        lineOffsets.push_back(nextOffset);
      }
    }
    return lineIndex < lineOffsets.size();
  };

  int terminalWidth = app_constants::ui::windows_content_width;
  int terminalHeight =
      app_constants::ui::windows_visible_file_rows +
      app_constants::terminal::screen_rows_reserved;
#ifdef _WIN32
  HANDLE console = GetStdHandle(STD_OUTPUT_HANDLE);
  CONSOLE_SCREEN_BUFFER_INFO consoleInfo{};
  WORD originalAttributes =
      app_constants::ui::windows_default_console_attributes;
  if (GetConsoleScreenBufferInfo(console, &consoleInfo)) {
    terminalWidth = consoleInfo.srWindow.Right - consoleInfo.srWindow.Left + 1;
    terminalHeight =
        consoleInfo.srWindow.Bottom - consoleInfo.srWindow.Top + 1;
    originalAttributes = consoleInfo.wAttributes;
  }
  terminal().clear();
#else
  const auto [width, height] = terminal().dimensions();
  terminalWidth = width;
  terminalHeight = height;
  terminal().clear();
#endif

  const int pageRows = std::max(
      1, terminalHeight - app_constants::ui::viewer_terminal_reserved_rows);
  const auto putAt = [terminalWidth](
                         int row, const std::string &text,
                         bool highlighted = false,
                         const std::vector<std::pair<size_t, size_t>>
                             &highlightRanges = {},
                         bool closeButton = false) {
#ifdef _WIN32
    HANDLE output = GetStdHandle(STD_OUTPUT_HANDLE);
    SetConsoleCursorPosition(
        output, COORD{0, static_cast<SHORT>(row)});
    const auto setColor = [output](bool useHighlight) {
      SetConsoleTextAttribute(
          output, useHighlight
                      ? BACKGROUND_RED | BACKGROUND_GREEN |
                            BACKGROUND_INTENSITY
                      : BACKGROUND_RED | BACKGROUND_GREEN | BACKGROUND_BLUE |
                            BACKGROUND_INTENSITY);
    };
#else
    std::cout << "\033[" << row + 1 << ";1H";
    const auto setColor = [](bool useHighlight) {
      std::cout << (useHighlight ? "\033[30;103m" : "\033[30;47m");
    };
#endif
    const std::string visible =
        text.substr(0, static_cast<size_t>(
                           terminalWidth -
                           (closeButton ? closeButtonWidth : 0)));
    setColor(highlighted);
    size_t cursor = 0;
    for (const auto &[start, length] : highlightRanges) {
      if (start >= visible.size() || length == 0)
        continue;
      const size_t boundedStart = std::max(cursor, start);
      const size_t boundedEnd = std::min(visible.size(), start + length);
      if (boundedStart >= boundedEnd)
        continue;
      std::cout << visible.substr(cursor, boundedStart - cursor);
      setColor(true);
      std::cout << visible.substr(boundedStart, boundedEnd - boundedStart);
      setColor(highlighted);
      cursor = boundedEnd;
    }
    std::cout << visible.substr(cursor)
              << std::string(static_cast<size_t>(std::max(
                                 0, terminalWidth -
                                        static_cast<int>(visible.size()) -
                                        (closeButton ? closeButtonWidth : 0))),
                             ' ');
    if (closeButton)
      print_close_button();
  };
  constexpr size_t linePrefixWidth =
      1 + app_constants::ui::viewer_hex_offset_width + 2;
  const size_t wrapWidth = static_cast<size_t>(
      std::max(1, terminalWidth - static_cast<int>(linePrefixWidth)));
  const auto readDisplayLine =
      [&](size_t displayIndex, size_t &sourceLineNumber,
          std::streamoff &sourceOffset) -> std::optional<std::string> {
    if (filterMode) {
      if (displayIndex >= filteredLines.size())
        return {};
      sourceLineNumber = filteredLines[displayIndex].line_number;
      sourceOffset = filteredLines[displayIndex].offset;
    } else {
      if (!ensureLineOffset(displayIndex))
        return {};
      sourceLineNumber = displayIndex + 1;
      sourceOffset = lineOffsets[displayIndex];
    }

    file.clear();
    file.seekg(sourceOffset);
    std::string line;
    if (!std::getline(file, line))
      return {};
    if (!line.empty() && line.back() == '\r')
      line.pop_back();
    return line;
  };
  const auto moveWrappedRow = [&](bool down) {
    if (down) {
      size_t sourceLineNumber = 0;
      std::streamoff sourceOffset = 0;
      const auto line =
          readDisplayLine(topLine, sourceLineNumber, sourceOffset);
      if (!line)
        return;
      if (topWrapOffset + wrapWidth < line->size()) {
        topWrapOffset += wrapWidth;
      } else {
        const size_t nextLine = topLine + 1;
        if (filterMode) {
          if (nextLine >= filteredLines.size())
            return;
        } else if (!ensureLineOffset(nextLine)) {
          return;
        }
        topLine = nextLine;
        topWrapOffset = 0;
      }
    } else if (topWrapOffset > 0) {
      topWrapOffset = topWrapOffset > wrapWidth
                          ? topWrapOffset - wrapWidth
                          : 0;
    } else if (topLine > 0) {
      --topLine;
      size_t sourceLineNumber = 0;
      std::streamoff sourceOffset = 0;
      const auto line =
          readDisplayLine(topLine, sourceLineNumber, sourceOffset);
      topWrapOffset =
          line && !line->empty()
              ? ((line->size() - 1) / wrapWidth) * wrapWidth
              : 0;
    }
  };

  const auto findMatchInRange =
      [&](const std::string &query, std::uintmax_t begin,
          std::uintmax_t end) -> std::optional<std::uintmax_t> {
    if (query.empty() || begin >= end)
      return {};

    std::ifstream searchFile(filePath, std::ios::binary);
    if (!searchFile)
      return {};
    constexpr size_t chunkSize =
        app_constants::ui::viewer_search_chunk_size;
    std::string carry;
    std::uintmax_t position = begin;
    const auto equalInsensitive = [](char left, char right) {
      return std::tolower(static_cast<unsigned char>(left)) ==
             std::tolower(static_cast<unsigned char>(right));
    };

    while (position < end) {
      searchFile.clear();
      searchFile.seekg(static_cast<std::streamoff>(position));
      const size_t carrySize = carry.size();
      std::string chunk = carry;
      const size_t readSize = static_cast<size_t>(
          std::min<std::uintmax_t>(chunkSize, end - position));
      std::string nextBytes(readSize, '\0');
      searchFile.read(nextBytes.data(),
                      static_cast<std::streamsize>(readSize));
      const size_t bytesRead = static_cast<size_t>(searchFile.gcount());
      chunk.append(nextBytes, 0, bytesRead);
      if (bytesRead == 0)
        break;

      const std::uintmax_t chunkStart = position - carrySize;
      const auto match = std::search(chunk.begin(), chunk.end(),
                                     query.begin(), query.end(),
                                     equalInsensitive);
      if (match != chunk.end()) {
        const auto matchOffset =
            chunkStart + static_cast<std::uintmax_t>(
                             std::distance(chunk.begin(), match));
        if (matchOffset >= begin &&
            matchOffset + query.size() <= end)
          return matchOffset;
      }

      const size_t overlap = std::min(query.size() - 1, chunk.size());
      carry = chunk.substr(chunk.size() - overlap);
      position += bytesRead;
    }
    return {};
  };

  bool running = true;
  bool cancelled = false;
  int automaticSteps = 0;
  auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(2);
  while (running) {
    std::ostringstream header;
    header << filePath.filename().string()
           << (hexMode ? "  [HEX]" : "  [TEXT]");
    if (wrapMode && !hexMode)
      header << "  [WRAP]";
    if (!searchText.empty())
      header << "  Search: " << searchText;
    if (filterMode)
      header << "  Filter: " << filterText << " ("
             << filteredLines.size() << " lines)";
    putAt(0, header.str(), false, {}, terminalWidth >= closeButtonWidth);

    size_t wrappedLineIndex = topLine;
    size_t wrappedCharacterOffset = topWrapOffset;
    for (int row = 0; row < pageRows; ++row) {
      std::ostringstream output;
      bool highlightLine = false;
      std::vector<std::pair<size_t, size_t>> highlightRanges;
      if (hexMode) {
        const std::uintmax_t offset =
            hexOffset +                             static_cast<std::uintmax_t>(row) *
                            app_constants::ui::viewer_bytes_per_row;
        if (!fileSizeError && offset < fileSize) {
          std::array<char, app_constants::ui::viewer_bytes_per_row> bytes{};
          file.clear();
          file.seekg(static_cast<std::streamoff>(offset));
          file.read(bytes.data(), static_cast<std::streamsize>(bytes.size()));
          const size_t count = static_cast<size_t>(file.gcount());
          highlightLine =
              searchHit >= offset && searchHit < offset + count;
          output << (searchHit >= offset && searchHit < offset + count ? '>' : ' ')
                 << std::hex << std::setfill('0')
                 << std::setw(app_constants::ui::viewer_hex_offset_width)
                 << offset
                 << "  ";
          for (size_t column = 0; column < bytes.size(); ++column) {
            if (column < count)
              output << std::setw(2)
                     << static_cast<unsigned int>(
                            static_cast<unsigned char>(bytes.at(column)))
                     << ' ';
            else
              output << "   ";
          }
          output << " |";
          for (size_t column = 0; column < count; ++column) {
            const unsigned char value =
                static_cast<unsigned char>(bytes.at(column));
            output << (std::isprint(value) ? static_cast<char>(value) : '.');
          }
          output << '|';
        }
      } else if (wrapMode) {
        size_t sourceLineNumber = 0;
        std::streamoff sourceOffset = 0;
        const auto line = readDisplayLine(wrappedLineIndex, sourceLineNumber,
                                          sourceOffset);
        if (line) {
          highlightLine =
              searchHit != std::numeric_limits<std::uintmax_t>::max() &&
              searchHit >= static_cast<std::uintmax_t>(sourceOffset) &&
              searchHit < static_cast<std::uintmax_t>(sourceOffset) +
                              line->size();
          if (wrappedCharacterOffset == 0) {
            output << (highlightLine ? '>' : ' ')
                   << std::setw(
                          app_constants::ui::viewer_hex_offset_width)
                   << sourceLineNumber << "  ";
          } else {
            output << std::string(linePrefixWidth, ' ');
          }
          const size_t contentStart =
              static_cast<size_t>(output.tellp());
          const size_t segmentStart =
              std::min(wrappedCharacterOffset, line->size());
          const size_t segmentEnd =
              std::min(line->size(), segmentStart + wrapWidth);
          output << line->substr(segmentStart, segmentEnd - segmentStart);
          if (filterMode && !filterText.empty()) {
            size_t matchPosition = 0;
            while ((matchPosition = line->find(filterText, matchPosition)) !=
                   std::string::npos) {
              const size_t matchEnd = matchPosition + filterText.size();
              const size_t visibleMatchStart =
                  std::max(matchPosition, segmentStart);
              const size_t visibleMatchEnd =
                  std::min(matchEnd, segmentEnd);
              if (visibleMatchStart < visibleMatchEnd)
                highlightRanges.emplace_back(
                    contentStart + visibleMatchStart - segmentStart,
                    visibleMatchEnd - visibleMatchStart);
              matchPosition = matchEnd;
            }
          }
          if (segmentEnd < line->size()) {
            wrappedCharacterOffset = segmentEnd;
          } else {
            ++wrappedLineIndex;
            wrappedCharacterOffset = 0;
          }
        } else {
          ++wrappedLineIndex;
          wrappedCharacterOffset = 0;
        }
      } else {
        const size_t lineIndex = topLine + static_cast<size_t>(row);
        if (filterMode) {
          if (lineIndex < filteredLines.size()) {
            const auto &filteredLine = filteredLines[lineIndex];
            file.clear();
            file.seekg(filteredLine.offset);
            std::string line;
            if (std::getline(file, line)) {
              if (!line.empty() && line.back() == '\r')
                line.pop_back();
              output << (searchHit >=
                                     static_cast<std::uintmax_t>(
                                         filteredLine.offset)
                                 ? '>'
                                 : ' ')
                     << std::setw(
                            app_constants::ui::viewer_hex_offset_width)
                     << filteredLine.line_number << "  ";
              const size_t contentStart =
                  static_cast<size_t>(output.tellp());
              output << line;
              size_t matchPosition = 0;
              while (!filterText.empty() &&
                     (matchPosition = line.find(filterText, matchPosition)) !=
                         std::string::npos) {
                highlightRanges.emplace_back(contentStart + matchPosition,
                                             filterText.size());
                matchPosition += filterText.size();
              }
            }
          } else if (lineIndex == 0) {
            output << "No lines match: " << filterText;
          }
        } else if (ensureLineOffset(lineIndex)) {
          file.clear();
          file.seekg(lineOffsets[lineIndex]);
          const std::streamoff lineStart = lineOffsets[lineIndex];
          std::string line;
          if (std::getline(file, line)) {
            std::streamoff lineEnd = file.tellg();
            if (lineEnd < 0)
              lineEnd = static_cast<std::streamoff>(fileSize);
            highlightLine =
                searchHit != std::numeric_limits<std::uintmax_t>::max() &&
                searchHit >= static_cast<std::uintmax_t>(lineStart) &&
                searchHit < static_cast<std::uintmax_t>(lineEnd);
            if (!line.empty() && line.back() == '\r')
              line.pop_back();
            output << (highlightLine ? '>' : ' ')
                   << std::setw(app_constants::ui::viewer_hex_offset_width)
                   << lineIndex + 1 << "  " << line;
          }
        }
      }
      putAt(row + 1, output.str(), highlightLine, highlightRanges);
    }

    std::ostringstream footer;
    if (!status.empty())
      footer << status << " | ";
    if (filterMode)
      footer << "Filtered by \"" << filterText << "\" ("
             << filteredLines.size() << ") | ";
    footer << "Up/Down Scroll | PgUp/PgDn Page | Home/End | / Search | "
           << "| Grep | n Next | h Hex | w Wrap: "
           << (wrapMode ? "On" : "Off") << " | F3/q/Esc Return";
    putAt(terminalHeight - 1, footer.str());
#ifndef _WIN32
    std::cout << app_constants::terminal::ansi_reset << std::flush;
#else
    SetConsoleTextAttribute(console, originalAttributes);
    std::cout << std::flush;
#endif
    status.clear();

    std::string_view command = read_user_command(
        automatic ? std::optional<std::chrono::steady_clock::time_point>(deadline)
                  : std::nullopt);
    if (automatic) {
      if (command == app_constants::input::command_escape) {
        cancelled = true;
        break;
      }
      if (command != app_constants::input::command_work_tick)
        continue;
      if (++automaticSteps == 4)
        break;
      deadline = std::chrono::steady_clock::now() + std::chrono::seconds(2);
      command = app_constants::input::command_down;
    }
    if ((command == app_constants::input::command_mouse_click ||
         command == app_constants::input::command_mouse_double_click) &&
        is_close_button_column(lastMouseColumn, terminalWidth) &&
        lastMouseRow == 1) {
      running = false;
    } else if (command == app_constants::input::command_exit) {
      cleanup_docker_cache();
#ifdef _WIN32
      done.store(true);
  running = false;
#else
      tcsetattr(STDIN_FILENO, TCSANOW, &g_orig_termios);
      std::cout << app_constants::terminal::ansi_reset
                << app_constants::terminal::ansi_show_cursor
                << app_constants::terminal::ansi_leave_fullscreen << std::flush;
      std::_Exit(0);
#endif
    }
    if (command == app_constants::input::command_view_quit ||
        command == app_constants::input::command_escape ||
        command == app_constants::input::command_f3) {
      running = false;
    } else if (command == app_constants::input::command_view_wrap) {
      if (hexMode) {
        status = "Word wrap is only available in text mode";
      } else {
        wrapMode = !wrapMode;
        topWrapOffset = 0;
        status = wrapMode ? "Word wrap on" : "Word wrap off";
      }
    } else if (command == app_constants::input::command_view_hex) {
      if (!hexMode) {
        filterMode = false;
        filterText.clear();
        filteredLines.clear();
        topWrapOffset = 0;
        if (ensureLineOffset(topLine))
          hexOffset = static_cast<std::uintmax_t>(lineOffsets[topLine]);
      }
      else if (hexMode && !lineOffsets.empty()) {
        while (!lineIndexComplete &&
               static_cast<std::uintmax_t>(lineOffsets.back()) <= hexOffset)
          ensureLineOffset(lineOffsets.size());
        const auto line = std::upper_bound(
            lineOffsets.begin(), lineOffsets.end(),
            static_cast<std::streamoff>(hexOffset));
        topLine = line == lineOffsets.begin()
                      ? 0
                      : static_cast<size_t>(
                            std::distance(lineOffsets.begin(), line) - 1);
      }
      hexMode = !hexMode;
    } else if (command == app_constants::input::command_up) {
      if (hexMode) {
        hexOffset = hexOffset >= app_constants::ui::viewer_bytes_per_row
                        ? hexOffset -
                              app_constants::ui::viewer_bytes_per_row
                        : 0;
      } else if (wrapMode) {
        moveWrappedRow(false);
      } else if (topLine > 0) {
        --topLine;
      }
    } else if (command == app_constants::input::command_down) {
      if (hexMode) {
        if (hexOffset + app_constants::ui::viewer_bytes_per_row < fileSize)
          hexOffset += app_constants::ui::viewer_bytes_per_row;
      } else if (wrapMode) {
        moveWrappedRow(true);
      } else if (filterMode) {
        if (topLine + 1 < filteredLines.size())
          ++topLine;
      } else if (ensureLineOffset(topLine + 1)) {
        ++topLine;
      }
    } else if (command == app_constants::input::command_home) {
      if (hexMode)
        hexOffset = 0;
      else {
        topLine = 0;
        topWrapOffset = 0;
      }
    } else if (command == app_constants::input::command_end) {
      if (hexMode) {
        hexOffset =
            fileSize == 0
                ? 0
                : ((fileSize - 1) /
                   app_constants::ui::viewer_bytes_per_row) *
                      app_constants::ui::viewer_bytes_per_row;
      } else if (wrapMode) {
        if (filterMode) {
          topLine = filteredLines.empty() ? 0 : filteredLines.size() - 1;
        } else {
          while (!lineIndexComplete)
            ensureLineOffset(lineOffsets.size());
          topLine = lineOffsets.empty() ? 0 : lineOffsets.size() - 1;
        }
        topWrapOffset = 0;
        size_t sourceLineNumber = 0;
        std::streamoff sourceOffset = 0;
        const auto line =
            readDisplayLine(topLine, sourceLineNumber, sourceOffset);
        if (line && !line->empty())
          topWrapOffset = ((line->size() - 1) / wrapWidth) * wrapWidth;
        for (int row = 1; row < pageRows; ++row)
          moveWrappedRow(false);
      } else if (filterMode) {
        topLine = filteredLines.size() > static_cast<size_t>(pageRows)
                      ? filteredLines.size() - static_cast<size_t>(pageRows)
                      : 0;
      } else {
        while (!lineIndexComplete)
          ensureLineOffset(lineOffsets.size());
        topLine = lineOffsets.size() > static_cast<size_t>(pageRows)
                      ? lineOffsets.size() - static_cast<size_t>(pageRows)
                      : 0;
      }
    } else if (command == app_constants::input::command_page_up) {
      if (hexMode) {
        const std::uintmax_t distance =
            static_cast<std::uintmax_t>(pageRows) *
            app_constants::ui::viewer_bytes_per_row;
        hexOffset = hexOffset > distance ? hexOffset - distance : 0;
      } else if (wrapMode) {
        for (int row = 0; row < pageRows; ++row)
          moveWrappedRow(false);
      } else {
        topLine = topLine > static_cast<size_t>(pageRows)
                      ? topLine - static_cast<size_t>(pageRows)
                      : 0;
      }
    } else if (command == app_constants::input::command_page_down) {
      if (hexMode) {
        const std::uintmax_t distance =
            static_cast<std::uintmax_t>(pageRows) *
            app_constants::ui::viewer_bytes_per_row;
        hexOffset = std::min(
            fileSize == 0 ? std::uintmax_t{0}
                          : ((fileSize - 1) /
                             app_constants::ui::viewer_bytes_per_row) *
                                app_constants::ui::viewer_bytes_per_row,
            hexOffset + distance);
      } else if (wrapMode) {
        for (int row = 0; row < pageRows; ++row)
          moveWrappedRow(true);
      } else if (filterMode) {
        const size_t target = topLine + static_cast<size_t>(pageRows);
        topLine = filteredLines.size() > static_cast<size_t>(pageRows)
                      ? std::min(target, filteredLines.size() -
                                             static_cast<size_t>(pageRows))
                      : 0;
      } else {
        const size_t target = topLine + static_cast<size_t>(pageRows);
        if (ensureLineOffset(target))
          topLine = target;
        else
          topLine = lineOffsets.size() > static_cast<size_t>(pageRows)
                        ? lineOffsets.size() -
                              static_cast<size_t>(pageRows)
                        : 0;
      }
    } else if (command == app_constants::input::command_view_filter) {
      const std::string prompt = "Filter lines (grep): ";
      const int promptRow = terminalHeight - 1;
      putAt(promptRow, prompt);
#ifdef _WIN32
      SetConsoleCursorPosition(
          console, COORD{static_cast<SHORT>(prompt.size()),
                         static_cast<SHORT>(promptRow)});
      CONSOLE_CURSOR_INFO cursorInfo{};
      if (GetConsoleCursorInfo(console, &cursorInfo)) {
        cursorInfo.bVisible = TRUE;
        SetConsoleCursorInfo(console, &cursorInfo);
      }
#else
      std::cout << app_constants::terminal::ansi_show_cursor << "\033["
                << promptRow + 1 << ';' << prompt.size() + 1 << 'H'
                << std::flush;
#endif
      const std::string query = read_line_input();
#ifdef _WIN32
      SetConsoleTextAttribute(console, originalAttributes);
#else
      std::cout << app_constants::terminal::ansi_hide_cursor;
#endif
      if (query.empty()) {
        filterMode = false;
        filterText.clear();
        filteredLines.clear();
        topLine = 0;
        topWrapOffset = 0;
        status = "Line filter cleared";
      } else {
        std::ifstream filterFile(filePath, std::ios::binary);
        if (!filterFile) {
          status = "Unable to read file for filtering";
        } else {
          std::vector<FilteredLine> matches;
          std::string line;
          size_t lineNumber = 0;
          while (true) {
            const std::streamoff lineOffset = filterFile.tellg();
            if (lineOffset < 0 || !std::getline(filterFile, line))
              break;
            ++lineNumber;
            if (line.find(query) != std::string::npos)
              matches.push_back({lineOffset, lineNumber});
          }
          filterText = query;
          filteredLines = std::move(matches);
          filterMode = true;
          hexMode = false;
          searchText.clear();
          searchHit = std::numeric_limits<std::uintmax_t>::max();
          nextSearchOffset = 0;
          topLine = 0;
          topWrapOffset = 0;
          status = "Matched " + std::to_string(filteredLines.size()) +
                   " line(s)";
        }
      }
    } else if (command == app_constants::input::command_filter) {
      filterMode = false;
      filterText.clear();
      filteredLines.clear();
      topLine = 0;
      topWrapOffset = 0;
      std::cout << "\033[" << terminalHeight << ";1H\033[2KSearch: "
                << std::flush;
#ifndef _WIN32
      std::cout << app_constants::terminal::ansi_show_cursor << std::flush;
#endif
      searchText = read_line_input();
#ifndef _WIN32
      std::cout << app_constants::terminal::ansi_hide_cursor;
#endif
      if (!searchText.empty() && !fileSizeError) {
        const std::uintmax_t start =
            nextSearchOffset < fileSize ? nextSearchOffset : 0;
        auto found = findMatchInRange(searchText, start, fileSize);
        if (!found && start > 0)
          found = findMatchInRange(searchText, 0, start);
        if (found) {
          searchHit = *found;
          nextSearchOffset = (*found + 1) % fileSize;
          status = "Found at byte " + std::to_string(*found);
          if (hexMode) {
            hexOffset = (*found / app_constants::ui::viewer_bytes_per_row) *
                        app_constants::ui::viewer_bytes_per_row;
          } else {
            while (!lineIndexComplete &&
                   static_cast<std::uintmax_t>(lineOffsets.back()) <= *found)
              ensureLineOffset(lineOffsets.size());
            const auto line = std::upper_bound(
                lineOffsets.begin(), lineOffsets.end(),
                static_cast<std::streamoff>(*found));
            topLine = line == lineOffsets.begin()
                          ? 0
                          : static_cast<size_t>(
                                std::distance(lineOffsets.begin(), line) - 1);
            topWrapOffset = 0;
          }
        } else {
          searchHit = std::numeric_limits<std::uintmax_t>::max();
          status = "Not found: " + searchText;
        }
      }
    } else if (command == app_constants::input::command_view_next &&
               !searchText.empty() && !fileSizeError) {
      const std::uintmax_t start =
          nextSearchOffset < fileSize ? nextSearchOffset : 0;
      auto found = findMatchInRange(searchText, start, fileSize);
      if (!found && start > 0)
        found = findMatchInRange(searchText, 0, start);
      if (found) {
        searchHit = *found;
        nextSearchOffset = (*found + 1) % fileSize;
        status = "Found at byte " + std::to_string(*found);
        if (hexMode) {
          hexOffset = (*found / app_constants::ui::viewer_bytes_per_row) *
                      app_constants::ui::viewer_bytes_per_row;
        } else {
          while (!lineIndexComplete &&
                 static_cast<std::uintmax_t>(lineOffsets.back()) <= *found)
            ensureLineOffset(lineOffsets.size());
          const auto line = std::upper_bound(
              lineOffsets.begin(), lineOffsets.end(),
              static_cast<std::streamoff>(*found));
          topLine = line == lineOffsets.begin()
                        ? 0
                        : static_cast<size_t>(
                              std::distance(lineOffsets.begin(), line) - 1);
          topWrapOffset = 0;
        }
      } else {
        status = "No further matches for " + searchText;
      }
    }
  }

#ifdef _WIN32
  SetConsoleTextAttribute(console, originalAttributes);
  terminal().clear();
#else
  terminal().clear();
#endif
  return !cancelled;
}

void FileManager::work_mode() {
  auto &activePath = isLeftPanelActive ? left_path : right_path;
  if (!docker_cache_root.empty() &&
      is_docker_cache_path(docker_cache_root, activePath)) {
    show_panel_dialog_message(
        isLeftPanelActive,
        {"Unavailable in this location.", "Press any key to return."});
    return;
  }

#ifdef _WIN32
  WindowsWorkModeKeepAlive keepAlive;
#endif
  auto &files = isLeftPanelActive ? left_files : right_files;
  auto &selection = isLeftPanelActive ? selectedLeftIndex : selectedRightIndex;
  auto &panelStart = isLeftPanelActive ? leftPanelStart : rightPanelStart;
  auto &filter = isLeftPanelActive ? left_filter : right_filter;
  auto &searchActive =
      isLeftPanelActive ? left_search_active : right_search_active;
  const std::string originalPath = activePath;
  const std::string originalFilter = filter;
  const int originalSelection = selection;
  const size_t originalStart = panelStart;
  const bool originalSearchActive = searchActive;
  filter.clear();
  searchActive = false;
  selection = 0;
  panelStart = 0;
  workModeActive = true;
  refresh_panel(isLeftPanelActive);

  struct WorkFrame {
    std::string path;
    size_t nextIndex;
  };
  std::vector<WorkFrame> frames{{activePath, 1}};
  constexpr std::array<std::chrono::milliseconds, 3> delays{
      std::chrono::milliseconds(1500), std::chrono::milliseconds(2200),
      std::chrono::milliseconds(2800)};
  size_t step = 0;
  auto deadline = std::chrono::steady_clock::now() + delays[step];
  while (true) {
    clear_screen();
    drawPanel(display_path(left_path), display_path(right_path));
    const auto command = read_user_command(deadline);
    if ((command == app_constants::input::command_mouse_click ||
         command == app_constants::input::command_mouse_double_click) &&
        lastMouseRow == 1) {
      workModeActive = false;
      process_mouse_click(false);
      if (done.load())
        break;
      workModeActive = true;
      continue;
    }
    if (command == app_constants::input::command_escape)
      break;
    if (command != app_constants::input::command_work_tick)
      continue;
    deadline = std::chrono::steady_clock::now() +
           delays[++step % delays.size()];

    auto &frame = frames.back();
    if (frame.nextIndex >= files.size()) {
      if (frames.size() == 1) {
        frame.nextIndex = 1;
      } else {
        frames.pop_back();
        activePath = frames.back().path;
        selection = 0;
        panelStart = 0;
        refresh_panel(isLeftPanelActive);
      }
      continue;
    }
    const size_t nextIndex = frame.nextIndex;
    if (static_cast<size_t>(selection) != nextIndex) {
      moveActiveSelection(static_cast<size_t>(selection) < nextIndex ? 1 : -1);
      continue;
    }
    ++frame.nextIndex;
    const auto path = files[nextIndex].path();
    const auto name = path.filename().string();
    if (name.empty() || name.front() == '.')
      continue;
    std::error_code error;
    const auto status = std::filesystem::symlink_status(path, error);
    if (error) {
      std::cerr << "Unable to inspect " << path << ": " << error.message()
                << '\n';
      continue;
    }
    if (std::filesystem::is_directory(status)) {
      frames.push_back({path.string(), 1});
      activePath = path.string();
      selection = 0;
      panelStart = 0;
      refresh_panel(isLeftPanelActive);
    } else if (std::filesystem::is_regular_file(status)) {
      const auto size = std::filesystem::file_size(path, error);
      if (error) {
        std::cerr << "Unable to inspect " << path << ": " << error.message()
                  << '\n';
        continue;
      }
      constexpr std::uintmax_t maximumWorkFileSize = 1024 * 1024;
      bool text = size <= maximumWorkFileSize;
      if (text) {
        std::ifstream input(path, std::ios::binary);
        if (!input) {
          std::cerr << "Unable to read " << path << '\n';
          continue;
        }
        char character;
        while (input.get(character)) {
          const auto byte = static_cast<unsigned char>(character);
          if ((byte < 32 && byte != '\n' && byte != '\r' && byte != '\t') ||
              byte == 127) {
            text = false;
            break;
          }
        }
        if (input.bad()) {
          std::cerr << "Unable to read " << path << '\n';
          continue;
        }
      }
    #ifdef _WIN32
      if (!show_file_viewer(path, true, !text))
        break;
    #else
      if (text && !show_file_viewer(path, true))
        break;
    #endif
      deadline = std::chrono::steady_clock::now() +
             delays[step % delays.size()];
    }
  }
  workModeActive = false;
  activePath = originalPath;
  filter = originalFilter;
  searchActive = originalSearchActive;
  selection = originalSelection;
  panelStart = originalStart;
  refresh_panel(isLeftPanelActive);
}

bool FileManager::process_navigation_input(std::string_view input) {
  if (workModeActive) {
    std::cerr << "Input blocked during read-only operation.\n";
    return true;
  }
  auto &activeFiles = isLeftPanelActive ? left_files : right_files;
  auto &activeMarkedFiles =
      isLeftPanelActive ? left_marked_files : right_marked_files;
  auto &activePath = isLeftPanelActive ? left_path : right_path;
  auto &activeFilter = isLeftPanelActive ? left_filter : right_filter;
  int &activeSelectedIndex =
      isLeftPanelActive ? selectedLeftIndex : selectedRightIndex;

  const std::string_view cmd = input;

  if (cmd == app_constants::input::command_mouse_click ||
      cmd == app_constants::input::command_mouse_double_click)
    return process_mouse_click(
        cmd == app_constants::input::command_mouse_double_click);

  // -------------------------------
  // Handle commands
  // -------------------------------

  if (cmd == app_constants::input::command_f9) {
    show_git_menu(activePath);
    return true;
  }

  if (cmd == app_constants::input::command_work) {
    work_mode();
    return true;
  }

  if (cmd == app_constants::input::command_docker) {
    const std::string container_id = show_docker_menu(isLeftPanelActive);
    if (!container_id.empty()) {
      const auto unique_suffix =
          std::chrono::steady_clock::now().time_since_epoch().count();
      const auto cache_root =
          std::filesystem::temp_directory_path() /
          ("SystemFileManagerX-docker-" + container_id + "-" +
           std::to_string(unique_suffix));
      std::error_code error;
      std::filesystem::create_directories(cache_root, error);
      if (error) {
        std::cerr << "Unable to create Docker browse cache: "
                  << error.message() << '\n';
        return true;
      }

      docker_container_id = container_id;
      docker_cache_root = cache_root;
      activePath = cache_root.string();
      activeFilter.clear();
      auto &searchActive =
          isLeftPanelActive ? left_search_active : right_search_active;
      auto &searchResults =
          isLeftPanelActive ? left_search_results : right_search_results;
      searchActive = false;
      searchResults.clear();
      auto &history = isLeftPanelActive ? left_directory_history
                                        : right_directory_history;
      auto &historyIndex = isLeftPanelActive ? left_history_index
                                             : right_history_index;
      history.resize(historyIndex + 1);
      history.push_back(activePath);
      historyIndex = history.size() - 1;
      activeSelectedIndex = app_constants::ui::first_selected_index;
      refresh_files();
    }
    return true;
  }

  if (cmd == app_constants::input::command_search_files) {
    search_files();
    return true;
  }

  if (cmd == app_constants::input::command_escape) {
    auto &searchActive =
        isLeftPanelActive ? left_search_active : right_search_active;
    auto &searchResults =
        isLeftPanelActive ? left_search_results : right_search_results;
    auto &searchPattern =
        isLeftPanelActive ? left_search_pattern : right_search_pattern;
    auto &activeSelectedIndex =
        isLeftPanelActive ? selectedLeftIndex : selectedRightIndex;
    if (searchActive) {
      searchActive = false;
      searchResults.clear();
      searchPattern.clear();
      activeSelectedIndex = app_constants::ui::first_selected_index;
      refresh_files();
      return true;
    }

    if (!docker_cache_root.empty() &&
        is_docker_cache_path(docker_cache_root, activePath)) {
      auto &history = isLeftPanelActive ? left_directory_history
                                        : right_directory_history;
      auto &historyIndex = isLeftPanelActive ? left_history_index
                                             : right_history_index;
      size_t returnIndex = historyIndex;
      while (returnIndex > 0 &&
             is_docker_cache_path(docker_cache_root, history[returnIndex]))
        --returnIndex;
      activePath = history[returnIndex];
      history.resize(returnIndex + 1);
      historyIndex = returnIndex;
      auto &activeMarkedFiles =
          isLeftPanelActive ? left_marked_files : right_marked_files;
      activeMarkedFiles.clear();
      activeSelectedIndex = app_constants::ui::first_selected_index;
      cleanup_docker_cache();
      refresh_files();
    }
    return true;
  }

  if (cmd == app_constants::input::command_f1) {
    show_help_screen();
    return true;
  }

  if (cmd == app_constants::input::command_toggle_hidden) {
    std::filesystem::path selectedPath;
    const bool hadSelection =
        activeSelectedIndex >= app_constants::ui::first_selected_index &&
        static_cast<size_t>(activeSelectedIndex) < activeFiles.size();
    if (hadSelection)
      selectedPath = activeFiles[activeSelectedIndex].path();

    show_hidden_files = !show_hidden_files;
    refresh_files();
    const auto selected = std::find_if(
        activeFiles.begin(), activeFiles.end(),
        [&selectedPath, hadSelection](const auto &entry) {
          return hadSelection && entry.path() == selectedPath;
        });
    activeSelectedIndex =
        selected == activeFiles.end()
            ? app_constants::ui::first_selected_index
            : static_cast<int>(std::distance(activeFiles.begin(), selected));
    return true;
  }

  if (cmd == app_constants::input::command_filter) {
    const auto filter = read_panel_dialog_prompt(
        isLeftPanelActive, {" FILTER ", "Empty input clears the filter."},
        "Filter: ");
    if (!filter)
      return true;
    activeFilter = *filter;
    activeSelectedIndex = app_constants::ui::first_selected_index;
    refresh_files();
    return true;
  }

  if (cmd == app_constants::input::command_backspace) {
    auto &searchActive =
        isLeftPanelActive ? left_search_active : right_search_active;
    auto &searchResults =
        isLeftPanelActive ? left_search_results : right_search_results;
    if (searchActive) {
      searchActive = false;
      searchResults.clear();
      activeFilter.clear();
      activeSelectedIndex = app_constants::ui::first_selected_index;
      refresh_files();
      return true;
    }
    const std::string nextPath =
        !docker_cache_root.empty() &&
                std::filesystem::path(activePath).lexically_normal() ==
                    docker_cache_root.lexically_normal()
            ? activePath
            : fileSystem->go_back(activePath);
    if (nextPath != activePath) {
      activePath = nextPath;
      auto &history = isLeftPanelActive ? left_directory_history
                                        : right_directory_history;
      auto &historyIndex = isLeftPanelActive ? left_history_index
                                             : right_history_index;
      history.resize(historyIndex + 1);
      history.push_back(activePath);
      historyIndex = history.size() - 1;
    }
    activeFilter.clear();
    refresh_files();
    return true;
  }

  if (cmd == app_constants::input::command_resize)
    return true;

  if (cmd == app_constants::input::command_exit) {
    cleanup_docker_cache();
#ifdef _WIN32
    done.store(true);
    return true;
#else
    tcsetattr(STDIN_FILENO, TCSANOW, &g_orig_termios);
    std::cout << app_constants::terminal::ansi_reset
              << app_constants::terminal::ansi_show_cursor
              << app_constants::terminal::ansi_leave_fullscreen << std::flush;
    std::_Exit(0); // immediately exit without flushing buffers
#endif
  }

  if (cmd == app_constants::input::command_tab) {
    isLeftPanelActive = !isLeftPanelActive;
    return false;
  }

  if (cmd == app_constants::input::command_up) {
    if (isLeftPanelActive) {
      if (selectedLeftIndex > 0)
        selectedLeftIndex--;
    } else {
      if (selectedRightIndex > 0)
        selectedRightIndex--;
    }
    return true;
  }

  if (cmd == app_constants::input::command_down) {
    if (isLeftPanelActive) {
      if (selectedLeftIndex + 1 < activeFiles.size())
        selectedLeftIndex++;
    } else {
      if (selectedRightIndex + 1 < activeFiles.size())
        selectedRightIndex++;
    }
    return true;
  }

  if (cmd == app_constants::input::command_mark ||
      cmd == app_constants::input::command_select_all) {
    std::vector<std::filesystem::path> eligible;
    for (const auto &entry : activeFiles) {
      if (entry.path().filename() != "..")
        eligible.push_back(entry.path());
    }
    if (cmd == app_constants::input::command_select_all) {
      const bool allMarked = !eligible.empty() &&
          std::all_of(eligible.begin(), eligible.end(), [&](const auto &path) {
            return std::find(activeMarkedFiles.begin(), activeMarkedFiles.end(),
                             path) != activeMarkedFiles.end();
          });
      if (allMarked) {
        activeMarkedFiles.clear();
      } else {
        activeMarkedFiles = std::move(eligible);
      }
    } else if (activeSelectedIndex >= 0 &&
               static_cast<size_t>(activeSelectedIndex) < activeFiles.size()) {
      const auto path =
          activeFiles[static_cast<size_t>(activeSelectedIndex)].path();
      if (path.filename() != "..") {
        const auto marked =
            std::find(activeMarkedFiles.begin(), activeMarkedFiles.end(), path);
        if (marked == activeMarkedFiles.end())
          activeMarkedFiles.push_back(path);
        else
          activeMarkedFiles.erase(marked);
      }
    }
    return true;
  }

  if (cmd == app_constants::input::command_home ||
      cmd == app_constants::input::command_end ||
      cmd == app_constants::input::command_page_up ||
      cmd == app_constants::input::command_page_down) {
    if (!activeFiles.empty()) {
      if (cmd == app_constants::input::command_home)
        activeSelectedIndex = 0;
      else if (cmd == app_constants::input::command_end)
        activeSelectedIndex = static_cast<int>(activeFiles.size() - 1);
      else if (cmd == app_constants::input::command_page_up)
        activeSelectedIndex =
            std::max(0, activeSelectedIndex -
                            app_constants::ui::page_navigation_rows);
      else
        activeSelectedIndex = std::min(
            static_cast<int>(activeFiles.size() - 1),
            activeSelectedIndex + app_constants::ui::page_navigation_rows);
    }
    return true;
  }

  if (cmd == app_constants::input::command_history_back ||
      cmd == app_constants::input::command_history_forward) {
    auto &history = isLeftPanelActive ? left_directory_history
                                      : right_directory_history;
    auto &historyIndex = isLeftPanelActive ? left_history_index
                                           : right_history_index;
    if (cmd == app_constants::input::command_history_back &&
        historyIndex > 0)
      --historyIndex;
    else if (cmd == app_constants::input::command_history_forward &&
             historyIndex + 1 < history.size())
      ++historyIndex;
    activePath = history[historyIndex];
    auto &searchActive =
        isLeftPanelActive ? left_search_active : right_search_active;
    auto &searchResults =
        isLeftPanelActive ? left_search_results : right_search_results;
    searchActive = false;
    searchResults.clear();
    activeFilter.clear();
    activeSelectedIndex = 0;
    refresh_files();
    return true;
  }

  if (cmd == app_constants::input::command_enter) { // Enter pressed
    if (activeSelectedIndex >= 0 &&
        static_cast<size_t>(activeSelectedIndex) < activeFiles.size()) {
      const auto selectedPath =
          activeFiles[static_cast<size_t>(activeSelectedIndex)].path();
      auto &searchActive =
          isLeftPanelActive ? left_search_active : right_search_active;
      auto &searchResults =
          isLeftPanelActive ? left_search_results : right_search_results;
      if (searchActive) {
        activePath = selectedPath.parent_path().string();
        searchActive = false;
        searchResults.clear();
        activeFilter.clear();
        auto &history = isLeftPanelActive ? left_directory_history
                                          : right_directory_history;
        auto &historyIndex = isLeftPanelActive ? left_history_index
                                               : right_history_index;
        history.resize(historyIndex + 1);
        if (history.back() != activePath) {
          history.push_back(activePath);
          historyIndex = history.size() - 1;
        }
        refresh_files();
        const auto selected = std::find_if(
            activeFiles.begin(), activeFiles.end(),
            [&selectedPath](const auto &entry) {
              return entry.path() == selectedPath;
            });
        activeSelectedIndex =
            selected == activeFiles.end()
                ? app_constants::ui::first_selected_index
                : static_cast<int>(
                      std::distance(activeFiles.begin(), selected));
        return true;
      }
      std::error_code error;
      if (std::filesystem::is_directory(selectedPath, error) && !error) {
        activePath = std::filesystem::weakly_canonical(selectedPath, error)
                         .string();
        if (error)
          activePath = selectedPath.lexically_normal().string();
        auto &history = isLeftPanelActive ? left_directory_history
                                          : right_directory_history;
        auto &historyIndex = isLeftPanelActive ? left_history_index
                                               : right_history_index;
        history.resize(historyIndex + 1);
        if (history.back() != activePath) {
          history.push_back(activePath);
          historyIndex = history.size() - 1;
        }
        activeFilter.clear();
      } else {
        const bool selectedFromDocker =
            !docker_cache_root.empty() &&
            is_docker_cache_path(docker_cache_root, selectedPath);
        if (selectedFromDocker) {
          if (fetch_docker_file(docker_container_id, docker_cache_root,
                                selectedPath))
            show_file_viewer(selectedPath);
          else
            std::cerr << "Unable to read file from Docker container.\n";
        } else {
          fileSystem->open(selectedPath);
        }
      }
      refresh_files();
    }
    return true;
  }

  const bool activePanelIsDocker =
      !docker_cache_root.empty() &&
      is_docker_cache_path(docker_cache_root, activePath);
  const std::string &otherPanelPath =
      isLeftPanelActive ? right_path : left_path;
  const bool otherPanelIsDocker =
      !docker_cache_root.empty() &&
      is_docker_cache_path(docker_cache_root, otherPanelPath);
  const bool writesToDocker =
      (activePanelIsDocker &&
          (cmd == app_constants::input::command_f2 ||
           cmd == app_constants::input::command_f4 ||
           cmd == app_constants::input::command_f5 ||
           cmd == app_constants::input::command_f6 ||
           cmd == app_constants::input::command_f7 ||
           cmd == app_constants::input::command_f8)) ||
      (otherPanelIsDocker &&
       (cmd == app_constants::input::command_f5 ||
        cmd == app_constants::input::command_f6));
  if (writesToDocker) {
    show_panel_dialog_message(isLeftPanelActive,
                              {" DOCKER ", "Browsing is read-only.",
                               "Press any key to close."});
    return true;
  }

  // Windows-specific keys or other input handling can remain unchanged
  if (cmd == app_constants::input::command_f2) // Rename
  {
    if (!activeFiles.empty()) {
      std::filesystem::path original = activeFiles[selectedIndex()].path();
      const auto newName = read_panel_dialog_prompt(
          isLeftPanelActive,
          {" RENAME ", "Current: " + original.filename().string()},
          "New name: ");
      if (newName && !newName->empty()) {
        fileSystem->rename_file(original, *newName);
        refresh_files();
      }
    }
    return true;
  }

  if (cmd == app_constants::input::command_f3) // View
  {
    if (activeSelectedIndex >= 0 &&
        static_cast<size_t>(activeSelectedIndex) < activeFiles.size()) {
      const auto selected =
          activeFiles[static_cast<size_t>(activeSelectedIndex)].path();
      std::error_code error;
      if (!docker_cache_root.empty() &&
          is_docker_cache_path(docker_cache_root, selected) &&
          !std::filesystem::is_directory(selected, error) && !error &&
          !fetch_docker_file(docker_container_id, docker_cache_root, selected)) {
        std::cerr << "Unable to read file from Docker container.\n";
      } else {
        show_file_viewer(selected);
      }
    }
    return true;
  }

  if (cmd == app_constants::input::command_f4) // Edit
  {
    if (!activeFiles.empty())
      fileSystem->edit_file_content(activeFiles[selectedIndex()].path());
    return true;
  }

  if (cmd == app_constants::input::command_f5) // Copy
  {
    std::vector<std::filesystem::path> sources = activeMarkedFiles;
    if (sources.empty() && activeSelectedIndex >= 0 &&
        static_cast<size_t>(activeSelectedIndex) < activeFiles.size() &&
        activeFiles[static_cast<size_t>(activeSelectedIndex)].path().filename() !=
            "..")
      sources.push_back(
          activeFiles[static_cast<size_t>(activeSelectedIndex)].path());
    std::filesystem::path targetDir =
        isLeftPanelActive ? right_path : left_path;
    bool overwrite = false;
    bool hasConflict = false;
    for (const auto &source : sources) {
      const auto target = targetDir / source.filename();
      std::error_code error;
      if (std::filesystem::exists(target, error) && !error &&
          source.lexically_normal() != target.lexically_normal()) {
        hasConflict = true;
        break;
      }
    }
    if (hasConflict) {
      const auto answer = read_panel_dialog_prompt(
          isLeftPanelActive,
          {" COPY ", "One or more destinations already exist."},
          "Overwrite? [y/N]: ");
      if (!answer)
        return true;
      overwrite = answer && (*answer == "y" || *answer == "Y");
    }

    size_t succeeded = 0;
    size_t skippedOrFailed = 0;
    const PanelDialogGeometry progressGeometry =
        panel_dialog_geometry(isLeftPanelActive);
    for (const auto &source : sources) {
      const auto target = targetDir / source.filename();
      auto progress = [&source, &progressGeometry](std::uintmax_t completed,
                                                   std::uintmax_t total) {
        const auto percent = total == 0
                                 ? 100
                                 : static_cast<int>(
                                       100.0L *
                                       static_cast<long double>(completed) /
                                       static_cast<long double>(total));
        draw_panel_dialog(progressGeometry,
                          {" COPY ", "Copying " +
                                         source.filename().string() + ": " +
                                         std::to_string(percent) + "%"});
      };
      if (fileSystem->copy_file(source, target, overwrite, progress)) {
        ++succeeded;
        activeMarkedFiles.erase(
            std::remove(activeMarkedFiles.begin(), activeMarkedFiles.end(),
                        source),
            activeMarkedFiles.end());
      } else {
        ++skippedOrFailed;
      }
    }
    show_panel_dialog_message(
        isLeftPanelActive,
        {" COPY ", "Copied " + std::to_string(succeeded) + " item(s);",
         std::to_string(skippedOrFailed) + " skipped or failed.",
         "Press any key to return."});
    refresh_files();
    return true;
  }

  if (cmd == app_constants::input::command_f6) // Move
  {
    std::vector<std::filesystem::path> sources = activeMarkedFiles;
    if (sources.empty() && activeSelectedIndex >= 0 &&
        static_cast<size_t>(activeSelectedIndex) < activeFiles.size() &&
        activeFiles[static_cast<size_t>(activeSelectedIndex)].path().filename() !=
            "..")
      sources.push_back(
          activeFiles[static_cast<size_t>(activeSelectedIndex)].path());
    std::filesystem::path targetDir =
        isLeftPanelActive ? right_path : left_path;
    bool overwrite = false;
    bool hasConflict = false;
    for (const auto &source : sources) {
      const auto target = targetDir / source.filename();
      std::error_code error;
      if (std::filesystem::exists(target, error) && !error &&
          source.lexically_normal() != target.lexically_normal()) {
        hasConflict = true;
        break;
      }
    }
    if (hasConflict) {
      const auto answer = read_panel_dialog_prompt(
          isLeftPanelActive,
          {" MOVE ", "One or more destinations already exist."},
          "Overwrite? [y/N]: ");
      if (!answer)
        return true;
      overwrite = answer && (*answer == "y" || *answer == "Y");
    }

    size_t succeeded = 0;
    size_t skippedOrFailed = 0;
    const PanelDialogGeometry progressGeometry =
        panel_dialog_geometry(isLeftPanelActive);
    for (const auto &source : sources) {
      const auto target = targetDir / source.filename();
      auto progress = [&source, &progressGeometry](std::uintmax_t completed,
                                                   std::uintmax_t total) {
        const auto percent = total == 0
                                 ? 100
                                 : static_cast<int>(
                                       100.0L *
                                       static_cast<long double>(completed) /
                                       static_cast<long double>(total));
        draw_panel_dialog(progressGeometry,
                          {" MOVE ", "Moving " +
                                         source.filename().string() + ": " +
                                         std::to_string(percent) + "%"});
      };
      if (fileSystem->move_file(source, target, overwrite, progress)) {
        ++succeeded;
        activeMarkedFiles.erase(
            std::remove(activeMarkedFiles.begin(), activeMarkedFiles.end(),
                        source),
            activeMarkedFiles.end());
      } else {
        ++skippedOrFailed;
      }
    }
    show_panel_dialog_message(
        isLeftPanelActive,
        {" MOVE ", "Moved " + std::to_string(succeeded) + " item(s);",
         std::to_string(skippedOrFailed) + " skipped or failed.",
         "Press any key to return."});
    refresh_files();
    return true;
  }

  if (cmd == app_constants::input::command_f7) // New Folder
  {
    const auto folderName = read_panel_dialog_prompt(
        isLeftPanelActive, {" NEW FOLDER "}, "Folder name: ");
    if (folderName && !folderName->empty())
      fileSystem->create_folder(activePath, *folderName);

    refresh_files();
    return true;
  }

  if (cmd == app_constants::input::command_f8) // Delete
  {
    std::vector<std::filesystem::path> targets = activeMarkedFiles;
    if (targets.empty() && activeSelectedIndex >= 0 &&
        static_cast<size_t>(activeSelectedIndex) < activeFiles.size() &&
        activeFiles[static_cast<size_t>(activeSelectedIndex)].path().filename() !=
            "..")
      targets.push_back(
          activeFiles[static_cast<size_t>(activeSelectedIndex)].path());
    if (targets.empty())
      return true;

    const auto confirmation = read_panel_dialog_prompt(
        isLeftPanelActive,
        {" DELETE ", "Delete " + std::to_string(targets.size()) +
                         " item(s)?"},
        "Delete? [y/N]: ");

    size_t deleted = 0;
    if (confirmation &&
        (*confirmation == "y" || *confirmation == "Y")) {
      for (const auto &target : targets) {
        if (fileSystem->delete_file(target)) {
          ++deleted;
          activeMarkedFiles.erase(
              std::remove(activeMarkedFiles.begin(), activeMarkedFiles.end(),
                          target),
              activeMarkedFiles.end());
        }
      }
    } else {
      show_panel_dialog_message(
          isLeftPanelActive,
          {" DELETE ", std::string(app_constants::ui::message_delete_cancelled),
           "Press any key to return."});
    }

    if (deleted > 0) {
      show_panel_dialog_message(
          isLeftPanelActive,
          {" DELETE ", "Deleted " + std::to_string(deleted) + " item(s).",
           "Press any key to return."});
    }
    refresh_files();
    return true;
  }

  return false;
}

int FileManager::selectedIndex() const {
  return isLeftPanelActive ? selectedLeftIndex : selectedRightIndex;
}

bool FileManager::process_mouse_click(bool doubleClick) {
  const auto [width, height] = terminal().dimensions();
  const int terminalHeight = height;
  const int totalWidth = std::max(
      app_constants::terminal::minimum_panel_content_width,
      width - app_constants::terminal::panel_row_horizontal_padding);
  const int visibleRows = std::max(
      app_constants::terminal::minimum_visible_file_rows,
      height - app_constants::terminal::screen_rows_reserved);
  constexpr int separatorWidth =
      app_constants::terminal::panel_separator_width;
  const int leftPanelWidth =
      (totalWidth - separatorWidth) /
      app_constants::ui::panel_split_divisor;
  const int rowContentWidth =
      totalWidth - app_constants::ui::right_panel_extra_trailing_space;
  const int rightPanelWidth =
      rowContentWidth - separatorWidth - leftPanelWidth;
  const int footerContentWidth =
      totalWidth - app_constants::terminal::row_border_padding;
  const std::string fullMenu(app_constants::ui::menu_footer);
  const bool footerUsesTwoLines =
      fullMenu.size() > static_cast<size_t>(footerContentWidth);
  if (footerUsesTwoLines) {
    previousClickTime = {};
    previouslyClickedEntry.reset();
    return true;
  }

  const int clickColumn = lastMouseColumn;
  const int topRightColumn =
      totalWidth +
      app_constants::terminal::panel_border_horizontal_padding + 2;
  if (lastMouseRow == 1 && is_close_button_column(clickColumn, topRightColumn))
    return process_navigation_input(app_constants::input::command_exit);

  if (doubleClick && lastMouseRow == 2) {
    const int leftPanelFirstColumn = 3;
    const int rightPanelFirstColumn =
        leftPanelFirstColumn + leftPanelWidth + separatorWidth;
    const bool clickedLeftPanel =
        clickColumn >= leftPanelFirstColumn &&
        clickColumn < leftPanelFirstColumn + leftPanelWidth;
    const bool clickedRightPanel =
        clickColumn >= rightPanelFirstColumn &&
        clickColumn < rightPanelFirstColumn + rightPanelWidth;
    if (clickedLeftPanel || clickedRightPanel) {
      isLeftPanelActive = clickedLeftPanel;
      const std::string_view label = clickedLeftPanel
                                         ? app_constants::ui::left_panel_label
                                         : app_constants::ui::right_panel_label;
      const int panelFirstColumn =
          clickedLeftPanel ? leftPanelFirstColumn : rightPanelFirstColumn;
      const int panelWidth =
          clickedLeftPanel ? leftPanelWidth : rightPanelWidth;
      const size_t labelWidth = label.size();
      const int pathClickOffset =
          clickColumn - panelFirstColumn - static_cast<int>(labelWidth);
      auto &activePath = clickedLeftPanel ? left_path : right_path;
      if (pathClickOffset >= 0 &&
          static_cast<size_t>(pathClickOffset) <
              static_cast<size_t>(panelWidth) - labelWidth &&
          (docker_cache_root.empty() ||
           !is_docker_cache_path(docker_cache_root, activePath))) {
        const std::string pathText = activePath;
        const size_t visiblePathWidth =
            static_cast<size_t>(panelWidth) - labelWidth;
        const bool truncated = pathText.size() > visiblePathWidth;
        const size_t truncationWidth =
            truncated
                ? static_cast<size_t>(app_constants::terminal::
                                          path_ellipsis_length)
                : 0;
        if (!truncated ||
            static_cast<size_t>(pathClickOffset) >= truncationWidth) {
          const size_t originalOffset =
              truncated
                  ? pathText.size() -
                        (visiblePathWidth - truncationWidth) +
                        static_cast<size_t>(pathClickOffset) -
                        truncationWidth
                  : static_cast<size_t>(pathClickOffset);
          size_t componentStart = 0;
          while (componentStart < pathText.size() &&
                 (pathText[componentStart] == '/' ||
                  pathText[componentStart] == '\\'))
            ++componentStart;
          while (componentStart < pathText.size()) {
            size_t componentEnd = componentStart;
            while (componentEnd < pathText.size() &&
                   pathText[componentEnd] != '/' &&
                   pathText[componentEnd] != '\\')
              ++componentEnd;
            if (originalOffset >= componentStart &&
                originalOffset < componentEnd) {
              std::error_code error;
              const std::filesystem::path target =
                  std::filesystem::weakly_canonical(
                      std::filesystem::path(
                          pathText.substr(0, componentEnd)),
                      error);
              const bool isDirectory =
                  !error && std::filesystem::is_directory(target, error);
              if (!error && isDirectory) {
                activePath = target.string();
                auto &activeFilter =
                    clickedLeftPanel ? left_filter : right_filter;
                auto &searchActive =
                    clickedLeftPanel ? left_search_active : right_search_active;
                auto &searchResults =
                    clickedLeftPanel ? left_search_results
                                     : right_search_results;
                auto &searchPattern =
                    clickedLeftPanel ? left_search_pattern
                                     : right_search_pattern;
                auto &history =
                    clickedLeftPanel ? left_directory_history
                                     : right_directory_history;
                auto &historyIndex =
                    clickedLeftPanel ? left_history_index : right_history_index;
                activeFilter.clear();
                searchActive = false;
                searchResults.clear();
                searchPattern.clear();
                int &selection =
                    clickedLeftPanel ? selectedLeftIndex : selectedRightIndex;
                selection = app_constants::ui::first_selected_index;
                history.resize(historyIndex + 1);
                if (history.back() != activePath) {
                  history.push_back(activePath);
                  historyIndex = history.size() - 1;
                }
                refresh_files();
              }
              break;
            }
            componentStart = componentEnd + 1;
            while (componentStart < pathText.size() &&
                   (pathText[componentStart] == '/' ||
                    pathText[componentStart] == '\\'))
              ++componentStart;
          }
        }
      }
      return true;
    }
  }
  std::vector<std::string> footerLines;
  footerLines.push_back(fullMenu);
  const int firstFooterRow = visibleRows + 6;
  if (doubleClick && lastMouseRow >= firstFooterRow &&
      lastMouseRow < firstFooterRow + static_cast<int>(footerLines.size()) &&
      clickColumn >= 3) {
    const std::string &footer =
        footerLines[static_cast<size_t>(lastMouseRow - firstFooterRow)];
    const size_t clickedOffset = static_cast<size_t>(clickColumn - 3);
    size_t segmentStart = 0;
    while (segmentStart < footer.size()) {
      const size_t separator = footer.find('|', segmentStart);
      const size_t segmentEnd =
          separator == std::string::npos ? footer.size() : separator;
      if (clickedOffset >= segmentStart && clickedOffset < segmentEnd) {
        const std::string_view segment(footer.data() + segmentStart,
                                       segmentEnd - segmentStart);
        const size_t first = segment.find_first_not_of(' ');
        if (first == std::string_view::npos)
          return true;
        const std::string_view option = segment.substr(first);
        if (option.rfind("F1", 0) == 0)
          return process_navigation_input(app_constants::input::command_f1);
        if (option.rfind("F2", 0) == 0)
          return process_navigation_input(app_constants::input::command_f2);
        if (option.rfind("F3", 0) == 0)
          return process_navigation_input(app_constants::input::command_f3);
        if (option.rfind("F4", 0) == 0)
          return process_navigation_input(app_constants::input::command_f4);
        if (option.rfind("F5", 0) == 0)
          return process_navigation_input(app_constants::input::command_f5);
        if (option.rfind("F6", 0) == 0)
          return process_navigation_input(app_constants::input::command_f6);
        if (option.rfind("F7", 0) == 0)
          return process_navigation_input(app_constants::input::command_f7);
        if (option.rfind("F8", 0) == 0)
          return process_navigation_input(app_constants::input::command_f8);
        if (option.rfind("F9", 0) == 0)
          return process_navigation_input(app_constants::input::command_f9);
        if (option.rfind("Alt+D", 0) == 0)
          return process_navigation_input(app_constants::input::command_docker);
        if (option.rfind("Alt+F", 0) == 0)
          return process_navigation_input(
              app_constants::input::command_search_files);
        if (option.rfind("Alt+W", 0) == 0)
          return process_navigation_input(app_constants::input::command_work);
        if (option.rfind("Tab", 0) == 0)
          return process_navigation_input(app_constants::input::command_tab);
        if (option.rfind("Alt+X", 0) == 0)
          return process_navigation_input(app_constants::input::command_exit);
        return true;
      }
      if (separator == std::string::npos)
        break;
      segmentStart = separator + 1;
    }
  }

  const int leftPanelFirstColumn = 3;
  const int rightPanelFirstColumn =
      leftPanelFirstColumn + leftPanelWidth + separatorWidth;
  if (lastMouseRow < 5 ||
      lastMouseRow >= 5 + visibleRows)
    return true;

  bool clickedLeftPanel = false;
  if (clickColumn >= leftPanelFirstColumn &&
      clickColumn < leftPanelFirstColumn + leftPanelWidth) {
    clickedLeftPanel = true;
  } else if (clickColumn < rightPanelFirstColumn ||
             clickColumn >= rightPanelFirstColumn + rightPanelWidth) {
    return true;
  }
  isLeftPanelActive = clickedLeftPanel;

  auto &files = clickedLeftPanel ? left_files : right_files;
  int &selection = clickedLeftPanel ? selectedLeftIndex : selectedRightIndex;
  const size_t start = clickedLeftPanel ? leftPanelStart : rightPanelStart;
  const size_t index =
      start + static_cast<size_t>(lastMouseRow - 5);
  if (index >= files.size()) {
    previouslyClickedEntry.reset();
    return true;
  }
  const auto &clickedEntry = files[index].path();
  const bool sameEntry = previouslyClickedEntry &&
                         previouslyClickedLeftPanel == clickedLeftPanel &&
                         *previouslyClickedEntry == clickedEntry;
  selection = static_cast<int>(index);
  if (doubleClick) {
    previouslyClickedEntry.reset();
    if (!sameEntry)
      return true;
    return process_navigation_input(app_constants::input::command_enter);
  }
  previouslyClickedEntry = clickedEntry;
  previouslyClickedLeftPanel = clickedLeftPanel;
  return true;
}

int FileManager::process_file_selection_input(std::string_view input) {
  if (workModeActive) {
    std::cerr << "Selection blocked during read-only operation.\n";
    return -1;
  }
  int index = 0;
  try {
    index = std::stoi(std::string(input));

    if (isLeftPanelActive) {
      if (index >= 0 && index < static_cast<int>(left_files.size())) {
        const auto selected = left_files[static_cast<size_t>(index)].path();
        std::error_code error;
        if (std::filesystem::is_directory(selected, error) && !error) {
          if (!docker_cache_root.empty() &&
              std::filesystem::path(left_path).lexically_normal() ==
                  docker_cache_root.lexically_normal() &&
              selected.filename() == "..")
            return index;
          left_path = std::filesystem::weakly_canonical(selected, error).string();
          if (error)
            left_path = selected.lexically_normal().string();
          left_filter.clear();
          left_directory_history.resize(left_history_index + 1);
          if (left_directory_history.back() != left_path) {
            left_directory_history.push_back(left_path);
            left_history_index = left_directory_history.size() - 1;
          }
        } else {
          if (!docker_cache_root.empty() &&
              is_docker_cache_path(docker_cache_root, selected)) {
            if (fetch_docker_file(docker_container_id, docker_cache_root,
                                  selected))
              show_file_viewer(selected);
            else
              std::cerr << "Unable to read file from Docker container.\n";
          } else {
            fileSystem->open(selected);
          }
        }
      } else {
        // Do nothing;
      }
    } else {
      if (index >= 0 && index < static_cast<int>(right_files.size())) {
        const auto selected = right_files[static_cast<size_t>(index)].path();
        std::error_code error;
        if (std::filesystem::is_directory(selected, error) && !error) {
          if (!docker_cache_root.empty() &&
              std::filesystem::path(right_path).lexically_normal() ==
                  docker_cache_root.lexically_normal() &&
              selected.filename() == "..")
            return index;
          right_path =
              std::filesystem::weakly_canonical(selected, error).string();
          if (error)
            right_path = selected.lexically_normal().string();
          right_filter.clear();
          right_directory_history.resize(right_history_index + 1);
          if (right_directory_history.back() != right_path) {
            right_directory_history.push_back(right_path);
            right_history_index = right_directory_history.size() - 1;
          }
        } else {
          if (!docker_cache_root.empty() &&
              is_docker_cache_path(docker_cache_root, selected)) {
            if (fetch_docker_file(docker_container_id, docker_cache_root,
                                  selected))
              show_file_viewer(selected);
            else
              std::cerr << "Unable to read file from Docker container.\n";
          } else {
            fileSystem->open(selected);
          }
        }
      } else {
        // Do nothing;
      }
    }
  } catch (...) {
    // done.store(true);
    // Do nothing;
  }
  return index;
}
