param (
    [switch]$SkipCMake,
    [switch]$SkipMariaDB,
    [switch]$SkipSQLite
)

Add-Type -Assembly System.IO.Compression.FileSystem
Import-Module $env:ChocolateyInstall\helpers\chocolateyProfile.psm1

function CheckPathForDll($DllName, $AdditionalPaths) {
    $splitPaths = $env:PATH.Split(';')
    if ($AdditionalPaths) {
        $splitPaths += $AdditionalPaths
    }

    foreach ($path in $splitPaths) {
        if ($path -and (Test-Path $path)) {
            $dllPath = Join-Path $path $DllName
            if (Test-Path $dllPath) {
                Write-Output "Found $DllName in $path"
                return
            }
        }
    }

    Write-Warning "Warning: $DllName not found in `$env:PATH"
}

$CurrentIdentity = [Security.Principal.WindowsPrincipal][Security.Principal.WindowsIdentity]::GetCurrent()
if (-not $CurrentIdentity.IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)) {
    Write-Error "This script must be run as administrator since it downloads/installs software dependencies."
    exit -1
}

$DownloadDir = Join-Path $PSScriptRoot 'download'
if (-not (Test-Path $DownloadDir)) {
    New-Item $DownloadDir -ItemType Directory | Out-Null
}

# Ensure TLS 1.2 is used, older versions of powershell use TLS 1.0 as the default
#
[Net.ServicePointManager]::SecurityProtocol = [Net.SecurityProtocolType]::Tls12

# Use chocolatey to install dependencies
#
Get-Command choco -ErrorVariable res 2>&1 > $null
if ($res) {
    Set-ExecutionPolicy Bypass -Scope Process -Force
    Invoke-Expression ((New-Object System.Net.WebClient).DownloadString('https://chocolatey.org/install.ps1'))
}

if (-not $SkipCMake) {
    Write-Output "Installing CMake..."
    choco install -y cmake --installargs '"ADD_CMAKE_TO_PATH=System"' | Out-Null

    refreshenv
    if (-not (Get-Command cmake)) {
        Write-Warning "Could not detect cmake.exe after install. Shell may need to be restarted."
    }
}

$MariaDBURL = "https://downloads.mariadb.com/Connectors/c/connector-c-2.3.7/mariadb-connector-c-2.3.7-win32.msi"
if (-not $SkipMariaDB) {
    Write-Output "Installing MariaDB..."
    $DownloadedFile = (Join-Path $DownloadDir "mariadb.msi")
    (New-Object System.Net.WebClient).DownloadFile($MariaDBURL, $DownloadedFile)
    msiexec /I $DownloadedFile /qn

    # Update the path with the new directory
    # This is a fixed default value of the MSI that is downloaded
    #
    $InstallDir="C:\Program Files (x86)\MariaDB\MariaDB Connector C\lib"
    $PATHRegKey = 'Registry::HKEY_LOCAL_MACHINE\System\CurrentControlSet\Control\Session Manager\Environment'
    $Old_Path=(Get-ItemProperty -Path $PATHRegKey -Name Path).Path
    if ($Old_Path.IndexOf($InstallDir, [System.StringComparison]::OrdinalIgnoreCase) -lt 0) {
        Set-ItemProperty -Path $PATHRegKey -Name PATH -Value ("$InstallDir;$Old_Path")
    }

    refreshenv
    CheckPathForDll -DllName "libmariadb.dll"
}

$Sqlite3URL = "https://sqlite.org/2019/sqlite-amalgamation-3300100.zip"
if (-not $SkipSQLite) {
    Write-Output "Installing SQLite..."
    choco install -y sqlite --params "/NoTools" | Out-Null

    $LocalSqliteDir = (Join-Path $PSScriptRoot "../sqlite")
    foreach ($dir in @($LocalSqliteDir, "$LocalSqliteDir\bin", "$LocalSqliteDir\src", "$LocalSqliteDir\include")) {
        if (-not (Test-Path $dir)) {
            mkdir $dir | Out-Null
        }
    }
    $LocalSqliteDir = Resolve-Path $LocalSqliteDir

    # This is a guaranteed download location due to OSS Chocolatey restrictions
    #
    $ChocoSqliteInstallDir = "C:\ProgramData\chocolatey\lib\sqlite\tools"
    foreach ($item in ((Get-ChildItem -Force -Recurse $ChocoSqliteInstallDir | Where-Object {$_.Name -like '*.dll'}))) {
        Copy-Item -Force $item.FullName (Join-Path $LocalSqliteDir "bin")
    }

    $DownloadedFile = (Join-Path $DownloadDir "sqlite3.zip")
    try {
        # This throws for some reason but still downloads the file?
        #
        (New-Object System.Net.WebClient).DownloadFile($Sqlite3URL, $DownloadedFile)
        $zip = [IO.Compression.ZipFile]::OpenRead($DownloadedFile)

        foreach ($entry in ($zip.Entries | Where-Object {$_.Name -like '*.h' -or $_.Name -like '*.c'})) {
            $filename = [System.IO.Path]::GetFileName($entry)
            if ($filename -like '*.h') {
                [System.IO.Compression.ZipFileExtensions]::ExtractToFile($entry, "$LocalSqliteDir\include\$filename", $true)
            } elseif ($filename -like '*.c') {
                [System.IO.Compression.ZipFileExtensions]::ExtractToFile($entry, "$LocalSqliteDir\src\$filename", $true)
            }
        }
    }
    catch {}
    finally {
        if ($zip) {
            $zip.Dispose()
        }
    }

    refreshenv
    CheckPathForDll -DllName "sqlite3.dll" -AdditionalPaths "$LocalSqliteDir\bin"
}

if (-not (Get-Command vswhere)) {
    Write-Output "Installing vswhere..."
    choco install -y vswhere | Out-Null

    refreshenv
    if (-not (Get-Command vswhere)) {
        Write-Warning "Could not detect vswhere after install. Shell may need to be restarted."
    }
}

$JSON_VERSION="3.9.1"
if (-not (Test-Path (Join-Path $PSScriptRoot "..\json"))) {
    New-Item -ItemType Directory -Path (Join-Path $PSScriptRoot "..\json")

    $JsonUrl="https://raw.githubusercontent.com/nlohmann/json/v$JSON_VERSION/single_include/nlohmann/json.hpp"
    $DownloadedFile=(Join-Path $DownloadDir "json.hpp")
    (New-Object System.Net.WebClient).DownloadFile($JsonUrl, $DownloadedFile)

    if (-not (Test-Path $DownloadedFile)) {
        Write-Error "Error downloading JSON library"
    } else {
        Copy-Item $DownloadedFile (Join-Path $PSScriptRoot "..\json")
    }
}

if ((Test-Path $DownloadDir)) {
    Remove-Item $DownloadDir -Recurse -Force
}
