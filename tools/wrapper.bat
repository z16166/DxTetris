@echo off
cd /d H:\tetris_cpp
set PATH=C:\Strawberry\c\bin;%PATH%
call "C:\Program Files\Microsoft Visual Studio\18\Enterprise\Common7\Tools\VsDevCmd.bat" -no_logo -arch=x64 -host_arch=x64 >nul
cmake --build build > build\build_log.txt 2>&1
type build\build_log.txt | findstr /C:"error" /C:"FAILED" /C:"ninja"
