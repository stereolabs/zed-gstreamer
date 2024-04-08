#ifdef __aarch64__

#include "ArgusCapture.hpp"
#include "ArgusComponent.hpp"
#include <functional>
#include <future>
#include <stdio.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <sys/stat.h>
#include <sys/mman.h>
#include <fcntl.h>
#include <errno.h>
#include <stdlib.h>
#include <signal.h>
#include <poll.h>
#include <string>
#include "NvBufSurface.h"


#define SHARPEN_FACTOR 10.f
#define DEFAULT_SHARPEN_VALUE 1
#define DEFAULT_DENOISER_VALUE 0.5f
#define DUAL_SESSION_CAPTURE 1

#define MAX_CAPTURE_QUEUE_SIZE 2
#define V4L2_BUFFERS_NUM    4


#ifndef NSEC_PER_SEC
#define NSEC_PER_SEC                   1000000ULL
#endif
unsigned long long fromtimeval(timeval in) {
    unsigned long long time_in_micros = NSEC_PER_SEC * in.tv_sec + in.tv_usec;
    return time_in_micros;
}

using namespace Argus;
using namespace EGLStream;
using namespace std;
using namespace oc;


/* Correlate v4l2 pixel format and NvBuffer color format */
typedef struct
{
    unsigned int v4l2_pixfmt;
    NvBufSurfaceColorFormat nvbuff_color;
} nv_color_fmt;


/**
 * clamp x to the interval [l, u]
 */
template <typename T>
inline void clamp(T& x,const T min,const T max)
{
  if (x < min)
    x = min;
  if (x > max)
    x = max;
}

static nv_color_fmt nvcolor_fmt[] =
{
    /* TODO: add more pixel format mapping */
    {V4L2_PIX_FMT_UYVY, NVBUF_COLOR_FORMAT_UYVY},
    {V4L2_PIX_FMT_VYUY, NVBUF_COLOR_FORMAT_VYUY},
    {V4L2_PIX_FMT_YUYV, NVBUF_COLOR_FORMAT_YUYV},
    {V4L2_PIX_FMT_YVYU, NVBUF_COLOR_FORMAT_YVYU},
    {V4L2_PIX_FMT_GREY, NVBUF_COLOR_FORMAT_GRAY8},
    {V4L2_PIX_FMT_YUV420M, NVBUF_COLOR_FORMAT_YUV420},
};

static NvBufSurfaceColorFormat
get_nvbuff_color_fmt(unsigned int v4l2_pixfmt)
{
    unsigned i;

    for (i = 0; i < sizeof(nvcolor_fmt) / sizeof(nvcolor_fmt[0]); i++)
    {
        if (v4l2_pixfmt == nvcolor_fmt[i].v4l2_pixfmt)
            return nvcolor_fmt[i].nvbuff_color;
    }

    return NVBUF_COLOR_FORMAT_INVALID;
}


std::vector<oc::ArgusDevice> ArgusV4l2Capture::getV4l2Devices()
{
  std::vector<oc::ArgusDevice> out;
  int n_max = MAX_GMSL_CAMERAS; // just to get a MAX number , not related to GMSL
  for (int i=0;i<n_max;i++)
    {
      std::string dev_name = "/dev/video"+std::to_string(i);
      int fd=open(dev_name.c_str(), O_RDONLY);
      if (fd != -1) {
          //TODO : Need to add checks
          oc::ArgusDevice dev;
          dev.available = true;
          dev.id = i;
          dev.name = dev_name;
          dev.badge =" none";
          out.push_back(dev);
          }
      close(fd);
    }


  return out;
}


ArgusV4l2Capture::ArgusV4l2Capture():ArgusVirtualCapture()
{
  memset(&ctx, 0, sizeof(nv_v4l2_context_t));
  ctx.cam_fd = -1;
  ctx.cam_pixfmt = V4L2_PIX_FMT_YUYV;
  ctx.frame = 0;
  ctx.g_buff = NULL;
  ctx.capture_dmabuf = true;
  ctx.enable_verbose = mConfig.verbose_level>0;
  opened_ = false;
  exit_ = true;
}


