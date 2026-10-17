@echo off
cd /d "%~dp0"
py -3.12 -B games\burnout3-pal\tools\autotest.py %*
exit /b %errorlevel%
