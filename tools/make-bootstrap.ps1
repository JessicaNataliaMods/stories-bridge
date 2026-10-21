param([string]$OutputFile=(Join-Path $PSScriptRoot '..\out\world-stage\data\script\main.scm'))
$ErrorActionPreference='Stop'
$taskOutput=[IO.Path]::GetFullPath($OutputFile)
if(Test-Path -LiteralPath $taskOutput){throw 'Bootstrap output already exists'}
$taskBytes=[Collections.Generic.List[byte]]::new()
function Add-U16([uint16]$Value) {$taskBytes.AddRange([BitConverter]::GetBytes($Value))}
function Add-U32([uint32]$Value) {$taskBytes.AddRange([BitConverter]::GetBytes($Value))}
function Add-Integer([int]$Value) {$taskBytes.Add(1);$taskBytes.AddRange([BitConverter]::GetBytes($Value))}
function Add-Float([single]$Value) {$taskBytes.Add(6);$taskBytes.AddRange([BitConverter]::GetBytes($Value))}
function Add-Local([uint16]$Value) {$taskBytes.Add(3);Add-U16 $Value}
function Add-Global([uint16]$Value) {$taskBytes.Add(2);Add-U16 $Value}
function Add-Stage([int]$Value){
    Add-U16 0x0004;Add-Global 20;Add-Integer $Value
    # CGame::Initialise processes scripts once, then updates the camera. The
    # player must exist before this first script pass yields to that camera.
    if($Value -ne 1){Add-U16 0x0001;Add-Integer 0}
}
function Add-Header([uint32]$Next,[byte]$Index){Add-U16 2;$taskBytes.Add(1);Add-U32 $Next;$taskBytes.Add($Index)}
# Six native SA SCM chunks. All addresses are native file offsets, with no LCS prefix.
Add-Header 512 ([byte][char]'s');while($taskBytes.Count -lt 512){$taskBytes.Add(0)}
Add-Header 548 0;Add-U32 1;for($taskI=0;$taskI -lt 24;$taskI++){$taskBytes.Add(0)}
Add-Header 572 1;$taskMainSizePosition=$taskBytes.Count;Add-U32 0;Add-U32 0;Add-U16 0;Add-U16 0;Add-U32 32
Add-Header 588 2;Add-U32 0;Add-U32 0
Add-Header 600 3;Add-U32 0
Add-Header 616 4;Add-U32 504;Add-U32 1
if($taskBytes.Count -ne 616){throw 'SCM header layout mismatch'}
Add-U16 0x03A4;$taskBytes.Add(9);$taskBytes.AddRange([Text.Encoding]::ASCII.GetBytes('LCSSABT'+[char]0))
Add-U16 0x016A;Add-Integer 0;Add-Integer 0
Add-U16 0x00C0;Add-Integer 9;Add-Integer 0
# Until the road graph exists, SA traffic/population must not use its old routes.
Add-U16 0x01EB;Add-Float 0
Add-U16 0x03DE;Add-Float 0
Add-Stage 1
# Standard SCM exports used by existing SA/CLEO mods. Publish them in the first
# script pass before yielding, not just private bootstrap locals.
Add-U16 0x0053;Add-Integer 0;Add-Float 880;Add-Float (-542);Add-Float 20;Add-Global 8
Add-U16 0x01F5;Add-Global 8;Add-Global 12
# 03F7 (LOAD_COLLISION) has no vanilla SA handler and consumes no parameters.
# LOAD_SCENE performs the synchronous scene/collision load needed here.
Add-Stage 2
Add-U16 0x03CB;Add-Float 880;Add-Float (-542);Add-Float 20
Add-U16 0x0001;Add-Integer 0
# -100 is native SA's ground-height sentinel. Map collision is streamed first.
Add-Stage 3
Add-U16 0x00A1;Add-Global 12;Add-Float 880;Add-Float (-542);Add-Float (-100)
Add-U16 0x01B4;Add-Global 8;Add-Integer 1
Add-U16 0x0373
Add-U16 0x016A;Add-Integer 500;Add-Integer 1
# Marker read by the ASI: this bootstrap and the new world have initialized.
Add-U16 0x0004;Add-Global 16;Add-Integer 0x4C435357
$taskLoop=$taskBytes.Count
Add-U16 0x0001;Add-Integer 0
Add-U16 0x0002;Add-Integer $taskLoop
$taskSizeBytes=[BitConverter]::GetBytes([uint32]$taskBytes.Count)
for($taskI=0;$taskI -lt 4;$taskI++){$taskBytes[$taskMainSizePosition+$taskI]=$taskSizeBytes[$taskI]}
New-Item -ItemType Directory -Path ([IO.Path]::GetDirectoryName($taskOutput)) -Force | Out-Null
[IO.File]::WriteAllBytes($taskOutput,$taskBytes.ToArray())
[pscustomobject]@{File=$taskOutput;NativeSA=$true;Entry=616;Bytes=$taskBytes.Count;LcsCampaignStarted=$false;GameTested=$false}|ConvertTo-Json
