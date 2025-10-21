# Usage Guide

SystemFileManagerX is a keyboard-driven console file manager. Start it from a
terminal; it displays the current working directory in the left panel and its
parent in the right panel. See the [build instructions](build_instructions.md)
for platform-specific setup.

On Linux, the interface uses the full terminal screen and adjusts its panel
width and visible file rows when the terminal is resized.

Entries show an icon before the filename to distinguish folders, links, and
common file types such as images, audio, video, archives, code, text, and PDFs.
Windows uses compact text badges in the same column.
File sizes below 1 MiB are shown in KB; larger files are shown in MB, with
three decimal places.

## Navigation

- Use Up and Down to move the selection; Home and End select the first and
  last entries, and Page Up and Page Down move by ten entries.
- The `..` entry moves to the parent directory. Backspace does the same.
- Press Enter to open the selected directory or launch the selected file with
  the system default application. A number followed by Enter selects that
  entry by its displayed index.
- Use Alt+Left and Alt+Right to move backward and forward through the active
  panel's directory history.
- Press Tab to switch the active panel.
- Click an entry to select it; double-click to open it (in terminals that
  support mouse reporting). The two clicks must be on the same entry; other
  keyboard or mouse actions cancel the double-click. Double-click a shortcut
  in the bottom menu to run that action. Click the [✖] in a window's top-right
  corner to close it; on the main screen it exits the application.
- Press Alt+X to exit.
- Press Alt+D to list running Docker containers and browse one in the active
  panel. The ID defaults to the first result from `docker ps -q | head -n 1`;
  press Enter to use it or enter another ID. Container browsing is read-only;
  Enter or F3 fetches and views the selected file from the container. Press
  Escape to return to the host directory.
- Press Alt+F to find files recursively from the active panel directory using a
  filename pattern such as `*.log`. Results show paths relative to the search
  root; press Enter on a result to navigate to its folder and select it. This
  also searches inside the active Docker container. Press Escape to close the
  search results or cancel the search dialog.
- Press `.` to toggle hidden files; they are shown by default.
- Press `/` to filter the active panel by a case-insensitive filename
  substring. Press Enter to apply the filter, or submit an empty filter to
  clear it. Filters clear when you change directories.

Each panel sorts directories before files, then sorts names
case-insensitively with a deterministic case-sensitive tie-break.

## Function keys

| Key | Action |
|---|---|
| F1 | Show keyboard help |
| F2 | Rename the selected item |
| F3 | Open the selected file in the interactive viewer |
| F4 | Edit with `nano` on Linux or Notepad on Windows |
| F5 | Copy marked items, or the selected item, to the other panel |
| F6 | Move marked items, or the selected item, to the other panel |
| F7 | Create a folder in the active panel |
| F8 | Delete marked items, or the selected item, after confirmation |
| F9 | Open the Git menu |
| Alt+D | Browse a running Docker container in the active panel (read-only) |
| Alt+F | Find files recursively by filename pattern |
| Alt+X | Exit the application |

The F3 viewer pages through large files without loading the entire file into
memory. Use Up/Down to scroll, Page Up/Down to move by a page, `/` to search,
`n` for the next match, `h` to toggle hexadecimal view, or `|` to filter to
lines containing the entered text. Press `w` to toggle word wrap for long
lines. Each occurrence of the filter text is highlighted in matching lines.
Press `q`, F3, or Escape to return. F4 requires `nano` on Linux, and launching
files requires `xdg-open`.

Press Space to mark or unmark the selected entry; marked names are prefixed
with `*`. Ctrl+A marks all entries in the active panel, or clears all marks
when all entries are already marked. F5, F6, and F8 operate on marked entries
when any are marked, otherwise they operate on the selected entry. Directories
are copied recursively and can be moved or deleted as a unit.

F5 and F6 ask once per batch before overwriting existing destinations. An
approved directory overwrite merges contents and replaces matching files.
Copies and cross-device moves show byte progress while transferring data;
cross-device moves remove the source only after the copy succeeds.

## Git menu

Press F9 to check whether the active panel's directory is inside a valid Git
working tree. When it is, a menu appears in the active panel and shows the
repository root. It offers Git status, diff, GUI, log, commit, fetch, pull,
and push. Status, diff, log, commit, fetch, pull, and push output is displayed
in the active panel.
Use Up/Down or Page Up/Page Down to scroll; press Escape to return to the Git
menu. Press Escape again there to return to the directory listing. The GUI
option opens `git gui` separately. For a commit, enter the subject and body
one line at a time; finish with a line containing only `.`. Blank lines are
preserved. For example:

```text
[ISSUE-ID] Short summary

* Detailed change
.
```

Push runs `git push` only after you confirm with `y`; any other response
cancels it. Git must be installed, and `git gui` must be available to use the
GUI option.
