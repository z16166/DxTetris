@echo off
call "C:\Program Files\Microsoft Visual Studio\18\Enterprise\Common7\Tools\VsDevCmd.bat" -no_logo -arch=x64 -host_arch=x64
cmake --build H:\tetris_cpp\third_party\DirectXTK\out\build\x64-Release
