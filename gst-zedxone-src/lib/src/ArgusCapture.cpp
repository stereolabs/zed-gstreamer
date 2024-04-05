#ifdef __aarch64__

#include "ArgusCapture.hpp"
#include "ArgusComponent.hpp"

using namespace std;
using namespace oc;


void ArgusVirtualCapture::getVersion(int& major, int& minor, int &patch)
{
    major = ARGUS_CAPTURE_VERSION_MAJOR;
    minor = ARGUS_CAPTURE_VERSION_MINOR;
    patch = ARGUS_CAPTURE_VERSION_PATCH;
}

ArgusVirtualCapture::ArgusVirtualCapture()
{
  ptr_buffer_sdk=nullptr;
}

bool ArgusVirtualCapture::isOpened()
{
  return opened_;
}

int ArgusVirtualCapture::getWidth()
{
  if (!opened_)
    return 0;
  return width;
}


int ArgusVirtualCapture::getHeight()
{
  if (!opened_)
    return 0;
  return height;
}

int ArgusVirtualCapture::getFPS()
{
  if (!opened_)
    return 0;
  return fps;
}

int ArgusVirtualCapture::getNumberOfChannels()
{
  if (!opened_)
    return 0;
  return nChannel;
}


bool ArgusVirtualCapture::isNewFrame()
{
  return consumer_newFrame;
}

void ArgusVirtualCapture::enterCriticalSection()
{
    mut_access.lock();
}


void ArgusVirtualCapture::exitCriticalSection()
{
    mut_access.unlock();
}

void ArgusVirtualCapture::setPtr(unsigned char *ptr_sdk) {
  if (ptr_sdk)
    ptr_buffer_sdk = ptr_sdk;
  else
    ptr_buffer_sdk = internal_buffer_grab;
}


unsigned char * ArgusVirtualCapture::getPixels() {
    consumer_newFrame = false;
    return ptr_buffer_sdk;
}


uint64_t ArgusVirtualCapture::getImageTimestampinUs()
{
  return consumer_image_ts_us;
}



#endif
