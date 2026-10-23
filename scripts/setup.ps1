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
    # Folders holding the x86 VCLibs DLLs, if the game folder lacks them: an
    # extracted Microsoft.VCLibs.*.appx, or its installed WindowsApps folder.
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
$Proxies = @('d3dcompiler_47.dll', 'XAudio2_8.dll')
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
function Assert-X86PE([string]$Path) {
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
        if ($reader.ReadUInt32() -ne 0x00004550 -or $reader.ReadUInt16() -ne 0x014C) {
            throw "Expected an x86 (I386) PE file: $Path"
        }
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
Assert-X86PE $executable

# Each VCLibs DLL comes from the game folder or a runtime folder, in that order.
$runtimeFiles = @{}
foreach ($dependency in $package.Dependencies) {
    if (-not $VCLibs.ContainsKey($dependency)) { continue }
    foreach ($name in $VCLibs[$dependency]) {
        $found = @(@($source) + $runtimeFolders | ForEach-Object { Join-Path $_ $name } |
            Where-Object { Test-Path -LiteralPath $_ -PathType Leaf } | Select-Object -First 1)
        if (-not $found) {
            throw "Missing $name ($dependency). Copy the x86 DLLs from the $dependency package beside the game, or pass its folder with -RuntimeDirectory."
        }
        Assert-X86PE $found[0]
        $runtimeFiles[$name] = $found[0]
    }
}
foreach ($name in @('appxportable.dll') + $Proxies) { Assert-X86PE (Join-Path $build $name) }
$originalHash = (Get-FileHash -LiteralPath $executable -Algorithm SHA256).Hash

[IO.Directory]::CreateDirectory($destinationPath) | Out-Null
foreach ($item in Get-ChildItem -LiteralPath $source -Force) {
    if ($Excluded -contains $item.Name -or $item.Name -like 'appx-portable*.log') { continue }
    Copy-Item -LiteralPath $item.FullName -Destination $destinationPath -Recurse -Force
}
foreach ($name in $runtimeFiles.Keys) {
    Copy-Item -LiteralPath $runtimeFiles[$name] -Destination (Join-Path $destinationPath $name) -Force
}
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
foreach ($name in @('appxportable.dll') + $Proxies) {
    Copy-AtomicDll (Join-Path $build $name) (Join-Path $destinationPath $name)
}
$stagedHash = (Get-FileHash -LiteralPath (Join-Path $destinationPath $package.Executable) -Algorithm SHA256).Hash
$sourceHash = (Get-FileHash -LiteralPath $executable -Algorithm SHA256).Hash
if ($originalHash -ne $stagedHash -or $originalHash -ne $sourceHash) { throw "$($package.Executable) SHA-256 verification failed." }
Write-Output "Prepared $destinationPath"
Write-Output "$($package.Executable) SHA-256: $stagedHash (source and staged copy match)"
