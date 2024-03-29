set(libname gstzedxonesrc)

set(SOURCES
		gstzedxonesrc.cpp
		)
		
set(HEADERS
		gstzedxonesrc.h
		)

link_directories(${LIBRARY_INSTALL_DIR})

message( " * ${libname} plugin added")

add_library(${libname} MODULE
		${SOURCES}
		${HEADERS}
)

if (CMAKE_BUILD_TYPE EQUAL "DEBUG")
		message("   ${libname}: Debug mode")
		add_definitions(-g)
else()
		message("   ${libname}: Release mode")
		add_definitions(-O2)
endif()

target_link_libraries (
	${libname} LINK_PUBLIC
	${GLIB2_LIBRARIES}
	${GOBJECT_LIBRARIES}
	${GSTREAMER_LIBRARY}
	${GSTREAMER_BASE_LIBRARY}
	${GSTREAMER_VIDEO_LIBRARY}
	${CMAKE_CURRENT_BINARY_DIR}/../gst-zed-meta/libgstzedmeta.so
)

install(TARGETS ${libname} LIBRARY DESTINATION ${PLUGIN_INSTALL_DIR})
