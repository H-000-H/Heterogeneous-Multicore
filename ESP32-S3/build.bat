@echo off
CALL D:\Espressif_vscode\frameworks\esp-idf-v5.5.2\export.bat > build_env_log.txt 2>&1
IF %ERRORLEVEL% NEQ 0 (
    echo export.bat failed with error %ERRORLEVEL%
    type build_env_log.txt
    exit /b %ERRORLEVEL%
)
cd /d D:\Heterogeneous-Multicore-project\ESP32-S3
idf.py build 2>&1
EXIT /B %ERRORLEVEL%
