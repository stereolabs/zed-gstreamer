#ifndef __ARGUSCAPTURE_COMPONENT_H
#define __ARGUSCAPTURE_COMPONENT_H

#ifdef __aarch64__
#include <unistd.h>
#include <sys/mman.h>
#include "Argus/Argus.h"
#include "EGLStream/NV/ImageNativeBuffer.h"
//#include "nvbuf_utils.h"
#include "nvbufsurface.h"
#include "nvbufsurftransform.h"

#include "NvBuffer.h"
#include "EGLStream/EGLStream.h"
#include "Argus/Ext/SyncSensorCalibrationData.h"
#include "Argus/Types.h"
#include <math.h>
#include <shared_mutex>
#include <linux/version.h>
#include <memory>


#define SAFE_DELETE(e) if (e) { \
    delete(e);\
    e = nullptr;}\

#define ACQUIZ_NV12 1
#define ROUND_UP_EVEN(x) 2 * ((x + 1) / 2)
#define MAX_MODULE_STRING 32
#define MAX_CAM_DEVICE 16
#define MAX_GMSL_CAMERAS 8


#define ARGUS_CAPTURE_VERSION_MAJOR 1
#define ARGUS_CAPTURE_VERSION_MINOR 1
#define ARGUS_CAPTURE_VERSION_PATCH 0

namespace oc
{

  enum class ARGUS_CAMERA_STATE {
      OFF,
      OPENING,
      RUNNING,
      FROZEN
  };


  class ArgusProvider
  {
  public:

  public:
      static ArgusProvider* getInstance();
      static void DeleteInstance();

      ~ArgusProvider(){}
      static Argus::UniqueObj<Argus::CameraProvider> cameraProvider;//(CameraProvider::create());
      static ARGUS_CAMERA_STATE camera_states[MAX_GMSL_CAMERAS];
      static void changeState(int id, ARGUS_CAMERA_STATE state);
      static ARGUS_CAMERA_STATE getState(int id);
      static bool hasCameraOpening();
      static bool hasCameraFrozen();
  private:
      ArgusProvider(){}
      ArgusProvider (const ArgusProvider&){}
      static ArgusProvider* instance;

  };


  class ArgusComponent
  {

  public :
      ArgusComponent() {
          capRequest.reset();
          mStream.reset();
          mFrameConsumer.reset();
          mCaptureSession.reset();
          devices.reset();
      }

      ~ArgusComponent(){
         close();
      }

      void close(bool force_stop=true)
      {

          if (force_stop)
          {
              if (mCaptureSession)
              {
                  auto iCaptureSession = Argus::interface_cast<Argus::ICaptureSession>(mCaptureSession);
                  if (iCaptureSession) {
                      iCaptureSession->stopRepeat();
                      iCaptureSession->waitForIdle();
                  }
              }
          }

          if (mStream)
          {
              auto iStream = Argus::interface_cast<Argus::IEGLOutputStream>(mStream);
              if (iStream) {
                  iStream->disconnect();
              }
          }



          iSourceSettings=nullptr;
          capRequest.reset();
          mStream.reset();
          mFrameConsumer.reset();
          mCaptureSession.reset();
          if (devices)
            devices->clear();
          devices.reset();
      }

      Argus::UniqueObj<Argus::CaptureSession> mCaptureSession; //Argus::CaptureSession
      Argus::UniqueObj<Argus::OutputStream> mStream; //Argus::OutputStream
      Argus::UniqueObj<EGLStream::FrameConsumer> mFrameConsumer; //EGLStream::FrameConsumer
      Argus::ISourceSettings* iSourceSettings=nullptr; //For Settings
      Argus::CameraDevice* cameraDevice =nullptr;
      Argus::UniqueObj<Argus::Request> capRequest;
      std::shared_ptr<std::vector<Argus::CameraDevice*>> devices;
  };

}

#endif

#endif
