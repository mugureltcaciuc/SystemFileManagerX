#include <atomic>
#include <array>
#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <string>
#include <thread>
#include <vector>

#include "Constants.h"
#include "FileManager.h"
#include "FileSystemFactory.h"
#include "Globals.h"
#include "MainCharConstants.h"
#include "OsUtils.h"

#ifdef _WIN32
#include "WindowsFileSystem.h"

#include <conio.h>
#include <windows.h>

#pragma comment(lib, "User32.lib")
#else
#include "LinuxFileSystem.h"

#include <fcntl.h>
#include <sys/select.h>
#include <termios.h>
#include <unistd.h>
#endif

using namespace std;

namespace main_constants {
constexpr int key_release_poll_ms = 10;
constexpr int key_monitor_poll_ms = 50;
constexpr int nonblocking_select_timeout_seconds = 0;
constexpr int nonblocking_select_timeout_microseconds = 0;
constexpr int escape_ascii_code = 27;
constexpr size_t arrow_sequence_length = 3;
constexpr unsigned int arrow_monitor_delay_microseconds = 100000;
constexpr int successful_exit_code = 0;
} // namespace main_constants

void monitor_down_key(std::atomic<bool> &flag) {
  bool pressed_once = false;
#ifdef _WIN32
  while (!done.load()) {
    if (GetAsyncKeyState(VK_DOWN) & 0x8000) {
      if (!pressed_once) {
        flag.store(true);
        pressed_once = true;
        while (GetAsyncKeyState(VK_DOWN) & 0x8000) {
          std::this_thread::sleep_for(
              std::chrono::milliseconds(main_constants::key_release_poll_ms));
        }
      }
    } else {
      pressed_once = false;
    }
    std::this_thread::sleep_for(
        std::chrono::milliseconds(main_constants::key_monitor_poll_ms));
  }

#else
  struct termios oldt{}, newt{};
  tcgetattr(STDIN_FILENO, &oldt);
  newt = oldt;
  newt.c_lflag &= ~(ICANON | ECHO);
  tcsetattr(STDIN_FILENO, TCSANOW, &newt);

  int oldf = fcntl(STDIN_FILENO, F_GETFL);
  fcntl(STDIN_FILENO, F_SETFL, oldf | O_NONBLOCK);

  while (flag) {
    std::array<char, main_constants::arrow_sequence_length> buf{};
    const auto bytes_read = read(STDIN_FILENO, buf.data(), buf.size());
    if (bytes_read > 0) {
      if (buf[0] == main_constants::escape_ascii_code &&
          buf[1] == main_constants::escape_sequence_separator &&
          buf[2] == main_constants::down_arrow_code) {
        flag = false;
        break;
      }
    }
    usleep(main_constants::arrow_monitor_delay_microseconds);
  }

  tcsetattr(STDIN_FILENO, TCSANOW, &oldt);
  fcntl(STDIN_FILENO, F_SETFL, oldf);
#endif
}

void monitor_up_key(std::atomic<bool> &flag) {
#ifndef _WIN32
  bool pressed_once = false;

  // Save original terminal settings
  struct termios orig_termios{};
  tcgetattr(STDIN_FILENO, &orig_termios);

  // Set terminal to raw mode (non-canonical, no echo)
  struct termios raw = orig_termios;
  raw.c_lflag &= ~(ICANON | ECHO);
  tcsetattr(STDIN_FILENO, TCSANOW, &raw);

  // Make stdin non-blocking
  int old_flags = fcntl(STDIN_FILENO, F_GETFL);
  fcntl(STDIN_FILENO, F_SETFL, old_flags | O_NONBLOCK);

  while (!done.load()) {
    std::array<char, main_constants::arrow_sequence_length> buf{};
    const auto bytes_read = read(STDIN_FILENO, buf.data(), buf.size());
    if (bytes_read > 0) {
      // Detect arrow up: ESC [ A
      if (bytes_read >=
              static_cast<ssize_t>(main_constants::arrow_sequence_length) &&
          buf[0] == main_constants::escape_ascii_code &&
          buf[1] == main_constants::escape_sequence_separator &&
          buf[2] == main_constants::up_arrow_code) {
        if (!pressed_once) {
          flag.store(true);
          pressed_once = true;
        }
      } else {
        pressed_once = false; // reset when other keys pressed
      }
    }

    std::this_thread::sleep_for(
        std::chrono::milliseconds(main_constants::key_monitor_poll_ms));
  }

  // Restore original terminal settings
  tcsetattr(STDIN_FILENO, TCSANOW, &orig_termios);
  fcntl(STDIN_FILENO, F_SETFL, old_flags);
#endif
}