ARGUS_STATE ArgusV4l2Capture::openCamera(const ArgusCameraConfig &config,bool recreate_daemon)
{
  mConfig = config;
  mCvtfd = -1;
  ctx.frame = 0;
  ctx.cam_w = mConfig.mWidth;
  ctx.cam_h = mConfig.mHeight;
  ctx.fps = mConfig.mFPS;
  ctx.cam_w = mConfig.mWidth;
  std::string devname = std::string("/dev/video")+std::to_string(mConfig.mDeviceId);
  ctx.cam_devname = devname.c_str();


  /// Make sure it's not already open
  if (opened_)
    return ARGUS_STATE::ALREADY_OPEN;

  //Set port according to device required
  mPort = mConfig.mDeviceId;

  ArgusProvider::changeState(mPort,ARGUS_CAMERA_STATE::OPENING);
  if (mConfig.verbose_level>0)
    std::cout<<"[ArgusCapture] Camera at Port"<<mPort<<" is opening"<<std::endl;

  struct v4l2_format fmt;

  /* Open camera device */
  ctx.cam_fd = open(ctx.cam_devname, O_RDWR);
  if (ctx.cam_fd == -1)
  {
    if (mConfig.verbose_level>0)
        std::cout<<"[ArgusCapture] Failed to open camera for "<<mPort<<" || Err : "<<strerror(errno)<<std::endl;

    ArgusProvider::changeState(mPort,ARGUS_CAMERA_STATE::OFF);
    return ARGUS_STATE::INVALID_CAMERA_PROVIDER;
  }


  /* Set camera output format */
  memset(&fmt, 0, sizeof(fmt));
  fmt.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
  fmt.fmt.pix.width = ctx.cam_w;
  fmt.fmt.pix.height = ctx.cam_h;
  fmt.fmt.pix.pixelformat = ctx.cam_pixfmt;
  fmt.fmt.pix.field = V4L2_FIELD_INTERLACED;
  if (ioctl(ctx.cam_fd, VIDIOC_S_FMT, &fmt) < 0)
  {
      if (mConfig.verbose_level>0)
          std::cout<<"[ArgusCapture] Failed to SET camera output format for "<<mPort<<" || Err : "<<strerror(errno)<<std::endl;

      ArgusProvider::changeState(mPort,ARGUS_CAMERA_STATE::OFF);
      return ARGUS_STATE::INVALID_SOURCE_CONFIGURATION;
  }


  /* Get the real format in case the desired is not supported */
  memset(&fmt, 0, sizeof fmt);
  fmt.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
  if (ioctl(ctx.cam_fd, VIDIOC_G_FMT, &fmt) < 0)
  {
      if (mConfig.verbose_level>0)
          std::cout<<"[ArgusCapture] Failed to GET camera output format for "<<mPort<<" || Err : "<<strerror(errno)<<std::endl;

      ArgusProvider::changeState(mPort,ARGUS_CAMERA_STATE::OFF);
      return ARGUS_STATE::INVALID_SOURCE_CONFIGURATION;
  }

  if (fmt.fmt.pix.width != ctx.cam_w ||
          fmt.fmt.pix.height != ctx.cam_h ||
          fmt.fmt.pix.pixelformat != ctx.cam_pixfmt)
  {
      ctx.cam_w = fmt.fmt.pix.width;
      ctx.cam_h = fmt.fmt.pix.height;
      ctx.cam_pixfmt =fmt.fmt.pix.pixelformat;

      if (mConfig.verbose_level>0)
          std::cout<<"[ArgusCapture] Warning -- Resolution requested not supported for Port"<<mPort<<" : Switching to "<<ctx.cam_w<<" x "<<ctx.cam_h<<std::endl;
  }

  width =  ctx.cam_w;
  height = ctx.cam_h;
  nChannel = mConfig.mChannel;

  struct v4l2_streamparm streamparm;
  memset (&streamparm, 0x00, sizeof (struct v4l2_streamparm));
  streamparm.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
  ioctl (ctx.cam_fd, VIDIOC_G_PARM, &streamparm);

  if (mConfig.verbose_level>0)
      std::cout<<"[ArgusCapture] Port"<<mPort<<" : Resolution "<<fmt.fmt.pix.width<<" x "<<fmt.fmt.pix.height<<"@"<<streamparm.parm.capture.timeperframe.denominator<<"/"<<streamparm.parm.capture.timeperframe.numerator<<std::endl;

  NvBufSurf::NvCommonAllocateParams camparams = {0};
  int fd[V4L2_BUFFERS_NUM] = {0};

  /* Allocate global buffer context */
  ctx.g_buff = (nv_buffer *)malloc(V4L2_BUFFERS_NUM * sizeof(nv_buffer));
  if (ctx.g_buff == NULL)
  {
      if (mConfig.verbose_level>0)
          std::cout<<"[ArgusCapture] Failed to allocate global buffer context for "<<mPort<<std::endl;

      ArgusProvider::changeState(mPort,ARGUS_CAMERA_STATE::OFF);
      return ARGUS_STATE::INVALID_CAPTURE_REQUEST;
  }

  if (mConfig.verbose_level>0)
      std::cout<<"[ArgusCapture] Port"<<mPort<<" : Resolution "<<fmt.fmt.pix.width<<" x "<<fmt.fmt.pix.height<<"@"<<streamparm.parm.capture.timeperframe.denominator<<"/"<<streamparm.parm.capture.timeperframe.numerator<<std::endl;

  camparams.memType = NVBUF_MEM_SURFACE_ARRAY;
  camparams.width = width;
  camparams.height = height;
  camparams.layout = NVBUF_LAYOUT_PITCH;
  camparams.colorFormat = get_nvbuff_color_fmt(ctx.cam_pixfmt);
  camparams.memtag = NvBufSurfaceTag_CAMERA;
  if (NvBufSurf::NvAllocate(&camparams, V4L2_BUFFERS_NUM, fd))
  {
      if (mConfig.verbose_level>0)
          std::cout<<"[ArgusCapture] Failed to create NvBuffer for "<<mPort<<std::endl;

      ArgusProvider::changeState(mPort,ARGUS_CAMERA_STATE::OFF);
      return ARGUS_STATE::INVALID_CAPTURE_SESSION;
  }
  if (mConfig.verbose_level>0)
      std::cout<<"[ArgusCapture] Port"<<mPort<<" : Resolution "<<fmt.fmt.pix.width<<" x "<<fmt.fmt.pix.height<<"@"<<streamparm.parm.capture.timeperframe.denominator<<"/"<<streamparm.parm.capture.timeperframe.numerator<<std::endl;


  /* Create buffer and provide it with camera */
  for (unsigned int index = 0; index < V4L2_BUFFERS_NUM; index++)
  {
      NvBufSurface *pSurf = NULL;

      ctx.g_buff[index].dmabuff_fd = fd[index];

      if (-1 == NvBufSurfaceFromFd(fd[index], (void**)(&pSurf)))
        {
            if (mConfig.verbose_level>0)
                std::cout<<"[ArgusCapture] Failed to get NvBuffer parameters for "<<mPort<<std::endl;

            ArgusProvider::changeState(mPort,ARGUS_CAMERA_STATE::OFF);
            return ARGUS_STATE::INVALID_CAPTURE_SESSION;
        }

      /* TODO: add multi-planar support
         Currently only supports YUV422 interlaced single-planar */
      if (ctx.capture_dmabuf) {
          if (-1 == NvBufSurfaceMap (pSurf, 0, 0, NVBUF_MAP_READ_WRITE))
          {
              if (mConfig.verbose_level>0)
                  std::cout<<"[ArgusCapture] Failed to map buffer for "<<mPort<<std::endl;

              ArgusProvider::changeState(mPort,ARGUS_CAMERA_STATE::OFF);
              return ARGUS_STATE::INVALID_CAPTURE_SESSION;
          }

          ctx.g_buff[index].start = (unsigned char *)pSurf->surfaceList[0].mappedAddr.addr[0];
          ctx.g_buff[index].size = pSurf->surfaceList[0].dataSize;
      }
  }

  if (mConfig.verbose_level>0)
      std::cout<<"[ArgusCapture] Port"<<mPort<<" : Resolution "<<fmt.fmt.pix.width<<" x "<<fmt.fmt.pix.height<<"@"<<streamparm.parm.capture.timeperframe.denominator<<"/"<<streamparm.parm.capture.timeperframe.numerator<<std::endl;

  NvBufSurf::NvCommonAllocateParams transfparams;
  transfparams.memType = NVBUF_MEM_SURFACE_ARRAY;
  transfparams.width = width;
  transfparams.height = height;
  transfparams.layout = NVBUF_LAYOUT_PITCH;
  if (mConfig.mSwapRB)
  transfparams.colorFormat = NVBUF_COLOR_FORMAT_RGBA;
  else
  transfparams.colorFormat = NVBUF_COLOR_FORMAT_BGRA;
  transfparams.memtag = NvBufSurfaceTag_NONE;
  /* Create Render buffer */
  NvBufSurf::NvAllocate(&transfparams, 1, &mCvtfd);

  std::cout<<" Request camera Buffers"<<std::endl;

  if (!requestCameraBuffers())
  {
      if (mConfig.verbose_level>0)
          std::cout<<"[ArgusCapture] Failed to REQUEST camera buffers for "<<mPort<<std::endl;

      ArgusProvider::changeState(mPort,ARGUS_CAMERA_STATE::OFF);
      return ARGUS_STATE::INVALID_CAPTURE_SESSION;
  }

  std::cout<<" startStream "<<std::endl;

  if (!startStream())
  {
      if (mConfig.verbose_level>0)
          std::cout<<"[ArgusCapture] Failed to REQUEST camera buffers for "<<mPort<<std::endl;

      ArgusProvider::changeState(mPort,ARGUS_CAMERA_STATE::OFF);
      return ARGUS_STATE::CANNOT_START_STREAM_REQUEST;
  }

  if (mConfig.verbose_level>0)
    std::cout<<"[ArgusCapture] --> Using Resolution " <<width<<"x"<<height<<"@"<<fps<<std::endl;
  internal_buffer_grab = (unsigned char*)malloc(width * height * nChannel);
  if (!use_outer_buffer)
    ptr_buffer_sdk = internal_buffer_grab;

  /// We can say that we are open
  opened_ = true;
  exit_ = false;
  consumer_newFrame =false;
  producer = new std::thread(&ArgusV4l2Capture::produce,this);
  consumer = new std::thread(&ArgusV4l2Capture::consume,this);

  std::cout<<" Done "<<std::endl;

  return ARGUS_STATE::OK;
}


