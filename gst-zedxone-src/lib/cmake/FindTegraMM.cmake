##set(TegraMM_ROOT $ENV{HOME}/tegra_multimedia_api)
set(TegraMM_ROOT /usr/src/jetson_multimedia_api)
set(TegraMM_FOUND FALSE)

if(EXISTS ${TegraMM_ROOT})
  # set packages
  set(TegraMM_INCLUDE_DIRS ${TegraMM_ROOT}/include ${TegraMM_ROOT}/include/libjpeg-8b /usr/include/libdrm)
  set(TegraMM_LIBRARY_DIRS /usr/lib/aarch64-linux-gnu/tegra /usr/lib/aarch64-linux-gnu)
  set(TegraMM_LIBRARIES nvargus_socketclient nvjpeg drm nvbufsurftransform nvbufsurface nvosd EGL v4l2 GLESv2 X11 pthread)
  file(GLOB TegraMM_COMMON_SOURCES ${TegraMM_ROOT}/samples/common/classes/*.cpp)
  include_directories(${TegraMM_INCLUDE_DIRS})
  link_directories(${TegraMM_LIBRARY_DIRS})
  set(TegraMM_FOUND TRUE)
endif()

