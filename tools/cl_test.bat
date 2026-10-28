@echo off
cd /d H:\tetris_cpp\build
set PATH=C:\Strawberry\c\bin;%PATH%
call "C:\Program Files\Microsoft Visual Studio\18\Enterprise\Common7\Tools\VsDevCmd.bat" -no_logo -arch=x64 -host_arch=x64 >nul
cl /nologo /std:c++20 /EHsc /c /DWIN32 /D_WINDOWS /DNDEBUG /I H:\tetris_cpp\third_party\DirectXTK\Inc /I H:\tetris_cpp\assets\generated H:\tetris_cpp\src\ui_font.cpp
if errorlevel 1 (echo CL_FAILED & exit /b 1)
dumpbin /symbols ui_font.obj | findstr /C:"End@UiFont"
