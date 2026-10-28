cd H:\tetris_cpp
set PATH=C:\Strawberry\c\bin;%PATH%
call "C:\Program Files\Microsoft Visual Studio\18\Enterprise\Common7\Tools\VsDevCmd.bat" -no_logo -arch=x64 -host_arch=x64
cd build
cmake -G Ninja -DCMAKE_BUILD_TYPE=Release ..
cmake --build . 2>&1 | tee build_log.txt
