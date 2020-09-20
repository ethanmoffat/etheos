param (
    [switch]$Clean,
    [switch]$Debug,
    [switch]$Test,
    $BuildDir = "build"
)

function EnsureMariaDB() {
    $splitPaths = $env:PATH.Split(';')
    foreach ($path in $splitPaths) {
        if ($path -and (Test-Path $path)) {
            $mariaDbPath = Join-Path $path "libmariadb.dll"
            if (Test-Path $mariaDbPath) {
                Write-Output "Found MariaDB in $path"

                # Add the include path for MariaDB to the path so CMake can find the package
                #
                $includePath = Resolve-Path (Join-Path $path "..\include")
                if ($env:PATH.IndexOf($includePath, [System.StringComparison]::OrdinalIgnoreCase) -lt 0) {
                    Write-Output "Adding $includePath to `$env:PATH"
                    $env:PATH = $env:PATH + ";$includePath"
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

EnsureMariaDB

# For building on Windows, force sqlite3 off until we get better dependency management (TODO: dependency management)
# For building on Windows, force precompiled headers off
#

if ($Debug) {
    $buildMode = "Debug"
} else {
    $buildMode = "Release"
}

cmake -DEOSERV_WANT_SQLSERVER=ON -DEOSERV_USE_PRECOMPILED_HEADERS=OFF -G "Visual Studio 15 2017" ..
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
    ./install/test/eoserv_test.exe
}
