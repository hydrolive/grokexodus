@echo off
REM Launch Unreal outside the agent Job Object. cmd start / Start-Process
REM stay in the job and die when the tool command ends — the user then
REM has no editor. Win32_Process.Create is breakaway.
powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0LaunchEditor.ps1"
