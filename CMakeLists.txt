cmake_minimum_required(VERSION 3.16)
project(UDocEditor VERSION 0.1 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_AUTOUIC ON)

find_package(Qt6 REQUIRED COMPONENTS Core Gui Widgets)

qt_add_executable(UDocEditor
    main.cpp
    mainwindow.cpp mainwindow.h
    documentviewport.cpp documentviewport.h
    renderengine.cpp renderengine.h
    commands.cpp commands.h
    document.cpp document.h
    page.cpp page.h
    element.cpp element.h
    udoc_object.cpp udoc_object.h
    udoc_types.cpp udoc_types.h
)

target_link_libraries(UDocEditor PRIVATE
    Qt6::Core
    Qt6::Gui
    Qt6::Widgets
)

set_target_properties(UDocEditor PROPERTIES
    WIN32_EXECUTABLE TRUE
    MACOSX_BUNDLE TRUE
)
