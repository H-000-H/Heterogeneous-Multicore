@echo off
CALL C:\esp\v5.5.2\esp-idf\export.bat > C:\Users\18514\flash_log_pre.txt 2>&1
IF %ERRORLEVEL% NEQ 0 (
    echo Using direct python...
)
D:\Espressif_vscode\python_env\idf5.5_py3.11_env\Scripts\python.exe D:\Espressif_vscode\frameworks\esp-idf-v5.5.2\components\esptool_py\esptool\esptool.py --port COM13 --baud 921600 write_flash 0x0 C:\Users\18514\mini-tree-rt-treadtest\build\bootloader\bootloader.bin 0x8000 C:\Users\18514\mini-tree-rt-treadtest\build\partition_table\partition-table.bin 0x10000 C:\Users\18514\mini-tree-rt-treadtest\build\mini-tree-rt-treadtest.bin
EXIT /B %ERRORLEVEL%
