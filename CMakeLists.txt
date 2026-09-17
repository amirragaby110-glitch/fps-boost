# FPS Booster Pro - Microsoft Visual C++ / MinGW build
# Alternative to build.py (Zig). Same C++ sources, same output.
#   cmake -B build-vs -G "Visual Studio 17 2022" -A x64
#   cmake --build build-vs --config Release
cmake_minimum_required(VERSION 3.20)
project(FPSBoosterPro LANGUAGES CXX RC)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

set(APP_SOURCES
    src/main.cpp
    src/strings.cpp
    src/util.cpp
    src/sysinfo.cpp
    src/tweaks.cpp
    src/games.cpp
    src/ui.cpp
    src/power.cpp
    src/net.cpp
    src/watch.cpp
    src/maxfps.cpp
)

set(APP_LIBS
    comctl32 gdi32 gdiplus shell32 ole32 uuid
    powrprof advapi32 comdlg32 winhttp iphlpapi dxgi ws2_32
)

# Resource scripts need absolute asset paths
set(ASSETS_DIR "${CMAKE_CURRENT_SOURCE_DIR}/assets")
set(DATA_DIR "${CMAKE_CURRENT_SOURCE_DIR}/data")
set(RES_DIR "${CMAKE_CURRENT_SOURCE_DIR}/res")
configure_file(res/app.rc.in "${CMAKE_CURRENT_BINARY_DIR}/app.rc" @ONLY)
configure_file(res/setup.rc.in "${CMAKE_CURRENT_BINARY_DIR}/setup.rc" @ONLY)

add_executable(fpsbooster WIN32 ${APP_SOURCES} "${CMAKE_CURRENT_BINARY_DIR}/app.rc")
target_include_directories(fpsbooster PRIVATE src)
target_compile_definitions(fpsbooster PRIVATE
    UNICODE _UNICODE _WIN32_WINNT=0x0A00 _CRT_SECURE_NO_WARNINGS
)
target_link_libraries(fpsbooster PRIVATE ${APP_LIBS})

add_executable(FPSBooster-Setup WIN32 src/setup.cpp "${CMAKE_CURRENT_BINARY_DIR}/setup.rc")
target_include_directories(FPSBooster-Setup PRIVATE src)
target_compile_definitions(FPSBooster-Setup PRIVATE
    UNICODE _UNICODE _WIN32_WINNT=0x0A00 _CRT_SECURE_NO_WARNINGS
)
target_link_libraries(FPSBooster-Setup PRIVATE shell32 ole32 uuid comctl32 gdi32)

if(MSVC)
    foreach(t fpsbooster FPSBooster-Setup)
        # /utf-8 is required: sources contain Persian (UTF-8) string literals
        target_compile_options(${t} PRIVATE /O2 /EHsc /W3 /utf-8)
    endforeach()
    # app uses int main() with the Windows (GUI) subsystem
    set_target_properties(fpsbooster PROPERTIES LINK_FLAGS "/ENTRY:mainCRTStartup")
else()
    target_link_options(fpsbooster PRIVATE -mwindows)
    target_link_options(FPSBooster-Setup PRIVATE -mwindows -municode)
endif()
