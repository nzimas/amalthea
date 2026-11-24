@echo off
rem Restore the stock TRIMUI system. Optional: restore.bat -Backup path\to\backup-folder
powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0amalthea.ps1" restore %*
echo.
pause
