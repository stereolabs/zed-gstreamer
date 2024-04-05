#ifdef __aarch64__

#include "ArgusCapture.hpp"
#include "ArgusComponent.hpp"
#include <functional>
#include <future>

#define SHARPEN_FACTOR 10.f
#define DEFAULT_SHARPEN_VALUE 1
#define DEFAULT_DENOISER_VALUE 0.5f
#define DUAL_SESSION_CAPTURE 1
#define MAX_CAPTURE_QUEUE_SIZE 2



/**
 * Resources for Argus
 */
//https://forums.developer.nvidia.com/t/capture-problem-by-using-libargus-instead-of-v4l2/84145
// !! IMPORTANT WHEN using multiple camera GMSL !! --> MOdify nvargus-dameon service
// sudo sed -i '/^\[Service\]$/ a Environment="enableCamInfiniteTimeout=1"' /etc/systemd/system/nvargus-daemon.service

using namespace Argus;
using namespace EGLStream;
using namespace std;
using namespace oc;



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

#define STEP_ROI 32

oc::Rect convertACRegion(Argus::AcRegion reg)
{
  oc::Rect roi_;
  roi_.x = reg.left();
  roi_.y = reg.top();
  roi_.width = reg.width();
  roi_.height = reg.height();
  return roi_;
}

Argus::AcRegion convertACRegion(oc::Rect roi)
{
  int roi_x = (roi.x/STEP_ROI)*STEP_ROI;
  int roi_y = (roi.y/STEP_ROI)*STEP_ROI;
  int roi_w = (roi.width/STEP_ROI)*STEP_ROI;
  int roi_h = (roi.height/STEP_ROI)*STEP_ROI;
  Argus::AcRegion reg = Argus::AcRegion(roi_x,roi_y,roi_x+roi_w,roi_y+roi_h,1.0);
  return reg;
}


std::vector<oc::ArgusDevice> ArgusBayerCapture::getArgusDevices()
{
  std::vector<oc::ArgusDevice> out;

  /// -->Get GMLSL devices from Argus
  auto iCameraProvider = interface_cast<ICameraProvider>(ArgusProvider::getInstance()->cameraProvider);
  /// If we cannot get the camera provider, argus is probably in a broken state --> communicate to the daemon for a restart
  if (!iCameraProvider)
    {
#if USE_ZMQ_DAEMON
      int mTCPPort = ZMQ_DAEMON_SOCKET_PORT;
      ///Read service file
      CSimpleIniA service_file;
      service_file.SetUnicode();
      SI_Error rc =service_file.LoadFile("/etc/systemd/system/zed_x_daemon.service");
      if (!(rc < 0))
        {
          std::vector<std::string> elems;
          split("Service:Port", ':', elems);
          mTCPPort = atoi(service_file.GetValue(elems.front().c_str(), elems.back().c_str(), std::to_string(ZMQ_DAEMON_SOCKET_PORT).c_str()));
        }

      zmq::socket_type s_zmq_type = zmq::socket_type::push;
      std::unique_ptr<zmq::context_t> s_zmq_context;
      std::unique_ptr<zmq::socket_t> s_zmq_socket;
      s_zmq_context.reset(new zmq::context_t(1));
      std::string endpoint = std::string("tcp://127.0.0.1:")+to_string(mTCPPort); //Use tcp because icp cannot be shared between sudo /no-root and the daemon must be root
      s_zmq_socket.reset( new zmq::socket_t(*s_zmq_context.get(), s_zmq_type));
      s_zmq_socket->setsockopt( ZMQ_LINGER, 0);
      s_zmq_socket->connect(endpoint);
      usleep(100000); //Wait for connection 100ms seems enough
      //Send Message to daemon
      std::string msg = "ZEDX#"+std::to_string(0)+"#"+std::to_string(0)+"#FROZEN";
      if (s_zmq_socket.get())
        s_zmq_socket->send(zmq::message_t(msg),zmq::send_flags::dontwait);


      s_zmq_socket->close();
      ArgusProvider::DeleteInstance();
      usleep(1000000); //Wait 1sc before doing something else
#endif
      return out;
    }

  std::shared_ptr<std::vector<Argus::CameraDevice*>> devices_all;
  devices_all.reset(new std::vector<Argus::CameraDevice*>());
  Argus::Status status = iCameraProvider->getCameraDevices(devices_all.get());
  if (status!=Argus::STATUS_OK)
    return out;

  for (int k=0;k<devices_all->size();k++)
    {
      Argus::CameraDevice* device_ = devices_all->at(k);
      Argus::ICameraProperties *iCameraProperties = Argus::interface_cast<Argus::ICameraProperties>(device_);
      if (!iCameraProperties)
        continue;

      oc::ArgusDevice prop;
      prop.id = k;
      prop.badge= iCameraProperties->getModuleString();
      char syncSensorId[MAX_MODULE_STRING];
      const Ext::ISyncSensorCalibrationData* iSyncSensorCalibrationData =interface_cast<const Ext::ISyncSensorCalibrationData>(device_);
      if (iSyncSensorCalibrationData)
        iSyncSensorCalibrationData->getSyncSensorModuleId(syncSensorId, sizeof(syncSensorId));
      prop.name = std::string(syncSensorId);
      auto iCaptureSession = UniqueObj<CaptureSession>(iCameraProvider->createCaptureSession(devices_all->at(k),NULL));
      prop.available = iCaptureSession?true:false;
      out.push_back(prop);
    }
  return out;
}

ArgusBayerCapture::ArgusBayerCapture():ArgusVirtualCapture()
{
  consumer_newFrame = false;
  producer = nullptr;
  consumer = nullptr;
  width = 0;
  height  = 0;

  lock();
  mCaptureQueue.clear();
  unlock();
  opened_ = false;
  exit_ = true;

  h = new ArgusComponent();
  mHasCaptureFrame = false;
}


