# SPDX-License-Identifier: GPL-3.0-or-later
$ErrorActionPreference = 'Stop'
$flasherScript = Join-Path $PSScriptRoot 'local-flasher.py'
$pythonLauncher = Get-Command py -ErrorAction SilentlyContinue
if ($pythonLauncher) {
    & $pythonLauncher.Source -3 $flasherScript @args
    exit $LASTEXITCODE
}
$pythonExecutable = Get-Command python -ErrorAction SilentlyContinue
if ($pythonExecutable -and $pythonExecutable.Source -notlike '*WindowsApps*') {
    & $pythonExecutable.Source $flasherScript @args
    exit $LASTEXITCODE
}
$codexPython = Join-Path $env:USERPROFILE '.cache\codex-runtimes\codex-primary-runtime\dependencies\python\python.exe'
if (Test-Path -LiteralPath $codexPython) {
    & $codexPython $flasherScript @args
    exit $LASTEXITCODE
}
Write-Error 'Python 3.9+ is required. Install Python, then run Start-Flasher.cmd again.'
exit 1
