set (SOURCES
  gstzedsrc.cpp)
    
set (HEADERS
  gstzedsrc.h)

include_directories(${CUDA_INCLUDE_DIRS})
include_directories(${ZED_INCLUDE_DIRS})
include_directories(${CMAKE_CURRENT_SOURCE_DIR}/include)

link_directories(${ZED_LIBRARY_DIR})
link_directories(${CUDA_LIBRARY_DIRS})
link_directories(${LIBRARY_INSTALL_DIR})

set(libname gstzed)

message( " * ${libname} plugin added")

add_library (${libname} MODULE
  ${SOURCES}
  ${HEADERS})

if (UNIX)    
  add_definitions(-std=c++11)
endif (UNIX)

add_definitions(-Wno-deprecated-declarations)

if (CMAKE_BUILD_TYPE EQUAL "DEBUG")
  message("${libname}: Debug mode")
  add_definitions(-g)
else()
  if (UNIX)
    add_definitions(-O2)
  endif (UNIX)
endif()

SET(ZED_LIBS ${ZED_LIBRARIES} ${CUDA_CUDA_LIBRARY} ${CUDA_CUDART_LIBRARY} ${CUDA_NPP_LIBRARIES_ZED})

target_link_libraries (${libname} LINK_PUBLIC  
  ${GLIB2_LIBRARIES}
  ${GOBJECT_LIBRARIES}  
  ${GSTREAMER_LIBRARY}
  ${GSTREAMER_BASE_LIBRARY}
  ${GSTREAMER_VIDEO_LIBRARY}  
  ${ZED_LIBS}
  ${CMAKE_CURRENT_BINARY_DIR}/../gst-zed-meta/libgstzedmeta.so
)

if (WIN32)
  install (FILES $<TARGET_PDB_FILE:${libname}> DESTINATION ${PDB_INSTALL_DIR} COMPONENT pdb OPTIONAL)
endif ()
install(TARGETS ${libname} LIBRARY DESTINATION ${PLUGIN_INSTALL_DIR})
