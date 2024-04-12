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

# Detect Visual Studio installations
$vsInstances = vswhere -all -format json | ConvertFrom-Json
$vsInstances | ForEach-Object {
    Write-Output "Instance: $($_.installationName), Version: $($_.installationVersion), Path: $($_.installationPath)"
}

# Get major versions
$vsVersions = $vsInstances | ForEach-Object { $_.installationVersion.Split('.')[0] } | Sort-Object -Unique -Descending

$generator = $null

# Choose generator
foreach ($versionMajor in $vsVersions) {
    switch ($versionMajor) {
        '16' { $generator = "Visual Studio $versionMajor 2019"; break }
        '15' { $generator = "Visual Studio 15 2017"; break }
        Default {
            $generator = "Visual Studio $versionMajor"
            Write-Output "Generator: $generator for version $versionMajor"
        }
    }
    if ($generator) { break }
}

# Validate generator
if (-not $generator) {
    Write-Error "Visual Studio not installed."
    exit -1
} else {
    Write-Output "Generator: $generator"
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
