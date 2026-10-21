param([string]$OutputFile=(Join-Path $PSScriptRoot '..\out\world-stage\models\lcs\paths.img'))
$ErrorActionPreference='Stop'
$taskOutput=[IO.Path]::GetFullPath($OutputFile)
if(Test-Path -LiteralPath $taskOutput){throw 'Navigation archive output already exists'}
$taskInput=Join-Path ([IO.Path]::GetDirectoryName($taskOutput)) ('empty-paths-'+[Guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Path $taskInput -Force|Out-Null
# SA DAT path data begins with five uint32 counts. All zero is an empty, valid
# graph. The native loader still allocates its area sentinel (one CPathNode).
# Missing streaming entries are different from a loaded area containing no roads.
for($taskArea=0;$taskArea -lt 64;$taskArea++){
    [IO.File]::WriteAllBytes((Join-Path $taskInput ('nodes'+$taskArea+'.dat')),[byte[]]::new(20))
}
& (Join-Path $PSScriptRoot '..\build\Release\lcs-img.exe') pack $taskInput $taskOutput
if($LASTEXITCODE){throw 'Empty native navigation archive packing failed'}
& (Join-Path $PSScriptRoot 'validate-navigation.ps1') -ArchiveFile $taskOutput
