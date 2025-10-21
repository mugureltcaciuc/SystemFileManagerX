#include <chrono>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "Constants.h"
#include "IFileSystem.h"

class FileManager
{
public:
    FileManager(std::unique_ptr<IFileSystem> fs, const std::string &path);
    void run();
    void setActivePanelLeft(bool isLeft);
    void moveActiveSelection(int offset);

private:
    bool isLeftPanelActive = true;
    int selectedLeftIndex = app_constants::ui::first_selected_index;
    int selectedRightIndex = app_constants::ui::first_selected_index;
    size_t leftPanelStart = 0;
    size_t rightPanelStart = 0;
    std::string left_path;
    std::string right_path;
    std::string docker_container_id;
    std::filesystem::path docker_cache_root;
    std::vector<std::string> left_directory_history;
    std::vector<std::string> right_directory_history;
    size_t left_history_index = 0;
    size_t right_history_index = 0;

    std::vector<std::filesystem::directory_entry> left_files;
    std::vector<std::filesystem::directory_entry> right_files;
    std::vector<std::filesystem::directory_entry> left_search_results;
    std::vector<std::filesystem::directory_entry> right_search_results;
    std::filesystem::path left_search_root;
    std::filesystem::path right_search_root;
    std::string left_search_pattern;
    std::string right_search_pattern;
    bool left_search_active = false;
    bool right_search_active = false;
    std::vector<std::filesystem::path> left_marked_files;
    std::vector<std::filesystem::path> right_marked_files;
    std::string left_filter;
    std::string right_filter;
    bool show_hidden_files = true;
    bool screen_initialized = false;
    bool workModeActive = false;
    int lastMouseColumn = 0;
    int lastMouseRow = 0;
    std::chrono::steady_clock::time_point previousClickTime{};
    int previousClickColumn = 0;
    int previousClickRow = 0;
    std::optional<std::filesystem::path> previouslyClickedEntry;
    bool previouslyClickedLeftPanel = true;

    std::unique_ptr<IFileSystem> fileSystem;

    void drawMenuBar(int contentWidth);
    std::filesystem::path display_path(const std::string &path) const;
    void drawPanel(const std::filesystem::path &leftPath, const std::filesystem::path &rightPath);
    void show_help_screen();
    bool show_file_viewer(const std::filesystem::path &filePath,
                          bool automatic = false,
                          bool initialHexMode = false);
    void work_mode();
    void show_git_menu(const std::filesystem::path &activePath);
    void show_git_output(std::string_view title, const std::string &output);
    void drawLeftRightPanels(std::vector<std::string> &buffer,
                             int leftPanelWidth, int rightPanelWidth,
                             int visibleRows);

    // Helper functions
    void refresh_files();
    void refresh_panel(bool isLeft);
    void search_files();
    void cleanup_docker_cache();
    void clear_screen();
    int selectedIndex() const;
    bool process_navigation_input(std::string_view input);
    bool process_mouse_click(bool doubleClick);
    int process_file_selection_input(std::string_view input);
    void print_line_with_dual_selection(const std::string &line, bool isLeftSelected, bool isRightSelected, size_t totalWidth);
    std::string_view read_user_command(
        std::optional<std::chrono::steady_clock::time_point> deadline = {});
};
