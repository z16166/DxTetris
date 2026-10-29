# CMakeLists.txt — MINE, final; DirectXTK static lib (built) + game target.
cmake_minimum_required(VERSION 3.29)
project(tetris LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
add_compile_options(/utf-8 /EHsc /guard:cf-)  # all sources are UTF-8 (cp936 default would mis-decode) ✓; /EHsc required for catch(...) to be wired — MSVC default C-style EH leaves it inert; CFG disabled — full-process fastfail 0xC0000409

set(DXTK "${CMAKE_CURRENT_SOURCE_DIR}/third_party/DirectXTK")
set(DXTK_LIB "${DXTK}/out/build/x64-Release/lib/DirectXTK.lib")

set(GEN_DIR "${CMAKE_CURRENT_SOURCE_DIR}/assets/generated")
set(SECTION_CSO "${GEN_DIR}/section_cso.inc")

# fxc from the active Windows SDK (VsDevCmd sets WindowsSdkVerBinPath; works on any runner/SDK version)
if(DEFINED ENV{WindowsSdkVerBinPath})
    set(FXC "$ENV{WindowsSdkVerBinPath}x64/fxc.exe")
endif()
if((NOT FXC OR NOT EXISTS "${FXC}") AND DEFINED ENV{WindowsSdkDir})
    file(TO_CMAKE_PATH "$ENV{WindowsSdkDir}bin/*/x64/fxc.exe" FXC_GLOB)
    file(GLOB FXC_CANDIDATES "${FXC_GLOB}")
    if(FXC_CANDIDATES)
        list(SORT FXC_CANDIDATES)
        list(GET FXC_CANDIDATES 0 FXC)
    endif()
endif()
if(NOT FXC OR NOT EXISTS "${FXC}")
    find_program(FXC NAMES fxc)
endif()
if(NOT FXC)
    message(FATAL_ERROR "fxc.exe not found; run cmake from a Visual Studio developer prompt")
endif()
add_custom_command(
    OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/game_vs.cso"
    COMMAND ${FXC} /T vs_5_0 /E VSMain /Fo "${CMAKE_CURRENT_BINARY_DIR}/game_vs.cso" "${CMAKE_CURRENT_SOURCE_DIR}/shaders/game.h"
    DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/shaders/game.h
    COMMENT "fxc vertex shader"
)
add_custom_command(
    OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/game_ps.cso"
    COMMAND ${FXC} /T ps_5_0 /E PSMain /Fo "${CMAKE_CURRENT_BINARY_DIR}/game_ps.cso" "${CMAKE_CURRENT_SOURCE_DIR}/shaders/game.h"
    DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/shaders/game.h
    COMMENT "fxc pixel shader"
)

# embed csos into header (writes section_cso.inc; assemble_assets.py assembles assets.h)
find_package(Python3 COMPONENTS Interpreter REQUIRED)
add_custom_command(
    OUTPUT "${SECTION_CSO}"
    COMMAND ${Python3_EXECUTABLE} "${CMAKE_CURRENT_SOURCE_DIR}/tools/embed_csos.py" "${CMAKE_CURRENT_BINARY_DIR}/game_vs.cso" "${CMAKE_CURRENT_BINARY_DIR}/game_ps.cso"
    DEPENDS "${CMAKE_CURRENT_BINARY_DIR}/game_vs.cso" "${CMAKE_CURRENT_BINARY_DIR}/game_ps.cso" "${CMAKE_CURRENT_SOURCE_DIR}/tools/embed_csos.py" "${CMAKE_CURRENT_SOURCE_DIR}/tools/gencommon.py"
    COMMENT "embed csos into section_cso.inc"
)

# generators (run when inputs change; deterministic → byte-identical)
file(GLOB GEN_PY "${CMAKE_CURRENT_SOURCE_DIR}/tools/gen_*.py")
file(GLOB ASSETS_SRC "${CMAKE_CURRENT_SOURCE_DIR}/assets/src/*")
add_custom_command(
    OUTPUT "${GEN_DIR}/assets.h"
    COMMAND ${Python3_EXECUTABLE} "${CMAKE_CURRENT_SOURCE_DIR}/tools/assemble_assets.py"
    DEPENDS ${GEN_PY} ${ASSETS_SRC} "${CMAKE_CURRENT_SOURCE_DIR}/tools/gencommon.py" "${SECTION_CSO}"
    COMMENT "regenerate assets.h from assets/src inputs (deterministic)"
)
add_custom_target(assets DEPENDS "${GEN_DIR}/assets.h")

add_executable(tetris
    src/main.cpp
    src/engine.cpp
    src/game.cpp
    src/ui_font.cpp
)
add_dependencies(tetris assets)
# keep the exe folder self-contained so a double-click works without the project tree
file(GLOB TETRIS_RUNTIME_ASSETS "${CMAKE_CURRENT_SOURCE_DIR}/assets/src/*.png"
     "${CMAKE_CURRENT_SOURCE_DIR}/assets/src/*.wav")
add_custom_command(TARGET tetris POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E make_directory "$<TARGET_FILE_DIR:tetris>/assets/src"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different ${TETRIS_RUNTIME_ASSETS} "$<TARGET_FILE_DIR:tetris>/assets/src"
    COMMENT "copy runtime assets next to the exe")
target_include_directories(tetris PRIVATE "${DXTK}/Inc" "${GEN_DIR}")
target_link_libraries(tetris PRIVATE "${DXTK_LIB}")
target_link_libraries(tetris PRIVATE
    d3d11 dxgi dxguid uuid ole32 windowscodecs
    user32 gdi32 advapi32 shell32 winmm)
target_link_options(tetris PRIVATE "/SUBSYSTEM:WINDOWS" "/ENTRY:wWinMainCRTStartup" "/MAP")  # wWinMain; CFG stays off (DirectXTK objects carry CF metadata, but the image is not CF-linked)
target_compile_definitions(tetris PRIVATE UNICODE _UNICODE _WIN32_WINNT=0x0A00 $<$<CONFIG:Release>:NDEBUG>)   # W-API everywhere; without UNICODE DefWindowProc maps to the ANSI thunk and truncates wide titles; Win10+ API surface matches DirectXTK's XAudio 2.9 build
set_property(TARGET tetris PROPERTY MSVC_RUNTIME_LIBRARY "MultiThreaded")  # /MT: static CRT, no MSVCP140/VCRUNTIME140/ucrt DLLs needed