ARGUS_STATE ArgusBayerCapture::openCamera(const ArgusCameraConfig &config,bool recreate_daemon)
{
  mHasCaptureFrame = false;
  Argus::Status status = Argus::STATUS_UNAVAILABLE;
  mCaptureQueue.clear();
  mConfig = config;

  /// Make sure it's not already open
  if (opened_)
    return ARGUS_STATE::ALREADY_OPEN;

  //Set port according to device required
  mPort = mConfig.mDeviceId;

  ////////////////////////////
  ///// Dameon Connection ////
  ////////////////////////////
  //Recreate the daemon only if required (not the case if reboot is called)
  if (recreate_daemon)
    {
#if USE_ZMQ_DAEMON
      int mTCPPort = ZMQ_DAEMON_SOCKET_PORT;
      ///Read service file
      CSimpleIniA service_file;
      service_file.SetUnicode();
      SI_Error rc =service_file.LoadFile("/etc/systemd/system/zed_x_daemon.service");
      if (!(rc < 0))
        {
          std::vector<std::string> elems;
          split("Service:Port", ':', elems);
          mTCPPort = atoi(service_file.GetValue(elems.front().c_str(), elems.back().c_str(), std::to_string(ZMQ_DAEMON_SOCKET_PORT).c_str()));
        }
      else
        std::cout<<"[ZED-X][Warning] Failed to open zed_x service"<<std::endl;

      zmq_context.reset(new zmq::context_t(1));
      zmq_type = zmq::socket_type::push;
      std::string endpoint = std::string("tcp://127.0.0.1:")+to_string(mTCPPort); //Use tcp because icp cannot be shared between sudo /no-root and the daemon must be root
      zmq_socket.reset( new zmq::socket_t(*zmq_context.get(), zmq_type));
      zmq_socket->setsockopt( ZMQ_LINGER, 0 );
      zmq_socket->connect(endpoint);
#endif
    }

  ////////////////////////////
  ////// Camera Provider /////
  ////////////////////////////
  if (mConfig.verbose_level>0)
    std::cout<<"[ArgusCapture] Create camera provider : "<<std::endl;

  /// Create a Argus camera provider
  auto iCameraProvider = interface_cast<ICameraProvider>(ArgusProvider::getInstance()->cameraProvider);
  if (mConfig.verbose_level>0)
    std::cout<<"[ArgusCapture] Use camera provider : "<<ArgusProvider::getInstance()<<std::endl;
  if (!iCameraProvider) {
      if (mConfig.verbose_level>0)
        std::cout<<"[ArgusCapture] Invalid camera provider : "<<ArgusProvider::getInstance()<<" // "<<ArgusProvider::getInstance()->cameraProvider<<std::endl;

      //Send the broken argus socket message
      ArgusProvider::changeState(mPort,ARGUS_CAMERA_STATE::FROZEN);

      //Send Message to daemon
      std::string msg = "ZEDX#"+std::to_string(mPort)+"#"+std::to_string(0)+"#FROZEN";
#if USE_IPC_DAEMON
      ipc_sanity.send(msg,100);
#endif

#if USE_ZMQ_DAEMON
      if (zmq_socket.get())
        zmq_socket->send(zmq::message_t(msg),zmq::send_flags::dontwait);
#endif

      //Delete the instance so that it can be recreated at next open
      ArgusProvider::DeleteInstance();
      return ARGUS_STATE::INVALID_CAMERA_PROVIDER;
    }

  /// get all camera device
  h->devices.reset(new std::vector<Argus::CameraDevice*>());
  status = iCameraProvider->getCameraDevices(h->devices.get());
  if (Argus::STATUS_OK != status)
    {
      if (mConfig.verbose_level>0)
        std::cout<<"[ArgusCapture] INVALID_DEVICE_ENUMERATION"<<std::endl;
      return ARGUS_STATE::INVALID_DEVICE_ENUMERATION;
    }

  int nb_sensors_tot = -1;
  if (h->devices.get())
    nb_sensors_tot = h->devices->size();

  if (mConfig.verbose_level>0)
    std::cout<<"[ArgusCapture] Nb Sensors detected : "<<nb_sensors_tot<<std::endl;

  /// Check that we have at least 2 cameras and a pair
  if (nb_sensors_tot<=0)
    {
      return ARGUS_STATE::NO_CAMERA_AVAILABLE;
    }

  /// Set Left and Right camera device from conf ID

  if (mConfig.mDeviceId<nb_sensors_tot)
    {
      h->cameraDevice =  h->devices->at(mConfig.mDeviceId);
    }
  else
    return ARGUS_STATE::NO_CAMERA_AVAILABLE;

  ArgusProvider::changeState(mPort,ARGUS_CAMERA_STATE::OPENING);
  if (mConfig.verbose_level>0)
    std::cout<<"[ArgusCapture] Camera at Port"<<mPort<<" is opening"<<std::endl;

  if (!h->cameraDevice)
    {
      ArgusProvider::changeState(mPort,ARGUS_CAMERA_STATE::OFF);
      return ARGUS_STATE::INVALID_CAMERA_PROPERTIES;
    }

  /// Get Camera Properties to extract module information
  Argus::ICameraProperties *iCameraProperties = Argus::interface_cast<Argus::ICameraProperties>(h->cameraDevice);
  if (!iCameraProperties)
    {
      ArgusProvider::changeState(mPort,ARGUS_CAMERA_STATE::OFF);
      return ARGUS_STATE::INVALID_CAMERA_PROPERTIES;
    }

  /// Get badge name (Left and Right should be equal)

  Argus::Size2D<uint32_t> minROI = iCameraProperties->getMinAeRegionSize();
  mROIMinWidth= minROI.width();
  mROIMinHeight= minROI.height();


  if (mConfig.verbose_level>0)
    std::cout<<"[ArgusCapte] --> Create Session for Port " <<mPort<<std::endl;

  ////////////////////////////
  ////// Capture Session /////
  ////////////////////////////
  /// Create a single capture session for each sensor that compose the camera
  /// The single session will allow to acquire frame at a sync level
  /// First camera in list will be the "master" for AutoControl

  h->mCaptureSession = UniqueObj<CaptureSession>(iCameraProvider->createCaptureSession(h->cameraDevice,&status));
  if (! h->mCaptureSession.get()) {
      if (mConfig.verbose_level>0)
        std::cout<<"[ArgusCapture] --> Invalid Capture Session" <<h->mCaptureSession.get()<<" // status : "<<status<<std::endl;

      ArgusProvider::changeState(mPort,ARGUS_CAMERA_STATE::OFF);
      return ARGUS_STATE::INVALID_CAPTURE_SESSION;
    }

  /// Create a output stream settings to EGL for the capture
  auto streamSettings = UniqueObj<OutputStreamSettings>(interface_cast<ICaptureSession>(h->mCaptureSession)->createOutputStreamSettings(STREAM_TYPE_EGL));
  IOutputStreamSettings* iStreamSettings = interface_cast<IOutputStreamSettings>(streamSettings);
  IEGLOutputStreamSettings* iOutputStreamSettings= interface_cast<IEGLOutputStreamSettings>(streamSettings);
  if (!iOutputStreamSettings) {
      if (mConfig.verbose_level>0)
        std::cout<<"[ArgusCapture] --> INVALID_OUTPUT_STREAM_SETTINGS"<<std::endl;

      ArgusProvider::changeState(mPort,ARGUS_CAMERA_STATE::OFF);
      return ARGUS_STATE::INVALID_OUTPUT_STREAM_SETTINGS;
    }

  /// Create an Even resolution (only even supported)
  auto evenResolution = Argus::Size2D<uint32_t>(
        ROUND_UP_EVEN(mConfig.mWidth),
        ROUND_UP_EVEN(mConfig.mHeight)
        );

  /// set stream pixel format and resolution
  int st_a = iOutputStreamSettings->setPixelFormat(Argus::PIXEL_FMT_YCbCr_420_888);
  int st_b = iOutputStreamSettings->setResolution(evenResolution);
  int st_c = iOutputStreamSettings->setMetadataEnable(true);
  int st_d = iOutputStreamSettings->setEGLDisplay(EGL_NO_DISPLAY);
  if (st_a!=0 || st_b!=0 || st_c!=0 || st_d!=0) {
      ArgusProvider::changeState(mPort,ARGUS_CAMERA_STATE::OFF);
      return ARGUS_STATE::INVALID_OUTPUT_STREAM_SETTINGS;
    }

  /// Create EGL streams based on stream settings created above
  /// One stream per sensor

  iStreamSettings->setCameraDevice(h->cameraDevice);
  h->mStream =  UniqueObj<OutputStream>(interface_cast<ICaptureSession>(h->mCaptureSession)->createOutputStream(streamSettings.get(), &status));
  if (!h->mStream)
    {
      if (mConfig.verbose_level>0)
        std::cout<<"[ArgusCapture] --> INVALID_STREAM_CREATION"<<std::endl;
      ArgusProvider::changeState(mPort,ARGUS_CAMERA_STATE::OFF);
      return ARGUS_STATE::INVALID_STREAM_CREATION;
    }



  if (mConfig.verbose_level>0)
    std::cout<<"[ArgusCapture] --> Create Session Done" <<std::endl;


  /// Create Frame Consumer for each  stream
  h->mFrameConsumer = UniqueObj<FrameConsumer>(FrameConsumer::create(h->mStream.get(),&status));
  if (!h->mFrameConsumer.get())
    {
      if (mConfig.verbose_level>0)
        std::cout<<"[ArgusCapture] --> Invalid frame consumer " <<h->mFrameConsumer.get()<<std::endl;
      ArgusProvider::changeState(mPort,ARGUS_CAMERA_STATE::OFF);
      return ARGUS_STATE::INVALID_FRAME_CONSUMER;
    }


  /// Now create the producer thread/
  /// IMPORTANT : It must be here because it starts with a wait for connection.
  /// Connection is setup after the thread start
  exit_ = false;
  producer = new std::thread(&ArgusBayerCapture::produce,this);

  if (mConfig.verbose_level>0)
    std::cout<<"[ArgusCapture] --> Create producer Done" <<std::endl;

  /// Create the capture request (request will be trigger each frame in a "repeat" mode)
  h->capRequest = UniqueObj<Request>(interface_cast<ICaptureSession>(h->mCaptureSession)->createRequest());
  if (!h->capRequest)
    {
      ArgusProvider::changeState(mPort,ARGUS_CAMERA_STATE::OFF);
      return ARGUS_STATE::INVALID_CAPTURE_REQUEST;
    }


  if (mConfig.verbose_level>0)
    std::cout<<"[ArgusCapture] --> Create request Done" <<std::endl;


  ///// Enable the output stream created and linked to the request
  Argus::Status resReq = interface_cast<IRequest>(h->capRequest)->enableOutputStream(h->mStream.get());
  if (resReq!=Argus::STATUS_OK)
    {
      ArgusProvider::changeState(mPort,ARGUS_CAMERA_STATE::OFF);
      return ARGUS_STATE::INVALID_OUTPUT_STREAM_REQUEST;
    }


  if (mConfig.verbose_level>0)
    std::cout<<"[ArgusCapture] --> Create stream Done" <<std::endl;

  ///// Configure source settings (FPS/ AutoControl...)
  vector<SensorMode*> sensorModes;
  // 1. Get sensor mode
  iCameraProperties = interface_cast<ICameraProperties>(h->cameraDevice);
  status = iCameraProperties->getAllSensorModes(&sensorModes);
  if (Argus::STATUS_OK != status) {
      ArgusProvider::changeState(mPort,ARGUS_CAMERA_STATE::OFF);
      return ARGUS_STATE::INVALID_SOURCE_CONFIGURATION;
    }


  //2. Find sensor mode according to width/height
  int m_sensor_mode = -1;
  for (int k=0;k<sensorModes.size();k++)
    {
      ISensorMode* ssmode = Argus::interface_cast<Argus::ISensorMode>(sensorModes[k]);
      int s_width = ssmode->getResolution().width();
      int s_height = ssmode->getResolution().height();
      if (mConfig.mWidth==s_width && mConfig.mHeight == s_height)
        {
          m_sensor_mode = k;
          break;
        }

    }

  if (m_sensor_mode<0 || m_sensor_mode>=sensorModes.size())
    {
      ArgusProvider::changeState(mPort,ARGUS_CAMERA_STATE::OFF);
      return ARGUS_STATE::INVALID_SOURCE_CONFIGURATION;
    }


  //3. Set sensor mode to the source
  h->iSourceSettings = interface_cast<ISourceSettings>(interface_cast<IRequest>(h->capRequest)->getSourceSettings());
  ISourceSettings* isrcSettings = static_cast<ISourceSettings*>(h->iSourceSettings);
  status = isrcSettings->setSensorMode(sensorModes[m_sensor_mode]);
  if (Argus::STATUS_OK != status) {
      ArgusProvider::changeState(mPort,ARGUS_CAMERA_STATE::OFF);
      return ARGUS_STATE::INVALID_SOURCE_CONFIGURATION;
    }

  // 2. set frame duration
  status = isrcSettings->setFrameDurationRange(Argus::Range<uint64_t>(ONE_SECOND_NANOS/mConfig.mFPS,ONE_SECOND_NANOS/mConfig.mFPS));
  fps = mConfig.mFPS;

  // Get the digital ISP gain range and store it
  Argus::IAutoControlSettings* ac = Argus::interface_cast<Argus::IAutoControlSettings>(interface_cast<IRequest>(h->capRequest)->getAutoControlSettings());
  if (ac)
    {
      Range<float> ispDigitalGainRange = ac->getIspDigitalGainRange();
      digital_gain_limits_range.first = ispDigitalGainRange.min();
      digital_gain_limits_range.second = ispDigitalGainRange.max();
      digital_gain_current_range.first = digital_gain_limits_range.first;
      digital_gain_current_range.second = digital_gain_limits_range.second;

      ac->getToneMapCurve(RGBChannel::RGB_CHANNEL_R,&default_tone_mapping_lut_red);
      ac->getToneMapCurve(RGBChannel::RGB_CHANNEL_G,&default_tone_mapping_lut_green);
      ac->getToneMapCurve(RGBChannel::RGB_CHANNEL_B,&default_tone_mapping_lut_blue);
    }

  //3. Set Exposure range
  ISensorMode *iSensorMode = interface_cast<ISensorMode>(sensorModes[m_sensor_mode]);
  if (!iSensorMode)
    {
      ArgusProvider::changeState(mPort,ARGUS_CAMERA_STATE::OFF);
      return ARGUS_STATE::INVALID_SOURCE_CONFIGURATION;
    }
  Range<uint64_t> limitExposureTimeRange = iSensorMode->getExposureTimeRange();
  exposure_limits_range.first = limitExposureTimeRange.min()/1000ULL; //in us
  exposure_limits_range.second = std::min((unsigned long long)(ONE_SECOND_MICROS)/(fps),(unsigned long long)(limitExposureTimeRange.max()/1000ULL)); //Limit if FPS is too quick for DTS max exp (EXPMAX = 100% of FPS as most)
  exposure_current_range.first = exposure_limits_range.first;
  exposure_current_range.second = exposure_limits_range.second;
  if (exposure_limits_range.second!=limitExposureTimeRange.max()/1000ULL)
    setAutomaticExposure();


  Range<float> sensorModeAnalogGainRange = iSensorMode->getAnalogGainRange();
  analog_gain_limits_range.first = sensorModeAnalogGainRange.min();
  analog_gain_limits_range.second = sensorModeAnalogGainRange.max();
  analog_gain_current_range.first = analog_gain_limits_range.first;
  analog_gain_current_range.second = analog_gain_limits_range.second;

  //4. Set Clip Rect (if set)
  // configure stream settings
  auto iOutStreamSettings = interface_cast<IStreamSettings>(interface_cast<IRequest>(h->capRequest)->getStreamSettings(h->mStream.get()));
  if (!iOutStreamSettings) {
      ArgusProvider::changeState(mPort,ARGUS_CAMERA_STATE::OFF);
      return ARGUS_STATE::INVALID_SOURCE_CONFIGURATION;
    }

  // set stream resolution
  status = iOutStreamSettings->setSourceClipRect(Argus::Rectangle<float>(0,0,1.f,1.f));
  if (Argus::STATUS_OK != status) {
      return ARGUS_STATE::INVALID_SOURCE_CONFIGURATION;
    }

  /// Create the "stream-on" request --> in Repeat mode, acquireFrame will wait until new frame is available
  Argus::Status sts = interface_cast<ICaptureSession>(h->mCaptureSession)->repeat(h->capRequest.get());
  if (sts!=Argus::STATUS_OK)
    {
      ArgusProvider::changeState(mPort,ARGUS_CAMERA_STATE::OFF);
      return ARGUS_STATE::CANNOT_START_STREAM_REQUEST;
    }

  if (mConfig.verbose_level>0)
    std::cout<<"[ArgusCapture] --> configure stream Done" <<std::endl;


  /// Save configuration as it's OK
  width = mConfig.mWidth;
  height = mConfig.mHeight;
  nChannel = mConfig.mChannel;

  if (mConfig.verbose_level>0)
    std::cout<<"[ArgusCapture] --> Using Resolution " <<width<<"x"<<height<<"@"<<fps<<std::endl;
  internal_buffer_grab = (unsigned char*)malloc(width * height * nChannel);
  if (!use_outer_buffer)
    ptr_buffer_sdk = internal_buffer_grab;

  /// We can say that we are open
  opened_ = true;
  consumer_newFrame =false;
  consumer = new std::thread(&ArgusBayerCapture::consume,this);
  controler = new std::thread(&ArgusBayerCapture::control,this);

  if (mConfig.verbose_level>0)
    std::cout<<"[ArgusCapture] --> configure consumer Done" <<std::endl;

  //Wait to make sure we have a frame
  int timeout = 0;
  if (mConfig.verbose_level>0)
    std::cout<<"[ArgusCapture] --> Waiting for New frames" <<std::endl;

  ////////////////////////////
  ///// Dameon Comm ////
  ////////////////////////////
  std::string msg_ = "ZEDX#"+std::to_string(mPort)+"#"+std::to_string(0)+"#OPENING";
#if USE_IPC_DAEMON
  ipc_sanity.send(msg_,100);
#endif

#if USE_ZMQ_DAEMON
  if (zmq_socket.get())
    zmq::send_result_t rr = zmq_socket->send(zmq::message_t(msg_),zmq::send_flags::dontwait);
#endif

  while(!mHasCaptureFrame)
    {
      usleep(1000);
      timeout++;
      if (timeout>10000)
        break;
    }

  if (!mHasCaptureFrame)
    {
      ArgusProvider::changeState(mPort,ARGUS_CAMERA_STATE::OFF);
      return ARGUS_STATE::CAPTURE_TIMEOUT;
    }

  if (mConfig.verbose_level>0)
    std::cout<<"[ArgusCapture] --> Dispatch Request " <<std::endl;

  setDenoisingValue(DEFAULT_DENOISER_VALUE);
  setSharpening(DEFAULT_SHARPEN_VALUE);
  setAEAntiBanding(AEANTIBANDING::OFF);
  dispatchRequest();
  if (mConfig.verbose_level>0)
    std::cout<<"[ArgusCapture] --> Ready!" <<std::endl;


  return ARGUS_STATE::OK;
}

