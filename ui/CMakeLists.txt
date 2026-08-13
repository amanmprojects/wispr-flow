cmake_minimum_required(VERSION 3.16)
project(wispr-flow-ui LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)

find_package(Qt6 REQUIRED COMPONENTS Widgets Network DBus)

qt_add_executable(wispr-flow-ui
    main.cpp
    wfclient.cpp
    tray.cpp
    tray.h
    pill.cpp
    pill.h
    osd.cpp
    osd.h
    debugwin.cpp
    settingswin.cpp
    json.cpp
)

target_link_libraries(wispr-flow-ui PRIVATE Qt6::Widgets Qt6::Network Qt6::DBus)
