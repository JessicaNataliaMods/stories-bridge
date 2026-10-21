param(
    [Parameter(Mandatory)][string]$GameDirectory,
    [string]$WorldDirectory=(Join-Path $PSScriptRoot '..\out\world-stage')
)
$ErrorActionPreference='Stop'
$taskGame=(Resolve-Path -LiteralPath $GameDirectory).Path
$taskWorld=(Resolve-Path -LiteralPath $WorldDirectory).Path
# DEFAULT.DAT loads peds, vehicles and weapons, but not the generic definitions
# used by C3dMarkers, pickups and procedural objects. Keep these native resources
# available without instantiating San Andreas's world IPLs.
$taskLines=@(
    '# Native engine resources required alongside the LCS world',
    'IMG DATA\PATHS\CARREC.IMG',
    'IMG DATA\SCRIPT\SCRIPT.IMG',
    'IMG MODELS\CUTSCENE.IMG',
    'IMG MODELS\LCS\PATHS.IMG',
    'IMG MODELS\LCS\WORLD.IMG',
    'IDE DATA\MAPS\generic\vegepart.IDE',
    'IDE DATA\MAPS\generic\barriers.IDE',
    'IDE DATA\MAPS\generic\dynamic.IDE',
    'IDE DATA\MAPS\generic\dynamic2.IDE',
    'IDE DATA\MAPS\generic\multiobj.IDE',
    'IDE DATA\MAPS\generic\procobj.IDE',
    'IDE DATA\MAPS\veh_mods\veh_mods.IDE',
    'IDE DATA\TXDCUT.IDE',
    'IDE DATA\LCS\WORLD.IDE',
    'COLFILE 0 MODELS\LCS\WORLD.COL',
    'IPL DATA\MAP.ZON',
    'IPL DATA\INFO.ZON'
)
$taskManifest=Get-Content -LiteralPath (Join-Path $taskWorld 'data\lcs\world-manifest.json') -Raw|ConvertFrom-Json
if(!$taskManifest.PlacementFiles){throw 'Split the world IPL into bounded files before creating the SA profile'}
foreach($taskPlacement in $taskManifest.PlacementFiles){
    if($taskPlacement.File -notmatch '^world[0-9]+\.ipl$'){throw 'Invalid world IPL filename'}
    $taskLines+=('IPL DATA\LCS\'+$taskPlacement.File)
}
foreach($taskLine in $taskLines){
    if($taskLine -match '^(IMG|IDE|IPL) (.+)$'){
        $taskRelative=$Matches[2]
        $taskPath=Join-Path $taskWorld $taskRelative
        if(!(Test-Path -LiteralPath $taskPath -PathType Leaf)){$taskPath=Join-Path $taskGame $taskRelative}
        if(!(Test-Path -LiteralPath $taskPath -PathType Leaf)){throw "Native resource missing: $taskRelative"}
    }
}
[IO.File]::WriteAllLines((Join-Path $taskWorld 'data\gta.dat'),$taskLines,[Text.Encoding]::ASCII)
& (Join-Path $PSScriptRoot 'validate-sa-profile.ps1') -GameDirectory $taskGame -WorldDirectory $taskWorld
if(!$?){throw 'Native SA profile validation failed'}