bool ArgusV4l2Capture::requestCameraBuffers()
{
    /* Request camera v4l2 buffer */
    struct v4l2_requestbuffers rb;
    memset(&rb, 0, sizeof(rb));
    rb.count = V4L2_BUFFERS_NUM;
    rb.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    rb.memory = V4L2_MEMORY_DMABUF;
    if (ioctl(ctx.cam_fd, VIDIOC_REQBUFS, &rb) < 0)
    {
      printf("Failed to request v4l2 buffers: %s (%d)",
              strerror(errno), errno);
      return false;
    }


    if (rb.count != V4L2_BUFFERS_NUM)
    {
        printf("V4l2 buffer number is not as desired");
        return false;
    }

    for (unsigned int index = 0; index < V4L2_BUFFERS_NUM; index++)
    {
        struct v4l2_buffer buf;

        /* Query camera v4l2 buf length */
        memset(&buf, 0, sizeof buf);
        buf.index = index;
        buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
        buf.memory = V4L2_MEMORY_DMABUF;

        if (ioctl(ctx.cam_fd, VIDIOC_QUERYBUF, &buf) < 0)
          {
            printf("Failed to query buff: %s (%d)",
                    strerror(errno), errno);
            return false;
          }

        /* TODO: add support for multi-planer
           Enqueue empty v4l2 buff into camera capture plane */
        buf.m.fd = (unsigned long)ctx.g_buff[index].dmabuff_fd;
        if (buf.length != ctx.g_buff[index].size)
        {
            printf("Camera v4l2 buf length is not expected");
            ctx.g_buff[index].size = buf.length;
        }

        if (ioctl(ctx.cam_fd, VIDIOC_QBUF, &buf) < 0)
          {
            printf("Failed to enqueue buffers: %s (%d)",
                    strerror(errno), errno);
            return false;
          }
    }

    return true;
}
void ArgusV4l2Capture::closeCamera()
{
  opened_ = false;
}



