cmake_minimum_required(VERSION 3.21)
project(qr LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_AUTOMOC ON)

# The same split as WinShun's own winshun.capture (src/capture-plugin):
# qr.exe is a starter without Qt (qmlapp/stub/main.cpp); it loads QrApp.dll,
# built on WinShun's Qt (WS_QT_DIR), which runs the plugin protocol on a
# worker thread; the shared native preview owns its UI thread. plugin.json names
# qr.exe.
find_package(Qt6 REQUIRED COMPONENTS Core Gui)

add_executable(qr qmlapp/stub/main.cpp qr.rc)
target_compile_definitions(qr PRIVATE UNICODE _UNICODE NOMINMAX WIN32_LEAN_AND_MEAN)
# Beside QrApp.dll, where plugin.json's runtime.program points.
set_target_properties(qr PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_SOURCE_DIR}")
foreach(config DEBUG RELEASE RELWITHDEBINFO MINSIZEREL)
    set_target_properties(qr PROPERTIES RUNTIME_OUTPUT_DIRECTORY_${config} "${CMAKE_SOURCE_DIR}")
endforeach()
if(MSVC)
    target_compile_options(qr PRIVATE /utf-8 /W4 /permissive-)
    # Static CRT: the exe needs no runtime DLL, which the strict sandbox cannot load.
    set_target_properties(qr PROPERTIES MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
else()
    target_compile_options(qr PRIVATE -finput-charset=UTF-8 -fexec-charset=UTF-8 -Wall -Wextra -O2)
    target_link_options(qr PRIVATE -static -static-libgcc -static-libstdc++)
endif()

# Qt Core/Gui provide the resident application, clipboard and PNG actions.
# The preview uses the same Win32 window as Wi-Fi. Pure logic has no Qt, so tests
# run it without a window server.
qt_add_library(QrApp SHARED
    main.cpp
    qmlapp/qmlmain.cpp
    qmlapp/QrPreview.cpp
    qmlapp/QrPreview.h
    qr_preview_window.h
)
target_compile_definitions(QrApp PRIVATE UNICODE _UNICODE NOMINMAX WIN32_LEAN_AND_MEAN)
target_include_directories(QrApp PRIVATE "${CMAKE_SOURCE_DIR}")
target_link_libraries(QrApp PRIVATE Qt6::Core Qt6::Gui user32 gdi32 gdiplus ole32)
# Beside qr.exe, which loads it from its own folder (a Windows DLL is a
# runtime artifact, hence RUNTIME_OUTPUT_DIRECTORY).
set_target_properties(QrApp PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_SOURCE_DIR}")
foreach(config DEBUG RELEASE RELWITHDEBINFO MINSIZEREL)
    set_target_properties(QrApp PROPERTIES RUNTIME_OUTPUT_DIRECTORY_${config} "${CMAKE_SOURCE_DIR}")
endforeach()
if(MSVC)
    target_compile_options(QrApp PRIVATE /utf-8 /W4 /permissive-)
    set_target_properties(QrApp PROPERTIES MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
else()
    target_compile_options(QrApp PRIVATE -finput-charset=UTF-8 -fexec-charset=UTF-8 -Wall -Wextra)
endif()

# The unit tests: pure C++, no Qt, no windows.
add_executable(qr_tests tests/test_qr.cpp)
target_include_directories(qr_tests PRIVATE "${CMAKE_SOURCE_DIR}")
target_compile_definitions(qr_tests PRIVATE _CRT_SECURE_NO_WARNINGS)
if(MSVC)
    target_compile_options(qr_tests PRIVATE /utf-8 /W4 /permissive-)
    set_target_properties(qr_tests PROPERTIES MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
else()
    target_compile_options(qr_tests PRIVATE -finput-charset=UTF-8 -fexec-charset=UTF-8 -Wall -Wextra -O2)
endif()
set_target_properties(qr_tests PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_SOURCE_DIR}/build/tests")
foreach(config DEBUG RELEASE RELWITHDEBINFO MINSIZEREL)
    set_target_properties(qr_tests PROPERTIES RUNTIME_OUTPUT_DIRECTORY_${config} "${CMAKE_SOURCE_DIR}/build/tests")
endforeach()

enable_testing()
add_test(NAME qr_tests COMMAND qr_tests)

# Generated app.ico frames, also embedded in the GUI binary.
qt_add_resources(QrApp "plugin_window_icon"
    PREFIX /plugin-icons BASE "${CMAKE_CURRENT_SOURCE_DIR}/icons"
    FILES
        icons/app-16.png
        icons/app-20.png
        icons/app-24.png
        icons/app-32.png
        icons/app-40.png
        icons/app-48.png
        icons/app-64.png
        icons/app-256.png
)