void ArgusBayerCapture::closeCamera()
{
  exit_ = true;
  opened_ = false;

  //Close Session
  h->close();


  std::string msg = "ZEDX#"+std::to_string(mPort)+"#"+std::to_string((int)0)+"#CLOSING";
#if USE_IPC_DAEMON
  ipc_sanity.send(msg,100);
#endif

#if USE_ZMQ_DAEMON
  if (zmq_socket.get())
    zmq_socket->send(zmq::message_t(msg),zmq::send_flags::dontwait);
#endif


  // Abort threads and capture
  abortCapture();
  clearBuffers();
  clearImageQueues();
  abortControlThread();

#if USE_IPC_DAEMON
  //ipc_sanity.close();
#endif

#if USE_ZMQ_DAEMON
  if (zmq_socket.get())
    {
      zmq_socket->close();
      zmq_socket.reset(0);
      zmq_context->shutdown();
      zmq_context.reset(0);
    }
#endif

}

ArgusBayerCapture::~ArgusBayerCapture()
{
  closeCamera();
  if (h) delete h;
  h=nullptr;
}

ARGUS_STATE ArgusBayerCapture::reboot()
{
  //Send Message to daemon
  std::string msg = "ZEDX#"+std::to_string(mPort)+"#"+std::to_string((int)0)+"#FROZEN";
#if USE_IPC_DAEMON
  ipc_sanity.send(msg,100);
#endif

#if USE_ZMQ_DAEMON
  if (zmq_socket.get())
    zmq_socket->send(zmq::message_t(msg),zmq::send_flags::dontwait);
#endif


  //Exit capture thread
  exit_ = true;
  opened_ = false;

  //Abort Capture, clear buffers and queue. DO NOT CLOSE CONTROLLER THREAD
  abortCapture();
  clearBuffers();
  clearImageQueues();

  //Close Session
  h->close(false); //Camera provider is no more valid... no need to stop the request
  usleep(1000000); //Wait 1S
  ArgusProvider::DeleteInstance();
  usleep(1000000); //Wait 1S
  //Re-open camera
  return openCamera(mConfig,false);
}

