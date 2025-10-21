# Build Instructions

## Requirements

- C++17 compiler
- CMake 3.10 or later
- Git

## Linux

On Debian or Ubuntu, install the compiler and build tools:

```bash
sudo apt update
sudo apt install build-essential cmake
```

From the repository root, configure the build directory once:

```bash
cmake -S . -B build
```

Build the executable:

```bash
cmake --build build -j2
```

If `build/CMakeCache.txt` is removed, configure again before building. Run the
file manager from a terminal:

```bash
./build/SystemFileManagerX
```

It opens in the current working directory. Change directory before launching
to browse a different location. F4 uses `nano` to edit files, and opening a
file uses `xdg-open`; install `nano` and `xdg-utils` if those commands are not
available.

### Download a prebuilt RHEL 8 executable

The GitHub Actions **Linux build** workflow creates the
`SystemFileManagerX-linux-rhel8` artifact using AlmaLinux 8, targeting
compatibility with RHEL 8.10. Download the artifact ZIP from a successful
workflow run and extract it. Then extract the included tarball and launch the
executable:

```bash
tar -xzf SystemFileManagerX-linux-rhel8.tar.gz
./SystemFileManagerX
```

### Install for your user

Install a copy under `~/.local/bin`, independent of the build directory:

```bash
cmake --install build --prefix "$HOME/.local"
```

Add `~/.local/bin` to `PATH` for the current terminal if needed:

```bash
export PATH="$HOME/.local/bin:$PATH"
```

To keep it on `PATH` in future Bash terminal tabs, add that export to
`~/.bashrc` (use your shell's equivalent startup file for other shells). Then
launch the installed application from any directory with:

```bash
SystemFileManagerX
```

After rebuilding an update, run the install command again to replace the
installed executable. Alternatively, install for all users with
`sudo cmake --install build --prefix /usr/local`.

## Windows (Visual Studio)

Install Visual Studio with the Desktop Development with C++ workload and CMake.
From a Visual Studio Developer Command Prompt, configure and build:

```powershell
cmake -S . -B build
cmake --build build --config Release
```

Run the Release executable:

```powershell
.\build\Release\SystemFileManagerX.exe
```

The application starts in the working directory from which it is launched.
