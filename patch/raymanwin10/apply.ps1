#requires -Version 5.1
[CmdletBinding()]
param([Parameter(Mandatory = $true)][string]$GameDirectory)
$ErrorActionPreference = 'Stop'
$game = (Resolve-Path -LiteralPath $GameDirectory).Path
$manifest = [xml](Get-Content -LiteralPath (Join-Path $game 'AppxManifest.xml') -Raw)
$identity = $manifest.SelectSingleNode("//*[local-name()='Identity']")
if ($identity.GetAttribute('Name') -ne 'Ubisoft.RaymanFiestaRunWindows10Edition' -or
    $identity.GetAttribute('Version') -ne '1.0.23.0') {
    throw 'This caption patch supports Rayman Fiesta Run Windows 10 Edition 1.0.23.0.'
}
Copy-Item -LiteralPath (Join-Path $PSScriptRoot 'CustomTitleBar.xaml') -Destination (Join-Path $game 'CustomTitleBar.xaml') -Force
Write-Output "Prepared portable caption bindings in $game. The game executable is unchanged."
