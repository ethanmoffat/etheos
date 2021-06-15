# ETHEOS

## Table of Contents

- [Getting Started on Windows](#getting-started-on-windows)
- [Getting Started on Linux](#getting-started-on-linux)
- [Running](#running)
- [Development](#development)

## Getting Started on Windows

Visual Studio 2017 is required for the compiler toolchain in order to build on Windows. You will need to select the "Desktop Development with C++" workload when installing. Windows 10 SDK is required for the ODBC (SQL server) driver libraries (can be installed as part of Visual Studio).

### Getting the source

> ⚠️ You *must* use git to clone the repository. Downloading the zip and trying to build the source from there on Windows is not supported due to the fact that there is an ICO file that is stored in git-lfs, which is an invalid format when downloaded via the zip file.

You may download git for Windows from: [https://git-scm.com/downloads](https://git-scm.com/downloads). Most users will want to use the installation defaults. If you don't know what a setting means during installation, do not change it.

The following commands must be run on a new system
1. `git lfs install`

    a. This command only needs to be run *once* per machine.

2. `git clone https://github.com/ethanmoffat/etheos.git`
3. `cd etheos`

The build and dependency install scripts may now be run from the `etheos` directory.

### Dependencies

The dependencies for building ETHEOS on Windows are:

- CMake (>= 2.6)
- SQLite
- MariaDB

#### Automatic Dependency Installation

A convenience script has been provided which installs Chocolatey (package manager) and each of the required dependencies. To automatically install the dependencies, run `.\scripts\install-deps.ps1` as administrator or from an elevated powershell terminal.

#### Manual Dependency Installation

> ⚠️ This workflow does not support SQLite

1. Download and install [CMake](https://github.com/Kitware/CMake/releases/download/v3.16.0/cmake-3.16.0-win64-x64.msi).
   - Make sure it is in the PATH environment variable.
2. Download and install the [MariaDB C connector](https://mariadb.com/downloads/?showall=1&tab=mariadbtx&group=mariadb_server&version=10.4.10#connectors).
   - Choose for C/C++ and Windows/x86 (linking with x64 is not supported; EOSERV builds as 32-bit).

### Build and Install

Run `.\build-windows.ps1` in a new powershell terminal to build the source with support for all available database engines (default: SQL server) and install the project into a local directory (default: `install`) under the repository root.

## Getting Started on Linux

This process has been testing on Ubuntu 18.04 (including a Windows Subsystem for Linux (WSL) workflow).

### Dependencies

The dependencies for building ETHEOS on Linux are:

- g++
- CMake (>= 2.6)
- SQLite
- MariaDB
- OCDB (SQL server) [optional]

#### Automatic Dependency Installation

A convenience script has been provided which installs each of the required dependencies. To automatically install the dependencies, run `sudo ./scripts/install-deps.sh`.

#### Manual Dependency Installation

To manually install the required dependencies, run the following commands:

```bash
sudo apt-get update
sudo apt-get install libmariadb-dev libsqlite3-dev cmake g++
```

If you would like to build with the Unix ODBC driver for SQL Server support, run the following commands:

```bash
sudo su
curl https://packages.microsoft.com/keys/microsoft.asc | apt-key add -
curl https://packages.microsoft.com/config/ubuntu/18.04/prod.list > /etc/apt/sources.list.d/mssql-release.list
exit

sudo apt-get update
sudo ACCEPT_EULA=Y apt-get install msodbcsql17 unixodbc-dev
```

> ℹ️ See [instructions here](https://docs.microsoft.com/en-us/sql/connect/odbc/linux-mac/installing-the-microsoft-odbc-driver-for-sql-server?view=sql-server-ver15) for installing the Unix ODCB driver on distributions/versions other than Ubuntu 18.04.

### Build and Install

Run `./build-linux.sh -i` to build the source with support for all available database engines (default: SQL server) and install the project into a local directory (default: `install`) under the repository root.

## Running

> ⚠️ Configuration and data files need to be placed before executing.

From the local `install` directory, run the ETHEOS executable (`.\eoserv.exe` on Windows or `./eoserv` on Linux).

## Development

Development within [Visual Studio Code](https://code.visualstudio.com/) (vscode) is supported. The following vscode extensions are recommended:

- [C/C++](https://marketplace.visualstudio.com/items?itemName=ms-vscode.cpptools)
- [CMake](https://marketplace.visualstudio.com/items?itemName=twxs.cmake)
