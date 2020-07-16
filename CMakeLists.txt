cmake_minimum_required(VERSION "3.6")

set(proj_name gst-plugins-zed)
project(${proj_name})

set(CMAKE_SHARED_MODULE_PREFIX "lib")
set(CMAKE_SHARED_LIBRARY_PREFIX "lib")

# add local cmake modules
list(APPEND CMAKE_MODULE_PATH ${CMAKE_SOURCE_DIR}/cmake/modules)

include(MacroLogFeature)

find_package(GStreamer REQUIRED COMPONENTS base)
macro_log_feature(GSTREAMER_FOUND "GStreamer" "Required to build ${proj_name}" "http://gstreamer.freedesktop.org/" TRUE "1.2.0")
macro_log_feature(GSTREAMER_BASE_LIBRARY_FOUND "GStreamer base library" "Required to build ${proj_name}" "http://gstreamer.freedesktop.org/" FALSE "1.2.0")

find_package(GStreamerPluginsBase COMPONENTS video)
macro_log_feature(GSTREAMER_VIDEO_LIBRARY_FOUND "GStreamer video library" "Required to build ${proj_name}" "http://gstreamer.freedesktop.org/" FALSE "1.2.0")

find_package(GLIB2 REQUIRED)
macro_log_feature(GLIB2_FOUND "GLib" "Required to build ${proj_name}" "http://www.gtk.org/" TRUE)

find_package(GObject REQUIRED)
macro_log_feature(GOBJECT_FOUND "GObject" "Required to build ${proj_name}" "http://www.gtk.org/" TRUE)

find_package(ZED 3 REQUIRED)
macro_log_feature(ZED_FOUND "ZED SDK" "Required to build ${proj_name}" "www.stereolabs.com" TRUE "3.0")

find_package(CUDA ${ZED_CUDA_VERSION} EXACT)
macro_log_feature(CUDA_FOUND "Nvidia CUDA library" "Required to build ${proj_name}" "www.nvidia.com" TRUE "${ZED_CUDA_VERSION}")

# Setup common environment
include_directories(
    .
    ${GSTREAMER_INCLUDE_DIR}
    ${GLIB2_INCLUDE_DIR})

# values used when registering plugins
add_definitions(-DGST_PACKAGE_VERSION="0.1")
add_definitions(-DGST_PACKAGE_LICENSE="LGPL")
add_definitions(-DGST_PACKAGE_ORIGIN="https://github.com/stereolabs/zed-gstreamer")
add_definitions(-DGST_PACKAGE_NAME="${CMAKE_PROJECT_NAME}")
add_definitions(-DPACKAGE="${CMAKE_PROJECT_NAME} package")

if(CMAKE_INSTALL_PREFIX_INITIALIZED_TO_DEFAULT)
  if (WIN32)
    get_filename_component(_PREFIX "${GSTREAMER_PLUGIN_DIR}/../" REALPATH)
  else ()
    #set(_PREFIX "/usr/lib")
    get_filename_component(_PREFIX "${GSTREAMER_PLUGIN_DIR}/../" REALPATH)
  endif ()
  set(CMAKE_INSTALL_PREFIX "${_PREFIX}"
      CACHE PATH "Common prefix for all installed files (e.g., plugins will be installed under CMAKE_INSTALL_PREFIX/gstreamer-1.0)" FORCE)
endif(CMAKE_INSTALL_PREFIX_INITIALIZED_TO_DEFAULT)

if (NOT LIBRARY_INSTALL_DIR)
  set(LIBRARY_INSTALL_DIR "${CMAKE_INSTALL_PREFIX}/"
      CACHE PATH "Location to install libraries (e.g., libgstklv-1.0-0.so/dll)")
endif ()
if (NOT PLUGIN_INSTALL_DIR)
  set(PLUGIN_INSTALL_DIR "${CMAKE_INSTALL_PREFIX}/gstreamer-1.0/"
      CACHE PATH "Location to install plugins (e.g., libgstpylon.so/dll)")
endif()
if (WIN32 AND NOT PDB_INSTALL_DIR)
  set(PDB_INSTALL_DIR "${CMAKE_INSTALL_PREFIX}/gstreamer-1.0/"
      CACHE PATH "Location to install PDB debug files (e.g., libgstpylon.pdb)")
endif()

if (NOT INCLUDE_INSTALL_DIR)
  get_filename_component(INC_PREFIX "${GSTREAMER_PLUGIN_DIR}/../../../" REALPATH)
  set(INCLUDE_INSTALL_DIR "${INC_PREFIX}/include//gstreamer-1.0/gst/zed/"
      CACHE PATH "Location to install include headers" FORCE)
endif ()

add_subdirectory(gst-zed-meta)
if(ZED_FOUND)
	add_subdirectory(gst-zed-src)
else()
    message( "ZED SDK not available. 'zedsrc' will not be installed")
endif()
add_subdirectory(gst-zed-demux)
add_subdirectory(gst-zed-data-csv-sink)
add_subdirectory(gst-zed-obj-det-sink)


macro_display_feature_log()

message("")
message("Plugin install dir:")
message(${PLUGIN_INSTALL_DIR})
message("")
message("Library install dir:")
message(${LIBRARY_INSTALL_DIR})
message("")
message("Include install dir:")
message(${INCLUDE_INSTALL_DIR})
message("")

#message(${LIBRARY_OUTPUT_PATH})