void ArgusBayerCapture::clearImageQueues()
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

void ArgusBayerCapture::clearBuffers()
{

  if (internal_buffer_grab)
    {
      free(internal_buffer_grab);
      internal_buffer_grab = nullptr;
    }
}

void ArgusBayerCapture::abortCapture()
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

void ArgusBayerCapture::abortControlThread()
{
  if (controler)
    {
      controler->join();
      delete controler;
      controler = nullptr;
    }

}

int ArgusBayerCapture::convert(void* frame,argusMonoImage& image)
{

  auto resolution = Argus::Size2D<uint32_t>(width,height);
  IFrame* iframe = (IFrame*)frame;
  auto iNativeBuffer = interface_cast<EGLStream::NV::IImageNativeBuffer>(iframe->getImage());
  if (fd == -1)
    {
      //JP4.6 --> 4.9
      //JP5.0 --> 5.10
#if LINUX_VERSION_CODE <= KERNEL_VERSION(5,0,0)
      //Not supported on JP4.6
      fd = iNativeBuffer->createNvBuffer(resolution,
                                            NvBufferColorFormat_ABGR32,
                                            NvBufferLayout_Pitch);
#else
      if (nChannel==3)
      {
          if (!mConfig.mSwapRB)
          fd = iNativeBuffer->createNvBuffer(resolution,NVBUF_COLOR_FORMAT_RGB,NVBUF_LAYOUT_PITCH);
          else
          fd = iNativeBuffer->createNvBuffer(resolution,NVBUF_COLOR_FORMAT_BGR,NVBUF_LAYOUT_PITCH);

      }
      else if (nChannel==4)
      {
          if (!mConfig.mSwapRB)
          fd = iNativeBuffer->createNvBuffer(resolution,NVBUF_COLOR_FORMAT_BGRA,NVBUF_LAYOUT_PITCH);
          else
          fd = iNativeBuffer->createNvBuffer(resolution,NVBUF_COLOR_FORMAT_RGBA,NVBUF_LAYOUT_PITCH);
      }
#endif
      if (fd==-1)
        {
          mArgusGrabState = ARGUS_STATE::CAPTURE_FAILURE;
          return -1;
        }
    }

  if (iNativeBuffer->copyToNvBuffer(fd) != STATUS_OK)
    {
      std::cout<<"[copyToNvBuffer] Failed"<<std::endl;
      mArgusGrabState = ARGUS_STATE::CAPTURE_FAILURE;
      return -2;
    }

  if (fd>0)
    {
      int hr = dump_dmabuf(fd,0,image);
      if (hr!=0)
      {
        std::cout<<"[dump_dmabuf] Failed"<<std::endl;
        return -3;
      }
    }

  return 0;
}


void ArgusBayerCapture::produce()
{
  mHasCaptureFrame=false;
  if (mConfig.verbose_level>2)
    std::cout<<"[ArgusCapture][Debug] : "<<" Wait for connection"<<std::endl;

  IEGLOutputStream *iStream = interface_cast<IEGLOutputStream>(h->mStream);
  if (iStream->waitUntilConnected() != STATUS_OK)
    {
      mArgusGrabState = ARGUS_STATE::CONNECTION_TIMEOUT;
      return;
    }
  if (mConfig.verbose_level>2)
    std::cout<<"[[ArgusCapture][Debug] : "<<" Wait for consumer"<<std::endl;

  IFrameConsumer* iFrameConsumer = interface_cast<IFrameConsumer>(h->mFrameConsumer);
  if (!iFrameConsumer)
    {
      mArgusGrabState = ARGUS_STATE::CONNECTION_TIMEOUT;
      return;
    }
  if (mConfig.verbose_level>2)
    std::cout<<"[ArgusCapture][Debug] : "<<" Done for connection"<<std::endl;

  bool is_running = false;
  isfrozen_ = false;
  int consecutive_timeout = 0;
  Argus::Status arg_stat ;
  while(!exit_)
    {
      //Set the timeout to 1ms
      mut_irequest.lock();
      UniqueObj<Frame> frame(iFrameConsumer->acquireFrame(100000000,&arg_stat)); //100ms timeout
      mut_irequest.unlock();
      if (frame)
        {
          // Use the IFrame interface to print out the frame number/timestamp, and
          // to provide access to the Image in the Frame.
          IFrame *iFrameLeft = interface_cast<IFrame>(frame);
          if (!iFrameLeft)
            printf("Failed to get left IFrame interface.\n");
          else
            {
              argusMonoImage newImage = {0};
              IArgusCaptureMetadata *iArgusCaptureMetadata = interface_cast<IArgusCaptureMetadata>(frame);
              CaptureMetadata *metadata = iArgusCaptureMetadata->getMetadata();
              ICaptureMetadata *iMetadata = interface_cast<ICaptureMetadata>(metadata);
              newImage.imageTimestamp =(iMetadata->getSensorTimestamp())/1000ULL; //Get in us
              newImage.frameExposureTime = (iMetadata->getSensorExposureTime())/1000ULL; //Get in 1/100 ms
              newImage.frameAnalogGain = iMetadata->getSensorAnalogGain();
              newImage.frameDigitalGain = iMetadata->getIspDigitalGain();
              int c_res= convert((void*)iFrameLeft,newImage);
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
              is_running = true;
              mArgusGrabState = ARGUS_STATE::OK;
              consecutive_timeout = 0;
              mHasCaptureFrame = true;
            }
        }
      else{
          if (is_running && !ArgusProvider::hasCameraOpening())
            {
              switch (arg_stat) {
                case Argus::STATUS_DISCONNECTED:
                case Argus::STATUS_END_OF_STREAM :
                case Argus::STATUS_CANCELLED:
                  {
                    std::cout<<"[ArgusCapture][EoF] CAM "<<mPort<<" is frozen : "<<(int)arg_stat<<std::endl;
                    isfrozen_ = true;
                    usleep(100000);
                  }
                  break;

                case Argus::STATUS_TIMEOUT:
                default:
                  {
                    consecutive_timeout++;
                    if (consecutive_timeout>20) //Normally after 1s
                      {
                        std::cout<<"[ArgusCapture][Timeout] CAM "<<mPort<<" is frozen"<<std::endl;
                        isfrozen_ = true;
                        usleep(100000);
                      }

                  }
                  break;
                }
            }
        }
      //Small wait
      usleep(10);
    }
}

