set (SOURCES
  gstzedmeta.cpp)
    
set (HEADERS
  gstzedmeta.h)

set(libname gstzedmeta)

message( " * ${libname} library added")

add_library (${libname} SHARED
  ${SOURCES}
  ${HEADERS})

add_definitions(-std=c++11 -g -O2)

target_link_libraries (${libname} LINK_PUBLIC
  ${GLIB2_LIBRARIES}
  ${GOBJECT_LIBRARIES}
  ${GSTREAMER_LIBRARY}
  ${GSTREAMER_BASE_LIBRARY}
  ${GSTREAMER_VIDEO_LIBRARY}
)

set_target_properties(${libname} PROPERTIES
    PUBLIC_HEADER "${HEADERS}"
)

if (WIN32)
  install (FILES $<TARGET_PDB_FILE:${libname}> DESTINATION ${PDB_INSTALL_DIR} COMPONENT pdb OPTIONAL)
endif ()
install( TARGETS ${libname}
    LIBRARY DESTINATION ${LIBRARY_INSTALL_DIR}
    PUBLIC_HEADER DESTINATION ${INCLUDE_INSTALL_DIR}
)
