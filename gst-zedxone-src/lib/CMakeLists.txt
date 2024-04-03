cmake_minimum_required(VERSION "3.6")
project(zedarguscapture)

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

set(libname zedarguscapture)

set(CMAKE_MODULE_PATH ${CMAKE_MODULE_PATH} ${CMAKE_CURRENT_SOURCE_DIR}/cmake)
set(CMAKE_CXX_STANDARD 14)
set(CMAKE_DEBUG_POSTFIX "d")

find_package(TegraMM REQUIRED)
if(${TegraMM_FOUND})
  message(STATUS "Found Tegra Multimedia API")
else()
  message(STATUS "Could not find Tegra Multimedia API")
endif()

if (CMAKE_SYSTEM_PROCESSOR MATCHES aarch64)
set(TegraMM_LIBRARY_DIRS "/usr/lib/aarch64-linux-gnu" "/usr/lib/aarch64-linux-gnu/tegra")
set(TegraMM_LIBRARY_DIRS_CROSSCPL "/usr/xcc/aarch64-unknown-linux-gnu/lib/aarch64-linux-gnu/tegra" "/usr/xcc/aarch64-unknown-linux-gnu/lib/aarch64-linux-gnu" "/usr/xcc/aarch64-unknown-linux-gnu/lib/" "${cross_root}/lib")
link_directories(${TegraMM_LIBRARY_DIRS})
link_directories(${TegraMM_LIBRARY_DIRS_CROSSCPL})
SET(SPECIAL_OS_LIBS v4l2 nvargus_socketclient nvbufsurftransform nvbufsurface EGL pthread)
SET(NATIV_CMD "find /usr/lib/aarch64-linux-gnu/ -type f -name libnvbuf_utils* 2> /dev/null")
SET(CROSS_CMD "find /usr/xcc/aarch64-unknown-linux-gnu/ -type f -name libnvbuf_utils* 2> /dev/null")
execute_process(COMMAND bash "-c" ${NATIV_CMD} OUTPUT_VARIABLE NVBUF_UTILS_LIBRARY_NATIV)
execute_process(COMMAND bash "-c" ${CROSS_CMD} OUTPUT_VARIABLE NVBUF_UTILS_LIBRARY_CC)
if(NVBUF_UTILS_LIBRARY_NATIV OR NVBUF_UTILS_LIBRARY_CC)
    SET(SPECIAL_OS_LIBS ${SPECIAL_OS_LIBS} nvbuf_utils)
endif()
endif()

### Set folder for sources and includes
SET(SRC_FOLDER ${CMAKE_CURRENT_SOURCE_DIR}/src)
SET(HDR_FOLDER ${CMAKE_CURRENT_SOURCE_DIR}/include)
include_directories(${HDR_FOLDER})

### List source files of library
FILE(GLOB_RECURSE ARGUS_CAPTURE_SRC "${SRC_FOLDER}/*.c*")
FILE(GLOB_RECURSE ARGUS_CAPTURE_HDR "${HDR_FOLDER}/*.h*")

## Definitions
add_definitions(-O3)

## DEBUG/ SANITIZER options
IF(NOT WIN32)
    add_definitions(-Werror=return-type)
    IF (BUILD_WITH_SANITIZER)
        message("!! Building with address sanitizer and -g !!")
        set (CMAKE_CXX_FLAGS_DEBUG "${CMAKE_CXX_FLAGS_DEBUG} -fno-omit-frame-pointer -static-libasan -fsanitize=address")
        set (CMAKE_LINKER_FLAGS_DEBUG "${CMAKE_LINKER_FLAGS_DEBUG} -fno-omit-frame-pointer -static-libasan -fsanitize=address")
        set (CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -fno-omit-frame-pointer -static-libasan -fsanitize=address -Wall -Wextra -g")
        set (CMAKE_LINKER_FLAGS "${CMAKE_LINKER_FLAGS} -fno-omit-frame-pointer -static-libasan -fsanitize=address -Wall -Wextra -g")
        add_definitions(-g -fsanitize=address -fno-omit-frame-pointer -static-libasan -Wall -Wextra)
    ENDIF()

    IF (BUILD_WITH_DEBUGINFOS)
        message("!! Building with -g !!")
        add_definitions(-g)
    ENDIF()
ENDIF()

message("TegraMM_COMMON_SOURCES:")
message("${TegraMM_COMMON_SOURCES}")
message("SPECIAL_OS_LIBS:")
message("${SPECIAL_OS_LIBS}")

### Add sources for library
add_library(${libname} SHARED ${ARGUS_CAPTURE_SRC} ${ARGUS_CAPTURE_HDR} ${TegraMM_COMMON_SOURCES})

### Link library with deps
target_link_libraries(${libname} ${SPECIAL_OS_LIBS} drm X11)

set_target_properties(${libname} PROPERTIES
    PUBLIC_HEADER "${ARGUS_CAPTURE_HDR}"
)

install( TARGETS ${libname}
    LIBRARY DESTINATION ${LIBRARY_INSTALL_DIR}
    PUBLIC_HEADER DESTINATION ${INCLUDE_INSTALL_DIR}
)

 