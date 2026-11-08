@echo off
powershell -NoProfile -File "%~dp0build.ps1" %*
exit /b %errorlevel%
