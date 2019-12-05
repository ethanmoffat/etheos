# project ETHEOS

## Getting started

### Windows

Visual Studio 2017 is a hard-coded dependency of Windows build.

1. Download and install [CMake](https://github.com/Kitware/CMake/releases/download/v3.16.0/cmake-3.16.0-win64-x64.msi)
2. Download and install the [MariaDB C connector](https://mariadb.com/downloads/?showall=1&tab=mariadbtx&group=mariadb_server&version=10.4.10#connectors)
   - Choose for C/C++ and Windows/x86 (linking with x64 is not supported -- EOSERV builds as 32-bit)
3. Clone this repository
4. Run `.\build-windows.ps1` in a new powershell terminal
   - This will build and install the project into a local directory 'install' under the repository root

### Linux

This process has been tested on WSL (Windows Subsystem for Linux) using Ubuntu 18.04.

1. Install the dependencies
```bash
sudo apt-get update
sudo apt-get install libmariadb-dev libsqlite3-dev cmake g++
```

2. Clone this repository
```bash
git clone git@github.com:EndlessOpenSource/etheos.git
cd etheos
```

3. Kick off a build.
```bash
mkdir build && cd build
cmake -G "Unix Makefiles" .. # Add -D{option} with the options from CMakeLists.txt to customize the build
cmake --build . --config Debug --target eoserv --
cmake --build . --config Debug --target install --
```

### Running

From the `install` directory, run the eoserv executable (`.\eoserv.exe` on Powershell, `./eoserv` on linux).

Configuration / data files need to be placed before executing.

## Development

Development within [Visual Studio Code](https://code.visualstudio.com/) is supported. Recommended extensions:
   - [C/C++](https://marketplace.visualstudio.com/items?itemName=ms-vscode.cpptools)
   - [CMake](https://marketplace.visualstudio.com/items?itemName=twxs.cmake)