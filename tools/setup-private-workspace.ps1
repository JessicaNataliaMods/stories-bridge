param([string]$PrivateRoot = (Join-Path $PSScriptRoot '../../private-bridge'))
$ErrorActionPreference = 'Stop'
$taskPublicRoot = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
$taskPrivateRoot = [IO.Path]::GetFullPath($PrivateRoot)
if ($taskPrivateRoot.StartsWith($taskPublicRoot + [IO.Path]::DirectorySeparatorChar, [StringComparison]::OrdinalIgnoreCase) -or $taskPrivateRoot -eq $taskPublicRoot) {
    throw 'PrivateRoot must be outside the public repository'
}
foreach ($taskPart in @('originals','references','generated','metadata','fixtures','build','workspace/lcs-sa')) {
    New-Item -ItemType Directory -Path (Join-Path $taskPrivateRoot $taskPart) -Force | Out-Null
}
$taskLinks = @{
    'local'=''; 'build'='build'; 'out'='workspace/lcs-sa/out';
    'reports'='workspace/lcs-sa/reports'; 'vendor'='workspace/lcs-sa/vendor';
    'assets'='workspace/lcs-sa/assets'; 'dist'='workspace/lcs-sa/dist';
    'handoff'='workspace/lcs-sa/handoff'; 'tests/fixtures'='fixtures'
}
foreach ($taskName in $taskLinks.Keys) {
    $taskPath = Join-Path $taskPublicRoot $taskName
    $taskTarget = if ($taskLinks[$taskName]) { Join-Path $taskPrivateRoot $taskLinks[$taskName] } else { $taskPrivateRoot }
    New-Item -ItemType Directory -Path $taskTarget -Force | Out-Null
    if (Test-Path -LiteralPath $taskPath) {
        $taskExisting = Get-Item -LiteralPath $taskPath -Force
        if ($taskExisting.LinkType -ne 'Junction' -or [IO.Path]::GetFullPath($taskExisting.Target[0]) -ne [IO.Path]::GetFullPath($taskTarget)) {
            throw "Refusing to replace an existing directory: $taskName"
        }
    } else {
        New-Item -ItemType Junction -Path $taskPath -Target $taskTarget | Out-Null
    }
}
git -C $taskPublicRoot config core.hooksPath .githooks
if ($LASTEXITCODE) { throw 'Could not enable Git source-safety hooks' }
Write-Output 'Private workspace links ready; source-safety hooks enabled.'
