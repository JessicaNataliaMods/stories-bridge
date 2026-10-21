param(
    [string]$ConvertedDirectory=(Join-Path $PSScriptRoot '..\out\lcs-converted'),
    [string]$ExtractedModels=(Join-Path $PSScriptRoot '..\out\img-v1'),
    [string]$CampaignAudit=(Join-Path $PSScriptRoot '..\reports\campaign-global-audit.json'),
    [string]$OutputDirectory=(Join-Path $PSScriptRoot '..\out\campaign-entities-v2'),
    [switch]$ValidateOnly
)
$ErrorActionPreference='Stop'
$taskSource=(Resolve-Path -LiteralPath $ConvertedDirectory).Path
$taskModels=(Resolve-Path -LiteralPath $ExtractedModels).Path
$taskOutput=[IO.Path]::GetFullPath($OutputDirectory)
$taskConverter=Join-Path $PSScriptRoot '..\build\Release\lcs-convert-rw.exe'
$taskManifestPath=Join-Path $taskOutput 'manifest.json'

function Get-TaskHash([string]$Path) {
    $taskStream=[IO.File]::OpenRead($Path)
    $taskAlgorithm=[Security.Cryptography.SHA256]::Create()
    try {return [BitConverter]::ToString($taskAlgorithm.ComputeHash($taskStream)).Replace('-','')}
    finally {$taskAlgorithm.Dispose();$taskStream.Dispose()}
}
if($ValidateOnly) {
    $taskManifest=Get-Content -LiteralPath $taskManifestPath -Raw | ConvertFrom-Json
    if($taskManifest.Schema -ne 2 -or !$taskManifest.Prepared -or $taskManifest.Installed -or
       $taskManifest.GameExecuted -or $taskManifest.CampaignPlayable -or $taskManifest.NativeBindingsAssigned){throw 'Entity staging manifest claims unverified native integration'}
    foreach($taskFile in $taskManifest.SourceFiles) {
        if((Get-TaskHash $taskFile.Path) -ne $taskFile.Sha256){throw "Original campaign source changed: $($taskFile.Path)"}
    }
    foreach($taskFile in $taskManifest.OutputFiles) {
        if((Get-TaskHash (Join-Path $taskOutput $taskFile.RelativePath)) -ne $taskFile.Sha256){throw "Prepared campaign asset changed: $($taskFile.RelativePath)"}
    }
    foreach($taskResource in $taskManifest.Resources | Where-Object {$_.GeometryValidated}) {
        $taskValidation=& $taskConverter (Join-Path $taskOutput ('resources\'+$taskResource.Name)) --validate-actors
        if($LASTEXITCODE){throw "Prepared campaign asset no longer validates: $($taskResource.Name)"}
    }
    Write-Output "PASS: $($taskManifest.Resources.Count) original entity records and $($taskManifest.OutputFiles.Count) output hashes; gameExecuted=false nativeBindingsAssigned=false"
    return
}
if(Test-Path -LiteralPath $taskOutput){throw 'Campaign entity staging exists; use ValidateOnly or a new directory'}
$taskIde=Join-Path $taskSource 'DATA\DEFAULT.IDE'
$taskAudit=Get-Content -LiteralPath $CampaignAudit -Raw | ConvertFrom-Json
if(!$taskAudit.inventoryComplete -or $taskAudit.decodeErrors -or $taskAudit.missionBlocks -ne 118){throw 'An incomplete campaign audit cannot supply the special actor inventory'}
$taskResources=@{};$taskDefinitions=@();$taskSection=''
function Add-TaskResource([string]$Name,[string]$Texture,[string]$Use) {
    $Name=$Name.ToLowerInvariant();$Texture=$Texture.ToLowerInvariant()
    if($Name -notmatch '^[a-z0-9_]{1,23}$' -or $Texture -notmatch '^[a-z0-9_]{1,23}$'){throw 'Invalid original entity filename'}
    if(!$taskResources.ContainsKey($Name)) {
        $taskResources[$Name]=[pscustomobject]@{Name=$Name;Texture=$Texture;Uses=@($Use);Status='pending';GeometryValidated=$false;NativeBindingsAssigned=$false;NativeAnimationVerified=$false;Reason=''}
    }else {
        if($taskResources[$Name].Texture -ne $Texture){throw "Conflicting original texture association: $Name"}
        $taskResources[$Name].Uses+=,$Use
    }
}
foreach($taskLine in [IO.File]::ReadLines($taskIde)) {
    $taskText=($taskLine -split '#')[0].Trim();if(!$taskText){continue}
    if($taskText -eq 'end'){$taskSection='';continue}
    if($taskText -match '^[a-zA-Z][a-zA-Z0-9_]*$'){$taskSection=$taskText;continue}
    if($taskSection -notin @('peds','cars')){continue}
    $taskFields=@($taskText.Split(',') | ForEach-Object {$_.Trim()})
    if($taskFields.Count -lt 10){throw "Invalid original entity definition: $taskText"}
    $taskId=[int]$taskFields[0];$taskName=$taskFields[1].ToLowerInvariant()
    $taskDefinitions+=[pscustomobject]@{LcsId=$taskId;Name=$taskName;Texture=$taskFields[2].ToLowerInvariant();Class=$taskSection;OriginalFields=$taskFields;NativeId=$null}
    # These slots intentionally have no static DFF. Their real named resources
    # come from LOAD_SPECIAL_CHARACTER/LOAD_SPECIAL_MODEL across all missions.
    if($taskName -eq 'null' -or $taskName -match '^special\d+$'){continue}
    Add-TaskResource $taskName $taskFields[2] ('DEFAULT.IDE/'+$taskSection+'/'+$taskId)
}
foreach($taskCommand in $taskAudit.commands | Where-Object {$_.name -in @('COMMAND_LOAD_SPECIAL_CHARACTER','COMMAND_LOAD_SPECIAL_MODEL','COMMAND_STORE_PLAYER_OUTFIT')}) {
    foreach($taskName in $taskCommand.textOperands){Add-TaskResource $taskName $taskName $taskCommand.name}
}
New-Item -ItemType Directory -Path $taskOutput -Force | Out-Null
$taskSourceFiles=@{};$taskOutputFiles=@()
foreach($taskFile in @($taskIde,(Join-Path $taskSource 'DATA\MAIN.SCM'))) {
    $taskSourceFiles[$taskFile]=[pscustomobject]@{Path=$taskFile;Sha256=(Get-TaskHash $taskFile)}
}
# The handler audit is generated during development. Freeze the exact input
# inventory here, so adding an adapter later does not invalidate original assets.
$taskAuditSnapshot=Join-Path $taskOutput 'campaign-audit-source.json'
Copy-Item -LiteralPath $CampaignAudit -Destination $taskAuditSnapshot
$taskOutputFiles+=[pscustomobject]@{RelativePath='campaign-audit-source.json';Sha256=(Get-TaskHash $taskAuditSnapshot)}
foreach($taskResource in $taskResources.Values | Sort-Object Name) {
    $taskDff=Join-Path $taskModels ($taskResource.Name+'.dff')
    $taskTxd=Join-Path $taskModels ($taskResource.Texture+'.txd')
    if(!(Test-Path -LiteralPath $taskTxd)){$taskTxd=Join-Path $taskSource ('models\'+$taskResource.Texture+'.txd')}
    if(!(Test-Path -LiteralPath $taskDff) -or !(Test-Path -LiteralPath $taskTxd)) {
        $taskResource.Status='missing_source'
        $taskResource.Reason='Original DFF present='+[bool](Test-Path -LiteralPath $taskDff)+'; original TXD present='+[bool](Test-Path -LiteralPath $taskTxd)
        continue
    }
    $taskDirectory=Join-Path $taskOutput ('resources\'+$taskResource.Name)
    New-Item -ItemType Directory -Path $taskDirectory -Force | Out-Null
    try {
        foreach($taskInput in @($taskDff,$taskTxd)) {
            $taskSourceFiles[$taskInput]=[pscustomobject]@{Path=$taskInput;Sha256=(Get-TaskHash $taskInput)}
            $taskDestination=Join-Path $taskDirectory ([IO.Path]::GetFileName($taskInput))
            $taskConversion=& $taskConverter $taskInput $taskDestination 2>&1
            if($LASTEXITCODE){throw ($taskConversion -join ' ')}
            $taskOutputFiles+=[pscustomobject]@{RelativePath=('resources\'+$taskResource.Name+'\'+[IO.Path]::GetFileName($taskInput));Sha256=(Get-TaskHash $taskDestination)}
        }
        $taskHierarchy=& $taskConverter (Join-Path $taskDirectory ($taskResource.Name+'.dff')) --info 2>&1
        if($LASTEXITCODE){throw ($taskHierarchy -join ' ')}
        $taskHierarchyPath=Join-Path $taskDirectory 'hierarchy.txt'
        [IO.File]::WriteAllLines($taskHierarchyPath,[string[]]$taskHierarchy)
        $taskValidation=& $taskConverter $taskDirectory --validate-actors 2>&1
        if($LASTEXITCODE){throw ($taskValidation -join ' ')}
        $taskValidationPath=Join-Path $taskDirectory 'validation.txt'
        [IO.File]::WriteAllLines($taskValidationPath,[string[]]$taskValidation)
        foreach($taskFile in @($taskHierarchyPath,$taskValidationPath)) {
            $taskOutputFiles+=[pscustomobject]@{RelativePath=('resources\'+$taskResource.Name+'\'+[IO.Path]::GetFileName($taskFile));Sha256=(Get-TaskHash $taskFile)}
        }
        $taskResource.GeometryValidated=$true;$taskResource.Status='geometry_prepared'
        $taskResource.Reason='SA class registration, animation/node adaptation, shared textures and handling are still required'
    }catch {
        $taskResource.Status='conversion_or_validation_failed';$taskResource.Reason=$_.Exception.Message
    }
}
# Convert the actual shared dictionary too; geometry preparation does not prove
# that every material's parent dictionary is mounted in the native engine.
$taskGeneric=Join-Path $taskSource 'models\generic.txd'
$taskSourceFiles[$taskGeneric]=[pscustomobject]@{Path=$taskGeneric;Sha256=(Get-TaskHash $taskGeneric)}
$taskGenericOut=Join-Path $taskOutput 'generic.txd'
$taskGenericResult=& $taskConverter $taskGeneric $taskGenericOut 2>&1
if($LASTEXITCODE){throw ($taskGenericResult -join ' ')}
$taskOutputFiles+=[pscustomobject]@{RelativePath='generic.txd';Sha256=(Get-TaskHash $taskGenericOut)}
$taskManifest=[pscustomobject]@{
    Schema=2;Prepared=$true;Installed=$false;GameExecuted=$false;CampaignPlayable=$false;NativeBindingsAssigned=$false
    Source=$taskSource;ExtractedModels=$taskModels;Definitions=$taskDefinitions
    Resources=@($taskResources.Values | Sort-Object Name);SourceFiles=@($taskSourceFiles.Values | Sort-Object Path);OutputFiles=$taskOutputFiles
    GeometryValidatedCount=@($taskResources.Values | Where-Object {$_.GeometryValidated}).Count
    MissingSourceCount=@($taskResources.Values | Where-Object {$_.Status -eq 'missing_source'}).Count
    FailedConversionCount=@($taskResources.Values | Where-Object {$_.Status -eq 'conversion_or_validation_failed'}).Count
}
$taskManifest | ConvertTo-Json -Depth 8 | Set-Content -LiteralPath $taskManifestPath -Encoding UTF8
Write-Output "Campaign entity batch: $($taskManifest.Definitions.Count) definitions, $($taskManifest.Resources.Count) distinct resources, $($taskManifest.GeometryValidatedCount) geometry validated, $($taskManifest.MissingSourceCount) missing source, $($taskManifest.FailedConversionCount) conversion failures; gameExecuted=false nativeBindingsAssigned=false"
