$ErrorActionPreference='Stop'
$taskProject=Split-Path -Parent $PSScriptRoot
$taskSource=Join-Path $taskProject '..\lcs-ps2\AUDIO\NEWS'
$taskTarget=Join-Path $taskProject 'out\lcs-converted\audio\news'
$taskDecoder=Join-Path $taskProject 'build\Release\lcs-cutscene-audio.exe'
New-Item -ItemType Directory -Path $taskTarget -Force | Out-Null
$taskFiles=@()
foreach($taskLetter in [char[]]'ABCDEFGHIJKLMN'){
    $taskOriginal=Join-Path $taskSource ('NEWS_'+$taskLetter+'.VB')
    $taskWave=Join-Path $taskTarget ('NEWS_'+$taskLetter+'.wav')
    if(!(Test-Path -LiteralPath $taskWave)){
        & $taskDecoder $taskOriginal $taskWave | Out-Null
        if($LASTEXITCODE){throw 'Original PS2 news decoding failed'}
    }
    $taskFiles += [pscustomobject]@{Index=([int]$taskLetter-[int][char]'A');Original=$taskOriginal;SourceHash=(Get-FileHash -LiteralPath $taskOriginal).Hash;Wave=$taskWave;WaveHash=(Get-FileHash -LiteralPath $taskWave).Hash}
}
[pscustomobject]@{OriginalNewsStreams=14;GameExecuted=$false;AudioPlayed=$false;Files=$taskFiles} | ConvertTo-Json -Depth 4 | Set-Content -LiteralPath (Join-Path $taskTarget 'manifest.json') -Encoding UTF8
Write-Output 'Prepared all 14 original news streams; no audio played or game executed'
