cmake_minimum_required(VERSION 3.16)
project(rm_chat VERSION 1.0 LANGUAGES CXX)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(Qt6 REQUIRED COMPONENTS Quick Network Gui)
qt_standard_project_setup(REQUIRES 6.5)

qt_add_executable(rm_chat
    main.cpp
    chatclient.cpp chatclient.h
    configstore.cpp configstore.h
    inkcanvas.cpp inkcanvas.h
    ../common/epaper.cpp ../common/epaper.h
    ../common/penreader.cpp ../common/penreader.h
    ../common/penmouse.cpp ../common/penmouse.h)
target_include_directories(rm_chat PRIVATE ../common)

qt_add_qml_module(rm_chat
    URI rm_chat_module
    VERSION 1.0
    QML_FILES Main.qml
    SOURCES inkcanvas.h inkcanvas.cpp)

target_link_libraries(rm_chat PRIVATE Qt6::Quick Qt6::Network Qt6::GuiPrivate ${CMAKE_DL_LIBS})
