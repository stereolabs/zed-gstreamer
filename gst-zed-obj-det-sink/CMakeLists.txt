set (SOURCES
  gstzedoddisplaysink.cpp
)
    
set (HEADERS
  gstzedoddisplaysink.h
)

set(libname gstzedoddisplaysink)

message( " * ${libname} plugin added")

include_directories(${OpenCV_INCLUDE_DIRS})
include_directories(${CMAKE_CURRENT_SOURCE_DIR}/include)

link_directories(${OpenCV_LIBRARY_DIRS})
link_directories(${LIBRARY_INSTALL_DIR})

add_library (${libname} MODULE
  ${SOURCES}
  ${HEADERS})

if (UNIX)    
  add_definitions(-std=c++11 -Wno-deprecated-declarations -Wno-write-strings)
endif (UNIX)

if (CMAKE_BUILD_TYPE EQUAL "DEBUG")
  message("${libname}: Debug mode")
  add_definitions(-g)
else()
  add_definitions(-O2)
endif()

SET(ZED_LIBS ${ZED_LIBRARIES} ${CUDA_CUDA_LIBRARY} ${CUDA_CUDART_LIBRARY} ${CUDA_NPP_LIBRARIES_ZED})

if (WIN32)
  set(CMAKE_SHARED_LIBRARY_PREFIX "")
  set(CMAKE_STATIC_LIBRARY_PREFIX "")

  target_link_libraries (${libname} LINK_PUBLIC
    ${GLIB2_LIBRARIES}
    ${GOBJECT_LIBRARIES}
    ${GSTREAMER_LIBRARY}
    ${GSTREAMER_BASE_LIBRARY}
    ${GSTREAMER_VIDEO_LIBRARY}
    ${OpenCV_LIBRARIES}
    gstzedmeta_shared
  )
else()
  target_link_libraries (${libname} LINK_PUBLIC
    ${GLIB2_LIBRARIES}
    ${GOBJECT_LIBRARIES}
    ${GSTREAMER_LIBRARY}
    ${GSTREAMER_BASE_LIBRARY}
    ${GSTREAMER_VIDEO_LIBRARY}
    ${OpenCV_LIBRARIES}
    ${CMAKE_CURRENT_BINARY_DIR}/../gst-zed-meta/libgstzedmeta.so
  )
endif()

if (WIN32)
  install (FILES $<TARGET_PDB_FILE:${libname}> DESTINATION ${PDB_INSTALL_DIR} COMPONENT pdb OPTIONAL)
endif ()
install(TARGETS ${libname} LIBRARY DESTINATION ${PLUGIN_INSTALL_DIR})