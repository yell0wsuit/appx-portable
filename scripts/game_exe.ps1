#requires -Version 3.0
# Prints the game executable a staged folder's AppxManifest.xml names, or
# Game.exe when the folder has no manifest. Used by run.ps1 and play.bat.
param([Parameter(Mandatory = $true)][string]$GameDirectory)
$ErrorActionPreference = 'Stop'
$exe = 'Game.exe'
$manifest = Join-Path $GameDirectory 'AppxManifest.xml'
if (Test-Path -LiteralPath $manifest -PathType Leaf) {
    $settings = New-Object Xml.XmlReaderSettings
    $settings.DtdProcessing = [Xml.DtdProcessing]::Prohibit
    $settings.XmlResolver = $null
    $reader = [Xml.XmlReader]::Create($manifest, $settings)
    try { $xml = New-Object Xml.XmlDocument; $xml.Load($reader) } finally { $reader.Dispose() }
    # By local name: Windows 8, 8.1 and 10 manifests use different namespaces.
    $application = $xml.SelectSingleNode("//*[local-name()='Application']")
    if ($application -and $application.GetAttribute('Executable')) { $exe = $application.GetAttribute('Executable') }
}
Write-Output $exe
