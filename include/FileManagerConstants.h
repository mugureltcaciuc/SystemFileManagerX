#pragma once

#include <array>
#include <string_view>

namespace app_constants {
namespace terminal {
constexpr int fallback_columns = 80;
constexpr int fallback_rows = 24;
constexpr int minimum_panel_content_width = 16;
constexpr int panel_separator_width = 4;
constexpr int panel_border_horizontal_padding = 2;
constexpr int panel_row_horizontal_padding = 4;
constexpr int row_border_padding = 1;
constexpr int screen_rows_reserved = 8;
constexpr int minimum_visible_file_rows = 1;
constexpr int resize_input_timeout_ms = 100;
constexpr int pending_input_timeout_ms = 50;
constexpr int microseconds_per_millisecond = 1000;
constexpr int poll_timeout_ms = 0;
constexpr int bytes_per_character = 1;
constexpr int select_no_ready = 0;
constexpr int no_dimensions = 0;
constexpr int state_not_requested = 0;
constexpr int path_ellipsis_length = 3;
constexpr char border_corner = '+';
constexpr char border_top = '=';
constexpr char border_middle = '-';
constexpr char border_bottom = '=';
constexpr char border_vertical = '|';
constexpr char padding = ' ';
constexpr char ellipsis = '.';
constexpr std::string_view terminal_erase_character = "\b \b";
constexpr std::string_view panel_separator = " || ";
} // namespace terminal

namespace input {
constexpr int escape = 27;
constexpr int enter = 10;
constexpr int carriage_return = 13;
constexpr int backspace = 127;
constexpr int delete_backspace = 8;
constexpr int tab = 9;
constexpr int printable_first = 32;
constexpr int printable_last = 126;
constexpr int linux_arrow_up = 'A';
constexpr int linux_arrow_down = 'B';
constexpr int linux_sequence_csi = '[';
constexpr int linux_sequence_ss3 = 'O';
constexpr int linux_sequence_function_prefix = '1';
constexpr int linux_function_key_2 = '2';
constexpr int linux_function_key_3 = '3';
constexpr int linux_function_key_4 = '4';
constexpr int linux_function_key_5 = '5';
constexpr int linux_function_key_6 = '7';
constexpr int linux_function_key_7 = '8';
constexpr int linux_function_key_8 = '9';
constexpr int linux_function_key_9_first = '2';
constexpr int linux_function_key_9_second = '0';
constexpr int linux_sequence_terminator = '~';
constexpr int linux_f2_ss3 = 'Q';
constexpr int linux_f3_ss3 = 'R';
constexpr int linux_f4_ss3 = 'S';
constexpr int linux_f1_ss3 = 'P';
constexpr int windows_zero_prefix = 0;
constexpr int windows_extended_prefix = 224;
constexpr int windows_f2 = 60;
constexpr int windows_f3 = 61;
constexpr int windows_f4 = 62;
constexpr int windows_f5 = 63;
constexpr int windows_f6 = 64;
constexpr int windows_f7 = 65;
constexpr int windows_f8 = 66;
constexpr int windows_f9 = 67;
constexpr int windows_f1 = 59;
constexpr int windows_alt_x = 45;
constexpr int windows_alt_d = 32;
constexpr int windows_alt_f = 33;
constexpr int windows_alt_w = 17;
constexpr int windows_arrow_up = 72;
constexpr int windows_arrow_down = 80;
constexpr int windows_home = 71;
constexpr int windows_page_up = 73;
constexpr int windows_end = 79;
constexpr int windows_page_down = 81;
constexpr int windows_alt_left = 155;
constexpr int windows_alt_right = 157;
constexpr int yes_lowercase = 'y';
constexpr int yes_uppercase = 'Y';
constexpr int no_data = 0;
constexpr int invalid_input = -1;
constexpr int toggle_hidden_key = '.';
constexpr int filter_key = '/';
constexpr int mark_key = ' ';
constexpr int select_all_key = 1;
constexpr std::string_view command_backspace = "__BACKSPACE__";
constexpr std::string_view command_resize = "__RESIZE__";
constexpr std::string_view command_escape = "__ESC__";
constexpr std::string_view command_tab = "__TAB__";
constexpr std::string_view command_up = "__UP__";
constexpr std::string_view command_down = "__DOWN__";
constexpr std::string_view command_home = "__HOME__";
constexpr std::string_view command_end = "__END__";
constexpr std::string_view command_page_up = "__PAGE_UP__";
constexpr std::string_view command_page_down = "__PAGE_DOWN__";
constexpr std::string_view command_history_back = "__HISTORY_BACK__";
constexpr std::string_view command_history_forward = "__HISTORY_FORWARD__";
constexpr std::string_view command_enter = "__ENTER__";
constexpr std::string_view command_unknown = "__UNKNOWN__";
constexpr std::string_view command_f2 = "__F2__";
constexpr std::string_view command_f3 = "__F3__";
constexpr std::string_view command_f4 = "__F4__";
constexpr std::string_view command_f5 = "__F5__";
constexpr std::string_view command_f6 = "__F6__";
constexpr std::string_view command_f7 = "__F7__";
constexpr std::string_view command_f8 = "__F8__";
constexpr std::string_view command_f9 = "__F9__";
constexpr std::string_view command_f1 = "__F1__";
constexpr std::string_view command_exit = "__EXIT__";
constexpr std::string_view command_docker = "__DOCKER__";
constexpr std::string_view command_work = "__WORK__";
constexpr std::string_view command_work_tick = "__WORK_TICK__";
constexpr std::string_view command_search_files = "__SEARCH_FILES__";
constexpr std::string_view command_toggle_hidden = "__TOGGLE_HIDDEN__";
constexpr std::string_view command_filter = "__FILTER__";
constexpr std::string_view command_mark = "__MARK__";
constexpr std::string_view command_select_all = "__SELECT_ALL__";
constexpr std::string_view command_view_quit = "__VIEW_QUIT__";
constexpr std::string_view command_view_hex = "__VIEW_HEX__";
constexpr std::string_view command_view_next = "__VIEW_NEXT__";
constexpr std::string_view command_view_filter = "__VIEW_FILTER__";
constexpr std::string_view command_view_wrap = "__VIEW_WRAP__";
constexpr std::string_view command_mouse_click = "__MOUSE_CLICK__";
constexpr std::string_view command_mouse_double_click = "__MOUSE_DOUBLE_CLICK__";
#ifdef __linux__
constexpr int KEY_UP = linux_arrow_up;
constexpr int KEY_DOWN = linux_arrow_down;
constexpr int KEY_F2 = linux_f2_ss3;
constexpr int KEY_F3 = linux_f3_ss3;
constexpr int KEY_F4 = linux_f4_ss3;
constexpr int KEY_ESC = escape;
constexpr int KEY_ENTER = enter;
constexpr int KEY_BACKSPACE = backspace;
constexpr int KEY_TAB = tab;
#else
constexpr int KEY_EXT_PREFIX1 = windows_zero_prefix;
constexpr int KEY_EXT_PREFIX2 = windows_extended_prefix;
constexpr int KEY_UP = windows_arrow_up;
constexpr int KEY_DOWN = windows_arrow_down;
constexpr int KEY_F2 = windows_f2;
constexpr int KEY_F3 = windows_f3;
constexpr int KEY_F4 = windows_f4;
constexpr int KEY_F5 = windows_f5;
constexpr int KEY_F6 = windows_f6;
constexpr int KEY_F7 = windows_f7;
constexpr int KEY_F8 = windows_f8;
constexpr int KEY_F9 = windows_f9;
constexpr int KEY_ESC = escape;
constexpr int KEY_ENTER = carriage_return;
constexpr int KEY_BACKSPACE = delete_backspace;
constexpr int KEY_TAB = tab;
#endif
} // namespace input

namespace ui {
constexpr int index_column_width = 6;
constexpr int icon_column_width = 3;
#ifdef _WIN32
constexpr int icon_terminal_width = 2;
#else
constexpr int icon_terminal_width = 2;
#endif
constexpr int size_column_width = 11;
constexpr int size_column_minimum_panel_width = 47;
constexpr int metadata_column_separator_width = 2;
constexpr int filename_size_gap_width = 5;
constexpr int timestamp_column_width = 16;
constexpr int timestamp_column_minimum_panel_width = 64;
constexpr int bytes_per_megabyte = 1024 * 1024;
constexpr int bytes_per_kilobyte = 1024;
constexpr char hidden_filename_prefix = '.';
constexpr char ascii_uppercase_first = 'A';
constexpr char ascii_uppercase_last = 'Z';
constexpr char ascii_case_offset = 'a' - 'A';
constexpr int file_size_precision = 3;
constexpr int timestamp_buffer_size = 20;
constexpr int windows_content_width = 144;
constexpr int windows_visible_file_rows = 20;
constexpr int page_navigation_rows = 10;
constexpr int viewer_bytes_per_row = 16;
constexpr int viewer_hex_offset_width = 8;
constexpr int viewer_terminal_reserved_rows = 2;
constexpr int viewer_search_chunk_size = 64 * 1024;
constexpr int name_truncation_suffix_length = 1;
constexpr int name_minimum_width = 1;
constexpr int hidden_column_width = 0;
constexpr int windows_default_console_attributes = 7;
constexpr int panel_split_divisor = 2;
constexpr int right_panel_extra_trailing_space = 1;
constexpr int file_row_right_border_padding = 2;
constexpr std::string_view left_panel_label = "LEFT: ";
constexpr std::string_view right_panel_label = "RIGHT: ";
constexpr std::string_view active_left_panel = ">>> LEFT PANEL <<<";
constexpr std::string_view inactive_left_panel = "Left Panel";
constexpr std::string_view active_right_panel = ">>> RIGHT PANEL <<<";
constexpr std::string_view inactive_right_panel = "Right Panel";
constexpr std::string_view directory_icon = "📁";
constexpr std::string_view symlink_icon = "🔗";
constexpr std::string_view image_icon = "🖼️";
constexpr std::string_view audio_icon = "🎵";
constexpr std::string_view video_icon = "🎬";
constexpr std::string_view archive_icon = "📦";
constexpr std::string_view code_icon = "💻";
constexpr std::string_view text_icon = "📄";
constexpr std::string_view pdf_icon = "📕";
constexpr std::string_view generic_file_icon = "📄";
constexpr std::array<std::string_view, 7> image_extensions = {
    ".png", ".jpg", ".jpeg", ".gif", ".bmp", ".svg", ".webp"};
constexpr std::array<std::string_view, 5> audio_extensions = {
    ".mp3", ".wav", ".flac", ".ogg", ".m4a"};
constexpr std::array<std::string_view, 6> video_extensions = {
    ".mp4", ".mkv", ".avi", ".mov", ".webm", ".mpeg"};
constexpr std::array<std::string_view, 7> archive_extensions = {
    ".zip", ".tar", ".gz", ".bz2", ".xz", ".7z", ".rar"};
constexpr std::array<std::string_view, 12> code_extensions = {
    ".c", ".cpp", ".h", ".hpp", ".py", ".js", ".ts", ".java", ".rs", ".go",
    ".sh", ".cmake"};
constexpr std::array<std::string_view, 10> text_extensions = {
    ".txt", ".md", ".log", ".csv", ".json", ".xml", ".yaml", ".yml", ".ini",
    ".conf"};
constexpr std::string_view index_prefix_selected = "[* ";
constexpr std::string_view index_prefix_unselected = "[  ";
constexpr std::string_view index_suffix = "]";
constexpr std::string_view filename_truncation = "~";
constexpr std::string_view no_file_size = "-";
constexpr std::string_view unavailable_value = "-";
constexpr std::string_view megabyte_suffix = " MB";
constexpr std::string_view timestamp_format = "%Y-%m-%d %H:%M";
constexpr std::string_view path_truncation = "...";
constexpr std::string_view menu_footer =
    "F1 - Help | F2 - Rename | F3 - View | F4 - Edit | F5 - Copy | F6 - Move | F7 - New | F8 - Del | F9 - Git | Alt+D - Docker | Alt+F - Find files | Alt+W - Work | Tab - Switch Panel | Alt+X - Exit";
constexpr std::string_view menu_footer_first_line =
    "F1 Help | F2 Rename | Alt+W Work | F3 View | F4 Edit | F5 Copy | F6 Move";
constexpr std::string_view menu_footer_second_line =
    "F7 New | F8 Del | F9 Git | Alt+D Docker | Alt+F Find files | Tab Panels | Alt+X Exit";
constexpr std::string_view hidden_files_indicator = " [hidden shown]";
constexpr std::string_view filter_indicator_prefix = " [filter: ";
constexpr std::string_view filter_indicator_suffix = "]";
constexpr std::string_view search_indicator_prefix = " [search: ";
constexpr std::string_view search_indicator_suffix = "]";
constexpr std::string_view filter_prompt = "Filter active panel (empty clears): ";
constexpr std::string_view prompt_rename = "Enter new name for ";
constexpr std::string_view prompt_new_folder = "Enter new folder name: ";
constexpr std::string_view prompt_delete_prefix = "Delete ";
constexpr std::string_view prompt_delete_suffix = "? [y/N]: ";
constexpr std::string_view prompt_continue = "Press any key to continue...";
constexpr std::string_view message_delete_empty =
    "Delete failed: no items were removed.";
constexpr std::string_view message_delete_success = "Deleted ";
constexpr std::string_view message_delete_count = " item(s).";
constexpr std::string_view message_delete_cancelled = "Delete cancelled.";
constexpr std::string_view git_menu_title = "Git";
constexpr std::string_view git_repository_valid = "Valid Git repository: ";
constexpr std::string_view git_repository_invalid = "Not inside a Git repository.";
constexpr std::string_view git_menu_options =
    "1 Status |  2 Diff |  3 GUI  | 4 Log  |  5 Commit |  6 Fetch |  7 Pull |  8 Push |  0 Back";
constexpr std::string_view git_choice_prompt = "Select Git option: ";
constexpr std::string_view git_push_confirmation =
    "Push to the configured remote? [y/N]: ";
constexpr std::string_view git_push_cancelled = "Push cancelled.";
constexpr std::string_view git_invalid_option = "Invalid Git option.";
constexpr std::string_view git_command_failed = "Git command failed (exit code ";
constexpr std::string_view git_command_succeeded = "Git command completed.";
constexpr std::string_view git_press_enter = "Press Enter to return...";
constexpr std::string_view git_commit_instructions =
    "Enter commit message; finish with a line containing only '.'.";
constexpr std::string_view git_commit_prompt = "Commit message: ";
constexpr std::string_view git_commit_cancelled = "Commit cancelled.";
constexpr std::string_view git_rev_parse_command =
    "git rev-parse --show-toplevel 2>/dev/null";
constexpr std::string_view git_rev_parse_command_windows =
    "git rev-parse --show-toplevel 2>nul";
constexpr std::string_view git_status_command = "git status";
constexpr std::string_view git_diff_command = "git diff";
constexpr std::string_view git_gui_command = "git gui";
constexpr std::string_view git_log_command = "git log --decorate --graph";
constexpr std::string_view git_commit_stdin_command = "git commit -F -";
constexpr std::string_view git_fetch_command = "git fetch";
constexpr std::string_view git_pull_command = "git pull";
constexpr std::string_view git_push_command = "git push";
constexpr std::string_view git_newline = "\n";
constexpr int git_output_buffer_size = 512;
constexpr char git_yes_lowercase = 'y';
constexpr char git_yes_uppercase = 'Y';
} // namespace ui

namespace filesystem {
constexpr std::string_view windows_clear_command = "cls";
} // namespace filesystem
} // namespace app_constants
