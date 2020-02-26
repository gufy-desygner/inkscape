cmake_minimum_required (VERSION 2.8)

project( pdf-extract )

find_package(PkgConfig)

pkg_check_modules( POPPLER REQUIRED poppler-glib )
include_directories( ${POPPLER_INCLUDE_DIRS} )
link_directories( ${POPPLER_LIBRARY_DIRS} )

add_executable(pdf-extractor
    PagesCatalog.h
    tools.h
    PdfAnchor.h

    pdf-extractor.cpp
    PagesCatalog.cpp
    tools.cpp
    PdfAnchor.cpp
)
set_target_properties( pdf-extractor PROPERTIES CXX_STANDARD 11 )
target_link_libraries( pdf-extractor popt poppler)
