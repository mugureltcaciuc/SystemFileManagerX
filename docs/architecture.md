# Architecture

SystemFileManagerX is a C++17 console file manager. The UI and navigation logic
are in `FileManager`; filesystem operations are abstracted behind `IFileSystem`
and implemented for Linux and Windows.

## Components

- `main.cpp` initializes terminal input and runs the application loop.
- `FileSystemFactory` selects the implementation for the current platform.
- `FileManager` handles keyboard input, panel rendering, navigation, and
  dispatches file operations.
- `IFileSystem` defines the platform-independent filesystem interface.
- `LinuxFileSystem` and `WindowsFileSystem` implement that interface using
  platform-specific behavior.

## Data flow

1. `main.cpp` creates a filesystem implementation through `FileSystemFactory`.
2. `FileManager` reads an action and updates the active panel or item.
3. Filesystem actions are dispatched through `IFileSystem`.
4. The file lists are refreshed and rendered again.

Tests can provide an alternate `IFileSystem` implementation, such as the mock
in `tests/mocks`, to isolate file-manager behavior from real filesystem
operations.
