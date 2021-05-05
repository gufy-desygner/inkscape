 
set(pdf_SRC
    pdf-input.cpp
	pdf-parser.cpp
	png-merge.cpp
	svg-builder.cpp
	BookMarks.cpp

	# -------
	#2geom.h
	# Headers
	pdf-input.h
	pdf-parser.h
	png-merge.h
	svg-builder.h
	BookMarks.h
)
message("${pdf_SRC}")
#add_inkscape_lib(pdf_LIB "${pdf_SRC}")
add_library(jsoncpp pdf_LIB SHARED ${pdf_SRC})
