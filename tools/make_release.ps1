# Makes one version of the collection, laid out as its two GitHub releases serve it (src/games.h).
#
#   powershell -ExecutionPolicy Bypass -File tools\make_release.ps1 -Version 1.0.0 -Out dist\releases
#   powershell -ExecutionPolicy Bypass -File tools\make_release.ps1 -Version 1.0.1 -Earlier 1.0.0 -Out dist\releases
#
# Writes <Out>\v<Version>\:
#   WhoppingCollection-v<Version>-windows-x64.zip   the release players see (tag v<Version>): the portable
#                                launcher with the three games in its games folder, unzipped anywhere (as King
#                                Kong Recompiled ships); each game asks for its disc image the first time
#   packages\                    the packages release (tag packages-v<Version>), what the launcher downloads:
#     WhoppingCollection-v<Version>-windows-x64.zip                        the launcher's own update
#     SneakKing-, BigBumpin-, PocketBikeRacer-v<Version>-windows-x64.zip  the games
#     SHA256SUMS.txt                                                       all four
# Each zip holds a file list (update-files.txt) made by the TRG updater's make_update_files.py, so updates
# replace exactly the release's files and never the protected ones (disc files, settings, saves, installs).
# Set VERSION to the release's version; the launcher is rebuilt here, the games must be built first with each
# game's build.bat <short>-release.
param(
  [Parameter(Mandatory)][string]$Version,
  [Parameter(Mandatory)][string]$Out,
  [string]$Earlier = ''
)
$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
$tool = Join-Path $root 'third_party\trg-launcher\tools\make_update_files.py'
$sdk = Join-Path $root 'tools\rexglue\win-amd64\bin'
$crt = 'C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Redist\MSVC\14.44.35112\x64\Microsoft.VC143.CRT'
$crtFiles = 'msvcp140.dll', 'msvcp140_atomic_wait.dll', 'vcruntime140.dll', 'vcruntime140_1.dll'
$parts = @(
  @{ Folder = 'WhoppingCollection'; Build = Join-Path $root 'build'; Exe = 'whopping_launcher.exe'; Sdk = $false;
     Protect = @('games', 'save-backups', 'cache', 'whopping.txt', 'test-userdata') },
  @{ Folder = 'SneakKing'; Id = 'sneak_king'; Build = Join-Path $root 'games\sneak_king\out\build\bk-release'; Exe = 'sneak_king.exe'; Sdk = $true;
     Protect = @('game', 'sneak_king.toml', 'logs') },
  @{ Folder = 'BigBumpin'; Id = 'big_bumpin'; Build = Join-Path $root 'games\big_bumpin\out\build\bb-release'; Exe = 'big_bumpin.exe'; Sdk = $true;
     Protect = @('game', 'big_bumpin.toml', 'logs') },
  @{ Folder = 'PocketBikeRacer'; Id = 'pocketbike_racer'; Build = Join-Path $root 'games\pocketbike_racer\out\build\pr-release'; Exe = 'pocketbike_racer.exe'; Sdk = $true;
     Protect = @('game', 'pocketbike_racer.toml', 'logs') }
)
# The launcher reports the VERSION file's version: it must be this release's, and the launcher is rebuilt with it.
$fileVersion = (Get-Content (Join-Path $root 'VERSION') -TotalCount 1).Trim()
if ($fileVersion -ne $Version) { throw "VERSION says ${fileVersion}, set it to $Version first" }
cmd /c "`"$(Join-Path $root 'build.bat')`"" | Out-Null
if ($LASTEXITCODE -ne 0) { throw 'the launcher did not build' }
$Out = [IO.Path]::GetFullPath($Out)
$top = Join-Path $Out "v$Version"
if (Test-Path $top) { throw "$top already exists" }
$tag = Join-Path $top 'packages'
New-Item -ItemType Directory -Force $tag | Out-Null
$stageRoot = Join-Path $Out "stage\v$Version"

foreach ($p in $parts) {
  $exe = Join-Path $p.Build $p.Exe
  if (-not (Test-Path $exe)) { throw "$exe is missing: build it first" }
  if ($p.Sdk) {  # no developer aids in a release
    $text = [Text.Encoding]::ASCII.GetString([IO.File]::ReadAllBytes($exe))
    $dev = [regex]::Matches($text, 'BK_DEV_\w+|BK_DUMP_IMAGE').Count
    if ($dev) { throw "$($p.Exe) is a developer build ($dev developer strings)" }
  }
  $stage = Join-Path $stageRoot $p.Folder
  New-Item -ItemType Directory -Force $stage | Out-Null
  Copy-Item $exe $stage
  if ($p.Sdk) { Copy-Item (Join-Path $sdk 'rexruntime.dll'), (Join-Path $sdk 'rexgpu-xenos.dll') $stage }
  foreach ($f in $crtFiles) { Copy-Item (Join-Path $crt $f) $stage }
  $zip = Join-Path $tag "$($p.Folder)-v$Version-windows-x64.zip"
  # Each part's own name pattern, as the launcher's updater picks it from the release (install.cpp, app.cpp).
  $pyArgs = @($tool, 'package', $stage, '--version', $Version, '--zip', $zip, '--asset', "$($p.Folder)-*-windows-x64.zip")
  foreach ($x in $p.Protect) { $pyArgs += @('--protect', $x) }
  if ($Earlier) { $pyArgs += @('--earlier', (Join-Path $Out "v$Earlier\packages\$($p.Folder)-v$Earlier-windows-x64.zip")) }
  $log = & python.exe @pyArgs 2>&1
  if ($LASTEXITCODE -ne 0) { $log | Select-Object -Last 10; throw "packaging $($p.Folder) failed" }
  "{0,-48} {1,6:N1} MB" -f (Split-Path -Leaf $zip), ((Get-Item $zip).Length / 1MB)
}

# The players' release: the launcher's zip with each game's zip unpacked into games\<id>, exactly as the
# launcher's own download would put it there (its update list included, so game updates work as usual).
$players = Join-Path $stageRoot 'players'
if (Test-Path $players) { Remove-Item -Recurse -Force $players }
Expand-Archive (Join-Path $tag "WhoppingCollection-v$Version-windows-x64.zip") $players
foreach ($p in $parts | Where-Object { $_.Id }) {
  $unpack = Join-Path $stageRoot "unpack-$($p.Folder)"
  if (Test-Path $unpack) { Remove-Item -Recurse -Force $unpack }
  Expand-Archive (Join-Path $tag "$($p.Folder)-v$Version-windows-x64.zip") $unpack
  $dest = Join-Path $players "WhoppingCollection\games\$($p.Id)"
  New-Item -ItemType Directory -Force $dest | Out-Null
  Copy-Item (Join-Path $unpack "$($p.Folder)\*") $dest -Recurse
}
$playersZip = Join-Path $top "WhoppingCollection-v$Version-windows-x64.zip"
Compress-Archive (Join-Path $players 'WhoppingCollection') $playersZip
"{0,-48} {1,6:N1} MB  (players: the launcher and the three games)" -f (Split-Path -Leaf $playersZip), ((Get-Item $playersZip).Length / 1MB)

# One SHA256SUMS.txt for the whole release (each package call above wrote one for its own zip only).
$sums = Get-ChildItem $tag -Filter '*.zip' | Sort-Object Name | ForEach-Object {
  "$((Get-FileHash $_.FullName -Algorithm SHA256).Hash.ToLower())  $($_.Name)"
}
$sums | Set-Content (Join-Path $tag 'SHA256SUMS.txt') -Encoding ascii
"SHA256SUMS.txt: $(@($sums).Count) zips"
"SDK pair: rexruntime $((Get-FileHash (Join-Path $sdk 'rexruntime.dll')).Hash.Substring(0, 8)), " +
  "rexgpu-xenos $((Get-FileHash (Join-Path $sdk 'rexgpu-xenos.dll')).Hash.Substring(0, 8))"
"Next: every zip through VirusTotal before anything is published."
