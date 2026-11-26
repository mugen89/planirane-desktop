# Publishes a new version: sets it in CMakeLists.txt, checks that CHANGELOG.md has its section, builds once
# locally (a broken build is never tagged), commits, tags vX.Y.Z and pushes. GitHub Actions then builds the
# setup and publishes the release (.github/workflows/release.yml); installed copies update themselves.
#
#   .\release.ps1 0.2.0
param([Parameter(Mandatory = $true)][string]$Version)
$ErrorActionPreference = 'Stop'
Set-Location $PSScriptRoot

if ($Version -notmatch '^\d+\.\d+\.\d+$') { throw 'The version looks like 1.2.3' }
if (git tag --list "v$Version") { throw "v$Version already exists" }
$other = git status --porcelain | Where-Object { $_ -notmatch ' (CHANGELOG\.md|CMakeLists\.txt)$' }
if ($other) { throw "Commit the other changes first:`n$($other -join "`n")" }
$log = [IO.File]::ReadAllText((Join-Path $PSScriptRoot 'CHANGELOG.md'))
if ($log -notmatch "(?m)^## $([regex]::Escape($Version))\b") { throw "Add a '## $Version' section to CHANGELOG.md first (it becomes the release notes)" }

$cm = Join-Path $PSScriptRoot 'CMakeLists.txt'
$text = [IO.File]::ReadAllText($cm) -replace 'project\(KanbanDesktop VERSION [0-9.]+', "project(KanbanDesktop VERSION $Version"
[IO.File]::WriteAllText($cm, $text, (New-Object Text.UTF8Encoding $false))

.\build.ps1
if ($LASTEXITCODE) { throw 'The build failed: nothing was committed' }

git add CMakeLists.txt CHANGELOG.md
git commit -m "Release $Version"
git tag "v$Version"
git push origin HEAD "v$Version"
"Pushed v$Version. The release appears at https://github.com/mugen89/planirane-desktop/releases in a few minutes."
