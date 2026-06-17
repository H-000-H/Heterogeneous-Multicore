@echo off
CALL C:\esp\v5.5.2\esp-idf\export.bat > C:\Users\18514\build_env_log.txt 2>&1
IF %ERRORLEVEL% NEQ 0 (
    echo export.bat failed with error %ERRORLEVEL%
    type C:\Users\18514\build_env_log.txt
    exit /b %ERRORLEVEL%
)
cd /d C:\Users\18514\mini-tree-rt-treadtest
idf.py build 2>&1
EXIT /B %ERRORLEVEL%
