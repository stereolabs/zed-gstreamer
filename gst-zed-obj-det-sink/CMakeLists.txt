set (SOURCES
  gstzedoddisplaysink.cpp
  TrackingViewer.cpp
  GLViewer.cpp)
    
set (HEADERS
  gstzedoddisplaysink.h
  utils.hpp
  TrackingViewer.hpp
  GLViewer.hpp)

set(libname gstzedoddisplaysink)

message( " * ${libname} plugin added")

include_directories(${OpenCV_INCLUDE_DIRS})
include_directories(${GLEW_INCLUDE_DIRS})
include_directories(${GLUT_INCLUDE_DIR})
include_directories(${CUDA_INCLUDE_DIRS})
include_directories(${ZED_INCLUDE_DIRS})
include_directories(${CMAKE_CURRENT_SOURCE_DIR}/include)

link_directories(${ZED_LIBRARY_DIR})
link_directories(${GLEW_LIBRARY_DIRS})
link_directories(${GLUT_LIBRARY_DIRS})
link_directories(${OpenGL_LIBRARY_DIRS})
link_directories(${CUDA_LIBRARY_DIRS})
link_directories(${OpenCV_LIBRARY_DIRS})
link_directories(${LIBRARY_INSTALL_DIR})

add_library (${libname} MODULE
  ${SOURCES}
  ${HEADERS})

add_definitions(-std=c++11 -g -O2 -Wno-deprecated-declarations -Wno-write-strings)

SET(ZED_LIBS ${ZED_LIBRARIES} ${CUDA_CUDA_LIBRARY} ${CUDA_CUDART_LIBRARY} ${CUDA_NPP_LIBRARIES_ZED})

target_link_libraries (${libname} LINK_PUBLIC
  ${GLIB2_LIBRARIES}
  ${GOBJECT_LIBRARIES}
  ${GSTREAMER_LIBRARY}
  ${GSTREAMER_BASE_LIBRARY}
  ${GSTREAMER_VIDEO_LIBRARY}
  ${ZED_LIBS}
  ${OPENGL_LIBRARIES}
  ${OpenCV_LIBRARIES}
  ${GLUT_LIBRARY}
  ${GLEW_LIBRARIES}
  ${CMAKE_CURRENT_BINARY_DIR}/../gst-zed-meta/libgstzedmeta.so
)

if (WIN32)
  install (FILES $<TARGET_PDB_FILE:${libname}> DESTINATION ${PDB_INSTALL_DIR} COMPONENT pdb OPTIONAL)
endif ()
install(TARGETS ${libname} LIBRARY DESTINATION ${PLUGIN_INSTALL_DIR})
