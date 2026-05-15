cmake_minimum_required(VERSION 3.16)

project(udoc1 VERSION 0.1 LANGUAGES CXX)

set(CMAKE_AUTOUIC ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(QT NAMES Qt6 Qt5 REQUIRED COMPONENTS Widgets LinguistTools)
find_package(Qt${QT_VERSION_MAJOR} REQUIRED COMPONENTS Widgets LinguistTools)

# ---- zlib -------------------------------------------------------
# Qt links zlib internally on all platforms, but we need the headers
# for #include <zlib.h> in pdf_xref.cpp.
# find_package locates the system zlib; on Windows with Qt's bundled
# zlib, set ZLIB_ROOT to Qt's installation prefix if needed.
set(ZLIB_ROOT "C:/msys64/ucrt64")
find_package(ZLIB REQUIRED)

set(TS_FILES udoc1_en_001.ts)

set(PROJECT_SOURCES
        main.cpp
        mainwindow.cpp
        mainwindow.h
        ${TS_FILES}
)

# ---- parser sources ---------------------------------------------
set(PARSER_SOURCES
    parsers/pdf/pdf_object.h
    parsers/pdf/pdf_object.cpp
    parsers/pdf/pdf_xref.h
    parsers/pdf/pdf_xref.cpp
    parsers/pdf/pdf_font.h
    parsers/pdf/pdf_font.cpp
    parsers/pdf/pdf_content_stream.h
    parsers/pdf/pdf_content_stream.cpp
    parsers/pdf/pdf_parser.h
    parsers/pdf/pdf_parser.cpp
)

if(${QT_VERSION_MAJOR} GREATER_EQUAL 6)
    qt_add_executable(udoc1
        MANUAL_FINALIZATION
        ${PROJECT_SOURCES}
        ${PARSER_SOURCES}
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
            ${PARSER_SOURCES}
        )
    else()
        add_executable(udoc1
            ${PROJECT_SOURCES}
            ${PARSER_SOURCES}
        )
    endif()

    qt5_create_translation(QM_FILES ${CMAKE_SOURCE_DIR} ${TS_FILES})
endif()

# ---- include paths ----------------------------------------------
# Project root: so #include "document.h" etc. work from parser files.
# parsers/pdf: so parser files can include each other without path prefixes.
target_include_directories(udoc1 PRIVATE
    ${CMAKE_SOURCE_DIR}
    ${CMAKE_SOURCE_DIR}/parsers/pdf
    ${ZLIB_INCLUDE_DIRS}
)

# ---- link libraries ---------------------------------------------
target_link_libraries(udoc1 PRIVATE
    Qt${QT_VERSION_MAJOR}::Widgets
    ZLIB::ZLIB
)

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
