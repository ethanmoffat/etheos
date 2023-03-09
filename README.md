# ETHEOS

[![CI Build Status](https://ethanmoffat.visualstudio.com/etheos/_apis/build/status/etheos?branchName=master)](https://ethanmoffat.visualstudio.com/etheos/_build/latest?definitionId=13&branchName=master)

## Table of Contents

- [Getting Started on Windows](#getting-started-on-windows)
- [Getting Started on Linux](#getting-started-on-linux)
- [Docker Image](#docker-image)
- [Running](#running)
- [Development](#development)
- [Integration Tests](#integration-tests)
- [Sample servers](#sample-servers)

## Getting Started on Windows

Visual Studio 2017 or 2019 is required for the compiler toolchain in order to build on Windows. You will need to select the "Desktop Development with C++" workload when installing. Windows 10 SDK is required for the ODBC (SQL server) driver libraries (can be installed as part of Visual Studio).

> ⚠️ If you have previously attempted to build eoserv using MinGW on Windows, please uninstall MinGW as it conflicts with the standard libraries provided by Visual Studio.

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

- CMake (>= 2.8.2)
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
- ODBC (SQL server) [optional]

#### Automatic Dependency Installation

A convenience script has been provided which installs each of the required dependencies. To automatically install the dependencies, run `sudo ./scripts/install-deps.sh`. Examine the script contents to view the options for running this script.

### Build and Install

Run `./build-linux.sh -i` to build the source with support for all available database engines (default: SQL server) and install the project into a local directory (default: `install`) under the repository root.

## Docker Image

A docker image is available on docker hub at `darthchungis/etheos`. Environment variables in the form of `ETHEOS_###` (all uppercase, case-sensitive) may be used to set configuration options. These options will override what is set by default in the configuration files.

For example:

```
docker pull darthchungis/etheos
docker run -it --rm -e 'ETHEOS_PORT=8079' -p 8079:8079 -v .\install\data:/etheos/data -v .\install\config_local:/etheos/config_local darthchungis/etheos:latest
```

## Running

> ⚠️ Configuration and data files need to be placed before executing.

From the local `install` directory, run the ETHEOS executable (`.\etheos.exe` on Windows or `./etheos` on Linux).

## Development

Development within [Visual Studio Code](https://code.visualstudio.com/) (vscode) is supported. The following vscode extensions are recommended:

- [C/C++](https://marketplace.visualstudio.com/items?itemName=ms-vscode.cpptools)
- [CMake](https://marketplace.visualstudio.com/items?itemName=twxs.cmake)

## Integration Tests

Integration tests have been added under `src/test/integration`. These are integration test scripts that are run via [EOBot](https://www.github.com/ethanmoffat/EndlessClient/tree/master/EOBot).

These tests are run as part of the release pipeline that deploys the sample server to `etheos.moffat.io:8078` (see below section). However, they may also be run locally to validate integration scenarios or add more integration test coverage.

Running these integration tests is only supported on Linux. Ubuntu 22.04 is the suggested platform. WSL 2 is supported.

First, download/install the following dependencies (installation instructions are left as an exercise for the user):
- python3
- docker cli
    - Please ensure you add your local user to the `docker` group via `sudo usermod -aG docker <username>` to avoid running the ci-test script with `sudo`
- EOBot from [the latest EndlessClient release](https://github.com/ethanmoffat/EndlessClient/releases)
    - You may also clone EndlessClient, build the `EndlessClient.Linux.sln` file, and reference the EOBot output directory

Then, run the script as follows (where `path/to/eobot` is the path to the directory container the EOBot executable file):

```bash
cd deploy
./ci-test.sh --self-contained --use-local --botdir path/to/eobot
```

This script will use a self-contained docker environment based on your local repository, build the source + docker image locally (omit --use-local to use the latest public etheos image), and set up an etheos container with a SQLite database backend on port 8078. Integration tests defined in the `src/test/integration` directory and listed in the `exec_tests()` function in `ci-test.sh` will be executed against the local docker container.

For more information on authoring test scripts, see the output of `EOBot --help`.

## Sample Servers

Sample servers are hosted in different environments. These servers use SQL Server as a database backend. Servers use default assets from EO v28.

1. `moffat.io:8078`
   - ⚠️ This server is not guaranteed to be running the latest version

2. `etheos.moffat.io:8078`
   - This server is deployed on completion of successful CI builds