ArgusV4l2Capture::~ArgusV4l2Capture()
{
   closeCamera();
}


ARGUS_STATE ArgusV4l2Capture::reboot()
{
  return ARGUS_STATE::UNSUPPORTED_FCT;
}

void ArgusV4l2Capture::clearImageQueues()
{
  lock();
  if (mCaptureQueue.size()>0)
    {
      for (int i=0;i<mCaptureQueue.size();i++)
        {
          argusMonoImage t = mCaptureQueue.front();
          free(t.imageData);
          mCaptureQueue.pop_front();
        }
    }
  unlock();
}

void ArgusV4l2Capture::clearBuffers()
{

  if (internal_buffer_grab)
    {
      free(internal_buffer_grab);
      internal_buffer_grab = nullptr;
    }
}

void ArgusV4l2Capture::abortCapture()
{

  if (producer)
    {
      producer->join();
      delete producer;
      producer = nullptr;
    }


  if (consumer)
    {
      consumer->join();
      delete consumer;
      consumer = nullptr;
    }
}

void ArgusV4l2Capture::abortControlThread()
{
  if (controler)
    {
      controler->join();
      delete controler;
      controler = nullptr;
    }

}


int ArgusV4l2Capture::convert(int dmabuf_fd,int plane,argusMonoImage& image)
{
    if (dmabuf_fd <= 0)
      return -1;

    int ret = -1;
    NvBufSurface *surf = NULL;
    NvBufSurfaceMapParams parm;

    NvBufSurfaceFromFd(dmabuf_fd, (void**)(&surf));
    ret = NvBufSurfaceGetMapParams(surf,0, &parm);

    if (ret != 0)
      {
        return -1;
      }

  //  ret = NvBufSurfaceMap(surf,dmabuf_fd, plane, NVBUF_MAP_READ);
  //  if (ret == 0)
  //    {
  //      int sr = NvBufSurfaceSyncForCpu(surf,dmabuf_fd, plane);
  //      if (sr!=0)
  //        std::cout<<"[ArgusCapture] NvBufferMemSyncForCpu FAILED"<<std::endl;

        image.pitch = 2*parm.planes[0].width;//parm.planes[plane].pitch;
  //      image.imageData = (uint8_t*)malloc(nChannel*parm.planes[plane].width*parm.planes[plane].height);
        image.imageData = (uint8_t*)malloc(parm.planes[0].pitch*parm.planes[0].height);


  //      if (parm.planes[plane].pitch!=nChannel*parm.planes[plane].width)
  //      {
  //          for (int i=0;i<parm.planes[plane].height;i++)
  //            memcpy((unsigned char*)image.imageData+i*nChannel*parm.planes[plane].width,(char *)surf._reserved+i*parm.planes[plane].pitch,nChannel*parm.planes[plane].width);
  //      }
  //      else
  //      {
  //          memcpy((unsigned char*)image.imageData,(char *)surf._reserved,parm.planes[plane].pitch*parm.planes[plane].height);
  //      }

        int c_res = NvBufSurface2Raw(surf,0,0,parm.planes[0].width,parm.planes[0].height,image.imageData);
        if (c_res!=0)
          return -3;

  //      NvBufSurfaceUnMap(&surf, dmabuf_fd, plane);
  //    }
  //  else
  //    {
  //      std::cout<<"[ArgusCapture] Fail to dma buf"<<std::endl;
  //      return -1;
  //    }

    return 0;
}


