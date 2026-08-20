cmake_minimum_required(VERSION 3.16)
project(wispr-flow-ui LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)

# Require Qt 6.2+ (first LTS with stable Qt6 Widgets/Network/DBus)
find_package(Qt6 6.2 REQUIRED COMPONENTS Widgets Network DBus)
if(NOT Qt6_FOUND)
    message(FATAL_ERROR
        "Qt6 >= 6.2 not found. Install Qt6 development packages:\n"
        "  Arch:    pacman -S qt6-base\n"
        "  Debian/Ubuntu: apt install qt6-base-dev\n"
        "  Fedora:  dnf install qt6-qtbase-devel\n"
        "  openSUSE: zypper install qt6-base-devel")
endif()

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

install(TARGETS wispr-flow-ui DESTINATION bin)
