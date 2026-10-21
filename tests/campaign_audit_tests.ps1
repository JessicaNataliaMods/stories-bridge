param([Parameter(Mandatory)][string]$Executable,[Parameter(Mandatory)][string]$OriginalScript)
$ErrorActionPreference='Stop'
$taskReportText=& $Executable $OriginalScript
if($LASTEXITCODE){throw 'The complete original campaign could not be decoded'}
$taskReport=($taskReportText -join "`n") | ConvertFrom-Json
if($taskReport.missionBlocks -ne 118 -or $taskReport.contextsScanned -ne 119 -or $taskReport.uniqueCommands -ne 973){throw 'Campaign control-flow inventory is incomplete or changed'}
if(!$taskReport.inventoryComplete -or $taskReport.decodeErrors -ne 0 -or $taskReport.unresolvedDynamicLabels -ne 0){throw 'Unresolved campaign instructions or branches'}
if($taskReport.gameExecuted -or $taskReport.playabilityVerified -or !$taskReport.sourceHandlerClauseIsNotRuntimeSupport){throw 'Static analysis must not claim runtime support'}
if($taskReport.missionDetails.Count -ne 118 -or $taskReport.missionDetails[9].index -ne 9 -or 'VIC1' -notin $taskReport.missionDetails[9].scriptNames -or 'VIC2' -notin $taskReport.missionDetails[48].scriptNames){throw 'Per-mission dependency inventory missed original story scripts'}
# These regressions originate in misleading original debug signatures: the
# implementation consumes additional numeric/text operands, or output handles.
foreach($taskName in @('COMMAND_INDUSTRIAL_PASSED','COMMAND_COMMERCIAL_PASSED','COMMAND_SUBURBAN_PASSED',
    'COMMAND_PRINT_HELP_FOREVER_NO_BRIEF','COMMAND_SET_PICKUP_VALUE','COMMAND_CHAR_FOLLOW_PATH',
    'COMMAND_GOSUB_FILE','COMMAND_CALL','COMMAND_START_NEW_SCRIPT','COMMAND_LOAD_CUTSCENE')) {
    $taskCommand=@($taskReport.commands | Where-Object {$_.name -eq $taskName})
    if($taskCommand.Count -ne 1 -or $taskCommand[0].instructionSites -lt 1){throw "Original control-flow dependency missing: $taskName"}
}
$taskScene=$taskReport.commands | Where-Object {$_.name -eq 'COMMAND_LOAD_CUTSCENE'}
if('HOMSWET' -notin $taskScene.textOperands){throw 'Original introduction dependency was missed'}
Write-Output "PASS: MAIN and all 118 original mission buffers; both condition outcomes and function/thread entries, zero decode errors; runtime behavior not simulated"