void ArgusV4l2Capture::produce()
{
  mHasCaptureFrame=false;
  if (mConfig.verbose_level>2)
    std::cout<<"[ArgusCapture][Debug] : "<<" Wait for connection"<<std::endl;

  struct pollfd fds[1];
  fds[0].fd = ctx.cam_fd;
  fds[0].events = POLLIN;

  if (mConfig.verbose_level>2)
    std::cout<<"[ArgusCapture][Debug] : "<<" Done for connection"<<std::endl;

  NvBufSurf::NvCommonTransformParams transform_params = {0};
  /* Init the NvBufferTransformParams */
  transform_params.src_top = 0;
  transform_params.src_left = 0;
  transform_params.src_width = width;
  transform_params.src_height = height;
  transform_params.dst_top = 0;
  transform_params.dst_left = 0;
  transform_params.dst_width = width;
  transform_params.dst_height = height;
  transform_params.flag = NVBUFSURF_TRANSFORM_FILTER;
  transform_params.flip = NvBufSurfTransform_None;
  transform_params.filter = NvBufSurfTransformInter_Bilinear;
  /* Wait for camera event with timeout = 5000 ms */
  while (poll(fds, 1, 5000) > 0 && !exit_)
  {
      if (fds[0].revents & POLLIN) {
          struct v4l2_buffer v4l2_buf;

          /* Dequeue a camera buff */
          memset(&v4l2_buf, 0, sizeof(v4l2_buf));
          v4l2_buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
          if (ctx.capture_dmabuf)
              v4l2_buf.memory = V4L2_MEMORY_DMABUF;
          else
              v4l2_buf.memory = V4L2_MEMORY_MMAP;
          if (ioctl(ctx.cam_fd, VIDIOC_DQBUF, &v4l2_buf) < 0)
              printf("Failed to dequeue camera buff: %s (%d)",
                      strerror(errno), errno);

          ctx.frame++;

          NvBufSurface *pSurf = NULL;
          if (-1 == NvBufSurfaceFromFd(ctx.g_buff[v4l2_buf.index].dmabuff_fd,
                  (void**)(&pSurf)))
              printf("Cannot get NvBufSurface from fd");
          if (ctx.capture_dmabuf) {
              /* Cache sync for VIC operation since the data is from CPU */
              if (-1 == NvBufSurfaceSyncForDevice (pSurf, 0, 0))
                  printf("Cannot sync output buffer");
          } else {
              /* Copies raw buffer plane contents to an NvBufsurface plane */
              if (-1 == Raw2NvBufSurface (ctx.g_buff[v4l2_buf.index].start, 0, 0,
                       ctx.cam_w, ctx.cam_h, pSurf))
                  printf("Cannot copy raw buffer to NvBufsurface plane");
          }

          /*  Convert the camera buffer from YUV422 to RGBA */
          if (NvBufSurf::NvTransform(&transform_params, ctx.g_buff[v4l2_buf.index].dmabuff_fd, mCvtfd))
              printf("Failed to convert the buffer");

          argusMonoImage newImage = {0};
          newImage.imageTimestamp =fromtimeval(v4l2_buf.timestamp);
          std::cout<<" newImage.imageTimestamp : "<<newImage.imageTimestamp<<std::endl;
          int c_res= convert( mCvtfd,0,newImage);
          if (c_res==0)
            {

              if (mCaptureQueue.size()>MAX_CAPTURE_QUEUE_SIZE)
                mCaptureQueue.pop_front();
              mCaptureQueue.push_back(newImage);
            }
            else
            mArgusGrabState = ARGUS_STATE::CAPTURE_CONVERT_FAILURE;

          if (mConfig.verbose_level>3)
            std::cout<<"[ArgusCapture][Debug] --> New image available for port : "<<mPort<<" Timestamp : "<<newImage.imageTimestamp<<"  with settings : "<<width<<"x"<<height<<"@"<<fps<<std::endl;

          mArgusGrabState = ARGUS_STATE::OK;
          mHasCaptureFrame = true;
          /* Enqueue camera buffer back to driver */
          if (ioctl(ctx.cam_fd, VIDIOC_QBUF, &v4l2_buf))
              printf("Failed to queue camera buffers: %s (%d)",
                      strerror(errno), errno);
      }
      else
        usleep(10);
  }


}

