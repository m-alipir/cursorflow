cmake_minimum_required(VERSION 3.25)

if(WIN32)
    project(CursorFlow LANGUAGES CXX RC)
else()
    project(CursorFlow LANGUAGES CXX)
endif()

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

if(WIN32)
    add_executable(CursorFlow WIN32
        src/main.cpp
        src/cursor_scheme.cpp
        src/cursor_scheme.h
        src/crash_restore.cpp
        src/crash_restore.h
        src/overlay_window.cpp
        src/overlay_window.h
        src/renderer_d2d.cpp
        src/renderer_d2d.h
        src/physics.cpp
        src/physics.h
        src/raw_input.cpp
        src/raw_input.h
        src/cursor_shape_sync.cpp
        src/cursor_shape_sync.h
        src/anticheat_watcher.cpp
        src/anticheat_watcher.h
        src/config.cpp
        src/config.h
        resources/app.rc
    )

    target_compile_definitions(CursorFlow PRIVATE
        UNICODE
        _UNICODE
        WIN32_LEAN_AND_MEAN
        NOMINMAX
        WINVER=0x0A00
        _WIN32_WINNT=0x0A00
    )

    target_include_directories(CursorFlow PRIVATE src)

    target_link_libraries(CursorFlow PRIVATE
        d2d1
        dwrite
        dxgi
        d3d11
        dcomp
        user32
        gdi32
        shcore
        ole32
        dwmapi
    )

    # app.rc already embeds our PerMonitorV2 manifest as resource ID 1;
    # disable the linker's own auto-generated manifest to avoid a
    # duplicate MANIFEST resource.
    target_link_options(CursorFlow PRIVATE /MANIFEST:NO)

    # Standalone settings GUI (see overlay_window.cpp's tray "Settings" menu
    # item, which launches this by name next to the main exe). Shares
    # config.h/.cpp with the overlay -- same Settings struct, same
    # config.ini format -- so the two never drift out of sync.
    add_executable(CursorFlowSettings WIN32
        src/settings_main.cpp
        src/config.cpp
        src/config.h
        resources/settings.rc
    )

    target_compile_definitions(CursorFlowSettings PRIVATE
        UNICODE
        _UNICODE
        WIN32_LEAN_AND_MEAN
        NOMINMAX
        WINVER=0x0A00
        _WIN32_WINNT=0x0A00
    )

    target_include_directories(CursorFlowSettings PRIVATE src)

    target_link_libraries(CursorFlowSettings PRIVATE
        comctl32
        user32
        gdi32
        gdiplus
        comdlg32
    )

    target_link_options(CursorFlowSettings PRIVATE /MANIFEST:NO)

elseif(UNIX AND NOT APPLE)
    # X11 (Xlib + XFixes + XInput2 + Xext/Shape + Xcursor) is the target --
    # Wayland is explicitly out of scope (see smooth-cursor-overlay-prompt.md:
    # compositor sandboxing makes reliable global cursor tracking/override
    # impractical there).
    find_package(X11 REQUIRED)
    if(NOT X11_Xfixes_FOUND)
        message(FATAL_ERROR "XFixes extension not found (libxfixes dev headers)")
    endif()
    if(NOT X11_Xi_FOUND)
        message(FATAL_ERROR "XInput2 extension not found (libxi dev headers)")
    endif()
    if(NOT X11_Xext_FOUND)
        message(FATAL_ERROR "Xext (Shape extension) not found (libxext dev headers)")
    endif()
    if(NOT X11_Xcursor_FOUND)
        message(FATAL_ERROR "Xcursor not found (libxcursor dev headers)")
    endif()

    find_package(PkgConfig REQUIRED)
    pkg_check_modules(CAIRO REQUIRED IMPORTED_TARGET cairo cairo-xlib)
    pkg_check_modules(GTK3 REQUIRED IMPORTED_TARGET gtk+-3.0)

    find_package(Threads REQUIRED)

    add_executable(CursorFlow
        src/physics.cpp
        src/physics.h
        src/config.h
        src/linux/main.cpp
        src/linux/config.cpp
        src/linux/overlay_window.cpp
        src/linux/overlay_window.h
        src/linux/cursor_scheme.cpp
        src/linux/cursor_scheme.h
        src/linux/crash_restore.cpp
        src/linux/crash_restore.h
        src/linux/cursor_shape_sync.cpp
        src/linux/cursor_shape_sync.h
        src/linux/renderer_cairo.cpp
        src/linux/renderer_cairo.h
        src/linux/raw_input.cpp
        src/linux/raw_input.h
        src/linux/anticheat_watcher.cpp
        src/linux/anticheat_watcher.h
    )

    target_include_directories(CursorFlow PRIVATE
        src
        src/linux
        ${X11_INCLUDE_DIR}
    )

    target_link_libraries(CursorFlow PRIVATE
        ${X11_LIBRARIES}
        ${X11_Xfixes_LIB}
        ${X11_Xi_LIB}
        ${X11_Xext_LIB}
        ${X11_Xcursor_LIB}
        PkgConfig::CAIRO
        Threads::Threads
    )

    # Standalone settings GUI (GTK3, flat white theme via CSS -- see
    # settings_main.cpp). Shares config.h/.cpp with the overlay -- same
    # Settings struct, same config.ini format -- so the two never drift
    # out of sync. A separate executable rather than adding a Linux tray-
    # icon subsystem, which X11 has no single standard protocol for.
    add_executable(CursorFlowSettings
        src/linux/settings_main.cpp
        src/linux/config.cpp
        src/config.h
    )

    target_include_directories(CursorFlowSettings PRIVATE
        src
        src/linux
    )

    target_link_libraries(CursorFlowSettings PRIVATE
        PkgConfig::GTK3
    )
else()
    message(FATAL_ERROR "Unsupported platform -- only Windows and Linux/X11 are targeted")
endif()