void ArgusBayerCapture::control()
{
  isfrozen_=false;
  while(!exit_)
    {
      usleep(100);
      if (isfrozen_ && !ArgusProvider::hasCameraFrozen())
        {
          ArgusProvider::changeState(mPort,ARGUS_CAMERA_STATE::FROZEN);
          reboot();
          isfrozen_ = false;
        }
    }
}

void ArgusBayerCapture::consume()
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

          current_exp_time = mNewImage.frameExposureTime;
          current_analog_gain= mNewImage.frameAnalogGain;
          current_digital_gain = mNewImage.frameDigitalGain;
          free(mNewImage.imageData);
          mCaptureQueue.pop_front();
          exitCriticalSection();
          consumer_newFrame = true;
          userNewFrameEvent.notify();


          if (s_frame_count%60==0)
            {
              ///Message is formatted this way
              /// --> "ZEDX#<GMSL_PORT>#<SUB_MODEL>#<STATUS>#<FRAME_COUNT>#...."
              ///
              ///
              ArgusProvider::changeState(mPort,ARGUS_CAMERA_STATE::RUNNING);
              std::string msg = "ZEDX#"+std::to_string(mPort)+"#"+std::to_string(0)+"#RUN#"+std::to_string(s_frame_count);

#if USE_IPC_DAEMON
              ipc_sanity.send(msg,100);
#endif

#if USE_ZMQ_DAEMON
              if (zmq_socket.get())
                {
                  zmq::send_result_t rr = zmq_socket->send(zmq::message_t(msg),zmq::send_flags::dontwait);
                }
#endif
            }
          s_frame_count++;


        }
      else
        usleep(10);

    }
}


///////////////////////////////////////////////////////////////////////////////////////////////////////////////
//////////////////////////////// EXPOSURE CONTROL/REQUEST /////////////////////////////////////////////////////
///////////////////////////////////////////////////////////////////////////////////////////////////////////////

uint64_t ArgusBayerCapture::getFrameExposureTime()
{
  if (!opened_)
    return 0;

  return current_exp_time;
}

int ArgusBayerCapture::getFrameExposurePercent()
{
  if (!opened_)
    return 0;

  float pp_ = 100.f * (float)(current_exp_time - exposure_limits_range.first)/ (float)(exposure_limits_range.second - exposure_limits_range.first);
  return (int)round(pp_);
}

int ArgusBayerCapture::setFrameExposureRange(uint64_t exp_low,uint64_t exp_high)
{
  if (!opened_)
    return -1;
  Argus::Status status = Argus::STATUS_DISCONNECTED;
  ISourceSettings* isrcSettings = static_cast<ISourceSettings*>(h->iSourceSettings);
  if (isrcSettings)
    {
      if (exp_low<=0 || exp_high<=0)
        {
          uint64_t target_low = exp_low;
          uint64_t target_high = exp_high;

          if (exp_low<=0)
            target_low = exposure_limits_range.first;
          if (exp_high<=0)
            target_high = exposure_limits_range.second;


          status = isrcSettings->setExposureTimeRange( Range<uint64_t>(target_low*1000ULL,target_high*1000ULL));
          if (status==Argus::STATUS_OK)
            {
              exposure_current_range.first=target_low;
              exposure_current_range.second=target_high;
            }
        }
      else
        {
          status = isrcSettings->setExposureTimeRange( Range<uint64_t>((exp_low)*1000ULL,(exp_high)*1000ULL));
          if (status==Argus::STATUS_OK && exp_low!=exp_high)
            {
              exposure_current_range.first=exp_low;
              exposure_current_range.second=exp_high;
            }
        }
    }

  return (int)status;
}

int ArgusBayerCapture::getFrameExposureRange(uint64_t& exp_low,uint64_t& exp_high)
{
  if (!opened_)
    return -1;


  exp_low=exposure_current_range.first;
  exp_high=exposure_current_range.second;
  return 0;
}

int ArgusBayerCapture::getExposureLimits(uint64_t& min_exp,uint64_t& max_exp)
{
  if (!opened_)
    return -1;

  min_exp = exposure_limits_range.first;
  max_exp = exposure_limits_range.second;
  return 0;
}

int ArgusBayerCapture::setManualExposure(int percent)
{
  if (!opened_)
    return -1;

  uint64_t time = exposure_limits_range.first + percent * float(exposure_limits_range.second - exposure_limits_range.first) / 100.f;
  return setFrameExposureRange(time,time);
}

int ArgusBayerCapture::setManualTimeExposure(uint64_t time)
{
  if (!opened_)
    return -1;

  return setFrameExposureRange(time,time);
}

