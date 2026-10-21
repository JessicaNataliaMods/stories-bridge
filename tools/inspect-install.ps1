param([Parameter(Mandatory)][string]$GameDirectory)
$ErrorActionPreference = 'Stop'
$taskGameRoot = (Resolve-Path -LiteralPath $GameDirectory).Path
$taskExe = Join-Path $taskGameRoot 'gta_sa.exe'
$taskBytes = [IO.File]::ReadAllBytes($taskExe)
function U16([int]$Offset) { [BitConverter]::ToUInt16($taskBytes,$Offset) }
function U32([int]$Offset) { [BitConverter]::ToUInt32($taskBytes,$Offset) }
$taskPe = U32 0x3C
if ((U16 0) -ne 0x5A4D -or (U32 $taskPe) -ne 0x4550) { throw 'Invalid PE executable' }
$taskMachine = U16 ($taskPe+4)
$taskSections = U16 ($taskPe+6)
$taskOptional = $taskPe+24
$taskBase = U32 ($taskOptional+28)
$taskSectionTable = $taskOptional+(U16 ($taskPe+20))
function AddressOffset([uint32]$Address) {
    $taskRva = $Address-$taskBase
    for($taskSection=0;$taskSection -lt $taskSections;$taskSection++) {
        $taskRow = $taskSectionTable+40*$taskSection
        $taskStart = U32 ($taskRow+12)
        $taskSize = U32 ($taskRow+16)
        if ($taskRva -ge $taskStart -and $taskRva-$taskStart -lt $taskSize) {
            return (U32 ($taskRow+20))+$taskRva-$taskStart
        }
    }
    throw ('Address 0x{0:X} is outside file sections' -f $Address)
}
$taskCallOffset = AddressOffset 0x53E981
$taskScriptLoopOffset = AddressOffset 0x53BFC7
$taskScriptStartupOffset = AddressOffset 0x53BCC9
$taskScriptStartupTarget=$null
if($taskBytes[$taskScriptStartupOffset] -eq 0xE8){$taskScriptStartupTarget=0x53BCC9+5+[BitConverter]::ToInt32($taskBytes,$taskScriptStartupOffset+1)}
$taskScriptLoopTarget=$null
if($taskBytes[$taskScriptLoopOffset] -eq 0xE8){$taskScriptLoopTarget=0x53BFC7+5+[BitConverter]::ToInt32($taskBytes,$taskScriptLoopOffset+1)}
$taskDispatcherOffset = AddressOffset 0x469EB0
$taskDispatcherBytes = [BitConverter]::ToString($taskBytes,$taskDispatcherOffset,16)
$taskCallTarget = $null
if ($taskBytes[$taskCallOffset] -eq 0xE8) {
    $taskCallTarget = 0x53E981+5+[BitConverter]::ToInt32($taskBytes,$taskCallOffset+1)
}
[pscustomobject]@{
    GameDirectory = $taskGameRoot
    ExecutableBytes = $taskBytes.Length
    SHA256 = (Get-FileHash -LiteralPath $taskExe -Algorithm SHA256).Hash
    Machine = ('0x{0:X4}' -f $taskMachine)
    ImageBase = ('0x{0:X8}' -f $taskBase)
    GameProcessCallBytes = [BitConverter]::ToString($taskBytes,$taskCallOffset,5)
    GameProcessCallTarget = if($null -ne $taskCallTarget) { '0x{0:X8}' -f $taskCallTarget } else { 'Not a CALL instruction' }
    GameProcessHookMatches = $taskMachine -eq 0x14C -and $taskBase -eq 0x400000 -and $taskCallTarget -eq 0x53BEE0
    NativeScriptDispatcherBytes = $taskDispatcherBytes
    NativeScriptDispatcherMatches = $taskDispatcherBytes -eq '66-FF-05-F4-47-A4-00-8B-51-14-33-C0-66-8B-02-83'
    NativeScriptLoopCallBytes = [BitConverter]::ToString($taskBytes,$taskScriptLoopOffset,5)
    NativeScriptLoopCallMatches = $taskMachine -eq 0x14C -and $taskBase -eq 0x400000 -and $taskScriptLoopTarget -eq 0x46A000
    NativeScriptStartupCallBytes = [BitConverter]::ToString($taskBytes,$taskScriptStartupOffset,5)
    NativeScriptStartupCallMatches = $taskMachine -eq 0x14C -and $taskBase -eq 0x400000 -and $taskScriptStartupTarget -eq 0x46A000
    IsCompactSize = $taskBytes.Length -eq 5189632
    HasAsiLoader = (Test-Path -LiteralPath (Join-Path $taskGameRoot 'dinput8.dll')) -or (Test-Path -LiteralPath (Join-Path $taskGameRoot 'vorbisFile.dll'))
} | ConvertTo-Json
