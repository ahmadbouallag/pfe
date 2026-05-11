cmake_minimum_required(VERSION 3.16)

project(udoc1 VERSION 0.1 LANGUAGES CXX)

set(CMAKE_AUTOUIC ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(QT NAMES Qt6 Qt5 REQUIRED COMPONENTS Widgets LinguistTools)
find_package(Qt${QT_VERSION_MAJOR} REQUIRED COMPONENTS Widgets LinguistTools)

set(TS_FILES udoc1_en_001.ts)

set(PROJECT_SOURCES
        main.cpp
        mainwindow.cpp
        mainwindow.h
        ${TS_FILES}
)

if(${QT_VERSION_MAJOR} GREATER_EQUAL 6)
    qt_add_executable(udoc1
        MANUAL_FINALIZATION
        ${PROJECT_SOURCES}
        udoc_forward.h
        udoc_types.h
        properties.h
        udoc_object.h
        udoc_object.cpp
        image.h
        image.cpp
        vector.h
        vector.cpp
        inline.h
        inline.cpp
        block.h
        block.cpp
        table.h
        table.cpp
        link.h
        link.cpp
        bookmark.h
        field.h
        field.cpp
        comment.h
        formula.h
        media.h
        group.h
        group.cpp
        element.h
        element.cpp
        story.h
        story.cpp
        page.h
        page.cpp
        tab.h
        tab.cpp
        document.h
        document.cpp
        header_footer.h
        render_engine.h
        render_engine.cpp
        selection.h
        commands.h
        input_handler.h
        input_handler.cpp
        document_view.h
        document_view.cpp
    )

    qt_create_translation(QM_FILES ${CMAKE_SOURCE_DIR} ${TS_FILES})
else()
    if(ANDROID)
        add_library(udoc1 SHARED
            ${PROJECT_SOURCES}
        )
    else()
        add_executable(udoc1
            ${PROJECT_SOURCES}
        )
    endif()

    qt5_create_translation(QM_FILES ${CMAKE_SOURCE_DIR} ${TS_FILES})
endif()

target_link_libraries(udoc1 PRIVATE Qt${QT_VERSION_MAJOR}::Widgets)

if(${QT_VERSION} VERSION_LESS 6.1.0)
  set(BUNDLE_ID_OPTION MACOSX_BUNDLE_GUI_IDENTIFIER com.example.udoc1)
endif()
set_target_properties(udoc1 PROPERTIES
    ${BUNDLE_ID_OPTION}
    MACOSX_BUNDLE_BUNDLE_VERSION ${PROJECT_VERSION}
    MACOSX_BUNDLE_SHORT_VERSION_STRING ${PROJECT_VERSION_MAJOR}.${PROJECT_VERSION_MINOR}
    MACOSX_BUNDLE TRUE
    WIN32_EXECUTABLE TRUE
)

include(GNUInstallDirs)
install(TARGETS udoc1
    BUNDLE DESTINATION .
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
)

if(QT_VERSION_MAJOR EQUAL 6)
    qt_finalize_executable(udoc1)
endif()
