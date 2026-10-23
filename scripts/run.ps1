#requires -Version 5.1
# Starts the staged game and waits for it to exit. Logging is off unless asked for:
#   -Log          failures and notes in appx-portable.log beside the game
#   -Trace        every traced call as well (implies -Log)
#   -Capture      back buffer to appx-frame.bmp on the first and every 120th frame
#   -Diagnostics  crashes with stack details, and heap checks (implies -Log)
#   -Passthrough  activate the operating system's classes and only log
#   -UnlockTrial  report an active, full base-app license
[CmdletBinding()]
param(
    [string]$GameDirectory = (Join-Path $PSScriptRoot '../local/game'),
    [switch]$Log,
    [switch]$Trace,
    [switch]$Capture,
    [switch]$Diagnostics,
    [switch]$Passthrough,
    [switch]$UnlockTrial
)
$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest
$game = (Resolve-Path -LiteralPath $GameDirectory).ProviderPath
$exe = & (Join-Path $PSScriptRoot 'game_exe.ps1') -GameDirectory $game
foreach ($name in @($exe, 'appxportable.dll')) {
    if (-not (Test-Path -LiteralPath (Join-Path $game $name) -PathType Leaf)) { throw "Missing $name; run setup.ps1 first." }
}
$arguments = @()
if ($Log) { $arguments += '--log' }
if ($Trace) { $arguments += '--trace' }
if ($Capture) { $arguments += '--capture' }
if ($Diagnostics) { $arguments += '--diagnostics' }
if ($Passthrough) { $arguments += '--passthrough' }
if ($UnlockTrial) { $arguments += '--unlock-trial' }
$start = @{ FilePath = (Join-Path $game $exe); WorkingDirectory = $game; PassThru = $true; Wait = $true }
if ($arguments) { $start.ArgumentList = $arguments }
$process = Start-Process @start
if ($process.ExitCode -ne 0) {
    $hint = if ($arguments) { " See $game\appx-portable.log" } else { ' Run again with -Log for details.' }
    throw "$exe exited with code $($process.ExitCode).$hint"
}
