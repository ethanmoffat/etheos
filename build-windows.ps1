param (
    [switch]$Clean,
    [switch]$Debug,
    [switch]$Test,
    [switch]$Offline,
    [string]$SqlServer="ON",
    [string]$MariaDB="OFF",
    [string]$Sqlite="OFF",
    $BuildDir = "build"
)

function EnsureMariaDB() {
    $splitPaths = $env:PATH.Split(';')
    foreach ($path in $splitPaths) {
        if ($path -and (Test-Path $path)) {
            $mariaDbPath = Join-Path $path "libmariadb.dll"
            if (Test-Path $mariaDbPath) {
                Write-Output "Found MariaDB in $path"

                # Prepend the include path for MariaDB to PATH so CMake can find the package
                #
                $includePath = Resolve-Path (Join-Path $path "..\include")
                if ($env:PATH.IndexOf($includePath, [System.StringComparison]::OrdinalIgnoreCase) -lt 0) {
                    Write-Output "Adding $includePath to `$env:PATH"
                    $env:PATH = "$includePath;$env:PATH"
                }

                return
            }
        }
    }

    Write-Warning "Warning: no MariaDB found in `$env:PATH - CMake may not be able to find the dependency"
}

if (-not (Get-Command cmake)) {
    Write-Error "CMake is not installed. Please install CMake before running this script."
    exit -1
}

if ($Clean -and (Test-Path $BuildDir)) {
    Remove-Item -Recurse -Force $BuildDir
}

if (-not (Test-Path $BuildDir)) {
    New-Item $BuildDir -ItemType Directory
}

Set-Location $BuildDir

if ($MariaDB -eq "ON") {
    EnsureMariaDB
}

if ($Debug) {
    $buildMode = "Debug"
} else {
    $buildMode = "Release"
}

$cmakeHelp=$(cmake --help)

# -requires param : ensure that the visual studio installs have the C++ workload
#
$vsVersions=$(vswhere -property installationVersion -requires "Microsoft.VisualStudio.Component.VC.Tools.x86.x64")
foreach ($vsVersion in $vsVersions) {
    $versionMajor = [int]$vsVersion.Substring(0, $vsVersion.IndexOf("."))
    switch($versionMajor) {
        15 { $generator = "Visual Studio 15 2017" }
        16 { $generator = "Visual Studio 16 2019" }
    }

    if ($generator -and -not ($cmakeHelp -match $generator)) {
        $generator = "" # Installed CMake version does not support the detected VS version
    }

    $vsInstallPath=$(vswhere -version "[$versionMajor.0,$($versionMajor+1).0)" -property installationPath)
}

if (-not $generator) {
    Write-Error "Unable to determine Visual Studio version. Is Visual Studio installed?"
    exit -1
} else {
    Write-Output "Using generator: $generator"
}

if (-not ($env:PATH -match [System.Text.RegularExpressions.Regex]::Escape($vsInstallPath))) {
    Write-Output "Adding to PATH: $vsInstallPath"
    [System.Environment]::SetEnvironmentVariable("PATH", "$vsInstallPath;$env:PATH", [System.EnvironmentVariableTarget]::Process)
}

if ($Offline) {
    $OfflineFlag="-DEOSERV_OFFLINE=ON"
}

$SqlServerFlag="-DEOSERV_WANT_SQLSERVER=$($SqlServer)"
$MariaDBFlag="-DEOSERV_WANT_MYSQL=$($MariaDB)"
$SqliteFlag="-DEOSERV_WANT_SQLITE=$($Sqlite)"

# For building on Windows, force precompiled headers off
#
$PrecompiledHeadersFlag = "-DEOSERV_USE_PRECOMPILED_HEADERS=OFF"

cmake $SqlServerFlag $MariaDBFlag $SqliteFlag $PrecompiledHeadersFlag -DCMAKE_GENERATOR_PLATFORM=Win32 $OfflineFlag -G $generator ..
$tmpResult=$?
if (-not $tmpResult)
{
    Set-Location $PSScriptRoot
    Write-Error "Error during cmake generation!"
    exit $tmpResult
}

cmake --build . --config $buildMode --target INSTALL --
$tmpResult=$?
if (-not $tmpResult)
{
    Set-Location $PSScriptRoot
    Write-Error "Error during cmake build!"
    exit $tmpResult
}

Set-Location $PSScriptRoot

if ($Test) {
    Set-Location install/test
    ./eoserv_test.exe
    Set-Location $PSScriptRoot
}
