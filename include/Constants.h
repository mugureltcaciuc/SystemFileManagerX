#pragma once

#include <string_view>

namespace app_constants {
namespace ui {
inline constexpr int first_selected_index = 0;
} // namespace ui

namespace terminal {
inline constexpr std::string_view ansi_reset = "\033[0m";
inline constexpr std::string_view ansi_panel_colors = "\033[30;47m";
inline constexpr std::string_view ansi_enter_fullscreen = "\033[?1049h";
inline constexpr std::string_view ansi_leave_fullscreen = "\033[?1049l";
inline constexpr std::string_view ansi_hide_cursor = "\033[?25l";
inline constexpr std::string_view ansi_show_cursor = "\033[?25h";
inline constexpr std::string_view ansi_enable_mouse_reporting =
    "\033[?1000h\033[?1006h";
inline constexpr std::string_view ansi_disable_mouse_reporting =
    "\033[?1006l\033[?1000l";
inline constexpr std::string_view ansi_clear_and_home = "\033[2J\033[H";
inline constexpr std::string_view ansi_selected_row = "\033[30;107m";
} // namespace terminal

namespace filesystem {
inline constexpr std::string_view copy_failed = "Copy failed: ";
inline constexpr std::string_view move_failed = "Move failed: ";
inline constexpr std::string_view delete_failed = "Delete failed: ";
inline constexpr std::string_view create_folder_failed = "Create folder failed: ";
inline constexpr std::string_view read_directory_failed =
    "Error reading directory: ";
inline constexpr std::string_view view_file_failed =
    "Failed to open file for viewing: ";
inline constexpr std::string_view file_missing = "File does not exist: ";
inline constexpr std::string_view rename_success = "Renamed to: ";
} // namespace filesystem
} // namespace app_constants
