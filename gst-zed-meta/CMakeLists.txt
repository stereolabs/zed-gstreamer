set (SOURCES
  gstzedmeta.cpp)
    
set (HEADERS
  gstzedmeta.h)

set(libname gstzedmeta)

add_library (${libname} MODULE
  ${SOURCES}
  ${HEADERS})

add_definitions(-std=c++11 -g -O2)

target_link_libraries (${libname}
  ${GLIB2_LIBRARIES}
  ${GOBJECT_LIBRARIES}
  ${GSTREAMER_LIBRARY}
  ${GSTREAMER_BASE_LIBRARY}
  ${GSTREAMER_VIDEO_LIBRARY})

if (WIN32)
  install (FILES $<TARGET_PDB_FILE:${libname}> DESTINATION ${PDB_INSTALL_DIR} COMPONENT pdb OPTIONAL)
endif ()
install(TARGETS ${libname} LIBRARY DESTINATION ${LIBRARY_INSTALL_DIR})
