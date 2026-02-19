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

set(SOURCES
    gstzedsrc.cpp
    )
    
set(HEADERS
    gstzedsrc.h
    )

include_directories(${CUDA_INCLUDE_DIRS})
include_directories(${ZED_INCLUDE_DIRS})
include_directories(${CMAKE_CURRENT_SOURCE_DIR}/include)

link_directories(${ZED_LIBRARY_DIR})
link_directories(${CUDA_LIBRARY_DIRS})
link_directories(${LIBRARY_INSTALL_DIR})

# Enable Advanced Capture API (RawBuffer zero-copy) on Jetson with ZED SDK >= 5.2
# This is only available for GMSL cameras on Jetson platforms
if(L4T_FOUND)
    # Add Jetson Multimedia API include path for nvbufsurface.h
    include_directories(/usr/src/jetson_multimedia_api/include)
    
    # ZED_VERSION_MAJOR and ZED_VERSION_MINOR are set by find_package(ZED) in root CMakeLists.txt
    message(STATUS "ZED SDK version: ${ZED_VERSION} (${ZED_VERSION_MAJOR}.${ZED_VERSION_MINOR})")
    
    # Enable zero-copy API for ZED SDK >= 5.2
    if((ZED_VERSION_MAJOR GREATER 5) OR (ZED_VERSION_MAJOR EQUAL 5 AND ZED_VERSION_MINOR GREATER_EQUAL 2))
        message(STATUS "Enabling Advanced Capture API (RawBuffer zero-copy) for GMSL cameras")
        add_definitions(-DSL_ENABLE_ADVANCED_CAPTURE_API)
    endif()
endif()

set(libname gstzedsrc)

message( " * ${libname} plugin added")

add_library(${libname} MODULE
    ${SOURCES}
    ${HEADERS}
    )

if(UNIX)
    message("   ${libname}: OS Unix")
    add_definitions(-std=c++11 -Wno-deprecated-declarations -Wno-write-strings)
endif(UNIX)

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

# Additional libraries for zero-copy RawBuffer support
set(ZEROCOPY_LIBS "")
if(L4T_FOUND)
    # Find gstreamer allocators for DMA-BUF support
    pkg_check_modules(GST_ALLOCATORS gstreamer-allocators-1.0)
    if(GST_ALLOCATORS_FOUND)
        list(APPEND ZEROCOPY_LIBS ${GST_ALLOCATORS_LIBRARIES})
        include_directories(${GST_ALLOCATORS_INCLUDE_DIRS})
    endif()
    # NvBufSurface is part of nvbufsurface library
    find_library(NVBUFSURFACE_LIB nvbufsurface PATHS /usr/lib/aarch64-linux-gnu/tegra)
    if(NVBUFSURFACE_LIB)
        list(APPEND ZEROCOPY_LIBS ${NVBUFSURFACE_LIB})
    endif()
    # NvBufSurfTransform for stereo side-by-side compositing
    find_library(NVBUFSURFTRANSFORM_LIB nvbufsurftransform
        PATHS /usr/lib/aarch64-linux-gnu/tegra /usr/lib/aarch64-linux-gnu/nvidia)
    if(NVBUFSURFTRANSFORM_LIB)
        list(APPEND ZEROCOPY_LIBS ${NVBUFSURFTRANSFORM_LIB})
    endif()
endif()

add_dependencies (${libname} gstzedmeta)

if (WIN32)
    target_link_libraries (${libname} LINK_PUBLIC
        ${GLIB2_LIBRARIES}
        ${GOBJECT_LIBRARIES}
        ${GSTREAMER_LIBRARY}
        ${GSTREAMER_BASE_LIBRARY}
        ${GSTREAMER_VIDEO_LIBRARY}
        gstzedmeta
        ${ZED_LIBS}
        )
else()
    target_link_libraries (${libname} LINK_PUBLIC
        ${GLIB2_LIBRARIES}
        ${GOBJECT_LIBRARIES}
        ${GSTREAMER_LIBRARY}
        ${GSTREAMER_BASE_LIBRARY}
        ${GSTREAMER_VIDEO_LIBRARY}
        ${ZED_LIBS}
        ${ZEROCOPY_LIBS}
        ${CMAKE_CURRENT_BINARY_DIR}/../gst-zed-meta/libgstzedmeta.so
        )
endif()

if (WIN32)
  install (FILES $<TARGET_PDB_FILE:${libname}> DESTINATION ${PDB_INSTALL_DIR} COMPONENT pdb OPTIONAL)
endif ()
install(TARGETS ${libname} LIBRARY DESTINATION ${PLUGIN_INSTALL_DIR})
