param(
    [Parameter(Mandatory)][string]$GameDirectory,
    [Parameter(Mandatory)][string]$OriginalBackupDirectory,
    [string]$WorldDirectory=(Join-Path $PSScriptRoot '..\out\world-stage'),
    [string]$OutputDirectory=(Join-Path $PSScriptRoot '..\out\world-stage\models\sa-core-clean')
)
$ErrorActionPreference='Stop'
$taskGame=(Resolve-Path -LiteralPath $GameDirectory).Path
$taskBackup=(Resolve-Path -LiteralPath $OriginalBackupDirectory).Path
$taskWorld=(Resolve-Path -LiteralPath $WorldDirectory).Path
$taskOutput=[IO.Path]::GetFullPath($OutputDirectory)
if(Test-Path -LiteralPath $taskOutput){throw 'Core archive output already exists; use a new directory'}
function Read-ModelNames([string[]]$Profiles){
    $taskNames=[Collections.Generic.HashSet[string]]::new([StringComparer]::OrdinalIgnoreCase)
    foreach($taskProfile in $Profiles){
        foreach($taskLine in [IO.File]::ReadLines($taskProfile)){
            $taskText=($taskLine -split '#')[0].Trim()
            if($taskText -notmatch '^IDE\s+(.+)$'){continue}
            $taskRelative=$Matches[1]
            $taskIde=Join-Path $taskWorld $taskRelative
            if(!(Test-Path -LiteralPath $taskIde)){$taskIde=Join-Path $taskGame $taskRelative}
            $taskSection=''
            foreach($taskDefinition in [IO.File]::ReadLines($taskIde)){
                $taskRow=($taskDefinition -split '#')[0].Trim();if(!$taskRow){continue}
                if($taskRow -eq 'end'){$taskSection='';continue}
                if($taskRow -match '^[a-z][a-z0-9]+$'){$taskSection=$taskRow;continue}
                if($taskSection -in @('objs','tobj','weap','hier','anim','cars','peds')){
                    $taskP=$taskRow -split ','|ForEach-Object {$_.Trim()}
                    if($taskP.Count -lt 3){throw 'Invalid original native model definition'}
                    $null=$taskNames.Add($taskP[1])
                }
            }
        }
    }
    return ,$taskNames
}
$taskDefault=Join-Path $taskGame 'data\default.dat'
$taskOriginal=Read-ModelNames @($taskDefault,(Join-Path $taskBackup 'data\gta.dat'))
$taskLoaded=Read-ModelNames @($taskDefault,(Join-Path $taskWorld 'data\gta.dat'))
$taskExcluded=@($taskOriginal|Where-Object {!$taskLoaded.Contains($_)}|Sort-Object)
# Keep genuine unnamed special models such as player.dff. Otherwise, leaving
# thousands of unused SA map DFFs fills CStreaming's fixed extra-object directory
# before the native player can be found by RequestSpecialModel.
$taskNamesFile=Join-Path $taskWorld 'data\lcs\excluded-sa-map-models.txt'
[IO.File]::WriteAllLines($taskNamesFile,$taskExcluded,[Text.Encoding]::ASCII)
$taskInstall=Get-Content -LiteralPath (Join-Path $taskBackup 'install-manifest.json') -Raw|ConvertFrom-Json
foreach($taskName in @('gta3.img','gta_int.img')){
    $taskRelative='models\'+$taskName;$taskOriginalPath=Join-Path $taskBackup $taskRelative
    $taskRow=@($taskInstall.Files|Where-Object {$_.Relative -eq $taskRelative})
    if($taskRow.Count -ne 1 -or (Get-FileHash -LiteralPath $taskOriginalPath -Algorithm SHA256).Hash -ne $taskRow[0].OldHash){throw 'Original SA archive backup hash mismatch'}
    & (Join-Path $PSScriptRoot '..\build\Release\lcs-img.exe') filter-native $taskOriginalPath (Join-Path $taskOutput $taskName) $taskNamesFile
    if($LASTEXITCODE){throw 'Native core archive preparation failed'}
}
[pscustomobject]@{OriginalModelNames=$taskOriginal.Count;LoadedModelNames=$taskLoaded.Count;ExcludedSAMapModels=$taskExcluded.Count;Output=$taskOutput;OriginalFilesModified=$false;GameExecuted=$false}|ConvertTo-Json