void ArgusV4l2Capture::consume()
{
  uint64_t s_frame_count = 0;
  while(!exit_)
    {

      int queue_size_ = mCaptureQueue.size();
      if (queue_size_>0)
        {
          enterCriticalSection();
          argusMonoImage mNewImage = mCaptureQueue.front();
          consumer_image_ts_us = mNewImage.imageTimestamp;
          if (ptr_buffer_sdk)
            memcpy(ptr_buffer_sdk,mNewImage.imageData,width*height*nChannel);
          else
            std::cout<<"[ArgusCapture][CRITICAL] Invalid pointer provided for buffer acquisition"<<std::endl;

          free(mNewImage.imageData);
          mCaptureQueue.pop_front();
          exitCriticalSection();
          consumer_newFrame = true;
          userNewFrameEvent.notify();

          s_frame_count++;


        }
      else
        usleep(10);

    }
}


int ArgusV4l2Capture::dump_dmabuf(int dmabuf_fd,
                              unsigned int plane,
                              argusMonoImage& image)
{
//  if (dmabuf_fd <= 0)
//    return -1;

//  void *psrc_data;
//  int ret = -1;

//  NvBufferParams parm;
//  ret = NvBufferGetParams(dmabuf_fd, &parm);
//  if (ret != 0)
//    {
//      return -1;
//    }

//  ret = NvBufferMemMap(dmabuf_fd, plane, NvBufferMem_Read, &psrc_data);
//  if (ret == 0)
//    {
//      int sr = NvBufferMemSyncForCpu(dmabuf_fd, plane, &psrc_data);
//      if (sr!=0)
//        std::cout<<"[ArgusCapture] NvBufferMemSyncForCpu FAILED"<<std::endl;

//      image.pitch = parm.pitch[plane];
//      image.imageData = (uint8_t*)malloc(parm.pitch[plane]*parm.height[plane]);
//      memcpy((unsigned char*)image.imageData,(char *)psrc_data,parm.pitch[plane]*parm.height[plane]);
//      NvBufferMemUnMap(dmabuf_fd, plane, &psrc_data);
//    }
//  else
//    {
//      std::cout<<"[ArgusCapture] Fail to dma buf"<<std::endl;
//      return -1;
//    }

//  return 0;
    if (dmabuf_fd <= 0)
      return -1;

    int ret = -1;
    NvBufSurface *surf = NULL;
    NvBufSurfaceMapParams parm;

    NvBufSurfaceFromFd(dmabuf_fd, (void**)(&surf));
    ret = NvBufSurfaceGetMapParams(surf,0, &parm);

    if (ret != 0)
      {
        return -1;
      }

  //  ret = NvBufSurfaceMap(surf,dmabuf_fd, plane, NVBUF_MAP_READ);
  //  if (ret == 0)
  //    {
  //      int sr = NvBufSurfaceSyncForCpu(surf,dmabuf_fd, plane);
  //      if (sr!=0)
  //        std::cout<<"[ArgusCapture] NvBufferMemSyncForCpu FAILED"<<std::endl;

        image.pitch = 2*parm.planes[0].width;//parm.planes[plane].pitch;
  //      image.imageData = (uint8_t*)malloc(nChannel*parm.planes[plane].width*parm.planes[plane].height);
        image.imageData = (uint8_t*)malloc(parm.planes[0].pitch*parm.planes[0].height);


  //      if (parm.planes[plane].pitch!=nChannel*parm.planes[plane].width)
  //      {
  //          for (int i=0;i<parm.planes[plane].height;i++)
  //            memcpy((unsigned char*)image.imageData+i*nChannel*parm.planes[plane].width,(char *)surf._reserved+i*parm.planes[plane].pitch,nChannel*parm.planes[plane].width);
  //      }
  //      else
  //      {
  //          memcpy((unsigned char*)image.imageData,(char *)surf._reserved,parm.planes[plane].pitch*parm.planes[plane].height);
  //      }

        int c_res = NvBufSurface2Raw(surf,0,0,parm.planes[0].width,parm.planes[0].height,image.imageData);
        if (c_res!=0)
          return -3;

  //      NvBufSurfaceUnMap(&surf, dmabuf_fd, plane);
  //    }
  //  else
  //    {
  //      std::cout<<"[ArgusCapture] Fail to dma buf"<<std::endl;
  //      return -1;
  //    }

    return 0;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////
//////////////////////////////// EXPOSURE CONTROL/REQUEST /////////////////////////////////////////////////////
///////////////////////////////////////////////////////////////////////////////////////////////////////////////

uint64_t ArgusV4l2Capture::getFrameExposureTime()
{
  return 0;
}

int ArgusV4l2Capture::getFrameExposurePercent()
{
    return -1;
}

int ArgusV4l2Capture::setFrameExposureRange(uint64_t exp_low,uint64_t exp_high)
{
    return -1;
}

int ArgusV4l2Capture::getFrameExposureRange(uint64_t& exp_low,uint64_t& exp_high)
{
    return -1;
}

int ArgusV4l2Capture::getExposureLimits(uint64_t& min_exp,uint64_t& max_exp)
{
    return -1;
}

int ArgusV4l2Capture::setManualExposure(int percent)
{
    return -1;
}

int ArgusV4l2Capture::setManualTimeExposure(uint64_t time)
{
    return -1;
}

int ArgusV4l2Capture::setAutomaticExposure()
{
  return -1;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////
//////////////////////////////// ANALOG GAIN (SENSOR) CONTROL/REQUEST /////////////////////////////////////////
///////////////////////////////////////////////////////////////////////////////////////////////////////////////

float ArgusV4l2Capture::getAnalogFrameGain()
{
    return -1;
}

int ArgusV4l2Capture::getAnalogFrameGainPercent()
{
    return -1;
}


int ArgusV4l2Capture::setAnalogFrameGainRange(float gain_low,float gain_high)
{
   return -1;
}

int ArgusV4l2Capture::getAnalogFrameGainRange(float& sgain_low,float& sgain_high)
{
    return -1;
}

int ArgusV4l2Capture::getAnalogGainLimits(float& min_gain,float& max_gain)
{
    return -1;
}

int ArgusV4l2Capture::setAutomaticAnalogGain()
{
    return -1;
}

int ArgusV4l2Capture::setManualAnalogGain(int percent)
{
    return -1;
}

int ArgusV4l2Capture::setManualAnalogGainReal(int db)
{
    return -1;
}


///////////////////////////////////////////////////////////////////////////////////////////////////////////////
//////////////////////////////// DIGITAL GAIN (ISP) CONTROL/REQUEST ///////////////////////////////////////////
///////////////////////////////////////////////////////////////////////////////////////////////////////////////

float ArgusV4l2Capture::getDigitalFrameGain()
{
  return 0;
}

int ArgusV4l2Capture::setDigitalFrameGainRange(float gain_low, float gain_high)
{
    return -1;
}

int ArgusV4l2Capture::getDigitalFrameGainRange(float& dgain_low,float& dgain_high)
{
    return -1;
}

int ArgusV4l2Capture::getDigitalGainLimits(float& min_gain,float& max_gain)
{
    return -1;
}

int ArgusV4l2Capture::setAutomaticDigitalGain()
{
  return -1;
}


int ArgusV4l2Capture::setManualDigitalGain(int percent)
{
    return -1;
}


int ArgusV4l2Capture::setManualDigitalGainReal(int fact)
{
    return -1;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////
//////////////////////////////// White Balance(ISP) CONTROL/REQUEST ///////////////////////////////////////////
///////////////////////////////////////////////////////////////////////////////////////////////////////////////
int ArgusV4l2Capture::setManualWhiteBalance(uint32_t color_temperature_)
{
  return -1;
}

uint32_t ArgusV4l2Capture::getManualWhiteBalance()
{
    return -1;
}

int ArgusV4l2Capture::setAutomaticWhiteBalance(int val)
{
    return -1;
}

int ArgusV4l2Capture::getAutomaticWhiteBalanceStatus()
{
    return -1;
}



///////////////////////////////////////////////////////////////////////////////////////////////////////////////
////////////////////////////////     Edge Enhancement/ Sharpening       ///////////////////////////////////////
///////////////////////////////////////////////////////////////////////////////////////////////////////////////

int ArgusV4l2Capture::setSharpening(float value)
{
    return -1;
}

float ArgusV4l2Capture::getSharpening()
{
    return 0.0;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////
/////////////////////////////////   Saturation(ISP) CONTROL/REQUEST ///////////////////////////////////////////
///////////////////////////////////////////////////////////////////////////////////////////////////////////////
int ArgusV4l2Capture::setColorSaturation(float saturation)
{
    return -1;
}


float ArgusV4l2Capture::getColorSaturation()
{
    return -1;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////
/////////////////////////////////   Exposure EV (ISP) CONTROL/REQUEST /////////////////////////////////////////
///////////////////////////////////////////////////////////////////////////////////////////////////////////////
int ArgusV4l2Capture::setExposureCompensation(float ev)
{
    return -1;
}


float ArgusV4l2Capture::getExposureCompensation()
{
    return 0.0;
}

//////////////////////////////////
///////////AEaNtibanding//////////
//////////////////////////////////
int ArgusV4l2Capture::setAEAntiBanding(AEANTIBANDING mode)
{
    return -1;
}




///////////////////////////////////////////////////////////////////////////////////////////////////////////////
/////////////////////////////////   Denoiser (ISP) CONTROL/REQUEST ///////////////////////////////////////////
///////////////////////////////////////////////////////////////////////////////////////////////////////////////
int ArgusV4l2Capture::setDenoisingValue(float value)
{
    return -1;
}

float ArgusV4l2Capture::getDenoisingValue(int side)
{
    return 0;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////
///////////////////////////////////   ToneMapping CONTROL/REQUEST   ///////////////////////////////////////////
///////////////////////////////////////////////////////////////////////////////////////////////////////////////
int ArgusV4l2Capture::getToneMappingCurveSize(int channel)
{
    return -1;
}


int ArgusV4l2Capture::setToneMappingFromGamma(int channel, float gamma)
{
    return -1;
}

float ArgusV4l2Capture::getGamma()
{
  return 0;
}


///////////////////////////////////////////////////////////////////////////////////////////////////////////////
///////////////////////////////////   AEC ROI    ///////////////////////////////////////////
///////////////////////////////////////////////////////////////////////////////////////////////////////////////

int ArgusV4l2Capture::setROIforAECAGC(oc::Rect roi)
{
    return -1;
}



int ArgusV4l2Capture::resetROIforAECAGC()
{
    return -1;
}


int ArgusV4l2Capture::getROIforAECAGC(oc::Rect &roi)
{
   //Unsupported
   return -1;
}



///////////////////////////////////////////////////////////////////////////////////////////////////////////////
//////////////////////////////// UTILS/INTERNAL  //////////////////////////////////////////////////////////////
///////////////////////////////////////////////////////////////////////////////////////////////////////////////
///
///

bool ArgusV4l2Capture::startStream()
{
    enum v4l2_buf_type type;

    /* Start v4l2 streaming */
    type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    if (ioctl(ctx.cam_fd, VIDIOC_STREAMON, &type) < 0)
    {
        printf("Failed to start streaming: %s (%d)",
                strerror(errno), errno);
        return false;
    }
    usleep(200);
    return true;
}

bool ArgusV4l2Capture::stopStream()
{
    enum v4l2_buf_type type;
    /* Stop v4l2 streaming */
    type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    if (ioctl(ctx.cam_fd, VIDIOC_STREAMOFF, &type))
      {
        printf("Failed to stop streaming: %s (%d)",
                strerror(errno), errno);
        return false;
      }
    return true;
}



void ArgusV4l2Capture::lock()
{
  mut_internal.lock();

}

void ArgusV4l2Capture::unlock()
{
  mut_internal.unlock();
}


#endif
