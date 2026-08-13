cmake_minimum_required(VERSION 3.20)

project(CRTBender VERSION 2.0.0 LANGUAGES CXX)

if(NOT WIN32)
    message(FATAL_ERROR "CRTBender is a Windows-only application.")
endif()

option(CRTB_XP "Build the 32-bit Windows XP GDI + Direct3D 9 backend" OFF)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

set(CRTBENDER_COMMON_SOURCES
    src/main.cpp
    src/config.cpp
    src/display.cpp
    src/geometry.cpp
    src/i18n.cpp
    src/warpmesh.cpp
    src/editor.cpp
    src/autostart.cpp
    src/util.cpp
    res/app.rc
)

if(CRTB_XP)
    if(NOT CMAKE_SIZEOF_VOID_P EQUAL 4)
        message(FATAL_ERROR "The Windows XP target must be configured as Win32/x86.")
    endif()
    set(CRTBENDER_RENDER_SOURCE src/render_xp.cpp src/xp_compat.cpp)
else()
    set(CRTBENDER_RENDER_SOURCE src/render.cpp)
endif()

add_executable(CRTBender WIN32 ${CRTBENDER_COMMON_SOURCES} ${CRTBENDER_RENDER_SOURCE})

target_include_directories(CRTBender PRIVATE src res)

target_compile_definitions(CRTBender PRIVATE
    UNICODE
    _UNICODE
    WIN32_LEAN_AND_MEAN
    NOMINMAX
)

if(CRTB_XP)
    target_compile_definitions(CRTBender PRIVATE
        CRTB_XP=1
        WINVER=0x0501
        _WIN32_WINNT=0x0501
        NTDDI_VERSION=0x05010000
    )
else()
    target_compile_definitions(CRTBender PRIVATE
        WINVER=0x0A00
        _WIN32_WINNT=0x0A00
    )
endif()

target_link_libraries(CRTBender PRIVATE
    comctl32
    shell32
    ole32
    uuid
    user32
    gdi32
    advapi32
)

if(CRTB_XP)
    target_link_libraries(CRTBender PRIVATE d3d9)
else()
    target_link_libraries(CRTBender PRIVATE d3d11 dxgi dxguid d3dcompiler)
endif()

if(MSVC)
    target_compile_options(CRTBender PRIVATE /W4 /permissive- /utf-8)
    # Static CRT: the exe runs on a clean Windows install with no redistributable.
    set_property(TARGET CRTBender PROPERTY
        MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
else()
    # MinGW: -municode is what makes wWinMain the entry point.
    target_compile_options(CRTBender PRIVATE -Wall -Wextra -municode
        -finput-charset=UTF-8 -fexec-charset=UTF-8)
    target_link_options(CRTBender PRIVATE
        -municode -static -static-libgcc -static-libstdc++)
    if(CRTB_XP)
        target_link_options(CRTBender PRIVATE
            -Wl,--major-subsystem-version,5,--minor-subsystem-version,1)
    endif()
endif()

if(CRTB_XP)
    set_target_properties(CRTBender PROPERTIES OUTPUT_NAME "CRTBender-WinXP-x86")
else()
    set_target_properties(CRTBender PROPERTIES OUTPUT_NAME "CRTBender")
endif()

# The warp math has no Windows dependencies, so it can be checked on its own.
#   cmake --build . --target warp_tests && ./warp_tests
add_executable(warp_tests EXCLUDE_FROM_ALL tests/test_warp.cpp src/warpmesh.cpp src/geometry.cpp)
target_include_directories(warp_tests PRIVATE src)

# The string tables are plain data, so they can be checked off-Windows too.
#   cmake --build . --target i18n_tests && ./i18n_tests
add_executable(i18n_tests EXCLUDE_FROM_ALL tests/test_i18n.cpp src/i18n.cpp)
target_include_directories(i18n_tests PRIVATE src)
if(MSVC)
    target_compile_options(i18n_tests PRIVATE /utf-8)
else()
    target_compile_options(i18n_tests PRIVATE -finput-charset=UTF-8 -fexec-charset=UTF-8)
endif()

add_executable(geometry_tests EXCLUDE_FROM_ALL
    tests/test_geometry.cpp src/geometry.cpp src/warpmesh.cpp)
target_include_directories(geometry_tests PRIVATE src)

add_executable(config_tests EXCLUDE_FROM_ALL tests/test_config.cpp)
target_include_directories(config_tests PRIVATE src)

install(TARGETS CRTBender RUNTIME DESTINATION .)
install(FILES README.md DESTINATION .)
