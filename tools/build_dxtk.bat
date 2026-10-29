@echo off
call "C:\Program Files\Microsoft Visual Studio\18\Enterprise\Common7\Tools\VsDevCmd.bat" -no_logo -arch=x64 -host_arch=x64
rem Build DirectXTK with the static CRT (/MT) so tetris.exe can also link /MT,
rem and with XAudio 2.9 (the XAudio2_9.dll that ships with Windows 10/11).
cmake -S H:\tetris_cpp\third_party\DirectXTK -B H:\tetris_cpp\third_party\DirectXTK\out\build\x64-Release -G Ninja ^
    -DCMAKE_BUILD_TYPE=Release ^
    -DCMAKE_MSVC_RUNTIME_LIBRARY=MultiThreaded ^
    -DDIRECTX_ARCH=x64 ^
    -DBUILD_SHARED_LIBS=OFF ^
    -DBUILD_TOOLS=OFF ^
    -DBUILD_XAUDIO_WIN10=ON ^
    -DBUILD_XAUDIO_WIN8=OFF ^
    -DBUILD_XAUDIO_REDIST=OFF
if errorlevel 1 exit /b 1
cmake --build H:\tetris_cpp\third_party\DirectXTK\out\build\x64-Release --target DirectXTK
