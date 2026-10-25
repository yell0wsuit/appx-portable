#requires -Version 5.1
# Stages an extracted Store package as a portable game folder:
#   - copies the package, leaving out its signature and package metadata
#   - adds the Visual C++ runtime the manifest depends on (VCLibs 110/120/140)
#   - decodes the percent-encoded names a raw .appx extraction keeps
#   - adds appxportable.dll and the proxy DLLs that load it
# The game executable is never modified; its SHA-256 is checked after copying.
[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)][string]$SourceDirectory,
    [Parameter(Mandatory = $true)][string]$Destination,
    [string]$BuildDirectory = (Join-Path $PSScriptRoot '../build/windows-x86'),
    # Where matching-architecture VCLibs DLLs are, if the game folder lacks them: a
    # Microsoft.VCLibs.*.appx, its extracted or installed WindowsApps folder.
    [string[]]$RuntimeDirectory = @()
)
$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest

# The runtime DLLs each VCLibs framework package provides to Store apps.
$VCLibs = @{
    'Microsoft.VCLibs.110.00' = @('msvcr110.dll', 'msvcp110.dll', 'vccorlib110.dll')
    'Microsoft.VCLibs.120.00' = @('msvcr120_app.dll', 'msvcp120_app.dll', 'vccorlib120_app.dll')
    'Microsoft.VCLibs.140.00' = @('vcruntime140_app.dll', 'msvcp140_app.dll', 'vccorlib140_app.dll')
}
# System DLLs games import from their folder; each loads appxportable.dll.
$Proxies = @('d3dcompiler_47.dll', 'XAudio2_8.dll', 'DWrite.dll')
if (Test-Path -LiteralPath (Join-Path $BuildDirectory 'XAudio2_9.dll')) { $Proxies += 'XAudio2_9.dll' }
$OptionalVCLibs140 = @('concrt140_app.dll', 'vcruntime140_1_app.dll', 'msvcp140_1_app.dll',
    'msvcp140_2_app.dll', 'msvcp140_atomic_wait_app.dll', 'msvcp140_codecvt_ids_app.dll', 'vcamp140_app.dll', 'vcomp140_app.dll')
# Package files the game never reads, and files setup itself provides.
$Excluded = @('AppxSignature.p7x', 'AppxBlockMap.xml', '[Content_Types].xml', 'AppxMetadata',
    'microsoft.system.package.metadata', 'SaveData', 'appx-portable.ini', 'appxportable.dll') + $Proxies

