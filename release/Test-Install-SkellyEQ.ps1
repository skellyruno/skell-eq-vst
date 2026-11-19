[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)][string]$PackageRoot,
    [string]$TestRoot = (Join-Path ([System.IO.Path]::GetTempPath()) ('Skelly-EQ-Install-Test-' + [Guid]::NewGuid().ToString('N')))
)
Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
$script:assertions = 0

function Assert([bool]$Condition, [string]$Message) {
    $script:assertions++
    if (-not $Condition) { throw $Message }
}
function AssertThrows([scriptblock]$Action, [string]$Message) {
    $script:assertions++
    $didThrow = $false
    try { & $Action | Out-Null } catch { $didThrow = $true }
    if (-not $didThrow) { throw $Message }
}
function CopyTree([string]$Source, [string]$Destination) {
    New-Item -ItemType Directory -Path $Destination -Force | Out-Null
    foreach ($item in Get-ChildItem -LiteralPath $Source -Force) {
        Copy-Item -LiteralPath $item.FullName -Destination $Destination -Recurse -Force
    }
}
function WriteJson([string]$Path, [object]$Value) {
    [System.IO.File]::WriteAllText($Path, ($Value | ConvertTo-Json -Depth 30), [System.Text.UTF8Encoding]::new($false))
}
function RebuildMainManifest([string]$Root) {
    $rootPath = [System.IO.Path]::GetFullPath($Root)
    $excluded = @('SHA256SUMS.txt','TOOLS-SHA256SUMS.txt','Install-SkellyEQ.ps1','INSTALL-WINDOWS.txt','THIRD-PARTY-LICENSE-STATUS.txt')
    $lines = foreach ($file in Get-ChildItem -LiteralPath $rootPath -Recurse -File -Force | Sort-Object FullName) {
        $relative = $file.FullName.Substring($rootPath.Length).TrimStart([char[]]@('\','/')).Replace('\','/')
        if ($relative -notin $excluded) {
            $hash = (Get-FileHash -LiteralPath $file.FullName -Algorithm SHA256).Hash.ToLowerInvariant()
            "$hash  $relative"
        }
    }
    [System.IO.File]::WriteAllLines((Join-Path $rootPath 'SHA256SUMS.txt'), [string[]]$lines, [System.Text.Encoding]::ASCII)
}

function NewDirectoryJunction([string]$Link, [string]$Target) {
    New-Item -ItemType Directory -Path $Target -Force | Out-Null
    & $env:ComSpec /d /c ('mklink /J "' + $Link + '" "' + $Target + '"') | Out-Null
    if ($LASTEXITCODE -ne 0 -or -not (Test-Path -LiteralPath $Link -PathType Container)) {
        throw "Could not create test junction: $Link"
    }
}
function RemoveDirectoryJunction([string]$Link) {
    if (Test-Path -LiteralPath $Link) {
        & $env:ComSpec /d /c ('rmdir "' + $Link + '"') | Out-Null
        if ($LASTEXITCODE -ne 0 -or (Test-Path -LiteralPath $Link)) {
            throw "Could not safely remove test junction: $Link"
        }
    }
}

$root = [System.IO.Path]::GetFullPath($PackageRoot)
$installer = Join-Path $root 'Install-SkellyEQ.ps1'
if (-not (Test-Path -LiteralPath $installer -PathType Leaf)) { throw 'Packaged installer is missing.' }
if (-not (Test-Path -LiteralPath (Join-Path $root 'SHA256SUMS.txt') -PathType Leaf) -or
    -not (Test-Path -LiteralPath (Join-Path $root 'TOOLS-SHA256SUMS.txt') -PathType Leaf)) {
    throw 'Package checksum manifests are missing.'
}

New-Item -ItemType Directory -Path $TestRoot -Force | Out-Null
try {

    # Strict JSON rejects duplicate/case-ambiguous properties and non-finite numbers
    # before a package can reach an install destination.
    $strictJsonRoot = Join-Path $TestRoot 'StrictJsonDuplicate'
    CopyTree $root $strictJsonRoot
    $strictBuildInfoPath = Join-Path $strictJsonRoot 'build-info.json'
    $strictBuildOriginal = [System.IO.File]::ReadAllText($strictBuildInfoPath)
    $strictBuildDuplicate = $strictBuildOriginal.Replace(
        '"product": "Skelly EQ",',
        ('"product": "Skelly EQ",' + [Environment]::NewLine + '  "Product": "Skelly EQ",')
    )
    if ($strictBuildDuplicate -ceq $strictBuildOriginal) { throw 'Strict JSON fixture did not find the product key.' }
    [System.IO.File]::WriteAllText($strictBuildInfoPath, $strictBuildDuplicate, [System.Text.UTF8Encoding]::new($false))
    $strictJsonTarget = Join-Path $TestRoot 'StrictJsonDuplicateInstall\VST3'
    AssertThrows {
        & (Join-Path $strictJsonRoot 'Install-SkellyEQ.ps1') -PackageRoot $strictJsonRoot -Vst3Directory $strictJsonTarget -BackupRoot (Join-Path $TestRoot 'StrictJsonDuplicateInstall\Backups') -Confirm:$false
    } 'Duplicate/case-ambiguous build-info JSON was accepted.'
    Assert (-not (Test-Path -LiteralPath $strictJsonTarget)) 'Duplicate build-info JSON modified the install destination.'

    $nonFiniteRoot = Join-Path $TestRoot 'StrictJsonNonFinite'
    CopyTree $root $nonFiniteRoot
    $nonFinitePath = Join-Path $nonFiniteRoot 'build-info.json'
    $nonFiniteOriginal = [System.IO.File]::ReadAllText($nonFinitePath)
    $nonFiniteJson = $nonFiniteOriginal.Replace('"schema_version": 1,', '"schema_version": NaN,')
    if ($nonFiniteJson -ceq $nonFiniteOriginal) { throw 'Non-finite JSON fixture did not find schema_version.' }
    [System.IO.File]::WriteAllText($nonFinitePath, $nonFiniteJson, [System.Text.UTF8Encoding]::new($false))
    $nonFiniteTarget = Join-Path $TestRoot 'StrictJsonNonFiniteInstall\VST3'
    AssertThrows {
        & (Join-Path $nonFiniteRoot 'Install-SkellyEQ.ps1') -PackageRoot $nonFiniteRoot -Vst3Directory $nonFiniteTarget -BackupRoot (Join-Path $TestRoot 'StrictJsonNonFiniteInstall\Backups') -Confirm:$false
    } 'Non-finite build-info JSON was accepted.'
    Assert (-not (Test-Path -LiteralPath $nonFiniteTarget)) 'Non-finite build-info JSON modified the install destination.'

    # A package root or any nested package directory containing a junction is rejected.
    $packageAliasTarget = Join-Path $TestRoot 'PackageAliasTarget'
    $packageAlias = Join-Path $TestRoot 'PackageAlias'
    CopyTree $root $packageAliasTarget
    NewDirectoryJunction $packageAlias $packageAliasTarget
    try {
        $aliasInstall = Join-Path $TestRoot 'PackageAliasInstall\VST3'
        AssertThrows {
            & $installer -PackageRoot $packageAlias -Vst3Directory $aliasInstall -BackupRoot (Join-Path $TestRoot 'PackageAliasInstall\Backups') -Confirm:$false
        } 'A junction package root was accepted.'
        Assert (-not (Test-Path -LiteralPath $aliasInstall)) 'A junction package root modified the install destination.'
    }
    finally { RemoveDirectoryJunction $packageAlias }

    $nestedPackage = Join-Path $TestRoot 'NestedReparsePackage'
    CopyTree $root $nestedPackage
    $nestedOutside = Join-Path $TestRoot 'NestedReparseOutside'
    $nestedLink = Join-Path $nestedPackage 'Skelly EQ.vst3\Contents\Resources\External'
    NewDirectoryJunction $nestedLink $nestedOutside
    try {
        $nestedInstall = Join-Path $TestRoot 'NestedReparseInstall\VST3'
        AssertThrows {
            & (Join-Path $nestedPackage 'Install-SkellyEQ.ps1') -PackageRoot $nestedPackage -Vst3Directory $nestedInstall -BackupRoot (Join-Path $TestRoot 'NestedReparseInstall\Backups') -Confirm:$false
        } 'A nested package junction was accepted.'
        Assert (-not (Test-Path -LiteralPath $nestedInstall)) 'A nested package junction modified the install destination.'
    }
    finally { RemoveDirectoryJunction $nestedLink }

    # Existing junction ancestors must be rejected before install or backup writes.
    $installOutside = Join-Path $TestRoot 'InstallJunctionOutside'
    $installAlias = Join-Path $TestRoot 'InstallJunctionAlias'
    NewDirectoryJunction $installAlias $installOutside
    try {
        $linkedInstallRoot = Join-Path $installAlias 'Programs\Common\VST3'
        AssertThrows {
            & $installer -PackageRoot $root -Vst3Directory $linkedInstallRoot -BackupRoot (Join-Path $TestRoot 'SafeBackups') -Confirm:$false
        } 'An install path with a junction ancestor was accepted.'
        Assert (-not (Test-Path -LiteralPath (Join-Path $installOutside 'Programs'))) 'Install wrote through a junction ancestor.'
    }
    finally { RemoveDirectoryJunction $installAlias }

    # Exercise the default per-user path and the missing-environment failure
    # before the ordinary install/update/rollback scenarios.
    $originalLocalAppData = [Environment]::GetEnvironmentVariable('LOCALAPPDATA', 'Process')
    try {
        $env:LOCALAPPDATA = Join-Path $TestRoot 'SyntheticLocalAppData'
        $defaultDry = & $installer -PackageRoot $root -WhatIf
        $expectedDefaultTarget = [System.IO.Path]::GetFullPath(
            (Join-Path $env:LOCALAPPDATA 'Programs\Common\VST3\Skelly EQ.vst3')
        )
        Assert ($defaultDry.status -eq 'what-if') 'Default per-user WhatIf did not return a dry-run status.'
        Assert ($defaultDry.target -eq $expectedDefaultTarget) 'Default per-user target did not resolve under LOCALAPPDATA.'
        Assert (-not (Test-Path -LiteralPath (Join-Path $env:LOCALAPPDATA 'Programs\Common\VST3'))) 'Default WhatIf created the VST3 directory.'
        Assert (-not (Test-Path -LiteralPath (Join-Path $env:LOCALAPPDATA 'Skelly Audio\Backups\Skelly-EQ'))) 'Default WhatIf created the backup directory.'

        Remove-Item Env:LOCALAPPDATA -ErrorAction SilentlyContinue
        $missingEnvMessage = $null
        try {
            & $installer -PackageRoot $root -WhatIf | Out-Null
        }
        catch {
            $missingEnvMessage = $_.Exception.Message
        }
        Assert ($missingEnvMessage -match 'LOCALAPPDATA is unavailable or not an absolute path') 'Missing LOCALAPPDATA did not fail closed with an actionable message.'

        $partialTarget = Join-Path $TestRoot 'NoLocalAppData\Partial\VST3'
        $partialEnvMessage = $null
        try {
            & $installer -PackageRoot $root -Vst3Directory $partialTarget -WhatIf | Out-Null
        }
        catch {
            $partialEnvMessage = $_.Exception.Message
        }
        Assert ($partialEnvMessage -match 'supply both -Vst3Directory and -BackupRoot') 'A partial custom path was accepted without LOCALAPPDATA.'
        Assert (-not (Test-Path -LiteralPath $partialTarget)) 'Partial custom-path failure created the VST3 directory.'

        $explicitTarget = Join-Path $TestRoot 'NoLocalAppData\Explicit\VST3'
        $explicitBackups = Join-Path $TestRoot 'NoLocalAppData\Explicit\Backups'
        $explicitDry = & $installer -PackageRoot $root -Vst3Directory $explicitTarget -BackupRoot $explicitBackups -WhatIf
        $expectedExplicitTarget = [System.IO.Path]::GetFullPath((Join-Path $explicitTarget 'Skelly EQ.vst3'))
        Assert ($explicitDry.status -eq 'what-if') 'Explicit custom paths failed without LOCALAPPDATA.'
        Assert ($explicitDry.target -eq $expectedExplicitTarget) 'Explicit custom VST3 path was not preserved.'
        Assert (-not (Test-Path -LiteralPath $explicitTarget)) 'Explicit custom WhatIf created the VST3 directory.'
        Assert (-not (Test-Path -LiteralPath $explicitBackups)) 'Explicit custom WhatIf created the backup directory.'
    }
    finally {
        if ($null -eq $originalLocalAppData) {
            Remove-Item Env:LOCALAPPDATA -ErrorAction SilentlyContinue
        }
        else {
            $env:LOCALAPPDATA = $originalLocalAppData
        }
    }

    $whatIfTarget = Join-Path $TestRoot 'WhatIf\VST3'
    $whatIfBackups = Join-Path $TestRoot 'WhatIf\Backups'
    $dry = & $installer -PackageRoot $root -Vst3Directory $whatIfTarget -BackupRoot $whatIfBackups -WhatIf
    Assert ($dry.status -eq 'what-if') 'WhatIf did not return a dry-run status.'
    Assert (-not (Test-Path -LiteralPath $whatIfTarget)) 'WhatIf created the install directory.'
    Assert (-not (Test-Path -LiteralPath $whatIfBackups)) 'WhatIf created the backup directory.'

    $targetRoot = Join-Path $TestRoot 'Installed\VST3'
    $backupRoot = Join-Path $TestRoot 'Installed\Backups'
    $fresh = & $installer -PackageRoot $root -Vst3Directory $targetRoot -BackupRoot $backupRoot -Confirm:$false
    Assert ($fresh.status -eq 'installed') 'Fresh install failed.'
    Assert ($fresh.version -eq '2.7.0') 'Fresh install reported the wrong version.'
    Assert ($null -eq $fresh.backup_path) 'Fresh install unexpectedly created a backup.'
    Assert (Test-Path -LiteralPath (Join-Path $targetRoot 'Skelly EQ.vst3\Contents\x86_64-win\Skelly EQ.vst3')) 'Installed Windows VST3 module is missing.'
    Assert (-not (Test-Path -LiteralPath $backupRoot)) 'Fresh install created an unnecessary backup.'


    $installedInfoPath = Join-Path $targetRoot 'Skelly EQ.vst3\Contents\Resources\moduleinfo.json'
    $installedInfoOriginal = [System.IO.File]::ReadAllText($installedInfoPath)
    $installedInfoDuplicate = $installedInfoOriginal.Replace(
        '"Name": "Skelly EQ",',
        ('"Name": "Skelly EQ",' + [Environment]::NewLine + '    "name": "Skelly EQ",')
    )
    if ($installedInfoDuplicate -ceq $installedInfoOriginal) { throw 'Duplicate moduleinfo fixture did not find the Name key.' }
    [System.IO.File]::WriteAllText($installedInfoPath, $installedInfoDuplicate, [System.Text.UTF8Encoding]::new($false))
    AssertThrows {
        & $installer -PackageRoot $root -Vst3Directory $targetRoot -BackupRoot $backupRoot -Confirm:$false
    } 'Duplicate/case-ambiguous VST3 moduleinfo JSON was accepted.'
    [System.IO.File]::WriteAllText($installedInfoPath, $installedInfoOriginal, [System.Text.UTF8Encoding]::new($false))

    $same = & $installer -PackageRoot $root -Vst3Directory $targetRoot -BackupRoot $backupRoot -Confirm:$false
    Assert ($same.status -eq 'already-installed') 'Repeat install was not idempotent.'
    Assert ($null -eq $same.backup_path) 'Idempotent install unexpectedly returned a backup.'

    $upgradeRoot = Join-Path $TestRoot 'UpgradePackage'
    CopyTree $root $upgradeRoot
    $moduleInfoPath = Join-Path $upgradeRoot 'Skelly EQ.vst3\Contents\Resources\moduleinfo.json'
    $info = [System.IO.File]::ReadAllText($moduleInfoPath) | ConvertFrom-Json
    $info.Version = '2.7.1'
    foreach ($class in $info.Classes) { $class.Version = '2.7.1' }
    WriteJson $moduleInfoPath $info
    $moduleInfoHash = (Get-FileHash -LiteralPath $moduleInfoPath -Algorithm SHA256).Hash.ToLowerInvariant()
    $moduleInfoBytes = (Get-Item -LiteralPath $moduleInfoPath).Length

    $verificationPath = Join-Path $upgradeRoot 'Skelly-EQ-moduleinfo-verification.json'
    $verification = [System.IO.File]::ReadAllText($verificationPath) | ConvertFrom-Json
    $verification.version = '2.7.1'
    $verification.source_moduleinfo_sha256 = $moduleInfoHash
    $verification.staged_moduleinfo_sha256 = $moduleInfoHash
    $verification.staged_moduleinfo_bytes = $moduleInfoBytes
    WriteJson $verificationPath $verification

    $buildInfoPath = Join-Path $upgradeRoot 'build-info.json'
    $buildInfo = [System.IO.File]::ReadAllText($buildInfoPath) | ConvertFrom-Json
    $buildInfo.version = '2.7.1'
    $buildInfo.moduleinfo_sha256 = $moduleInfoHash
    WriteJson $buildInfoPath $buildInfo

    $manifestPath = Join-Path $upgradeRoot 'Skelly-EQ-Windows-VST3-manifest.txt'
    $manifest = [System.IO.File]::ReadAllText($manifestPath)
    $manifest = [regex]::Replace($manifest, '(?m)^Version: .+$', 'Version: 2.7.1')
    [System.IO.File]::WriteAllText($manifestPath, $manifest, [System.Text.UTF8Encoding]::new($false))
    RebuildMainManifest $upgradeRoot


    $backupOutside = Join-Path $TestRoot 'BackupJunctionOutside'
    $backupAlias = Join-Path $TestRoot 'BackupJunctionAlias'
    NewDirectoryJunction $backupAlias $backupOutside
    try {
        $linkedBackupRoot = Join-Path $backupAlias 'Backups'
        AssertThrows {
            & (Join-Path $upgradeRoot 'Install-SkellyEQ.ps1') -PackageRoot $upgradeRoot -Vst3Directory $targetRoot -BackupRoot $linkedBackupRoot -Confirm:$false
        } 'A backup path with a junction ancestor was accepted.'
        $stillInstalled = [System.IO.File]::ReadAllText((Join-Path $targetRoot 'Skelly EQ.vst3\Contents\Resources\moduleinfo.json')) | ConvertFrom-Json
        Assert ($stillInstalled.Version -eq '2.7.0') 'Rejected junction backup path changed the installed version.'
        Assert (-not (Test-Path -LiteralPath (Join-Path $backupOutside 'Backups'))) 'Installer wrote through a backup junction ancestor.'
    }
    finally { RemoveDirectoryJunction $backupAlias }

    $updated = & (Join-Path $upgradeRoot 'Install-SkellyEQ.ps1') -PackageRoot $upgradeRoot -Vst3Directory $targetRoot -BackupRoot $backupRoot -Confirm:$false
    Assert ($updated.status -eq 'installed' -and $updated.version -eq '2.7.1') 'Upgrade did not install the new version.'
    Assert (-not [string]::IsNullOrWhiteSpace([string]$updated.backup_path)) 'Upgrade did not create a verified backup.'
    Assert (Test-Path -LiteralPath (Join-Path $updated.backup_path 'backup-manifest.json')) 'Backup manifest is missing.'
    Assert (Test-Path -LiteralPath (Join-Path $updated.backup_path 'Skelly EQ.vst3')) 'Backup bundle is missing.'

    $badBackup = Join-Path $TestRoot 'TamperedBackup'
    CopyTree $updated.backup_path $badBackup
    Add-Content -LiteralPath (Join-Path $badBackup 'Skelly EQ.vst3\Contents\Resources\moduleinfo.json') -Value 'tampered'
    AssertThrows {
        & $installer -RollbackFrom $badBackup -Vst3Directory $targetRoot -BackupRoot $backupRoot -Confirm:$false
    } 'Tampered backup was accepted.'


    $backupManifestPath = Join-Path $updated.backup_path 'backup-manifest.json'
    $backupManifestOriginal = [System.IO.File]::ReadAllText($backupManifestPath)
    $backupManifestDuplicate = $backupManifestOriginal.Replace(
        '"product": "Skelly EQ",',
        ('"product": "Skelly EQ",' + [Environment]::NewLine + '    "Product": "Skelly EQ",')
    )
    if ($backupManifestDuplicate -ceq $backupManifestOriginal) { throw 'Duplicate backup manifest fixture did not find the product key.' }
    [System.IO.File]::WriteAllText($backupManifestPath, $backupManifestDuplicate, [System.Text.UTF8Encoding]::new($false))
    AssertThrows {
        & $installer -RollbackFrom $updated.backup_path -Vst3Directory $targetRoot -BackupRoot $backupRoot -Confirm:$false
    } 'Duplicate/case-ambiguous backup manifest JSON was accepted.'
    [System.IO.File]::WriteAllText($backupManifestPath, $backupManifestOriginal, [System.Text.UTF8Encoding]::new($false))

    $rollback = & $installer -RollbackFrom $updated.backup_path -Vst3Directory $targetRoot -BackupRoot $backupRoot -Confirm:$false
    Assert ($rollback.status -eq 'rolled-back' -and $rollback.version -eq '2.7.0') 'Rollback did not restore the previous version.'
    Assert (-not [string]::IsNullOrWhiteSpace([string]$rollback.backup_path)) 'Rollback did not preserve the replaced version.'

    $tamperedPackage = Join-Path $TestRoot 'TamperedPackage'
    CopyTree $root $tamperedPackage
    Add-Content -LiteralPath (Join-Path $tamperedPackage 'PREVIEW-ONLY.txt') -Value 'tampered'
    $tamperedTarget = Join-Path $TestRoot 'TamperedInstall\VST3'
    AssertThrows {
        & (Join-Path $tamperedPackage 'Install-SkellyEQ.ps1') -PackageRoot $tamperedPackage -Vst3Directory $tamperedTarget -BackupRoot (Join-Path $TestRoot 'TamperedInstall\Backups') -Confirm:$false
    } 'Tampered package was accepted.'
    Assert (-not (Test-Path -LiteralPath $tamperedTarget)) 'Tampered package changed the install destination.'

    $invalidTargetRoot = Join-Path $TestRoot 'InvalidTarget\VST3'
    $invalidBundle = Join-Path $invalidTargetRoot 'Skelly EQ.vst3'
    New-Item -ItemType Directory -Path $invalidBundle -Force | Out-Null
    $sentinel = Join-Path $invalidBundle 'do-not-overwrite.txt'
    [System.IO.File]::WriteAllText($sentinel, 'preserve this unrelated directory')
    AssertThrows {
        & $installer -PackageRoot $root -Vst3Directory $invalidTargetRoot -BackupRoot (Join-Path $TestRoot 'InvalidTarget\Backups') -Confirm:$false
    } 'An invalid existing target was replaced.'
    Assert ((Get-Content -LiteralPath $sentinel -Raw) -eq 'preserve this unrelated directory') 'Invalid target contents were modified.'

    Write-Host "Skelly EQ preview install/update/rollback tests: $script:assertions assertions passed (strict JSON and reparse-point rejection included)."
}
finally {
    if (Test-Path -LiteralPath $TestRoot) {
        Remove-Item -LiteralPath $TestRoot -Recurse -Force -ErrorAction SilentlyContinue
    }
}