int main() {
#ifndef _WIN32
  tcgetattr(STDIN_FILENO, &g_orig_termios);
  struct termios raw_termios = g_orig_termios;
  raw_termios.c_lflag &= ~(ICANON | ECHO);
  tcsetattr(STDIN_FILENO, TCSANOW, &raw_termios);
  std::cout << app_constants::terminal::ansi_enter_fullscreen
            << app_constants::terminal::ansi_hide_cursor << std::flush;
#endif

#ifdef _WIN32
  const HANDLE outputHandle = GetStdHandle(STD_OUTPUT_HANDLE);
  const HWND consoleWindow = GetConsoleWindow();
  CONSOLE_SCREEN_BUFFER_INFO originalConsoleInfo{};
  const bool hasOriginalConsoleInfo =
      GetConsoleScreenBufferInfo(outputHandle, &originalConsoleInfo);
  CONSOLE_CURSOR_INFO originalCursorInfo{};
  const bool hasOriginalCursorInfo =
      GetConsoleCursorInfo(outputHandle, &originalCursorInfo);
  WINDOWPLACEMENT originalWindowPlacement{};
  originalWindowPlacement.length = sizeof(originalWindowPlacement);
  const bool hasOriginalWindowPlacement =
      consoleWindow != nullptr &&
      GetWindowPlacement(consoleWindow, &originalWindowPlacement);
  const UINT originalOutputCodePage = GetConsoleOutputCP();
  const bool hasOriginalOutputCodePage = originalOutputCodePage != 0;
  if (consoleWindow != nullptr)
    ShowWindow(consoleWindow, SW_MAXIMIZE);
  SetConsoleOutputCP(CP_UTF8);

  // Start key monitoring threads
  std::thread down_thread(monitor_down_key, std::ref(down_pressed));
  std::thread up_thread(monitor_up_key, std::ref(up_pressed));
#endif

  tab_pressed.store(false);

  std::string start_path = std::filesystem::current_path().string();
  auto fileSystem = FileSystemFactory::create(start_path, start_path);
  FileManager manager(std::move(fileSystem), start_path);
  manager.setActivePanelLeft(true);

  while (!done.load()) {
    manager.run();

    if (tab_pressed.load()) {
      tab_pressed.store(false);
    }

    if (down_pressed.load()) {
      manager.moveActiveSelection(1);
      down_pressed.store(false);
    }

    if (up_pressed.load()) {
      manager.moveActiveSelection(-1);
      up_pressed.store(false);
    }
  }

#ifdef _WIN32
  done.store(true);

  // Join threads
  for (auto &t : {&down_thread, &up_thread}) {
    if (t->joinable())
      t->join();
  }
  std::cout.flush();
  if (hasOriginalWindowPlacement)
    SetWindowPlacement(consoleWindow, &originalWindowPlacement);
  if (hasOriginalConsoleInfo)
    SetConsoleTextAttribute(outputHandle, originalConsoleInfo.wAttributes);
  if (hasOriginalCursorInfo)
    SetConsoleCursorInfo(outputHandle, &originalCursorInfo);
  std::system("cls");
  if (hasOriginalOutputCodePage)
    SetConsoleOutputCP(originalOutputCodePage);
#endif

  return main_constants::successful_exit_code;
}
