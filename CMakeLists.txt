# CMakeLists.txt — MINE, final; DirectXTK static lib (built) + game target.
cmake_minimum_required(VERSION 3.29)
project(tetris LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
add_compile_options(/utf-8 /EHsc /guard:cf-)  # all sources are UTF-8 (cp936 default would mis-decode) ✓; /EHsc required for catch(...) to be wired — MSVC default C-style EH leaves it inert; CFG disabled — full-process fastfail 0xC0000409

set(DXTK "H:/tetris_cpp/third_party/DirectXTK")
set(DXTK_LIB "H:/tetris_cpp/third_party/DirectXTK/out/build/x64-Release/lib/DirectXTK.lib")

set(GEN_DIR "${CMAKE_CURRENT_SOURCE_DIR}/assets/generated")
set(SECTION_CSO "${GEN_DIR}/section_cso.inc")

# fxc shader compile (commands verified working)
set(FXC "C:/Program Files (x86)/Windows Kits/10/bin/10.0.26100.0/x64/fxc.exe")
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
find_program(PYEXE py)
add_custom_command(
    OUTPUT "${SECTION_CSO}"
    COMMAND ${PYEXE} -3 "${CMAKE_CURRENT_SOURCE_DIR}/tools/embed_csos.py" "${CMAKE_CURRENT_BINARY_DIR}/game_vs.cso" "${CMAKE_CURRENT_BINARY_DIR}/game_ps.cso"
    DEPENDS "${CMAKE_CURRENT_BINARY_DIR}/game_vs.cso" "${CMAKE_CURRENT_BINARY_DIR}/game_ps.cso" "${CMAKE_CURRENT_SOURCE_DIR}/tools/embed_csos.py" "${CMAKE_CURRENT_SOURCE_DIR}/tools/gencommon.py"
    COMMENT "embed csos into section_cso.inc"
)

# generators (run when inputs change; deterministic → byte-identical)
file(GLOB GEN_PY "${CMAKE_CURRENT_SOURCE_DIR}/tools/gen_*.py")
file(GLOB ASSETS_SRC "${CMAKE_CURRENT_SOURCE_DIR}/assets/src/*")
add_custom_command(
    OUTPUT "${GEN_DIR}/assets.h"
    COMMAND ${PYEXE} -3 "${CMAKE_CURRENT_SOURCE_DIR}/tools/assemble_assets.py"
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
file(GLOB TETRIS_RUNTIME_ASSETS "${CMAKE_CURRENT_SOURCE_DIR}/assets/src/*.png")
add_custom_command(TARGET tetris POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E make_directory "$<TARGET_FILE_DIR:tetris>/assets/src"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different ${TETRIS_RUNTIME_ASSETS} "$<TARGET_FILE_DIR:tetris>/assets/src"
    COMMENT "copy runtime assets next to the exe")
target_include_directories(tetris PRIVATE "${DXTK}/Inc" "${GEN_DIR}")
target_link_libraries(tetris PRIVATE "${DXTK_LIB}")
target_link_libraries(tetris PRIVATE
    d3d11 dxgi dxguid uuid ole32 windowscodecs
    user32 gdi32 advapi32 shell32 winmm)
target_link_options(tetris PRIVATE "/SUBSYSTEM:WINDOWS" "/ENTRY:wWinMainCRTStartup" "/MAP")  # wWinMain ✓ / CFG off: DirectXTK lib objects lack CF metadata, exe link defaults to /guard:cf and fast-fails (0xC0000409) at startup
target_compile_definitions(tetris PRIVATE UNICODE _UNICODE $<$<CONFIG:Release>:NDEBUG>)   # W-API everywhere; without UNICODE DefWindowProc maps to the ANSI thunk and truncates wide titles
set_property(TARGET tetris PROPERTY MSVC_RUNTIME_LIBRARY "MultiThreadedDLL")  # MD — align DirectXTK.lib ✓ (single-config Release build)set_target_properties(tetris PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}")
