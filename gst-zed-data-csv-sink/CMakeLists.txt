set (SOURCES
  gstzeddatacsvsink.cpp)
    
set (HEADERS
  gstzeddatacsvsink.h)

set(libname gstzeddatacsvsink)

message( " * ${libname} plugin added")

link_directories(${LIBRARY_INSTALL_DIR})

add_library (${libname} MODULE
  ${SOURCES}
  ${HEADERS})

add_definitions(-std=c++11 -g -O2)

target_link_libraries (${libname}
  ${GLIB2_LIBRARIES}
  ${GOBJECT_LIBRARIES}
  ${GSTREAMER_LIBRARY}
  ${GSTREAMER_BASE_LIBRARY}
  ${GSTREAMER_VIDEO_LIBRARY}
  ${CMAKE_CURRENT_BINARY_DIR}/../gst-zed-meta/libgstzedmeta.so
)

if (WIN32)
  install (FILES $<TARGET_PDB_FILE:${libname}> DESTINATION ${PDB_INSTALL_DIR} COMPONENT pdb OPTIONAL)
endif ()
install(TARGETS ${libname} LIBRARY DESTINATION ${PLUGIN_INSTALL_DIR})