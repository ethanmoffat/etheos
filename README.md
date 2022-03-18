# ETHEOS

[![Build + Deployment (env:dev) Status](https://ethanmoffat.visualstudio.com/etheos/_apis/build/status/etheos?branchName=master)](https://ethanmoffat.visualstudio.com/etheos/_build/latest?definitionId=13&branchName=master)

[![CI test status](https://ethanmoffat.visualstudio.com/etheos/_apis/build/status/etheos-ci-test?branchName=master)](https://ethanmoffat.visualstudio.com/etheos/_build/latest?definitionId=17&branchName=master)

[![Deployment (env:test) status](https://ethanmoffat.visualstudio.com/etheos/_apis/build/status/etheos-deploy-test?branchName=master)](https://ethanmoffat.visualstudio.com/etheos/_build/latest?definitionId=18&branchName=master)

## Table of Contents

- [Getting Started on Windows](#getting-started-on-windows)
- [Getting Started on Linux](#getting-started-on-linux)
- [Running](#running)
- [Development](#development)
- [Sample servers](#sample-servers)

## Getting Started on Windows

Visual Studio 2017 or 2019 is required for the compiler toolchain in order to build on Windows. You will need to select the "Desktop Development with C++" workload when installing. Windows 10 SDK is required for the ODBC (SQL server) driver libraries (can be installed as part of Visual Studio).

### Getting the source

> ⚠️ You *must* use git to clone the repository. Downloading the zip and trying to build the source from there is not supported due to the fact that there is an ICO file that is stored in git-lfs, which is an invalid format when downloaded via the zip file.

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
- vswhere
- git (for getting bcrypt/googletest components)

#### Automatic Dependency Installation

A convenience script has been provided which installs Chocolatey (package manager) and each of the required dependencies. To automatically install the dependencies, run `.\scripts\install-deps.ps1` as administrator or from an elevated powershell terminal. Examine the script contents to view the options for running this script.

### Build and Install

Run `.\build-windows.ps1` in a new powershell terminal to build the source with support for all available database engines (default: SQL server) and install the project into a local directory (default: `install`) under the repository root. To compile with support for all database engines, run `.\build-windows.ps1 -SqlServer ON -MariaDB ON -Sqlite ON`. `-Debug` can be added to the command-line arguments to build in debug mode, otherwise, release mode is used.

## Getting Started on Linux

This process has been tested on Ubuntu 18.04 and 20.04 (including WSL/Windows Subsystem for Linux).

### Dependencies

The dependencies for building ETHEOS on Linux are:

- g++
- CMake (>= 2.6)
- SQLite
- MariaDB
- git (for getting bcrypt/googletest components)
- OCDB (SQL server) [optional]

#### Automatic Dependency Installation

A convenience script has been provided which installs each of the required dependencies. To automatically install the dependencies, run `sudo ./scripts/install-deps.sh`. Examine the script contents to view the options for running this script.

### Build and Install

Run `./build-linux.sh -i` to build the source with support for all available database engines (default: SQL server) and install the project into a local directory (default: `install`) under the repository root.

## Running

> ⚠️ Configuration and data files need to be placed before executing.

From the local `install` directory, run the ETHEOS executable (`.\etheos.exe` on Windows or `./etheos` on Linux).

## Development

Development within [Visual Studio Code](https://code.visualstudio.com/) (vscode) is supported. The following vscode extensions are recommended:

- [C/C++](https://marketplace.visualstudio.com/items?itemName=ms-vscode.cpptools)
- [CMake](https://marketplace.visualstudio.com/items?itemName=twxs.cmake)

## Sample Servers

A few sample servers are hosted in different environments. These servers use SQL Server as a database backend.

1. `moffat.io:8078`
   - ⚠️ This server is not guaranteed to be running the latest version

2. `dev.etheos.moffat.io:8078`
   - This server is updated on each pull request build

3. `test.etheos.moffat.io:8078`
   - This server is updated once integration tests pass (triggered by a CI build after a PR is merged)
