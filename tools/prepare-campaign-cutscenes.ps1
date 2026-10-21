param(
    [string]$OriginalCuts=(Join-Path $PSScriptRoot '..\out\first-mission-assets\original-cuts'),
    [string]$OriginalModels=(Join-Path $PSScriptRoot '..\out\img-v1'),
    [string]$OriginalAudio=(Join-Path $PSScriptRoot '..\local\originals\lcs-ps2\AUDIO\CUTSCENE'),
    [string]$OutputDirectory=(Join-Path $PSScriptRoot '..\out\campaign-cutscenes-prepared'),
    [switch]$ValidateOnly
)
$ErrorActionPreference='Stop'
function Get-TaskSha256([string]$Path) {
    $taskHashStream=[IO.File]::OpenRead($Path)
    $taskHashAlgorithm=[Security.Cryptography.SHA256]::Create()
    try{return [BitConverter]::ToString($taskHashAlgorithm.ComputeHash($taskHashStream)).Replace('-','')}
    finally{$taskHashAlgorithm.Dispose();$taskHashStream.Dispose()}
}
$taskCuts=(Resolve-Path -LiteralPath $OriginalCuts).Path
$taskModels=(Resolve-Path -LiteralPath $OriginalModels).Path
$taskAudio=(Resolve-Path -LiteralPath $OriginalAudio).Path
$taskOutput=[IO.Path]::GetFullPath($OutputDirectory)
$taskConverter=Join-Path $PSScriptRoot '..\build\Release\lcs-convert-rw.exe'
$taskIfp=Join-Path $PSScriptRoot '..\build\Release\lcs-ifp-inspect.exe'
$taskIfpAdapt=Join-Path $PSScriptRoot '..\build\Release\lcs-ifp-adapt.exe'
$taskImg=Join-Path $PSScriptRoot '..\build\Release\lcs-img.exe'
$taskAudioTool=Join-Path $PSScriptRoot '..\build\Release\lcs-cutscene-audio.exe'
if(!$ValidateOnly -and (Test-Path -LiteralPath $taskOutput)){throw 'Campaign cutscene output exists; choose a new directory'}
$taskCutFiles=@(Get-ChildItem -LiteralPath $taskCuts -Filter '*.cut' | Sort-Object Name)
if($taskCutFiles.Count -ne 24){throw 'Original campaign cutscene archive is incomplete (expected 24 scenes)'}
$taskScenes=@();$taskUniqueModels=@{}
foreach($taskCut in $taskCutFiles) {
    $taskSection='';$taskBindings=@{}
    foreach($taskLine in [IO.File]::ReadAllLines($taskCut.FullName)) {
        $taskLine=$taskLine.Trim().TrimEnd([char]0)
        if($taskLine -eq 'model'){$taskSection='model';continue}
        if($taskLine -eq 'end'){$taskSection='';continue}
        if($taskSection -ne 'model'){continue}
        $taskFields=@($taskLine.Split(',') | ForEach-Object {$_.Trim()})
        if($taskFields.Count -lt 3 -or $taskFields[0] -ne '1'){throw "Unverified native model declaration: $($taskCut.Name)"}
        $taskModel=$taskFields[1].ToLowerInvariant()
        if($taskModel -notmatch '^[a-z0-9_]{1,23}$'){throw "Invalid cutscene model name: $taskModel"}
        $taskUniqueModels[$taskModel]=$true
        for($taskField=2;$taskField -lt $taskFields.Count;$taskField++) {
            $taskAnimation=$taskFields[$taskField].ToLowerInvariant()
            if($taskAnimation -notmatch '^[a-z0-9_-]{1,23}$' -or $taskBindings.ContainsKey($taskAnimation)){throw 'Invalid/duplicate cutscene animation declaration'}
            $taskBindings[$taskAnimation]=$taskModel
        }
    }
    if(!$taskBindings.Count -or $taskBindings.Count -gt 50 -or @($taskBindings.Values | Sort-Object -Unique).Count -gt 20){throw 'Cutscene exceeds native model/object capacity'}
    $taskScenes+=[pscustomobject]@{Name=$taskCut.BaseName.ToLowerInvariant();Models=$taskBindings}
}
if($taskUniqueModels.Count -ne 42){throw 'Cutscene model inventory does not match all original scenes'}
$taskOriginalModelNames=@($taskUniqueModels.Keys | Sort-Object)
# CUTTEST's original CSCORD has duplicate bone ID zero and named skinned
# sequences (including Nub) that the SA bone-name table cannot resolve. Keep
# this resource explicitly unmounted instead of weakening actor validation.
$taskDeferred=@([pscustomobject]@{Name='cuttest';Reason='Original cscord: duplicate bone ID 0; skinned named sequences need retargeting for native SA'})
$taskScenes=@($taskScenes | Where-Object {$_.Name -ne 'cuttest'})
$taskUniqueModels=@{}
foreach($taskScene in $taskScenes){foreach($taskModel in $taskScene.Models.Values){$taskUniqueModels[$taskModel]=$true}}
if(!$ValidateOnly) {New-Item -ItemType Directory -Path (Join-Path $taskOutput 'models'),(Join-Path $taskOutput 'cuts'),(Join-Path $taskOutput 'audio'),(Join-Path $taskOutput 'validation') | Out-Null}
else {
    $taskPreviousManifest=Get-Content -LiteralPath (Join-Path $taskOutput 'manifest.json') -Raw | ConvertFrom-Json
    if(!$taskPreviousManifest.Prepared){throw 'Campaign cutscenes were not prepared'}
    foreach($taskFile in $taskPreviousManifest.Files) {
        if((Get-TaskSha256 (Join-Path $taskOutput $taskFile.Path)) -ne $taskFile.Sha256){throw "Prepared resource hash mismatch: $($taskFile.Path)"}
    }
    foreach($taskFile in $taskPreviousManifest.Sources) {
        if((Get-TaskSha256 $taskFile.Path) -ne $taskFile.Sha256){throw "Original resource hash mismatch: $($taskFile.Path)"}
    }
}
$taskBones=@{};$taskFrames=@{};$taskFrameTags=@{};$taskSkinned=@{}
foreach($taskModel in @($taskUniqueModels.Keys | Sort-Object)) {
    if(!$ValidateOnly) {
        foreach($taskExtension in 'dff','txd') {
            & $taskConverter (Join-Path $taskModels ($taskModel+'.'+$taskExtension)) (Join-Path $taskOutput ('models\'+$taskModel+'.'+$taskExtension)) | Out-Null
            if($LASTEXITCODE){throw "Cutscene resource conversion failed: $taskModel.$taskExtension"}
        }
    }
    $taskReport=@(& $taskConverter (Join-Path $taskOutput ('models\'+$taskModel+'.dff')) --info)
    if($LASTEXITCODE){throw "Converted scene model cannot be reread: $taskModel"}
    $taskBones[$taskModel]=@{};$taskFrames[$taskModel]=@{};$taskFrameTags[$taskModel]=@{};$taskSkinned[$taskModel]=$false
    foreach($taskLine in $taskReport) {
        if($taskLine -match '^bone=(\d+) flags='){$taskBones[$taskModel][$Matches[1]]=$true}
        if($taskLine -match '^frame=(.+)$'){$taskFrames[$taskModel][$Matches[1]]=$true}
        if($taskLine -match '^frame_bone name=(.+) bone_tag=(-?\d+)$'){$taskFrameTags[$taskModel][$Matches[1]]=[int]$Matches[2]}
        if($taskLine -eq 'skinned=true'){$taskSkinned[$taskModel]=$true}
    }
    if(!$ValidateOnly){[IO.File]::WriteAllLines((Join-Path $taskOutput ('validation\'+$taskModel+'-hierarchy.txt')),[string[]]$taskReport)}
}
$taskModelValidation=@(& $taskConverter (Join-Path $taskOutput 'models') --validate-actors)
if($LASTEXITCODE){throw 'Converted cutscene models failed structural/skin validation'}
if(!$ValidateOnly){[IO.File]::WriteAllLines((Join-Path $taskOutput 'validation\models.txt'),[string[]]$taskModelValidation)}
$taskSceneReports=@();$taskTotalBindings=0;$taskAudioCount=0
foreach($taskScene in $taskScenes) {
    $taskName=$taskScene.Name
    $taskIfpPath=Join-Path $taskOutput ('cuts\'+$taskName+'.ifp')
    if(!$ValidateOnly) {
        # The two Toshiko scenes refer to 'toshdoor'; the original model's
        # corresponding geometry frame is actually 'cstoshdoor'.
        $taskNormalizedPath=Join-Path $taskOutput ('validation\'+$taskName+'-normalized.ifp')
        $taskAdaptArguments=@((Join-Path $taskCuts ($taskName+'.ifp')),$taskNormalizedPath)
        if($taskName -in @('cashchp','deadly')){$taskAdaptArguments+=@('toshdoor','cstoshdoor')}
        $taskAdaptReport=@(& $taskIfpAdapt @taskAdaptArguments)
        if($LASTEXITCODE){throw "Original cutscene ANPK adaptation failed: $taskName"}
        $taskNormalizedReport=@(& $taskIfp $taskNormalizedPath --bindings)
        if($LASTEXITCODE){throw "Normalized ANPK cannot be reread: $taskName"}
        $taskBoneRows=@()
        foreach($taskLine in $taskNormalizedReport) {
            if($taskLine -notmatch '^binding animation=(\S+) name=(.+) bone_tag=-1 keyframes=(\d+)$'){continue}
            $taskAnimation=$Matches[1];$taskFrame=$Matches[2]
            if(!$taskScene.Models.ContainsKey($taskAnimation)){continue}
            $taskModel=$taskScene.Models[$taskAnimation]
            if(!$taskSkinned[$taskModel] -or !$taskFrameTags[$taskModel].ContainsKey($taskFrame)){continue}
            $taskBoneTag=$taskFrameTags[$taskModel][$taskFrame]
            if($taskBoneTag -lt 0 -or !$taskBones[$taskModel].ContainsKey([string]$taskBoneTag)){throw 'Named skin frame has no unique native hierarchy bone'}
            $taskBoneRows+=($taskAnimation+"`t"+$taskFrame+"`t"+$taskBoneTag)
        }
        $taskMappingPath=Join-Path $taskOutput ('validation\'+$taskName+'-bone-tags.tsv')
        [IO.File]::WriteAllLines($taskMappingPath,[string[]]$taskBoneRows)
        $taskTagReport=@(& $taskIfpAdapt $taskNormalizedPath $taskIfpPath --bone-tags $taskMappingPath)
        if($LASTEXITCODE){throw "Verified skin bone-tag adaptation failed: $taskName"}
        [IO.File]::WriteAllLines((Join-Path $taskOutput ('validation\'+$taskName+'-adaptation.txt')),[string[]]@($taskAdaptReport+$taskTagReport))
    }
    $taskAnimationReport=@(& $taskIfp $taskIfpPath --bindings)
    if($LASTEXITCODE){throw "Invalid original cutscene animation: $taskName"}
    $taskBindingCount=0;$taskAnimations=@{};$taskDuration=0.0;$taskUnbound=@();$taskUnused=@{}
    foreach($taskLine in $taskAnimationReport) {
        if($taskLine -match ' duration_seconds=(\S+)$'){$taskDuration=[Math]::Max($taskDuration,[double]::Parse($Matches[1],[Globalization.CultureInfo]::InvariantCulture))}
        if($taskLine -notmatch '^binding animation=(\S+) name=(.+) bone_tag=(-?\d+) keyframes=(\d+)$'){continue}
        $taskAnimation=$Matches[1];$taskFrame=$Matches[2];$taskTag=[int]$Matches[3]
        # IFP can include extra animations that CUT does not request (e.g.
        # CSPOLICE in SAYONAR); these do not create cutscene objects.
        if(!$taskScene.Models.ContainsKey($taskAnimation)){$taskUnused[$taskAnimation]=$true;continue}
        $taskModel=$taskScene.Models[$taskAnimation];$taskAnimations[$taskAnimation]=$true
        if($taskTag -ge 0) {
            if(!$taskBones[$taskModel].ContainsKey([string]$taskTag)){$taskUnbound+="Missing bone $taskAnimation/$taskTag";continue}
        } elseif(!$taskFrames[$taskModel].ContainsKey($taskFrame)){$taskUnbound+="Missing frame $taskAnimation/$taskFrame";continue}
        elseif($taskSkinned[$taskModel]) {
            # SA resolves skinned named sequences through a canonical bone-name
            # table, not the DFF frame strings. A match of those strings alone
            # must never be counted as a verified native association.
            $taskUnbound+="Skinned named sequence requires native bone-tag mapping: $taskAnimation/$taskFrame";continue
        }
        $taskBindingCount++
    }
    if($taskAnimations.Count -ne $taskScene.Models.Count){throw "Unbound scene model/animation: $taskName"}
    $taskCameraPath=if($ValidateOnly){Join-Path $taskOutput ('cuts\'+$taskName+'.dat')}else{Join-Path $taskCuts ($taskName+'.dat')}
    $taskCamera=[Text.Encoding]::ASCII.GetString([IO.File]::ReadAllBytes($taskCameraPath)).TrimEnd([char]0)
    $taskTracks=@($taskCamera.Split(';') | Where-Object {$_.Trim()})
    if($taskTracks.Count -ne 4){throw "Cutscene camera requires four tracks: $taskName"}
    $taskCameraKeys=@();$taskCameraDuration=0.0
    for($taskTrack=0;$taskTrack -lt 4;$taskTrack++) {
        $taskTokens=@($taskTracks[$taskTrack].Split(',') | ForEach-Object {$_.Trim().TrimEnd('f','F')} | Where-Object {$_})
        $taskValues=@($taskTokens | ForEach-Object {[double]::Parse($_,[Globalization.CultureInfo]::InvariantCulture)})
        $taskCount=[int]$taskValues[0];$taskStride=if($taskTrack -lt 2){4}else{10}
        if($taskCount -lt 2 -or $taskCount -ne $taskValues[0] -or $taskValues.Count -ne 1+$taskCount*$taskStride -or $taskValues.Count -gt 800){throw "Camera count/length exceeds native capacity: $taskName"}
        foreach($taskValue in $taskValues){if([double]::IsNaN($taskValue) -or [double]::IsInfinity($taskValue)){throw "Nonfinite camera value: $taskName"}}
        $taskPrevious=0.0
        for($taskKey=0;$taskKey -lt $taskCount;$taskKey++) {
            $taskTime=$taskValues[1+$taskKey*$taskStride]
            if($taskTime -lt $taskPrevious){throw "Decreasing camera time: $taskName"}
            $taskPrevious=$taskTime
        }
        $taskCameraDuration=[Math]::Max($taskCameraDuration,$taskPrevious);$taskCameraKeys+=$taskCount
    }
    $taskVb=Join-Path $taskAudio ($taskName+'.vb');$taskHasAudio=Test-Path -LiteralPath $taskVb
    if(!$taskHasAudio -and $taskName -ne 'cuttest'){throw "Original cutscene audio missing: $taskName"}
    $taskAudioDuration=$null
    if($taskHasAudio) {
        $taskAudioCount++
        if(!$ValidateOnly) {
            $taskAudioReport=@(& $taskAudioTool $taskVb (Join-Path $taskOutput ('audio\'+$taskName+'.wav')))
            if($LASTEXITCODE){throw "Cutscene audio decoding failed: $taskName"}
            [IO.File]::WriteAllLines((Join-Path $taskOutput ('validation\'+$taskName+'-audio.txt')),[string[]]$taskAudioReport)
        }
        $taskWave=[IO.File]::ReadAllBytes((Join-Path $taskOutput ('audio\'+$taskName+'.wav')))
        if($taskWave.Length -lt 44 -or [Text.Encoding]::ASCII.GetString($taskWave,0,4) -ne 'RIFF' -or [BitConverter]::ToUInt32($taskWave,4) -ne $taskWave.Length-8 -or [BitConverter]::ToUInt16($taskWave,22) -ne 2 -or [BitConverter]::ToUInt32($taskWave,24) -ne 32000 -or [BitConverter]::ToUInt16($taskWave,34) -ne 16 -or [BitConverter]::ToUInt32($taskWave,40) -ne $taskWave.Length-44){throw "Invalid prepared stereo wave: $taskName"}
        $taskAudioDuration=($taskWave.Length-44)/128000.0
    }
    if(!$ValidateOnly) {
        foreach($taskExtension in 'cut','dat'){Copy-Item -LiteralPath (Join-Path $taskCuts ($taskName+'.'+$taskExtension)) -Destination (Join-Path $taskOutput 'cuts')}
        [IO.File]::WriteAllLines((Join-Path $taskOutput ('validation\'+$taskName+'-animation.txt')),[string[]]$taskAnimationReport)
    }
    $taskTotalBindings+=$taskBindingCount
    $taskSceneReports+=[pscustomobject]@{Name=$taskName;Objects=$taskScene.Models.Count;Models=@($taskScene.Models.Values | Sort-Object -Unique);SequenceModelBindings=$taskBindingCount;UnboundSequences=$taskUnbound;BindingsComplete=($taskUnbound.Count -eq 0);UnusedAnimations=@($taskUnused.Keys | Sort-Object);AnimationDurationSeconds=$taskDuration;CameraKeys=$taskCameraKeys;CameraLastKey=$taskCameraDuration;OriginalAudio=$taskHasAudio;AudioDurationSeconds=$taskAudioDuration}
    Write-Output "Inspected $taskName : $taskBindingCount matched animation/model bindings, $($taskUnbound.Count) unresolved, four camera tracks, audio=$taskHasAudio"
}
if($ValidateOnly){
    # Compare typed values rather than JSON spelling/order, which differs
    # between Windows PowerShell and PowerShell 7 (notably final/final_2).
    if($taskSceneReports.Count -ne $taskPreviousManifest.Scenes.Count){throw 'Prepared scene count changed'}
    foreach($taskSceneReport in $taskSceneReports) {
        $taskExpected=@($taskPreviousManifest.Scenes | Where-Object {$_.Name -ceq $taskSceneReport.Name})
        if($taskExpected.Count -ne 1){throw 'Missing/duplicate prepared scene inventory row'}
        foreach($taskProperty in $taskSceneReport.PSObject.Properties) {
            $taskActualValues=@($taskProperty.Value);$taskExpectedValues=@($taskExpected[0].($taskProperty.Name))
            if($taskActualValues.Count -ne $taskExpectedValues.Count){throw "Scene inventory length changed: $($taskSceneReport.Name)/$($taskProperty.Name)"}
            for($taskValueIndex=0;$taskValueIndex -lt $taskActualValues.Count;$taskValueIndex++) {
                $taskActualValue=$taskActualValues[$taskValueIndex];$taskExpectedValue=$taskExpectedValues[$taskValueIndex]
                if($taskActualValue -is [double] -or $taskActualValue -is [single]) {
                    if([Math]::Abs([double]$taskActualValue-[double]$taskExpectedValue) -gt 1e-8){throw "Scene inventory number changed: $($taskSceneReport.Name)/$($taskProperty.Name)"}
                } elseif($taskActualValue -cne $taskExpectedValue){throw "Scene inventory value changed: $($taskSceneReport.Name)/$($taskProperty.Name) actual=$taskActualValue expected=$taskExpectedValue"}
            }
        }
    }
    if($taskPreviousManifest.NativeModelsMounted -or $taskPreviousManifest.NativeAnimationPlayed -or $taskPreviousManifest.NativeAudioPlayed -or $taskPreviousManifest.CutscenesPlayable -or $taskPreviousManifest.CampaignPlayable -or $taskPreviousManifest.GameExecuted){throw 'Offline preparation must not assert native playback/playability'}
    Write-Output "PASS: $($taskScenes.Count) scenes, $($taskUniqueModels.Count) models, $taskTotalBindings bindings, $taskAudioCount stereo tracks; known unresolved sequences stay explicit; no game execution";return
}
& $taskImg pack (Join-Path $taskOutput 'cuts') (Join-Path $taskOutput 'cuts.img')
if($LASTEXITCODE){throw 'Cannot pack all original cutscene data in IMG v2'}
& $taskImg pack (Join-Path $taskOutput 'models') (Join-Path $taskOutput 'cutscene-models.img')
if($LASTEXITCODE){throw 'Cannot pack all converted scene models in IMG v2'}
$taskFiles=@(Get-ChildItem -LiteralPath $taskOutput -Recurse -File | ForEach-Object {[pscustomobject]@{Path=$_.FullName.Substring($taskOutput.Length+1);Sha256=(Get-TaskSha256 $_.FullName)}})
$taskSources=@(Get-ChildItem -LiteralPath $taskCuts -File;foreach($taskModel in $taskOriginalModelNames){foreach($taskExtension in 'dff','txd'){Get-Item -LiteralPath (Join-Path $taskModels ($taskModel+'.'+$taskExtension))}};Get-ChildItem -LiteralPath $taskAudio -Filter '*.vb' -File) | ForEach-Object {[pscustomobject]@{Path=$_.FullName;Sha256=(Get-TaskSha256 $_.FullName)}}
[pscustomobject]@{Prepared=$true;Installed=$false;OriginalSceneCount=$taskCutFiles.Count;Scenes=$taskSceneReports;DeferredScenes=$taskDeferred;ModelCount=$taskUniqueModels.Count;SequenceModelBindings=$taskTotalBindings;StereoAudioTracks=$taskAudioCount;AudioFormat='PCM16 stereo 32000Hz';NativeArchiveFormat='IMGv2';NativeModelsMounted=$false;NativeAnimationPlayed=$false;NativeAudioPlayed=$false;CutscenesPlayable=$false;CampaignPlayable=$false;GameExecuted=$false;Files=$taskFiles;Sources=@($taskSources)} |
    ConvertTo-Json -Depth 6 | Set-Content -LiteralPath (Join-Path $taskOutput 'manifest.json') -Encoding UTF8
Write-Output 'Original cutscene resources prepared and checked; CUTTEST retargeting and native installation/playback remain pending'
