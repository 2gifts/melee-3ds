# Started by "Build Melee CIA.bat". Picks the work folder, fetches a private
# copy of Python (pinned and hash-checked) and runs tools\easy_build.py with it.
# Set MELEE_BUILD_DIR to choose the work folder yourself.
param([string]$Iso = '')
$ErrorActionPreference = 'Stop'
$ProgressPreference = 'SilentlyContinue'
try { [Net.ServicePointManager]::SecurityProtocol = [Net.SecurityProtocolType]::Tls12 } catch {}

$PythonUrl = 'https://api.nuget.org/v3-flatcontainer/python/3.14.7/python.3.14.7.nupkg'
$PythonSha256 = '46A4DA5529A92D18FF894911F6E6033A8253198D705B8161BF28C9123C87D46B'
$NeedBytes = 8GB
$Builder = Split-Path -Parent (Split-Path -Parent $PSScriptRoot)

function Stop-Build($Message) {
    Write-Host ''
    Write-Host $Message -ForegroundColor Yellow
    exit 1
}

function Test-Writable($Dir) {
    try {
        New-Item -ItemType Directory -Force -Path $Dir | Out-Null
        $probe = Join-Path $Dir '.write-test'
        Set-Content -Path $probe -Value 'ok'
        Remove-Item $probe
        return $true
    } catch {
        return $false
    }
}

Write-Host 'Melee for New 3DS - CIA builder'
Write-Host '==============================='
Write-Host 'This builds the game from your own Melee disc image. The first build takes'
Write-Host '10 to 30 minutes and needs an internet connection and about 8 GB of free space.'
Write-Host 'You can close this window at any time; running the builder again continues.'
Write-Host ''

if (-not [Environment]::Is64BitOperatingSystem) { Stop-Build 'This builder needs 64-bit Windows 10 or 11.' }

# A short folder outside OneDrive and user folders, on a drive with room.
# An earlier build's folder is reused so finished steps are kept.
$Work = $env:MELEE_BUILD_DIR
if (-not $Work) {
    $drives = @(Get-CimInstance Win32_LogicalDisk -Filter 'DriveType=3' |
        Sort-Object @{ Expression = { $_.DeviceID -ne 'C:' } }, DeviceID)
    foreach ($d in $drives) {
        if (Test-Path "$($d.DeviceID)\MeleeBuild\source") { $Work = "$($d.DeviceID)\MeleeBuild"; break }
    }
    if (-not $Work) {
        foreach ($d in $drives) {
            if ($d.FreeSpace -ge $NeedBytes -and (Test-Writable "$($d.DeviceID)\MeleeBuild")) {
                $Work = "$($d.DeviceID)\MeleeBuild"; break
            }
        }
    }
    if (-not $Work -and (Test-Writable "$env:LOCALAPPDATA\MeleeBuild")) {
        $free = (Get-PSDrive ($env:LOCALAPPDATA.Substring(0, 1))).Free
        if ($free -ge $NeedBytes) { $Work = "$env:LOCALAPPDATA\MeleeBuild" }
    }
    if (-not $Work) { Stop-Build 'No drive has 8 GB of free space for the build. Free up some space and run the builder again.' }
}
New-Item -ItemType Directory -Force -Path $Work | Out-Null
Write-Host "Working folder: $Work"
Write-Host ''

$Python = Join-Path $Work 'python\python.exe'
if (-not (Test-Path $Python)) {
    Write-Host 'Getting a private copy of Python (15 MB)...'
    $package = Join-Path $Work 'downloads\python.3.14.7.zip'
    New-Item -ItemType Directory -Force -Path (Split-Path $package) | Out-Null
    if (-not (Test-Path $package) -or (Get-FileHash $package -Algorithm SHA256).Hash -ne $PythonSha256) {
        try {
            Invoke-WebRequest -UseBasicParsing -Uri $PythonUrl -OutFile $package
        } catch {
            Stop-Build "Could not download Python ($($_.Exception.Message)).`nCheck your internet connection, then run the builder again."
        }
        if ((Get-FileHash $package -Algorithm SHA256).Hash -ne $PythonSha256) {
            Remove-Item $package
            Stop-Build 'The Python download was damaged. Run the builder again.'
        }
    }
    $unpack = Join-Path $Work 'python-unpack'
    $dest = Join-Path $Work 'python'
    foreach ($dir in @($unpack, $dest)) { if (Test-Path $dir) { Remove-Item -Recurse -Force $dir } }
    Add-Type -AssemblyName System.IO.Compression.FileSystem
    [IO.Compression.ZipFile]::ExtractToDirectory($package, $unpack)
    Move-Item (Join-Path $unpack 'tools') $dest
    Remove-Item -Recurse -Force $unpack
}

$arguments = @((Join-Path $Builder 'tools\easy_build.py'), '--work', $Work)
if ($Iso) { $arguments += @('--iso', $Iso) }
& $Python @arguments
exit $LASTEXITCODE