function Get-FullFileSystemPath([string]$Path) {
    if ($PWD.Provider.Name -ne 'FileSystem') { throw 'Run setup from a filesystem directory.' }
    return $ExecutionContext.SessionState.Path.GetUnresolvedProviderPathFromPSPath($Path)
}
function Assert-NoReparseAncestors([string]$Path) {
    $current = $Path
    while ($current) {
        if (Test-Path -LiteralPath $current) {
            $item = Get-Item -LiteralPath $current -Force
            if ($item.Attributes -band [IO.FileAttributes]::ReparsePoint) {
                throw "Reparse points are not allowed in staging paths: $current"
            }
        }
        $parent = [IO.Path]::GetDirectoryName($current.TrimEnd('\', '/'))
        if ($parent -eq $current) { break }
        $current = $parent
    }
}
function Assert-NoReparseTree([string]$Path) {
    # Inspect each directory before descending; do not traverse junctions or symlinks.
    $pending = New-Object 'System.Collections.Generic.Stack[string]'
    $pending.Push($Path)
    while ($pending.Count) {
        foreach ($item in Get-ChildItem -LiteralPath $pending.Pop() -Force) {
            if ($item.Attributes -band [IO.FileAttributes]::ReparsePoint) {
                throw "Reparse points are not allowed in source or destination: $($item.FullName)"
            }
            if ($item.PSIsContainer) { $pending.Push($item.FullName) }
        }
    }
}
function Read-PEMachine([string]$Path) {
    if (-not (Test-Path -LiteralPath $Path -PathType Leaf)) { throw "Missing runtime file: $Path" }
    Assert-NoReparseAncestors $Path
    $stream = [IO.File]::OpenRead($Path)
    $reader = New-Object IO.BinaryReader($stream)
    try {
        if ($stream.Length -lt 64 -or $reader.ReadUInt16() -ne 0x5A4D) { throw "Invalid PE file: $Path" }
        $stream.Position = 0x3C
        $offset = $reader.ReadUInt32()
        if ($offset -gt $stream.Length - 6) { throw "Invalid PE header: $Path" }
        $stream.Position = $offset
        if ($reader.ReadUInt32() -ne 0x00004550) { throw "Invalid PE signature: $Path" }
        $machine = $reader.ReadUInt16()
        if ($machine -notin @(0x014C, 0x8664)) { throw "Expected an x86 or x64 PE file: $Path" }
        return $machine
    } finally { $reader.Dispose() }
}
function Assert-PE([string]$Path) {
    if ((Read-PEMachine $Path) -ne $gameMachine) { throw "PE architecture must match the game ($gameArchitecture): $Path" }
}
function Test-ManagedPE([string]$Path) {
    $stream = [IO.File]::OpenRead($Path)
    $reader = New-Object IO.BinaryReader($stream)
    try {
        $stream.Position = 0x3c
        $offset = $reader.ReadUInt32()
        if ($offset -gt $stream.Length - 248) { return $false }
        $stream.Position = $offset + 24
        $magic = $reader.ReadUInt16()
        $directoryOffset = if ($magic -eq 0x20b) { 112 } else { 96 }
        $stream.Position = $offset + 24 + $directoryOffset + 14 * 8
        if ($stream.Position + 8 -gt $stream.Length) { return $false }
        return $reader.ReadUInt32() -ne 0
    } finally { $reader.Dispose() }
}
function Copy-AtomicDll([string]$Source, [string]$Target) {
    $temporary = "$Target.$([Guid]::NewGuid().ToString('N')).tmp"
    try {
        [IO.File]::Copy($Source, $temporary, $false)
        if ([IO.File]::Exists($Target)) {
            # [NullString]: PowerShell turns $null into "" for string arguments.
            [IO.File]::Replace($temporary, $Target, [NullString]::Value)
        } else {
            [IO.File]::Move($temporary, $Target)
        }
    } finally {
        if ([IO.File]::Exists($temporary)) { [IO.File]::Delete($temporary) }
    }
}
# An .appx is a zip with the VCLibs DLLs at its root. Only those names are
# extracted, so entry paths never reach the file system.
function Expand-RuntimePackage([string]$Package, [string]$Target) {
    if ([IO.Path]::GetExtension($Package) -ne '.appx') { throw "Not a folder or an .appx package: $Package" }
    Add-Type -AssemblyName System.IO.Compression.FileSystem
    $names = @($VCLibs.Values | ForEach-Object { $_ }) + $OptionalVCLibs140
    [IO.Directory]::CreateDirectory($Target) | Out-Null
    try { $zip = [IO.Compression.ZipFile]::OpenRead($Package) } catch { throw "Cannot read $Package as an .appx package." }
    try {
        foreach ($entry in $zip.Entries) {
            $name = $names | Where-Object { $_ -eq $entry.FullName } | Select-Object -First 1
            if ($name) { [IO.Compression.ZipFileExtensions]::ExtractToFile($entry, (Join-Path $Target $name), $true) }
        }
    } finally { $zip.Dispose() }
}
# The manifest's executable and framework dependencies. Elements are matched by
# local name: Windows 8, 8.1 and 10 manifests use different namespaces.
function Read-PackageManifest([string]$Directory) {
    $path = Join-Path $Directory 'AppxManifest.xml'
    if (-not (Test-Path -LiteralPath $path -PathType Leaf)) {
        throw "Missing $path. Extract the whole .appx package, not only its files."
    }
    $settings = New-Object Xml.XmlReaderSettings
    $settings.DtdProcessing = [Xml.DtdProcessing]::Prohibit
    $settings.XmlResolver = $null
    $reader = [Xml.XmlReader]::Create($path, $settings)
    try { $xml = New-Object Xml.XmlDocument; $xml.Load($reader) } finally { $reader.Dispose() }
    $application = $xml.SelectSingleNode("//*[local-name()='Application']")
    if (-not $application) { throw "$path declares no Application." }
    $executable = $application.GetAttribute('Executable')
    if (-not $executable -or [IO.Path]::IsPathRooted($executable) -or $executable -match '(^|[\\/])\.\.([\\/]|$)') {
        throw "$path has an unusable Executable: '$executable'"
    }
    $dependencies = @($xml.SelectNodes("//*[local-name()='PackageDependency']") | ForEach-Object { $_.GetAttribute('Name') })
    return [pscustomobject]@{ Executable = $executable; Dependencies = $dependencies }
}

$source = Get-FullFileSystemPath $SourceDirectory
$destinationPath = Get-FullFileSystemPath $Destination
$build = Get-FullFileSystemPath $BuildDirectory
$runtimeFolders = @($RuntimeDirectory | ForEach-Object { Get-FullFileSystemPath $_ })
foreach ($path in @($source, $destinationPath, $build) + $runtimeFolders) { Assert-NoReparseAncestors $path }
if (-not (Test-Path -LiteralPath $source -PathType Container)) { throw "Missing game directory: $source" }
$sourcePrefix = $source.TrimEnd('\', '/') + [IO.Path]::DirectorySeparatorChar
$destinationPrefix = $destinationPath.TrimEnd('\', '/') + [IO.Path]::DirectorySeparatorChar
if ($sourcePrefix.StartsWith($destinationPrefix, [StringComparison]::OrdinalIgnoreCase) -or
    $destinationPrefix.StartsWith($sourcePrefix, [StringComparison]::OrdinalIgnoreCase)) {
    throw 'Source and destination must not overlap; use a separate staging directory.'
}
Assert-NoReparseTree $source
if (Test-Path -LiteralPath $destinationPath) {
    if (-not (Test-Path -LiteralPath $destinationPath -PathType Container)) { throw 'Destination must be a directory.' }
    Assert-NoReparseTree $destinationPath
}
$package = Read-PackageManifest $source
$executable = Join-Path $source $package.Executable
$gameMachine = Read-PEMachine $executable
$gameArchitecture = if ($gameMachine -eq 0x8664) { 'x64' } else { 'x86' }

# A runtime .appx is read through a temporary folder, removed however setup ends.
$extracted = $null
trap { if ($extracted) { Remove-Item -LiteralPath $extracted -Recurse -Force -ErrorAction SilentlyContinue }; break }
$runtimeFolders = @(for ($index = 0; $index -lt $runtimeFolders.Count; $index++) {
    $path = $runtimeFolders[$index]
    if (Test-Path -LiteralPath $path -PathType Leaf) {
        if (-not $extracted) { $extracted = Join-Path ([IO.Path]::GetTempPath()) "appx-vclibs-$([Guid]::NewGuid().ToString('N'))" }
        Expand-RuntimePackage $path (Join-Path $extracted $index)
        Join-Path $extracted $index
    } else {
        $path
    }
})
# Each VCLibs DLL comes from the game folder or a runtime folder, in that order.
$runtimeFiles = @{}
foreach ($dependency in $package.Dependencies) {
    if (-not $VCLibs.ContainsKey($dependency)) { continue }
    foreach ($name in $VCLibs[$dependency]) {
        $found = @(@($source) + $runtimeFolders | ForEach-Object { Join-Path $_ $name } |
            Where-Object { Test-Path -LiteralPath $_ -PathType Leaf } | Select-Object -First 1)
        if (-not $found) {
            throw "Missing $name ($dependency). Pass the $gameArchitecture $dependency .appx, or a folder with its DLLs, with -RuntimeDirectory. 'Get the Visual C++ runtime' in README.md says where to download it."
        }
        Assert-PE $found[0]
        $runtimeFiles[$name] = $found[0]
    }
}
if ($package.Dependencies -contains 'Microsoft.VCLibs.140.00') {
    foreach ($name in $OptionalVCLibs140) {
        $found = @(@($source) + $runtimeFolders | ForEach-Object { Join-Path $_ $name } |
            Where-Object { Test-Path -LiteralPath $_ -PathType Leaf } | Select-Object -First 1)
        if ($found) {
            Assert-PE $found[0]
            $runtimeFiles[$name] = $found[0]
        }
    }
}
$shimFiles = @('appxportable.dll') + $Proxies
if (Test-ManagedPE $executable) { $shimFiles += 'appx-host.exe' }
foreach ($name in $shimFiles) { Assert-PE (Join-Path $build $name) }
$originalHash = (Get-FileHash -LiteralPath $executable -Algorithm SHA256).Hash

[IO.Directory]::CreateDirectory($destinationPath) | Out-Null
foreach ($item in Get-ChildItem -LiteralPath $source -Force) {
    if ($Excluded -contains $item.Name -or $item.Name -like 'appx-portable*.log') { continue }
    Copy-Item -LiteralPath $item.FullName -Destination $destinationPath -Recurse -Force
}
foreach ($name in $runtimeFiles.Keys) {
    Copy-Item -LiteralPath $runtimeFiles[$name] -Destination (Join-Path $destinationPath $name) -Force
}
if ($extracted) { Remove-Item -LiteralPath $extracted -Recurse -Force; $extracted = $null }
# A raw .appx extraction keeps OPC percent-encoded names (%21_Name.xml,
# User%20Controls); installed packages have them decoded, and games open the
# decoded names. Saves are left alone. Deepest paths first, so renaming a
# folder never invalidates a pending child.
# (SaveData is skipped by never descending into it: path prefixes can differ in
# 8.3 short and long forms.)
$encoded = Get-ChildItem -LiteralPath $destinationPath -Force | Where-Object { $_.Name -ne 'SaveData' } | ForEach-Object {
    $_
    if ($_.PSIsContainer) { Get-ChildItem -LiteralPath $_.FullName -Recurse -Force }
} | Where-Object { $_.Name -match '%[0-9A-Fa-f]{2}' }
foreach ($item in $encoded | Sort-Object { $_.FullName.Length } -Descending) {
    $decoded = [Uri]::UnescapeDataString($item.Name)
    if ($decoded.IndexOfAny([IO.Path]::GetInvalidFileNameChars()) -ge 0) { throw "Cannot decode package name: $($item.FullName)" }
    $target = Join-Path ([IO.Path]::GetDirectoryName($item.FullName)) $decoded
    if (Test-Path -LiteralPath $target) { Remove-Item -LiteralPath $target -Recurse -Force }
    Move-Item -LiteralPath $item.FullName -Destination $target
}
# Settings template, copied once so later edits survive re-staging.
$settings = Join-Path $destinationPath 'appx-portable.ini'
if (-not (Test-Path -LiteralPath $settings)) { Copy-Item -LiteralPath (Join-Path $PSScriptRoot 'appx-portable.ini') -Destination $settings }
foreach ($name in $shimFiles) {
    Copy-AtomicDll (Join-Path $build $name) (Join-Path $destinationPath $name)
}
$stagedHash = (Get-FileHash -LiteralPath (Join-Path $destinationPath $package.Executable) -Algorithm SHA256).Hash
$sourceHash = (Get-FileHash -LiteralPath $executable -Algorithm SHA256).Hash
if ($originalHash -ne $stagedHash -or $originalHash -ne $sourceHash) { throw "$($package.Executable) SHA-256 verification failed." }
Write-Output "Prepared $destinationPath"
Write-Output "$($package.Executable) SHA-256: $stagedHash (source and staged copy match)"
