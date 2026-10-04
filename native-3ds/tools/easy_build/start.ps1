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
$Issues = 'https://github.com/2gifts/melee-3ds/issues'

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

# Plain ASCII without spaces: the compilers are happiest with such paths.
function Test-PlainPath($Dir) { return $Dir -match '^[A-Za-z]:\\[A-Za-z0-9_\\.-]*$' }

function Get-Python($Package) {
    # Three tries, then Windows' own curl.exe (same pinned file either way).
    for ($try = 1; $try -le 3; $try++) {
        try {
            Invoke-WebRequest -UseBasicParsing -Uri $PythonUrl -OutFile $Package -TimeoutSec 120
            return
        } catch {
            $last = $_.Exception.Message
            Start-Sleep -Seconds (5 * $try)
        }
    }
    $curl = Join-Path $env:SystemRoot 'System32\curl.exe'
    if (Test-Path $curl) {
        try {
            & $curl -fsSL --retry 3 -o $Package $PythonUrl 2>$null
            if ($LASTEXITCODE -eq 0) { return }
        } catch {}
    }
    Stop-Build ("Could not download Python ($last).`n" +
        "Check your internet connection. Also make sure your PC's date and time are correct`n" +
        "(Windows Settings > Time & language). Then run the builder again.")
}

function Install-Python($Work) {
    $package = Join-Path $Work 'downloads\python.3.14.7.zip'
    New-Item -ItemType Directory -Force -Path (Split-Path $package) | Out-Null
    if (-not (Test-Path $package) -or (Get-FileHash $package -Algorithm SHA256).Hash -ne $PythonSha256) {
        Write-Host 'Getting a private copy of Python (15 MB)...'
        Get-Python $package
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

function Test-Python($Python) {
    if (-not (Test-Path $Python)) { return $false }
    try {
        $out = & $Python -c "import ssl, zipfile, ctypes, json; print('python ok')" 2>$null
        return ($LASTEXITCODE -eq 0 -and "$out" -match 'python ok')
    } catch {
        return $false
    }
}

try {
    Write-Host 'Melee for New 3DS - CIA builder'
    Write-Host '==============================='
    Write-Host 'This builds the game from your own Melee disc image. The first build takes'
    Write-Host '10 to 30 minutes and needs an internet connection and about 8 GB of free space.'
    Write-Host 'You can close this window at any time; running the builder again continues.'
    Write-Host ''

    if (-not [Environment]::Is64BitOperatingSystem -or [Environment]::OSVersion.Version.Major -lt 10) {
        Stop-Build 'This builder needs 64-bit Windows 10 or 11.'
    }
    if ($ExecutionContext.SessionState.LanguageMode -ne 'FullLanguage') {
        Stop-Build ("This PC does not allow the builder to run (it is probably a school or work PC`n" +
            'with programs locked down). Please use another PC.')
    }
    if ((Get-Date).Year -lt 2026) {
        Stop-Build ("Your PC's date is set to $((Get-Date).ToString('d MMMM yyyy')), which is wrong. Downloads cannot`n" +
            "work until it is correct. In Windows Settings > Time & language > Date & time, turn on`n" +
            '"Set time automatically" (or set the right date), then run the builder again.')
    }

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
        # Some PCs do not allow folders at the top of a drive. The shared
        # Public folder has a plain path; a user folder may contain spaces or
        # accented letters (in the user's name).
        foreach ($candidate in @("$env:PUBLIC\MeleeBuild", "$env:LOCALAPPDATA\MeleeBuild")) {
            if ($Work -or -not $candidate -or $candidate.StartsWith('\')) { continue }
            if (Test-Writable $candidate) {
                $free = (Get-PSDrive ($candidate.Substring(0, 1))).Free
                if ($free -ge $NeedBytes) { $Work = $candidate }
            }
        }
        if (-not $Work) {
            Stop-Build ('No drive has 8 GB of free space for the build. Free up some space (empty the Recycle Bin,' +
                "`ndelete big files you do not need), then run the builder again.")
        }
    }
    New-Item -ItemType Directory -Force -Path $Work | Out-Null
    Write-Host "Working folder: $Work"
    if (-not (Test-PlainPath $Work)) {
        Write-Host '(This folder name has spaces or special letters. If the build fails, mention it when' -ForegroundColor Yellow
        Write-Host ' you ask for help.)' -ForegroundColor Yellow
    }
    Write-Host ''

    $Python = Join-Path $Work 'python\python.exe'
    if (-not (Test-Python $Python)) {
        Install-Python $Work
        if (-not (Test-Python $Python)) {
            Stop-Build ("The builder's copy of Python does not start. Antivirus software may have blocked or`n" +
                "removed it: allow the folder $Work in your antivirus, then run the builder again.`n" +
                'Windows in "S mode" cannot run the builder at all.')
        }
    }

    $arguments = @((Join-Path $Builder 'tools\easy_build.py'), '--work', $Work)
    if ($Iso) { $arguments += @('--iso', $Iso) }
    & $Python @arguments
    exit $LASTEXITCODE
} catch {
    Write-Host ''
    Write-Host 'The builder could not start:' -ForegroundColor Yellow
    Write-Host "   $($_.Exception.Message)" -ForegroundColor Yellow
    Write-Host "If this keeps happening, ask for help at $Issues and include this message."
    exit 1
}
