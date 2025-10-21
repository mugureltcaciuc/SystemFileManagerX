#pragma once

#include <string_view>

namespace linux_file_system_constants {
constexpr char path_separator = '/';
constexpr std::string_view clear_command = "clear";
constexpr std::string_view open_command_prefix = "xdg-open \"";
constexpr std::string_view editor_command_prefix = "nano \"";
constexpr std::string_view command_quote = "\"";
constexpr std::string_view invalid_rename_name =
    "Rename failed: name cannot be empty or contain '/'.\n";
constexpr std::string_view view_header_prefix = "\n--- Viewing: ";
constexpr std::string_view view_header_suffix = " ---\n";
constexpr std::string_view view_footer = "\n--- End of file ---\n";
constexpr std::string_view view_continue = "Press Enter to go back...\n";
} // namespace linux_file_system_constants
