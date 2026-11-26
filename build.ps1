# Builds Kanban Desktop and its setup into .\dist (plus the setup's .sha256 for the updater)
param([switch]$Clean)
$ErrorActionPreference = 'Stop'
$root = $PSScriptRoot
$build = Join-Path $root 'build'
$dist = Join-Path $root 'dist'

foreach ($tool in 'cmake', 'ninja', 'g++', 'windres') {
    if (-not (Get-Command $tool -ErrorAction SilentlyContinue)) {
        throw "$tool was not found on PATH. Install WinLibs: winget install BrechtSanders.WinLibs.POSIX.UCRT"
    }
}
if ($Clean -and (Test-Path $build)) { Remove-Item $build -Recurse -Force }
if (-not (Test-Path (Join-Path $root 'res\app.ico'))) { python (Join-Path $root 'tools\make_icon.py') }

cmake -S $root -B $build -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_CXX_COMPILER=g++ | Out-Null
if ($LASTEXITCODE) { throw 'CMake configuration failed' }
cmake --build $build
if ($LASTEXITCODE) { throw 'Build failed' }

New-Item -ItemType Directory -Force $dist | Out-Null
try { Copy-Item (Join-Path $build 'KanbanDesktop.exe') $dist -Force -ErrorAction Stop }
catch { Write-Warning 'dist\KanbanDesktop.exe is running and was not replaced (close it with: dist\KanbanDesktop.exe --quit)' }
Copy-Item (Join-Path $build 'KanbanDesktop-Setup.exe') $dist -Force
# The app checks a downloaded update against this before running it.
$setup = Join-Path $dist 'KanbanDesktop-Setup.exe'
$hash = (Get-FileHash $setup -Algorithm SHA256).Hash.ToLowerInvariant()
[IO.File]::WriteAllText("$setup.sha256", "$hash  KanbanDesktop-Setup.exe`n")
Get-ChildItem $dist -Filter *.exe | ForEach-Object { '{0,-28} {1,8:N0} KB' -f $_.Name, ($_.Length / 1KB) }
"sha256 $hash"
