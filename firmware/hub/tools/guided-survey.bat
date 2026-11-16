@echo off
rem Guided Bluetooth test at the van. Close any "pio device monitor" first.
rem Usage: guided-survey.bat [COM port]   (default COM4)
set PORT=%1
if "%PORT%"=="" set PORT=COM4
set PY=%USERPROFILE%\.platformio\penv\Scripts\python.exe
if not exist "%PY%" set PY=python
cd /d "%~dp0.."
"%PY%" "%~dp0guided_survey.py" --port %PORT%
pause
