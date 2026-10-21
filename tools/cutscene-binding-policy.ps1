# These are terminal helper sequences present in INTRO's original IFP, absent
# from the original taxi driver skin. Both engines skip a missing association
# frame. Keep their keyframes, with no fabricated bone or retargeting.
function Get-IntroBindingPolicy([object]$Scene,[string]$PreparedDirectory) {
    $taskEndNames=@('bip01 headnub01','Bip01 L Finger0Nub','Bip01 R Finger0Nub','Bip01 L Toe0Nub','Bip01 R Toe0Nub')
    $taskExpected=@($taskEndNames | ForEach-Object {'Missing frame taxi_d/'+$_})
    if($Scene.Name -cne 'intro' -or $Scene.BindingsComplete -or $Scene.SequenceModelBindings -ne 113 -or
       @($Scene.UnboundSequences).Count -ne 5 -or @(Compare-Object $taskExpected @($Scene.UnboundSequences) -CaseSensitive).Count){
        throw 'Arrival binding exception does not match the verified original five terminal sequences'
    }
    $taskProject=(Resolve-Path -LiteralPath (Join-Path $PSScriptRoot '..')).Path
    $taskDriverInfo=@(& (Join-Path $taskProject 'build\Release\lcs-convert-rw.exe') (Join-Path $PreparedDirectory 'models\taxi_d.dff') --info)
    if($LASTEXITCODE -or $taskDriverInfo -cnotcontains 'skinned=true'){throw 'INTRO taxi driver skin could not be verified'}
    $taskAnimationInfo=@(& (Join-Path $taskProject 'build\Release\lcs-ifp-inspect.exe') (Join-Path $PreparedDirectory 'cuts\intro.ifp') --bindings)
    if($LASTEXITCODE){throw 'INTRO animation could not be verified'}
    $taskLcsBones=[IO.File]::ReadAllText((Join-Path $taskProject '..\res-relcs\src\animation\Bones.cpp'))
    $taskSaBones=[IO.File]::ReadAllText((Join-Path $taskProject 'vendor\sa-reference\RpAnimBlend.cpp'))
    $taskIgnored=@()
    foreach($taskName in $taskEndNames){
        if(@($taskAnimationInfo | Where-Object {$_ -ceq ('binding animation=taxi_d name='+$taskName+' bone_tag=-1 keyframes=2')}).Count -ne 1 -or
           $taskDriverInfo -icontains ('frame='+$taskName) -or $taskLcsBones.IndexOf('"'+$taskName+'"',[StringComparison]::OrdinalIgnoreCase) -ge 0 -or
           $taskSaBones.IndexOf('"'+$taskName+'"',[StringComparison]::OrdinalIgnoreCase) -ge 0){throw 'Terminal helper sequence is now bound or its original signature changed'}
        $taskIgnored+=[pscustomobject]@{Animation='taxi_d';Frame=$taskName;BoneTag=-1;Keyframes=2;Policy='Original engine skips absent association frame; keyframes preserved'}
    }
    return $taskIgnored
}
