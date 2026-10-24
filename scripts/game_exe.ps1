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
$path = Join-Path $GameDirectory $exe
if (Test-Path -LiteralPath $path -PathType Leaf) {
    $stream = [IO.File]::OpenRead($path)
    $reader = New-Object IO.BinaryReader($stream)
    try {
        if ($stream.Length -ge 64 -and $reader.ReadUInt16() -eq 0x5A4D) {
            $stream.Position = 0x3c
            $offset = $reader.ReadUInt32()
            if ($offset -le $stream.Length - 248) {
                $stream.Position = $offset
                if ($reader.ReadUInt32() -eq 0x4550) {
                    $stream.Position = $offset + 24
                    if ($reader.ReadUInt16() -eq 0x10b) {
                        $stream.Position = $offset + 232
                        if ($reader.ReadUInt32() -ne 0) { $exe = 'appx-host.exe' }
                    }
                }
            }
        }
    } finally { $reader.Dispose() }
}
Write-Output $exe
