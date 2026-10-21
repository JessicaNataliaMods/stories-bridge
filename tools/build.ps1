param([ValidateSet('Debug','Release')][string]$Configuration='Release')
$ErrorActionPreference='Stop'
$taskRoot = Split-Path -Parent $PSScriptRoot
$taskVsWhere=Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
$taskVsRoot=& $taskVsWhere -latest -products '*' -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
if (!$taskVsRoot) { throw 'Visual Studio C++ tools not found' }
$taskCmake=Join-Path $taskVsRoot 'Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe'
$taskCtest=Join-Path $taskVsRoot 'Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\ctest.exe'
$taskBuild=Join-Path $taskRoot 'build'
& $taskCmake -S $taskRoot -B $taskBuild -G 'Visual Studio 17 2022' -A Win32
if($LASTEXITCODE) { throw 'CMake configuration failed' }
& $taskCmake --build $taskBuild --config $Configuration --parallel 2 -- /nodeReuse:false
if($LASTEXITCODE) { throw 'Build failed' }
& $taskCtest --test-dir $taskBuild -C $Configuration --output-on-failure
if($LASTEXITCODE) { throw 'Tests failed' }
