cmake_minimum_required(VERSION 3.21)
project(qr LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_AUTOMOC ON) # the preview controller is a Q_OBJECT type

# The same split as WinShun's own winshun.capture (src/capture-plugin):
# qr.exe is a starter without Qt (qmlapp/stub/main.cpp); it loads QrApp.dll,
# built on WinShun's Qt (WS_QT_DIR), which runs the plugin protocol on a
# worker thread and the preview window on the GUI thread. plugin.json names
# qr.exe.
find_package(Qt6 REQUIRED COMPONENTS Core Gui Qml Quick)
# The QML module's resources under :/qt/qml/<URI>, as WinShun's own app does.
qt_policy(SET QTP0001 NEW)

add_executable(qr qmlapp/stub/main.cpp)
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

# The plugin itself: the query/action mapping (main.cpp) and the QML preview
# (qmlapp). Pure logic (qrcode.h, logic.h) has no Qt in it, so the unit tests
# run it without a window server.
qt_add_library(QrApp SHARED
    main.cpp
    qmlapp/qmlmain.cpp
    qmlapp/QrPreview.cpp
    qmlapp/QrPreview.h
    qmlapp/PreviewController.h
)
target_compile_definitions(QrApp PRIVATE UNICODE _UNICODE NOMINMAX WIN32_LEAN_AND_MEAN)
target_include_directories(QrApp PRIVATE "${CMAKE_SOURCE_DIR}")
target_link_libraries(QrApp PRIVATE Qt6::Core Qt6::Gui Qt6::Qml Qt6::Quick)
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

# The QML module URI QrApp: the one preview window.
set_source_files_properties("${CMAKE_CURRENT_SOURCE_DIR}/qmlapp/qml/Preview.qml"
    PROPERTIES QT_RESOURCE_ALIAS "Preview.qml")
set(qml_import_scan "")
if(QT6_IS_SHARED_LIBS_BUILD)
    set(qml_import_scan NO_IMPORT_SCAN) # as WinShun's (src/app/CMakeLists.txt)
endif()
qt_add_qml_module(QrApp
    URI QrApp
    OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/QrApp"
    VERSION 1.0
    NO_PLUGIN
    ${qml_import_scan}
    NO_GENERATE_EXTRA_QMLDIRS
    QML_FILES "${CMAKE_CURRENT_SOURCE_DIR}/qmlapp/qml/Preview.qml"
)
# Bytecode only, no QML compiled to C++: the plugin runs on whichever Qt 6 its
# WinShun comes with (see capture's CMakeLists for the reasoning).
set_target_properties(QrApp PROPERTIES QT_QMLCACHEGEN_ARGUMENTS "--only-bytecode")

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
