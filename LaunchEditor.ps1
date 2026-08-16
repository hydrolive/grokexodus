# Launch Unreal Editor detached from the agent Job Object.
$exe = "C:\Program Files\Epic Games\UE_5.8\Engine\Binaries\Win64\UnrealEditor.exe"
$proj = "E:\Github\grokexodus\GrokExodus\GrokExodus.uproject"
$cmd = '"{0}" "{1}" /Game/Voxel/Maps/Lvl_VoxelPlanet -skipcompile' -f $exe, $proj
$r = Invoke-CimMethod -ClassName Win32_Process -MethodName Create -Arguments @{
	CommandLine      = $cmd
	CurrentDirectory = "C:\Program Files\Epic Games\UE_5.8\Engine\Binaries\Win64"
}
if ($r.ReturnValue -ne 0 -or -not $r.ProcessId) {
	Write-Error "Win32_Process.Create failed ret=$($r.ReturnValue)"
	exit 1
}
Write-Output "Launched UnrealEditor pid=$($r.ProcessId)"
exit 0
