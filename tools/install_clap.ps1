<#
.SYNOPSIS
  Copy the Release CFS.clap to a folder FL Studio (or any CLAP host) scans.

.DESCRIPTION
  CLAP hosts on Windows look in %COMMONPROGRAMFILES%\CLAP (C:\Program Files\Common Files\CLAP) by default.
  Writing there needs an ELEVATED PowerShell ("Run as administrator"). Without elevation, pass -Dest with a
  folder of your own and add that folder in FL Studio: Options > Manage plugins > Plugin search paths.

  FL Studio keeps the DLL open while a project uses CFS, so close the project (or FL) before re-installing,
  or the copy fails with "file in use".

.EXAMPLE
  .\tools\install_clap.ps1                       # elevated: installs to C:\Program Files\Common Files\CLAP
.EXAMPLE
  .\tools\install_clap.ps1 -Dest D:\Plugins\CLAP # any folder you have added to FL's plugin search paths
#>
param(
    [string]$Dest = (Join-Path $env:CommonProgramFiles 'CLAP'),
    [string]$Build = (Join-Path $PSScriptRoot '..\build\release\hosts\clap\CFS.clap')
)

if (-not (Test-Path $Build)) { throw "Not found: $Build. Build first: . .\tools\devshell.ps1; cmake --workflow --preset release" }
if (-not (Test-Path $Dest)) { New-Item -ItemType Directory -Force $Dest | Out-Null }
Copy-Item -Force $Build $Dest
$installed = Join-Path $Dest 'CFS.clap'
$hash = (Get-FileHash $installed -Algorithm SHA256).Hash.Substring(0, 12)
Write-Host "Installed $installed (SHA-256 $hash...). In FL Studio: Options > Manage plugins > Find installed plugins."
