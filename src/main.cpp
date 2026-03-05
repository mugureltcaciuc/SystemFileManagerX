#include <atomic>
#include <chrono>

#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <string>
#include <thread>
#include <vector>

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

#include "FileManager.h"
#include "FileSystemFactory.h"

#include "Globals.h"

using namespace std;

void monitor_escape_key(std::atomic<bool> &flag) {
  bool pressed_once = false;
#ifdef _WIN32
  while (!done.load()) {
    if (GetAsyncKeyState(VK_ESCAPE) & 0x8000) {
      if (!pressed_once) {
        flag.store(true);
        pressed_once = true;
        while (GetAsyncKeyState(VK_ESCAPE) & 0x8000) {
          std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
      }
    } else {
      pressed_once = false;
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
  }
#else

  // Save original terminal settings
  struct termios orig_termios;
  tcgetattr(STDIN_FILENO, &orig_termios);

  // Set terminal to raw mode (non-canonical, no echo)
  struct termios raw = orig_termios;
  raw.c_lflag &= ~(ICANON | ECHO);
  tcsetattr(STDIN_FILENO, TCSANOW, &raw);

  // Make stdin non-blocking
  int old_flags = fcntl(STDIN_FILENO, F_GETFL);
  fcntl(STDIN_FILENO, F_SETFL, old_flags | O_NONBLOCK);

  while (!done.load()) {
    fd_set readfds;
    FD_ZERO(&readfds);
    FD_SET(STDIN_FILENO, &readfds);

    struct timeval tv;
    tv.tv_sec = 0;
    tv.tv_usec = 0;

    int ret = select(STDIN_FILENO + 1, &readfds, nullptr, nullptr, &tv);
    if (ret > 0 && FD_ISSET(STDIN_FILENO, &readfds)) {
      char ch;
      if (read(STDIN_FILENO, &ch, 1) > 0) {
        if (ch == 27) // ESC key ASCII code
        {
          if (!pressed_once) {
            flag.store(true);
            pressed_once = true;
          }
        } else {
          pressed_once = false;
        }
      }
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
  }

  // Restore original terminal settings
  tcsetattr(STDIN_FILENO, TCSANOW, &orig_termios);

#endif
}

void monitor_down_key(std::atomic<bool> &flag) {
  bool pressed_once = false;
#ifdef _WIN32
  while (!done.load()) {
    if (GetAsyncKeyState(VK_DOWN) & 0x8000) {
      if (!pressed_once) {
        flag.store(true);
        pressed_once = true;
        while (GetAsyncKeyState(VK_DOWN) & 0x8000) {
          std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
      }
    } else {
      pressed_once = false;
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
  }

#else
  struct termios oldt, newt;
  tcgetattr(STDIN_FILENO, &oldt);
  newt = oldt;
  newt.c_lflag &= ~(ICANON | ECHO);
  tcsetattr(STDIN_FILENO, TCSANOW, &newt);

  int oldf = fcntl(STDIN_FILENO, F_GETFL);
  fcntl(STDIN_FILENO, F_SETFL, oldf | O_NONBLOCK);

  while (flag) {
    char buf[3];
    if (read(STDIN_FILENO, buf, sizeof(buf)) > 0) {
      if (buf[0] == 27 && buf[1] == '[' && buf[2] == 'B') { // Down arrow
        flag = false;
        break;
      }
    }
    usleep(100000);
  }

  tcsetattr(STDIN_FILENO, TCSANOW, &oldt);
  fcntl(STDIN_FILENO, F_SETFL, oldf);
#endif
}

void monitor_up_key(std::atomic<bool> &flag) {
#ifndef _WIN32
  bool pressed_once = false;

  // Save original terminal settings
  struct termios orig_termios;
  tcgetattr(STDIN_FILENO, &orig_termios);

  // Set terminal to raw mode (non-canonical, no echo)
  struct termios raw = orig_termios;
  raw.c_lflag &= ~(ICANON | ECHO);
  tcsetattr(STDIN_FILENO, TCSANOW, &raw);

  // Make stdin non-blocking
  int old_flags = fcntl(STDIN_FILENO, F_GETFL);
  fcntl(STDIN_FILENO, F_SETFL, old_flags | O_NONBLOCK);

  while (!done.load()) {
    char buf[3];
    int n = read(STDIN_FILENO, buf, sizeof(buf));
    if (n > 0) {
      // Detect arrow up: ESC [ A
      if (n >= 3 && buf[0] == 27 && buf[1] == '[' && buf[2] == 'A') {
        if (!pressed_once) {
          flag.store(true);
          pressed_once = true;
        }
      } else {
        pressed_once = false; // reset when other keys pressed
      }
    }

    std::this_thread::sleep_for(std::chrono::milliseconds(50));
  }

  // Restore original terminal settings
  tcsetattr(STDIN_FILENO, TCSANOW, &orig_termios);
  fcntl(STDIN_FILENO, F_SETFL, old_flags);
#endif
}

int main() {
#ifndef _WIN32
  tcgetattr(STDIN_FILENO, &g_orig_termios);
#endif

  // Start key monitoring threads
  std::thread esc_thread(monitor_escape_key, std::ref(esc_pressed));
  std::thread down_thread(monitor_down_key, std::ref(down_pressed));
  std::thread up_thread(monitor_up_key, std::ref(up_pressed));

  tab_pressed.store(false);
  esc_pressed.store(false);

  std::string start_path = std::filesystem::current_path().string();
  auto fileSystem = FileSystemFactory::create(start_path, start_path);
  FileManager manager(std::move(fileSystem), start_path);
  manager.isLeftPanelActive = true;

  std::cout << "Running (press ESC to quit)..." << std::endl;

  while (!esc_pressed.load()) {
    manager.run();

    if (tab_pressed.load()) {
      tab_pressed.store(false);
    }

    if (down_pressed.load()) {
      if (manager.isLeftPanelActive) {
        manager.selectedLeftIndex++;
      } else {
        manager.selectedRightIndex++;
      }
      down_pressed.store(false);
    }

    if (up_pressed.load()) {
      std::cout << "!!! up_pressed" << std::endl;
      if (manager.isLeftPanelActive) {
        if (manager.selectedLeftIndex > 0) {
          manager.selectedLeftIndex--;
        }
      } else {
        if (manager.selectedRightIndex > 0) {
          manager.selectedRightIndex--;
        }
      }

      up_pressed.store(false);
    }
  }

  done.store(true);

  // Join threads
  for (auto &t : {&esc_thread, &down_thread, &up_thread}) {
    if (t->joinable())
      t->join();
  }

  return 0;
}