int ArgusBayerCapture::setAutomaticExposure()
{
  int stt =  setFrameExposureRange(exposure_current_range.first,exposure_current_range.second);
  return stt;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////
//////////////////////////////// ANALOG GAIN (SENSOR) CONTROL/REQUEST /////////////////////////////////////////
///////////////////////////////////////////////////////////////////////////////////////////////////////////////

float ArgusBayerCapture::getAnalogFrameGain()
{
  if (!opened_)
    return -1;

  return current_analog_gain;
}

int ArgusBayerCapture::getAnalogFrameGainPercent()
{
  if (!opened_)
    return 0;

  float pp_ = 100.f * (float)(current_analog_gain - analog_gain_limits_range.first)/ (float)(analog_gain_limits_range.second - analog_gain_limits_range.first);
  return (int)round(pp_);
}


int ArgusBayerCapture::setAnalogFrameGainRange(float gain_low,float gain_high)
{
  if (!opened_)
    return -1;

  ISourceSettings* isrcSettings = static_cast<ISourceSettings*>(h->iSourceSettings);
  Argus::Status status = Argus::STATUS_DISCONNECTED;
  if (isrcSettings)
    {
      if (gain_low<=0 || gain_high<=0)
        {
          float target_low = gain_low;
          float target_high = gain_high;

          if (gain_low<=0)
            target_low = analog_gain_limits_range.first;
          if (gain_high<=0)
            target_high = analog_gain_limits_range.second;

          status = isrcSettings->setGainRange( Range<float>(target_low,target_high));
          if (status == Argus::STATUS_OK)
            {
              analog_gain_current_range.first = target_low;
              analog_gain_current_range.second = target_high;
            }
        }
      else
        {
          status = isrcSettings->setGainRange( Range<float>(gain_low,gain_high));
          if (status == Argus::STATUS_OK && gain_low!=gain_high)
            {
              analog_gain_current_range.first=gain_low;
              analog_gain_current_range.second=gain_high;
            }
        }
    }
  status = isrcSettings->setGainRange(Range<float>(gain_low,gain_high));
  return (int)status;
}

int ArgusBayerCapture::getAnalogFrameGainRange(float& sgain_low,float& sgain_high)
{
  if (!opened_)
    return -1;

  sgain_low=analog_gain_current_range.first;
  sgain_high=analog_gain_current_range.second;
  return 0;

}

int ArgusBayerCapture::getAnalogGainLimits(float& min_gain,float& max_gain)
{
  if (!opened_)
    return -1;

  min_gain = analog_gain_limits_range.first;
  max_gain = analog_gain_limits_range.second;
  return 0;
}

int ArgusBayerCapture::setAutomaticAnalogGain()
{
  int stt =  setAnalogFrameGainRange(analog_gain_current_range.first,analog_gain_current_range.second);
  return stt;
}

int ArgusBayerCapture::setManualAnalogGain(int percent)
{
  if (!opened_)
    return -1;

  float real_gain_ = analog_gain_limits_range.first + percent * (analog_gain_limits_range.second - analog_gain_limits_range.first) / 100.f;
  return setAnalogFrameGainRange(real_gain_,real_gain_);
}

int ArgusBayerCapture::setManualAnalogGainReal(int db)
{
  if (!opened_)
    return -1;

  return setAnalogFrameGainRange((float)db/1000.f,(float)db/1000.f);
}


///////////////////////////////////////////////////////////////////////////////////////////////////////////////
//////////////////////////////// DIGITAL GAIN (ISP) CONTROL/REQUEST ///////////////////////////////////////////
///////////////////////////////////////////////////////////////////////////////////////////////////////////////

float ArgusBayerCapture::getDigitalFrameGain()
{
  return current_digital_gain;
}

int ArgusBayerCapture::setDigitalFrameGainRange(float gain_low, float gain_high)
{
  if (!opened_)
    return -1;


  Argus::IAutoControlSettings* ac = Argus::interface_cast<Argus::IAutoControlSettings>(interface_cast<IRequest>(h->capRequest)->getAutoControlSettings());
  Argus::Status status = Argus::STATUS_DISCONNECTED;
  if (ac)
    {
      if (gain_low<=0 || gain_high<=0)
        {
          float target_low = gain_low;
          float target_high = gain_high;

          if (gain_low<=0)
            target_low = digital_gain_limits_range.first;
          if (gain_high<=0)
            target_high = digital_gain_limits_range.second;

          status = ac->setIspDigitalGainRange( Range<float>(target_low,target_high));
          if (status == Argus::STATUS_OK)
            {
              digital_gain_current_range.first = target_low;
              digital_gain_current_range.second = target_high;
            }
        }
      else
        {
          status = ac->setIspDigitalGainRange( Range<float>(gain_low,gain_high));
          if (status == Argus::STATUS_OK && gain_low!=gain_high)
            {
              digital_gain_current_range.first = gain_low;
              digital_gain_current_range.second = gain_high;
            }
        }
    }
  return (int)status;
}

int ArgusBayerCapture::getDigitalFrameGainRange(float& dgain_low,float& dgain_high)
{
  if (!opened_)
    return -1;

  dgain_low=digital_gain_current_range.first;
  dgain_high=digital_gain_current_range.second;
  return 0;
}

int ArgusBayerCapture::getDigitalGainLimits(float& min_gain,float& max_gain)
{
  if (!opened_)
    return -1;

  min_gain = digital_gain_limits_range.first;
  max_gain = digital_gain_limits_range.second;
  return 0;
}

int ArgusBayerCapture::setAutomaticDigitalGain()
{
  int stt =  setDigitalFrameGainRange(digital_gain_current_range.first,digital_gain_current_range.first);
  return stt;
}

int ArgusBayerCapture::setManualDigitalGain(int percent)
{
  if (!opened_)
    return -1;

  float real_gain_ = digital_gain_limits_range.first + percent * (digital_gain_limits_range.second - digital_gain_limits_range.first) / 100.f;
  return setDigitalFrameGainRange(real_gain_,real_gain_);
}

int ArgusBayerCapture::setManualDigitalGainReal(int fact)
{
  if (!opened_)
    return -1;

  return setDigitalFrameGainRange(fact,fact);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////
//////////////////////////////// White Balance(ISP) CONTROL/REQUEST ///////////////////////////////////////////
///////////////////////////////////////////////////////////////////////////////////////////////////////////////
int ArgusBayerCapture::setManualWhiteBalance(uint32_t color_temperature_)
{
  if (!opened_)
    return -1;

  Argus::IAutoControlSettings* ac = Argus::interface_cast<Argus::IAutoControlSettings>(interface_cast<IRequest>(h->capRequest)->getAutoControlSettings());
  float whiteBalanceGains[4]={0};
  estimageRGGBGainFromColorTemperature_v2(color_temperature_,1.5,whiteBalanceGains[0],whiteBalanceGains[1],whiteBalanceGains[2],whiteBalanceGains[3]);
  color_temperature = color_temperature_;
  isManualWhiteBalance = true;
  Argus::Status status = Argus::STATUS_DISCONNECTED;
  if (ac)
    {
      BayerTuple<float> bayerGains(whiteBalanceGains[0], whiteBalanceGains[1], whiteBalanceGains[2], whiteBalanceGains[3]);
      status = ac->setWbGains(bayerGains);
      status =ac->setAwbMode(AWB_MODE_MANUAL);
      mAutoWhiteBalanceSetting = 0;
    }
  return (int)status;
}

uint32_t ArgusBayerCapture::getManualWhiteBalance()
{
  if (!opened_)
    return 0;
  return color_temperature;
}

int ArgusBayerCapture::setAutomaticWhiteBalance(int val)
{
  if (!opened_)
    return -1;

  isManualWhiteBalance = false;
  Argus::Status status = Argus::STATUS_DISCONNECTED;
  Argus::IAutoControlSettings* ac = Argus::interface_cast<Argus::IAutoControlSettings>(interface_cast<IRequest>(h->capRequest)->getAutoControlSettings());
  if (ac)
    {
      isManualWhiteBalance = false;
      switch (val)
        {
        case 0 :
          status =ac->setAwbMode(AWB_MODE_MANUAL);
          isManualWhiteBalance = true;
          break;

        case 1 :
          status =ac->setAwbMode(AWB_MODE_AUTO);
          break;
        case 2 :
          status =ac->setAwbMode(AWB_MODE_INCANDESCENT);
          break;
        case 3 :
          status =ac->setAwbMode(AWB_MODE_FLUORESCENT);
          break;
        case 4 :
          status =ac->setAwbMode(AWB_MODE_WARM_FLUORESCENT);
          break;
        case 5 :
          status =ac->setAwbMode(AWB_MODE_DAYLIGHT);
          break;
        case 6 :
          status =ac->setAwbMode(AWB_MODE_CLOUDY_DAYLIGHT);
          break;
        case 7 :
          status =ac->setAwbMode(AWB_MODE_TWILIGHT);
          break;
        }
      mAutoWhiteBalanceSetting=val;
    }

  return (int)status;
}

int ArgusBayerCapture::getAutomaticWhiteBalanceStatus()
{
  if (!opened_)
    return -1;
  return mAutoWhiteBalanceSetting;
}



///////////////////////////////////////////////////////////////////////////////////////////////////////////////
////////////////////////////////     Edge Enhancement/ Sharpening       ///////////////////////////////////////
///////////////////////////////////////////////////////////////////////////////////////////////////////////////

int ArgusBayerCapture::setSharpening(float value)
{

  if (!opened_)
    return -1;

  Argus::Status status = Argus::STATUS_DISCONNECTED;
  Argus::IEdgeEnhanceSettings *iEdgeEnhanceSettings = Argus::interface_cast<Argus::IEdgeEnhanceSettings>(h->capRequest);
  if (iEdgeEnhanceSettings)
    {
      clamp<float>(value,0,1.0);
      iEdgeEnhanceSettings->setEdgeEnhanceMode(Argus::EDGE_ENHANCE_MODE_HIGH_QUALITY);
      iEdgeEnhanceSettings->setEdgeEnhanceStrength(value);
    }
  return (int)status;
}

float ArgusBayerCapture::getSharpening()
{
  if (!opened_)
    return -1.0;

  float sharpen = 0;
  Argus::IEdgeEnhanceSettings *iEdgeEnhanceSettings = Argus::interface_cast<Argus::IEdgeEnhanceSettings>(h->capRequest);
  if (iEdgeEnhanceSettings)
    sharpen= iEdgeEnhanceSettings->getEdgeEnhanceStrength();

  return sharpen;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////
/////////////////////////////////   Saturation(ISP) CONTROL/REQUEST ///////////////////////////////////////////
///////////////////////////////////////////////////////////////////////////////////////////////////////////////
int ArgusBayerCapture::setColorSaturation(float saturation)
{
  if (!opened_)
    return -1;
  Argus::Status status = Argus::STATUS_DISCONNECTED;
  Argus::IAutoControlSettings* ac = Argus::interface_cast<Argus::IAutoControlSettings>(interface_cast<IRequest>(h->capRequest)->getAutoControlSettings());

  if (ac)
    {
      ac->setColorSaturationEnable(true);
      clamp<float>(saturation,0.f,2.f);
      ac->setColorSaturation(saturation);
    }
  return (int)status;
}

float ArgusBayerCapture::getColorSaturation()
{
  if (!opened_)
    return -1;

  Argus::IAutoControlSettings* ac = Argus::interface_cast<Argus::IAutoControlSettings>(interface_cast<IRequest>(h->capRequest)->getAutoControlSettings());
  float saturation = 0;
  if (ac)
    saturation = ac->getColorSaturation();

  return saturation;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////
/////////////////////////////////   Exposure EV (ISP) CONTROL/REQUEST /////////////////////////////////////////
///////////////////////////////////////////////////////////////////////////////////////////////////////////////
int ArgusBayerCapture::setExposureCompensation(float ev)
{
  if (!opened_)
    return -1;
  Argus::Status status = Argus::STATUS_DISCONNECTED;
  Argus::IAutoControlSettings* ac = Argus::interface_cast<Argus::IAutoControlSettings>(interface_cast<IRequest>(h->capRequest)->getAutoControlSettings());

  clamp<float>(ev,-2.0,2.0);
  if (ac)
    ac->setExposureCompensation(ev);

  dispatchRequest();
  return (int)status;
}

float ArgusBayerCapture::getExposureCompensation()
{
  if (!opened_)
    return -10.0;

  float exp_t_value = -10.0;
  Argus::IAutoControlSettings* ac = Argus::interface_cast<Argus::IAutoControlSettings>(interface_cast<IRequest>(h->capRequest)->getAutoControlSettings());
  if (ac)
    exp_t_value = ac->getExposureCompensation();
  return exp_t_value;
}

//////////////////////////////////
///////////AEaNtibanding//////////
//////////////////////////////////
int ArgusBayerCapture::setAEAntiBanding(AEANTIBANDING mode)
{
  if (!opened_)
    return -1;

  Argus::Status status = Argus::STATUS_DISCONNECTED;
  Argus::IAutoControlSettings* ac = Argus::interface_cast<Argus::IAutoControlSettings>(interface_cast<IRequest>(h->capRequest)->getAutoControlSettings());
  if (ac)
    {
      switch (mode)
        {
        case AEANTIBANDING::OFF :
          ac->setAeAntibandingMode(AE_ANTIBANDING_MODE_OFF);
          break;

        case AEANTIBANDING::AUTO :
          ac->setAeAntibandingMode(AE_ANTIBANDING_MODE_AUTO);
          break;

        case AEANTIBANDING::HZ50 :
          ac->setAeAntibandingMode(AE_ANTIBANDING_MODE_50HZ);
          break;

        case AEANTIBANDING::HZ60 :
          ac->setAeAntibandingMode(AE_ANTIBANDING_MODE_60HZ);
          break;

        }

    }


  dispatchRequest();
  return (int)status;
}




///////////////////////////////////////////////////////////////////////////////////////////////////////////////
/////////////////////////////////   Denoiser (ISP) CONTROL/REQUEST ///////////////////////////////////////////
///////////////////////////////////////////////////////////////////////////////////////////////////////////////
int ArgusBayerCapture::setDenoisingValue(float value)
{
  if (!opened_)
    return -1;

  Argus::Status status = Argus::STATUS_DISCONNECTED;
  Argus::IDenoiseSettings *iDenoiserSettings = Argus::interface_cast<Argus::IDenoiseSettings>(h->capRequest);
  if (iDenoiserSettings)
    {
      clamp<float>(value,0.f,1.f);
      status = iDenoiserSettings->setDenoiseMode(DENOISE_MODE_HIGH_QUALITY);
      status = iDenoiserSettings->setDenoiseStrength(value);
    }

  dispatchRequest();
  return (int)status;
}

float ArgusBayerCapture::getDenoisingValue(int side)
{
  if (!opened_)
    return -1;

  float value_= -1.f;
  Argus::IDenoiseSettings *iDenoiserSettings = Argus::interface_cast<Argus::IDenoiseSettings>(h->capRequest);
  if (iDenoiserSettings)
    value_ = iDenoiserSettings->getDenoiseStrength();

  return value_;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////
///////////////////////////////////   ToneMapping CONTROL/REQUEST   ///////////////////////////////////////////
///////////////////////////////////////////////////////////////////////////////////////////////////////////////
int ArgusBayerCapture::getToneMappingCurveSize(int channel)
{
  if (!opened_)
    return -1;

  int size = 0;
  Argus::IAutoControlSettings* ac = Argus::interface_cast<Argus::IAutoControlSettings>(interface_cast<IRequest>(h->capRequest)->getAutoControlSettings());
  if (ac)
    {
      size = ac->getToneMapCurveSize(static_cast<RGBChannel>(channel));
    }
  return size;
}

int ArgusBayerCapture::setToneMappingFromGamma(int channel, float gamma)
{
  if (!opened_)
    return -1;
  // return 0;

  if (gamma<1.5)
    gamma = 1.5;
  if (gamma>3.25)
    gamma = 3.25;

  Argus::Status status = Argus::STATUS_DISCONNECTED;
  Argus::IAutoControlSettings* ac = Argus::interface_cast<Argus::IAutoControlSettings>(interface_cast<IRequest>(h->capRequest)->getAutoControlSettings());
  if (ac)
    {

      int size_ = default_tone_mapping_lut_red.size();

      for (int i=0;i<size_;i++)
        {
          float linear_val =(float)i/(float)size_;
          float remapped_val = std::pow(linear_val,1.0/gamma);
          default_tone_mapping_lut_red[i] = remapped_val;
        }
      status = ac->setToneMapCurveEnable(true);
      status = ac->setToneMapCurve(static_cast<RGBChannel>(channel), default_tone_mapping_lut_red);
      current_gamma = gamma;
    }

  return (int)status;
}

float ArgusBayerCapture::getGamma()
{
  return current_gamma;
}


///////////////////////////////////////////////////////////////////////////////////////////////////////////////
///////////////////////////////////   AEC ROI    ///////////////////////////////////////////
///////////////////////////////////////////////////////////////////////////////////////////////////////////////
int ArgusBayerCapture::setROIforAECAGC(oc::Rect roi)
{
  if (!opened_)
    return -1;

  Argus::Status status = Argus::STATUS_DISCONNECTED;
  Argus::IAutoControlSettings* ac = Argus::interface_cast<Argus::IAutoControlSettings>(interface_cast<IRequest>(h->capRequest)->getAutoControlSettings());
  if (ac)
    {
      if (roi.width<mROIMinWidth || roi.height<mROIMinHeight)
        return Argus::STATUS_CANCELLED;

      vector<AcRegion> aec_region;
      AcRegion region =  convertACRegion(roi);
      if (region.left()+region.width()>=width || region.top()+region.height()>=height)
        return Argus::STATUS_CANCELLED;

      aec_region.push_back(region);
      status = ac->setAeRegions(aec_region);
    }
  dispatchRequest();
  return (int)status;
}

int ArgusBayerCapture::resetROIforAECAGC()
{
  if (!opened_)
    return -1;

  Argus::Status status = Argus::STATUS_DISCONNECTED;
  Argus::IAutoControlSettings* ac = Argus::interface_cast<Argus::IAutoControlSettings>(interface_cast<IRequest>(h->capRequest)->getAutoControlSettings());
  if (ac)
    {
      vector<AcRegion> aec_region;
      //Set an empty vector clears the regions
      status = ac->setAeRegions(aec_region);
    }
  dispatchRequest();
  return (int)status;
}

int ArgusBayerCapture::getROIforAECAGC(oc::Rect &roi)
{
  if (!opened_)
    return -1;

  Argus::Status status = Argus::STATUS_DISCONNECTED;
  Argus::IAutoControlSettings* ac = Argus::interface_cast<Argus::IAutoControlSettings>(interface_cast<IRequest>(h->capRequest)->getAutoControlSettings());
  if (ac)
    {
      vector<AcRegion> aec_region;
      status = ac->getAeRegions(&aec_region);
      if (aec_region.size()==1)
        {
          AcRegion reg = aec_region[0];
          roi = convertACRegion(reg);
        }
      else
        status = Argus::STATUS_INVALID_PARAMS;
    }
  dispatchRequest();
  return (int)status;
}



///////////////////////////////////////////////////////////////////////////////////////////////////////////////
//////////////////////////////// UTILS/INTERNAL  //////////////////////////////////////////////////////////////
///////////////////////////////////////////////////////////////////////////////////////////////////////////////
///
///

void ArgusBayerCapture::dispatchRequest()
{
  mut_irequest.lock();
  auto iCaptureSession = interface_cast<ICaptureSession>(h->mCaptureSession);
  iCaptureSession->stopRepeat();
  iCaptureSession->repeat(h->capRequest.get());
  mut_irequest.unlock();
}


int ArgusBayerCapture::dump_dmabuf(int dmabuf_fd,
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


void ArgusBayerCapture::lock()
{
  mut_internal.lock();

}

void ArgusBayerCapture::unlock()
{
  mut_internal.unlock();
}



/// Inspired from https://tannerhelland.com/2012/09/18/convert-temperature-rgb-algorithm-code.html
void ArgusBayerCapture::estimageRGGBGainFromColorTemperature(uint32_t KelvinT, float factor,float& r, float &g_even,float&g_odd,float &b)
{
  double tmpCalc;

  uint32_t tmpKelvin = KelvinT;

  //Temperature must fall between 1000 and 40000 degrees
  if(tmpKelvin < 1000)
    tmpKelvin = 1000;
  if(tmpKelvin > 40000)
    tmpKelvin = 40000;


  //All calculations require tmpKelvin \ 100, so only do the conversion once
  tmpKelvin = tmpKelvin / 100;

  //Calculate each color in turn
  //First: red
  if (tmpKelvin <= 66)
    r = 255;
  else
    {
      //Note: the R-squared value for this approximation is .988
      tmpCalc = tmpKelvin - 60;
      tmpCalc = 329.698727446 * pow(tmpCalc , -0.1332047592);
      r = tmpCalc;
      if(r < 0) r = 0;
      if(r > 255) r = 255;
    }

  //Second: green
  if(tmpKelvin <= 66) {
      //Note: the R-squared value for this approximation is .996
      tmpCalc = tmpKelvin;
      tmpCalc = 99.4708025861 * log(tmpCalc) - 161.1195681661;
      g_even = tmpCalc;
      g_odd = tmpCalc;

      if (g_even < 0) g_even = 0;
      if (g_odd < 0) g_odd = 0;
      if (g_even >255) g_even = 255;
      if (g_even > 255) g_odd = 255;
    }
  else
    {
      //Note: the R-squared value for this approximation is .987
      tmpCalc = tmpKelvin - 60;
      tmpCalc = 288.1221695283 * pow(tmpCalc ,-0.0755148492);
      g_even = tmpCalc;
      g_odd = tmpCalc;
      if (g_even < 0) g_even = 0;
      if (g_odd < 0) g_odd = 0;
      if (g_even >255) g_even = 255;
      if (g_even > 255) g_odd = 255;
    }

  //Third: blue
  if (tmpKelvin >= 66)
    b = 255;
  else if( tmpKelvin <= 19)
    b = 0;
  else
    {
      //Note: the R-squared value for this approximation is .998
      tmpCalc = tmpKelvin - 10;
      tmpCalc = 138.5177312231 * log(tmpCalc) - 305.0447927307;

      b = tmpCalc;
      if (b < 0) b = 0;
      if (b > 255) b = 255;
    }

  r/=127;
  g_even/=127;
  g_odd/=127;
  b/=127;

  float res_r = (r-1.0)/factor;
  float res_g_even = (g_even-1.0)/(2*factor);//factor;
  float res_g_odd = (g_odd-1.0)/(2*factor);
  float res_b = (b-1.0)/factor;


  r = 1.0+res_r;
  g_even = 1.0+res_g_even;
  g_odd = 1.0+res_g_odd;
  b = 1.0+res_b;
  return;
}

static const double XYZ_to_RGB[3][3] = {
  { 3.24071,	-0.969258,  0.0556352 },
  { -1.53726,	1.87599,    -0.203996 },
  { -0.498571,	0.0415557,  1.05707 }
};


//Based on https://github.com/sergiomb2/ufraw/blob/1aec313/ufraw_routines.c#L246-L294
void ArgusBayerCapture::estimageRGGBGainFromColorTemperature_v2(uint32_t KelvinT, float factor,float& r, float &g_even,float&g_odd,float &b)
{
  int c;
  float RGB[3];
  double xD, yD, X, Y, Z, max;
  double T = KelvinT;
  if (T<1400)
    T=1400;
  if (T>12000)
    T=12000;

  // Fit for CIE Daylight illuminant
  if (T <= 4000) {
      xD = 0.27475e9 / (T * T * T) - 0.98598e6 / (T * T) + 1.17444e3 / T + 0.145986;
    } else if (T <= 7000) {
      xD = -4.6070e9 / (T * T * T) + 2.9678e6 / (T * T) + 0.09911e3 / T + 0.244063;
    } else {
      xD = -2.0064e9 / (T * T * T) + 1.9018e6 / (T * T) + 0.24748e3 / T + 0.237040;
    }
  yD = -3 * xD * xD + 2.87 * xD - 0.275;

  // Fit for Blackbody using CIE standard observer function at 2 degrees
  //xD = -1.8596e9/(T*T*T) + 1.37686e6/(T*T) + 0.360496e3/T + 0.232632;
  //yD = -2.6046*xD*xD + 2.6106*xD - 0.239156;

  // Fit for Blackbody using CIE standard observer function at 10 degrees
  //xD = -1.98883e9/(T*T*T) + 1.45155e6/(T*T) + 0.364774e3/T + 0.231136;
  yD = -2.35563*xD*xD + 2.39688*xD - 0.196035;

  X = xD / yD;
  Y = 1;
  Z = (1 - xD - yD) / yD;
  max = 0;
  for (c = 0; c < 3; c++) {
      RGB[c] = X * XYZ_to_RGB[0][c] + Y * XYZ_to_RGB[1][c] + Z * XYZ_to_RGB[2][c];
      if (RGB[c] > max) max = RGB[c];
    }
  for (c = 0; c < 3; c++)
    RGB[c] = RGB[c] / max;


  //std::cout<<" RGB : "<<RGB[0]<<" , "<<RGB[1]<<" , "<<RGB[2]<<std::endl;

  //rescale
  double avg = (RGB[0]+RGB[1]+RGB[2])/3.0;



  r = RGB[0]*255;
  g_even = RGB[1]*255;
  g_odd = RGB[1]*255;
  b = RGB[2]*255;


  r/=127;
  g_even/=127;
  g_odd/=127;
  b/=127;

  float res_r = (r-1.0)/factor;
  float res_g_even = (g_even-1.0)/(2*factor);//factor;
  float res_g_odd = (g_odd-1.0)/(2*factor);
  float res_b = (b-1.0)/factor;


  r = 1.0+res_r;
  g_even = 1.0+res_g_even;
  g_odd = 1.0+res_g_odd;
  b = 1.0+res_b;
  return;

}


#endif
