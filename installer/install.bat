@echo off
rem Amalthea installer for Windows: double-click, or run from a command prompt (add -Yes to skip the prompt).
powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0amalthea.ps1" install %*
echo.
pause
