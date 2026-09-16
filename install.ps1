param([string]$GameDirectory='C:\IIDX', [string]$BuildDirectory=(Join-Path $PSScriptRoot 'build'))
$ErrorActionPreference='Stop'
foreach($name in @('IIDXLive.dll','iidx-live-obs.dll','IIDXLive.ini')) {
    if(-not (Test-Path -LiteralPath (Join-Path $BuildDirectory $name))) { throw "Build output missing: $name. Run build.ps1 first." }
}
$gamePath=(Resolve-Path -LiteralPath $GameDirectory).Path
if (-not (Test-Path -LiteralPath (Join-Path $gamePath 'bm2dx.dll'))) { throw 'bm2dx.dll not found in game directory.' }
if (Get-Process spice,spice64 -ErrorAction SilentlyContinue) { throw 'Close the game before installing.' }
$pluginPath=Join-Path $env:ProgramData 'obs-studio/plugins/iidx-live-obs'
$pluginDll=Join-Path $pluginPath 'bin/64bit/iidx-live-obs.dll'
if ((Test-Path -LiteralPath $pluginDll) -and (Get-Process obs64 -ErrorAction SilentlyContinue)) { throw 'Close OBS before replacing an existing IIDXLive OBS plugin.' }
$backup=Join-Path $gamePath ('IIDXLive-backup/'+(Get-Date -Format 'yyyyMMdd-HHmmss'))
New-Item -ItemType Directory -Force -Path $backup | Out-Null
foreach($name in @('IIDXLive.dll','IIDXLive.ini')) {
    $target=Join-Path $gamePath $name
    if(Test-Path -LiteralPath $target){Copy-Item -LiteralPath $target -Destination (Join-Path $backup $name)}
}
if(Test-Path -LiteralPath $pluginDll){Copy-Item -LiteralPath $pluginDll -Destination (Join-Path $backup 'iidx-live-obs.dll')}
Copy-Item -LiteralPath (Join-Path $BuildDirectory 'IIDXLive.dll') -Destination $gamePath
foreach($name in @('IIDXLive.ini')){
    $target=Join-Path $gamePath $name
    if(-not (Test-Path -LiteralPath $target)){Copy-Item -LiteralPath (Join-Path $BuildDirectory $name) -Destination $target}
}
New-Item -ItemType Directory -Force -Path (Join-Path $pluginPath 'bin/64bit'),(Join-Path $pluginPath 'data') | Out-Null
Copy-Item -LiteralPath (Join-Path $BuildDirectory 'iidx-live-obs.dll') -Destination $pluginDll
Copy-Item -LiteralPath (Join-Path $PSScriptRoot 'README.md') -Destination (Join-Path $pluginPath 'data/README.md')
Write-Output "Installed. Backup: $backup"
Write-Output 'Restart OBS, then add the IIDX source. Add IIDXLive.dll to the launcher DLL list independently of IIDXRecorder.dll.'
