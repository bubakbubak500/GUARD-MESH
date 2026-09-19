# SPDX-License-Identifier: GPL-3.0-or-later
$ErrorActionPreference = 'Stop'
$simulatorScript = Join-Path (Split-Path $PSScriptRoot) 'simulator\run.py'
$pythonLauncher = Get-Command py -ErrorAction SilentlyContinue
if ($pythonLauncher) {
    & $pythonLauncher.Source -3 $simulatorScript @args
    exit $LASTEXITCODE
}
$pythonExecutable = Get-Command python -ErrorAction SilentlyContinue
if ($pythonExecutable -and $pythonExecutable.Source -notlike '*WindowsApps*') {
    & $pythonExecutable.Source $simulatorScript @args
    exit $LASTEXITCODE
}
$codexPython = Join-Path $env:USERPROFILE '.cache\codex-runtimes\codex-primary-runtime\dependencies\python\python.exe'
if (Test-Path -LiteralPath $codexPython) {
    & $codexPython $simulatorScript @args
    exit $LASTEXITCODE
}
throw 'Python 3.9+ and Git are required. Install them, then run Start-Simulator.cmd again.'
