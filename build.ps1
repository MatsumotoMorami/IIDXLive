param(
    [Parameter(Mandatory=$true)][string]$ZigPath,
    [Parameter(Mandatory=$true)][string]$ObsSource,
    [string]$OutputDirectory = (Join-Path $PSScriptRoot 'build')
)
$ErrorActionPreference = 'Stop'
$ZigPath = (Resolve-Path -LiteralPath $ZigPath).Path
$ObsSource = (Resolve-Path -LiteralPath $ObsSource).Path
New-Item -ItemType Directory -Force -Path $OutputDirectory | Out-Null
$OutputDirectory = (Resolve-Path -LiteralPath $OutputDirectory).Path
# OBS headers must match the installed OBS API. Tested against obs-studio tag 32.2.2.
Set-Content -LiteralPath (Join-Path $OutputDirectory 'obsconfig.h') -Value '#pragma once'
$mh = Join-Path $PSScriptRoot 'vendor/minhook'
$mhFiles = @('src/buffer.c','src/hook.c','src/trampoline.c','src/hde/hde64.c') | ForEach-Object { Join-Path $mh $_ }
& $ZigPath c++ (Join-Path $PSScriptRoot 'game.cpp') @mhFiles "-I$mh/include" -shared -O2 -static -target x86_64-windows-gnu -o (Join-Path $OutputDirectory 'IIDXLive.dll')
if ($LASTEXITCODE) { throw 'Game bridge build failed' }
& $ZigPath dlltool -d (Join-Path $PSScriptRoot 'obs.def') -l (Join-Path $OutputDirectory 'obs.lib') -m i386:x86-64
if ($LASTEXITCODE) { throw 'OBS import library generation failed' }
& $ZigPath c++ (Join-Path $PSScriptRoot 'obs-plugin.cpp') (Join-Path $OutputDirectory 'obs.lib') "-I$ObsSource/libobs" "-I$OutputDirectory" -shared -O2 -static -std=c++17 -target x86_64-windows-gnu -o (Join-Path $OutputDirectory 'iidx-live-obs.dll')
if ($LASTEXITCODE) { throw 'OBS plugin build failed' }
Copy-Item -LiteralPath (Join-Path $PSScriptRoot 'IIDXLive.ini') -Destination $OutputDirectory
