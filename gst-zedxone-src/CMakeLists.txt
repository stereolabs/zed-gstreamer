################################################
## Generate symbols for IDE indexer (VSCode)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# Default to C99
if(NOT CMAKE_C_STANDARD)
  set(CMAKE_C_STANDARD 99)
endif()

# Default to C++14
if(NOT CMAKE_CXX_STANDARD)
  set(CMAKE_CXX_STANDARD 14)
endif()

add_definitions(-Werror=return-type)

option(LINK_SHARED_ZED "Link with the ZED SDK shared executable" ON)

if(NOT LINK_SHARED_ZED AND MSVC)
    message(FATAL_ERROR "LINK_SHARED_ZED OFF : ZED SDK static libraries not available on Windows")
endif()

include_directories(${CUDA_INCLUDE_DIRS})
include_directories(${ZED_INCLUDE_DIRS})
include_directories(${CMAKE_CURRENT_SOURCE_DIR}/include)

link_directories(${ZED_LIBRARY_DIR})
link_directories(${CUDA_LIBRARY_DIRS})
link_directories(${LIBRARY_INSTALL_DIR})

set(libname gstzedxonesrc)

set(SOURCES
  gstzedxonesrc.cpp
)
  
set(HEADERS
  gstzedxonesrc.h
)

message( " * ${libname} plugin added")

add_library(${libname} MODULE
  ${SOURCES}
  ${HEADERS}
)

message("   ${libname}: OS Unix - L4T")

if (CMAKE_BUILD_TYPE EQUAL "DEBUG")
  message("   ${libname}: Debug mode")
  add_definitions(-g)
else()
  message("   ${libname}: Release mode")
  add_definitions(-O2)
endif()

SET(ZED_LIBS
    ${ZED_LIBRARIES}
    ${CUDA_CUDA_LIBRARY}
    ${CUDA_CUDART_LIBRARY}
    ${CUDA_NPP_LIBRARIES_ZED}
)

add_dependencies (${libname} gstzedmeta)

target_link_libraries (
  ${libname} LINK_PUBLIC
  ${GLIB2_LIBRARIES}
  ${GOBJECT_LIBRARIES}
  ${GSTREAMER_LIBRARY}
  ${GSTREAMER_BASE_LIBRARY}
  ${GSTREAMER_VIDEO_LIBRARY}
  ${ZED_LIBS}
  ${CMAKE_CURRENT_BINARY_DIR}/../gst-zed-meta/libgstzedmeta.so
)

install(TARGETS ${libname} LIBRARY DESTINATION ${PLUGIN_INSTALL_DIR})
