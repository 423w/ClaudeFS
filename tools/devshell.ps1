<#
.SYNOPSIS
  Put MSVC (cl), CMake and Ninja on PATH in the *current* PowerShell session.

.DESCRIPTION
  The Ninja generator needs a compiler environment (PATH, INCLUDE, LIB, ...) that only Visual Studio's
  vcvars64.bat knows how to build. This script finds the newest VS with the C++ tools via vswhere, runs
  vcvars64.bat in cmd.exe, and copies the resulting environment back into PowerShell. VS 2022 also puts
  its bundled CMake and Ninja on PATH, so nothing else needs installing.

  Dot-source it so the changes stay in your shell:
      . .\tools\devshell.ps1
      cmake --preset release; cmake --build --preset release; ctest --preset release

  (The "x64 Native Tools Command Prompt for VS 2022" from the Start menu does the same thing, but in cmd.)
#>

# Note: no `$ErrorActionPreference = 'Stop'` here. This script is dot-sourced, so it would leak into your
# shell, and Windows PowerShell 5.1 would then treat any stderr line from cmake/ctest as a fatal error.
# Failures below `throw` explicitly instead.

# Some shells start without the Windows system folders on PATH (seen on the owner's machine), and
# vcvars64.bat needs cmd.exe, where.exe, reg.exe and friends from System32. Add them if missing.
$sys = Join-Path $env:SystemRoot 'System32'
if (-not (($env:Path -split ';') -contains $sys)) {
    $env:Path = "$sys;$env:SystemRoot;$sys\Wbem;$sys\WindowsPowerShell\v1.0;$env:Path"
}

$vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
if (-not (Test-Path $vswhere)) { throw "vswhere.exe not found: is Visual Studio (or its Build Tools) installed?" }

# -latest picks the newest install (VS 2022 over 2019); -requires makes sure it has the x64 C++ compiler.
$vs = & $vswhere -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
if (-not $vs) { throw "No Visual Studio install with the C++ x64 tools was found." }

$vcvars = Join-Path $vs 'VC\Auxiliary\Build\vcvars64.bat'
# vcvars64 itself calls vswhere by name, so put the Installer folder on PATH to keep it quiet.
$env:Path = "$(Split-Path $vswhere);$env:Path"

# Run vcvars64 in cmd, print the resulting environment with `set`, and import every NAME=value line.
$cmd = Join-Path $sys 'cmd.exe'
& $cmd /c "`"$vcvars`" >nul && set" | ForEach-Object {
    if ($_ -match '^([^=]+)=(.*)$') { Set-Item -Path "Env:$($Matches[1])" -Value $Matches[2] }
}

foreach ($tool in 'cl', 'cmake', 'ninja') {
    $found = Get-Command $tool -ErrorAction SilentlyContinue
    if (-not $found) { throw "$tool is still not on PATH after vcvars64; check the VS C++ and CMake components." }
}
Write-Host "CFS dev shell ready: $(Split-Path -Leaf $vs) | $((cmake --version)[0]) | ninja $(ninja --version)"
