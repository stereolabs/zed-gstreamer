#ifndef __ARGUS_CAPTURE_H
#define __ARGUS_CAPTURE_H

#ifdef __aarch64__
#include <cstdint>
#include <vector>
#include <memory>
#include <chrono>
#include <thread>
#include <mutex>
#include <deque>
#include <condition_variable>
#include <mutex>
#include <atomic>
#include <stdio.h>
#include <iostream>
#include <stdlib.h>

#define WIDTH_IDX 0
#define HEIGHT_IDX 1
#define ONE_SECOND_NANOS 1000000000
#define ONE_SECOND_MICROS 1000000

#define USE_ZMQ_DAEMON 0
#if USE_ZMQ_DAEMON
#include "zmq/zmq.hpp"
#endif

namespace oc
{

struct Rect{
  int x;
  int y;
  int width;
  int height;
};

struct ArgusDevice
{
   int id =0;
   bool available = false;
   std::string badge;
   std::string name;

};

class PollEvent
{
public:
	PollEvent()
	{
		event_signaled = false;
	}
	~PollEvent()
	{

	}
	inline void notify()
	{
		std::lock_guard<std::mutex> lk(mutex_poll);
		event_signaled = true;
		cv_poll.notify_all();
	}

	inline bool poll(int timeout_ms)
	{
		std::unique_lock<std::mutex> lk(mutex_poll);
		bool isFinishedWaiting = true;

		if (!event_signaled)
			isFinishedWaiting = cv_poll.wait_for(lk, std::chrono::milliseconds(timeout_ms), [this] {return event_signaled; });

		event_signaled = false;
		return isFinishedWaiting;
	}
private:
	std::condition_variable cv_poll;
	std::mutex mutex_poll;
	bool event_signaled;
};


class ArgusComponent;
class ArgusCameraConfig
{
public:
    void setStreamResolution(uint32_t w, uint32_t h) {
      mWidth = w;
      mHeight=h;
    }
    void setFPS(uint32_t fps_) { mFPS = fps_; }
    void setDeviceID(uint32_t id_) {mDeviceId = id_;}
    void setChannels(int c_) {mChannel = c_;}

    uint32_t mDeviceId;
    uint32_t mWidth;
    uint32_t mHeight;
    uint32_t mFPS;
    int mChannel = 4; //Only 4 supported (BGRA or RGBA)/ 3 not yet supported
    bool mSwapRB = false; //swap for RGB(A) or BGR(A) output
    int verbose_level = 0;
};

typedef struct {
    uint64_t imageTimestamp=0; // in us
    uint64_t frameExposureTime =0; // in us
    float frameAnalogGain =0;
    float frameDigitalGain= 0;
    int pitch = 0;
    uint8_t *imageData = nullptr;
} argusMonoImage;



enum class ARGUS_STATE {
    OK,
    ALREADY_OPEN,
    I2C_COMM_FAILED,
    INVALID_CAMERA_PROVIDER,
    INVALID_DEVICE_ENUMERATION,
    NO_CAMERA_AVAILABLE,
    INVALID_CAMERA_PROPERTIES,
    INVALID_CAMERA_SN,
    INVALID_CAPTURE_SESSION,
    INVALID_OUTPUT_STREAM_SETTINGS,
    INVALID_STREAM_CREATION,
    INVALID_FRAME_CONSUMER,
    INVALID_CAPTURE_REQUEST,
    INVALID_OUTPUT_STREAM_REQUEST,
    INVALID_SOURCE_CONFIGURATION,
    CANNOT_START_STREAM_REQUEST,
    CONNECTION_TIMEOUT,
    CAPTURE_TIMEOUT,
    CAPTURE_FAILURE,
    CAPTURE_UNSYNC,
    CAPTURE_CONVERT_FAILURE,
    UNSUPPORTED_FCT,
    UNKNOWN
};

inline std::string ARGUS_STATE2str(oc::ARGUS_STATE state) {  
    std::string out;
    switch (state) {
        case oc::ARGUS_STATE::OK:
            out = "OK";
            break;
        case oc::ARGUS_STATE::ALREADY_OPEN:
                    out = "ALREADY_OPEN";
            break;
        case oc::ARGUS_STATE::I2C_COMM_FAILED:
                    out = "I2C_COMM_FAILED";
            break;
        case oc::ARGUS_STATE::INVALID_CAMERA_PROVIDER:
                    out = "INVALID_CAMERA_PROVIDER";
            break;
        case oc::ARGUS_STATE::INVALID_DEVICE_ENUMERATION:
                    out = "INVALID_DEVICE_ENUMERATION";
            break;
        case oc::ARGUS_STATE::NO_CAMERA_AVAILABLE:
                    out = "NO_CAMERA_AVAILABLE";
            break;
        case oc::ARGUS_STATE::INVALID_CAMERA_PROPERTIES:
                    out = "INVALID_CAMERA_PROPERTIES";
            break;
        case oc::ARGUS_STATE::INVALID_CAMERA_SN:
                    out = "INVALID_CAMERA_SN";
            break;
        case oc::ARGUS_STATE::INVALID_CAPTURE_SESSION:
                    out = "INVALID_CAPTURE_SESSION";
            break;
        case oc::ARGUS_STATE::INVALID_OUTPUT_STREAM_SETTINGS:
                    out = "INVALID_OUTPUT_STREAM_SETTINGS";
            break;
        case oc::ARGUS_STATE::INVALID_STREAM_CREATION:
                    out = "INVALID_STREAM_CREATION";
            break;
        case oc::ARGUS_STATE::INVALID_FRAME_CONSUMER:
                    out = "INVALID_FRAME_CONSUMER";
            break;
        case oc::ARGUS_STATE::INVALID_CAPTURE_REQUEST:
                    out = "INVALID_CAPTURE_REQUEST";
            break;
        case oc::ARGUS_STATE::INVALID_OUTPUT_STREAM_REQUEST:
                    out = "INVALID_OUTPUT_STREAM_REQUEST";
            break;
        case oc::ARGUS_STATE::INVALID_SOURCE_CONFIGURATION:
                    out = "INVALID_SOURCE_CONFIGURATION";
            break;
        case oc::ARGUS_STATE::CANNOT_START_STREAM_REQUEST:
                    out = "CANNOT_START_STREAM_REQUEST";
            break;
        case oc::ARGUS_STATE::CONNECTION_TIMEOUT:
                    out = "CONNECTION_TIMEOUT";
            break;
        case oc::ARGUS_STATE::CAPTURE_TIMEOUT:
                    out = "CAPTURE_TIMEOUT";
            break;
        case oc::ARGUS_STATE::CAPTURE_FAILURE:
                    out = "CAPTURE_FAILURE";
            break;
        case oc::ARGUS_STATE::CAPTURE_UNSYNC:
                    out = "CAPTURE_UNSYNC";
            break;
        case oc::ARGUS_STATE::CAPTURE_CONVERT_FAILURE:
                    out = "CAPTURE_CONVERT_FAILURE";
            break;
        case oc::ARGUS_STATE::UNSUPPORTED_FCT:
                    out = "UNSUPPORTED_FCT";
            break;
        case oc::ARGUS_STATE::UNKNOWN:
                    out = "UNKNOWN";
            break;
    };

    return out;
}


enum class AEANTIBANDING
{
    OFF,
    AUTO,
    HZ50,
    HZ60
};

/// Pure virtual
class __attribute__((visibility("default"))) ArgusVirtualCapture
{
public :
  ArgusVirtualCapture();
  virtual ~ArgusVirtualCapture() {

  }

  virtual ARGUS_STATE openCamera(const ArgusCameraConfig &config,bool recreate_daemon = true)=0;
  virtual void closeCamera()=0;
  virtual ARGUS_STATE reboot()=0;


  virtual uint64_t getFrameExposureTime() =0;
  virtual int getFrameExposurePercent()=0;
  virtual int setFrameExposureRange(uint64_t, uint64_t)=0;
  virtual int getFrameExposureRange(uint64_t&, uint64_t&) =0;
  virtual int getExposureLimits(uint64_t&,uint64_t&)=0;
  virtual int setManualExposure(int)=0;
  virtual int setManualTimeExposure(uint64_t)=0;
  virtual int setAutomaticExposure()=0;

  virtual float getAnalogFrameGain()=0;
  virtual int getAnalogFrameGainPercent()=0;
  virtual int setAnalogFrameGainRange(float gain_low,float gain_high)=0;
  virtual int getAnalogFrameGainRange(float& sgain_low,float& sgain_high)=0;
  virtual int getAnalogGainLimits(float& min_gain,float& max_gain)=0;
  virtual int setAutomaticAnalogGain()=0;
  virtual int setManualAnalogGain(int percent)=0;
  virtual int setManualAnalogGainReal(float db)=0;
  virtual float getDigitalFrameGain()=0;
  virtual int setDigitalFrameGainRange(float gain_low,float gain_high)=0;
  virtual int getDigitalFrameGainRange(float& dgain_low,float& dgain_high)=0;
  virtual int getDigitalGainLimits(float& min_gain,float& max_gain)=0;
  virtual int setAutomaticDigitalGain()=0;
  virtual int setManualDigitalGain(int percent)=0;
  virtual int setManualDigitalGainReal(int factor)=0;

  virtual int setManualWhiteBalance(uint32_t color_temperature)=0;
  virtual uint32_t getManualWhiteBalance()=0;
  virtual int setAutomaticWhiteBalance(int val)=0;
  virtual int getAutomaticWhiteBalanceStatus()=0;

  virtual int setSharpening(float value)=0;
  virtual float getSharpening()=0;

  virtual int setColorSaturation(float saturation)=0;
  virtual float getColorSaturation()=0;


  virtual int getToneMappingCurveSize(int channel)=0;
  virtual int setToneMappingFromGamma(int channel,float gamma)=0;
  virtual float getGamma()=0;

  virtual int setAEAntiBanding(AEANTIBANDING mode)=0;

  virtual int setDenoisingValue(float value)=0;
  virtual float getDenoisingValue()=0;

  virtual int setExposureCompensation(float ev)=0;
  virtual float getExposureCompensation()=0;

  virtual int setROIforAECAGC(oc::Rect roi)=0;
  virtual int resetROIforAECAGC()=0;
  virtual int getROIforAECAGC(oc::Rect &roi)=0;



  ///
  /// \brief getImageTimestampinUs
  /// \return the image timestamp in microseconds
  ///
  uint64_t getImageTimestampinUs();

  ///
  /// \brief getLastCaptureState
  /// \return the argus status
  ///
  ARGUS_STATE getLastCaptureState() { return mArgusGrabState;}

  ///
  /// \brief isNewFrame
  /// \return true if a new frame is available. This should be the first call in the loop capture
  ///
  bool isNewFrame();


  ///
  /// \brief getPixels: get current image as unsigned char*
  /// \return pointer to the pixel array
  ///
  unsigned char* getPixels();

  ///
  /// \brief getVersion
  /// \param major : major version number
  /// \param minor : minor version number
  /// \param patch : patch version number
  ///
  static void getVersion(int& major, int& minor, int &patch);

  ///
  /// \brief isOpened
  /// \return true if camera has been open and running
  ///
  bool isOpened();
  ///
  /// \brief getWidth : get the effective width of single image
  /// \return 0 if not valid >0 otherwise
  ///
  int getWidth();

  ///
  /// \brief getHeight : get the effective height of single image
  /// \return 0 if not valid >0 otherwise
  ///
  int getHeight();

  ///
  /// \brief getFPS
  /// \return the FPS of camera
  ///
  int getFPS();

  ///
  /// \brief getNumberOfChannels
  /// \return the number of channels (3 or 4) for RGB/A
  ///
  int getNumberOfChannels();


  ///
  /// \brief enterCriticalSection: provide a mutex locking mechanism when using the buffer filled by the SDK
  ///
  void enterCriticalSection();

  ///
  /// \brief exitCriticalSection : same as enterCriticalSection() but for unlocking
  ///
  void exitCriticalSection();

  ////////////////////////////////////////////////////////////////////////////////////////////////////////
  ///////////////////////////////////////      UTILS       ///////////////////////////////////////////////
  ////////////////////////////////////////////////////////////////////////////////////////////////////////

  ///
  /// \brief setPtr : Pass a memory pointer to the internal buffer so that memcpy is avoid when using getPixels()
  /// \note : getPixels is still required to be called
  /// \param ptr_sdk : must be at the exact size (width*height*channels)
  ///
  void setPtr(unsigned char *ptr_sdk);


protected :
  int mPort = 0; //Refers to device ID
  int width = 0;
  int height = 0;
  int nChannel = 4;
  int fps = 0;
  bool opened_ = false;
  ArgusCameraConfig mConfig={0};

  PollEvent userNewFrameEvent;
  std::atomic<bool> consumer_newFrame;

  std::mutex mut_access;

  unsigned char *ptr_buffer_sdk = nullptr;
  bool use_outer_buffer = false;
  unsigned char *internal_buffer_grab = nullptr;


  ARGUS_STATE mArgusGrabState = ARGUS_STATE::OK;
  bool mHasCaptureFrame = false;


  unsigned long long consumer_image_ts_us = 0;
  // Capture Queue :
  // Filled by the producer, consumed by the consumer
  std::deque<argusMonoImage> mCaptureQueue;
};

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
/////////////////////////////////////////////////////    V4L2 CAPTURE WORKFLOW  ///////////////////////////////////////////////////////
///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


typedef struct
{
    /* User accessible pointer */
    unsigned char * start;
    /* Buffer length */
    unsigned int size;
    /* File descriptor of NvBuffer */
    int dmabuff_fd;
} nv_buffer;

typedef struct
{
    /* Camera v4l2 context */
    const char * cam_devname;
    char cam_file[16];
    int cam_fd;
    unsigned int cam_pixfmt;
    unsigned int cam_w;
    unsigned int cam_h;
    unsigned int frame;

    /* Global buffer ptr */
    nv_buffer * g_buff;
    bool capture_dmabuf;

    /* NVBuf renderer */
    int render_dmabuf_fd=-1;
    int fps;

    /* Verbose option */
    bool enable_verbose;

} nv_v4l2_context_t;

//// Argus Capture for YUV camera (Using internal ISP)
class __attribute__((visibility("default"))) ArgusV4l2Capture : public ArgusVirtualCapture
{
   public:
     ArgusV4l2Capture();
     ~ArgusV4l2Capture();

    ///
    /// \brief open the argus stereo camera
    /// \param config :: camera configuration
    /// \return 0 if success, !=0 otherwise
    ///
    ARGUS_STATE openCamera(const ArgusCameraConfig &config,bool recreate_daemon = true);

    ///
    /// \brief close the camera
    ///
    void closeCamera();

    ///
    /// \brief reboot the camera
    ///
    ARGUS_STATE reboot();


    ////////////////////////////////////////////////////////////////////////////////////////////
    //////////////////////////////////////  [UNSUPPORTED] VIDEO CTRL  //////////////////////////
    ////////////////////////////////////////////////////////////////////////////////////////////

    //// WARNING ////
    /// The following settings/control function are not available as-is in v4L2 //
    /// Keep them for conformity with the virtual class ////
    /// They will all returned -1 or 0 depending on output type //


    /// ---->
    uint64_t getFrameExposureTime();
    int getFrameExposurePercent();
    int setFrameExposureRange(uint64_t exp_low, uint64_t exp_high);
    int getFrameExposureRange(uint64_t &exp_low, uint64_t &exp_high);
    int getExposureLimits(uint64_t& min_exp,uint64_t& max_exp);
    int setManualExposure(int percent);
    int setManualTimeExposure(uint64_t time);
    int setAutomaticExposure();
    float getAnalogFrameGain();
    int getAnalogFrameGainPercent();
    int setAnalogFrameGainRange(float gain_low,float gain_high);
    int getAnalogFrameGainRange(float& sgain_low,float& sgain_high);
    int getAnalogGainLimits(float& min_gain,float& max_gain);
    int setAutomaticAnalogGain();
    int setManualAnalogGain(int percent);
    int setManualAnalogGainReal(float db);
    float getDigitalFrameGain();
    int setDigitalFrameGainRange(float gain_low,float gain_high);
    int getDigitalFrameGainRange(float& dgain_low,float& dgain_high);
    int getDigitalGainLimits(float& min_gain,float& max_gain);
    int setAutomaticDigitalGain();
    int setManualDigitalGain(int percent);
    int setManualDigitalGainReal(int factor);
    int setManualWhiteBalance(uint32_t color_temperature);
    uint32_t getManualWhiteBalance();
    int setAutomaticWhiteBalance(int val);
    int getAutomaticWhiteBalanceStatus();
    int setSharpening(float value);
    float getSharpening();
    int setColorSaturation(float saturation);
    float getColorSaturation();
    int getToneMappingCurveSize(int channel);
    int setToneMappingFromGamma(int channel,float gamma);
    float getGamma();
    int setAEAntiBanding(AEANTIBANDING mode);
    int setDenoisingValue(float value);
    float getDenoisingValue();
    int setExposureCompensation(float ev);
    float getExposureCompensation();
    int setROIforAECAGC(oc::Rect roi);
    int resetROIforAECAGC();
    int getROIforAECAGC(oc::Rect &roi);
    ////<-----

    ////////////////////////////////////////////////////////////////////////////////////////////
    ////////////////////////////////////// DETECTION ///////////////////////////////////////////
    ////////////////////////////////////////////////////////////////////////////////////////////

    ///
    /// \brief getV4l2Devices : get V4L2 compatbile devices
    /// \return a list of device properties
    ///
    static std::vector<oc::ArgusDevice> getV4l2Devices();


protected :
    void control();
    void consume();
    void produce();

//private fct
private :
    void clearBuffers();
    void clearImageQueues();
    void abortCapture();
    void abortControlThread();
    void lock();
    void unlock();
    int convert(int,int,argusMonoImage& image);
    bool requestCameraBuffers();
    bool startStream();
    bool stopStream();
    int dump_dmabuf(int dmabuf_fd, unsigned int plane, argusMonoImage& image);
//private components
private :
    bool exit_=false;
    bool isfrozen_ = false;
    int mCvtfd=-1;

    std::mutex mut_internal;

    std::thread* producer =nullptr;
    std::thread* controler = nullptr;
    std::thread* consumer = nullptr;

    nv_v4l2_context_t ctx;
};


///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
/////////////////////////////////////////////////////   ARGUS CAPTURE WORKFLOW  ///////////////////////////////////////////////////////
///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


//// Argus Capture for RAW camera (Using Jetson ISP)
class __attribute__((visibility("default"))) ArgusBayerCapture : public ArgusVirtualCapture
{
    friend class ArgusComponent;
    public:
    ///
    /// \brief ArgusCamera : Constructor/destructor
    /// \note destructor calls close if necessary
    ///
    ArgusBayerCapture();
    ~ArgusBayerCapture();

    ///
    /// \brief open the argus stereo camera
    /// \param config :: camera configuration
    /// \return 0 if success, !=0 otherwise
    ///
    ARGUS_STATE openCamera(const ArgusCameraConfig &config,bool recreate_daemon = true);


    ///
    /// \brief close the camera
    ///
    void closeCamera();

    ///
    /// \brief reboot the camera
    ///
    ARGUS_STATE reboot();


    ////////////////////////////////////////////////////////////////////////////////////////////
    //////////////////////////////////////  VIDEO CTRL  ////////////////////////////////////////
    ////////////////////////////////////////////////////////////////////////////////////////////

    //////////////////////////////////
    //////// EXPOSURE CTRL  //////////
    //////////////////////////////////
    /// \brief getFrameExposureTime
    /// \return the "readed" current frame exposure time in microseconds
    ///
    uint64_t getFrameExposureTime();

    ///
    /// \brief getFrameExposurePercent
    /// \return the value between [0 - 100]
    ///
    int getFrameExposurePercent();

    ///
    /// \brief setFrameExposureRange: set the Frame exposure range. Use that to set a fixed exposure (low == high)
    /// \param exp_low : min exposure time (in us) the sensor will not gow below
    /// \param exp_high : max exposure time (in us) the sensor will not gow higher
    /// \return 0 if success, <0 if camera unavailable, >0 if error from argus side
    ///
    int setFrameExposureRange(uint64_t exp_low, uint64_t exp_high);

    ///
    /// \brief getFrameExposureRange
    /// \param exp_low : min exposure time (in us) for the sensor in AEC
    /// \param exp_high : max exposure time (in us) the sensor will not gow higher
    /// \return 0 if success, <0 if camera unavailable, >0 if error from argus side
    ///
    int getFrameExposureRange(uint64_t &exp_low, uint64_t &exp_high);

    ///
    /// \brief getExposureRange : get the MIN/MAX range of the exposure time
    /// \param [out] min_exp : the returned minimum exposure time possible from the DTS configuration. Provided in microseconds
    /// \param [out] max_exp : the returned maximum exposure time possible from the DTS configuration. Provided in microseconds
    /// \return 0 if success, !=0 otherwise
    ///
    int getExposureLimits(uint64_t& min_exp,uint64_t& max_exp);

    ///
    /// \brief setManualExposure : set Manual exposure for the specified camera (side) between 0 (lowest exp) to 100 (highest exp)
    /// \param percent : [0-100] for exp range
    /// \return 0 if success, <0 if camera unavailable, >0 if error from argus side
    ///
    int setManualExposure(int percent);

    ///
    /// \brief setManualTimeExposure
    /// \param time: time in us
    /// \return 0 if success, <0 if camera unavailable, >0 if error from argus side
    ///
    int setManualTimeExposure(uint64_t time);

    ///
    /// \brief setAutomaticExposure
    /// \return 0 if success, !=0 otherwise
    ///
    int setAutomaticExposure();

    //////////////////////////////////
    //////// ANALOG GAIN  ////////////
    //////////////////////////////////
    ///
    /// \brief getFrameGain
    /// \return the "readed" current frame sensor analog gain in dB
    ///
    float getAnalogFrameGain();

    ///
    /// \brief getAnalogFrameGainPercent
    /// \return the analog gain as percentage between min/max range
    ///
    int getAnalogFrameGainPercent();

    ///
    /// \brief setAnalogFrameGainRange : set the sensor gain (analog) range. Use that to set a fixed analog gain (low == high)
    /// \param gain_low : min gain the sensor will not gow below
    /// \param gain_high : max gain the sensor will not gow higher
    /// \return 0 if success, <0 if camera unavailable, >0 if error from argus side
    ///
    int setAnalogFrameGainRange(float gain_low,float gain_high);


    ///
    /// \brief getAnalogFrameGainRange
    /// \param sgain_low
    /// \param sgain_high
    /// \return 0 if success, <0 if camera unavailable, >0 if error from argus side
    ///
    int getAnalogFrameGainRange(float& sgain_low,float& sgain_high);

    ///
    /// \brief getAnalogGainRange
    /// \param [out] min_gain : the returned minimum analog gain possible
    /// \param [out] max_gain : the returned maximum analog gain possible
    /// \return 0 if success, !=0 otherwise
    ///
    int getAnalogGainLimits(float& min_gain,float& max_gain);


    ///
    /// \brief setAutomaticAnalogGain
    /// \return 0 if success, !=0 otherwise
    ///
    int setAutomaticAnalogGain();


    ///
    /// \brief setManualGain : set Gain for the specified camera (side) between 0 (lowest exp) to 100 (highest exp)
    /// \param percent : [0-100] for gain range
    /// \return 0 if success, <0 if camera unavailable, >0 if error from argus side
    ///
    int setManualAnalogGain(int percent);

    ///
    /// \brief setManualGainReal
    /// \param db : db as real db *100
    /// \return 0 if success, <0 if camera unavailable, >0 if error from argus side
    ///
    int setManualAnalogGainReal(float db);

    //////////////////////////////////
    ////////// ISP GAIN /// //////////
    //////////////////////////////////
    ///
    /// \brief getDigitalFrameGain : get the current ISP gain
    /// \return the current ISP gain from the "side" camera
    ///
    float getDigitalFrameGain();

    ///
    /// \brief setDigitalFrameGainRange : set the ISP gain (digital) range. Use that to set a fixed digital gain (low == high)
    /// \param gain_low : min gain the ISP will not gow below
    /// \param gain_high : max gain the ISP will not gow higher
    /// \return 0 if success, <0 if camera unavailable, >0 if error from argus side
    ///
    int setDigitalFrameGainRange(float gain_low,float gain_high);

    ///
    /// \brief getDigitalFrameGainRange : get the ISP gain range
    /// \param dgain_low: min gain available for the ISP
    /// \param dgain_high: max gain available for the ISP
    /// \return 0 if success, <0 if camera unavailable, >0 if error from argus side
    ///
    int getDigitalFrameGainRange(float& dgain_low,float& dgain_high);

    ///
    /// \brief getAnalogGainRange : get the MIN/MAX range of the analog gain
    /// \param [out] min_gain : the returned minimum ISP gain possible in the ISP configuration
    /// \param [out] max_gain : the returned maximum ISP gain possible in the ISP configuration
    /// \return 0 if success, !=0 otherwise
    ///
    int getDigitalGainLimits(float& min_gain,float& max_gain);


    ///
    /// \brief setAutomaticDigitalGain
    /// \return 0 if success, !=0 otherwise
    ///
    int setAutomaticDigitalGain();

    ///
    /// \brief setManualDigitalGain : set Digital Gain for the specified camera (side) between 0 (lowest gain) to 100 (highest gain)
    /// \param percent : [0-100] for gain range
    /// \return 0 if success, <0 if camera unavailable, >0 if error from argus side
    ///
    int setManualDigitalGain(int percent);

    ///
    /// \brief setManualDigitalGainReal
    /// \param factor : ISP factor between 1 and 256
    /// \return 0 if success, <0 if camera unavailable, >0 if error from argus side
    ///
    int setManualDigitalGainReal(int factor);


    //////////////////////////////////
    /////////// AWB/WB  //////////////
    //////////////////////////////////

    ///
    /// \brief setManualWhiteBalance
    /// \param color_temperature : Temperature color in kevin (2800°K-12000°K)
    /// \return 0 if success, !=0 otherwise
    ///
    int setManualWhiteBalance(uint32_t color_temperature);


    ///
    /// \brief getManualWhiteBalance
    /// \return the current white balance
    ///
    uint32_t getManualWhiteBalance();

    ///
    /// \brief setAutomaticWhiteBalance
    /// \param val 0 manual, 1 auto , > 1 auto mode with different settings
    /// \return 0 if success, !=0 otherwise
    ///
    int setAutomaticWhiteBalance(int val);

    ///
    /// \brief getAutomaticWhiteBalanceStatus
    /// \return 0 if manual, >=1 if automatic (mode)
    ///
    int getAutomaticWhiteBalanceStatus();



    //////////////////////////////////
    /////////// Sharpen //////////////
    //////////////////////////////////

    ///
    /// \brief setSharpening
    /// \param value : Sharpening Value [0 - 1.0]
    /// \return 0 if success, !=0 otherwise
    ///
    int setSharpening(float value);

    ///
    /// \brief getSharpening
    /// \return Sharpening Value [0 - 1.0]. <0 inidcates error
    ///
    float getSharpening();

    //////////////////////////////////
    /////////// Saturation //////////
    //////////////////////////////////

    ///
    /// \brief setColorSaturation
    /// \param saturation : saturation Value [0.0 - 2.0]
    /// \default Value is 1.0
    /// \return 0 if success, !=0 otherwise
    ///
    int setColorSaturation(float saturation);

    ///
    /// \brief getColorSaturation
    /// \return
    ///
    float getColorSaturation();



    //////////////////////////////////
    /////////// ToneMapping //////////
    //////////////////////////////////

    ///
    /// \brief getToneMappingCurveSize
    /// \param channel : Red==0, Green = 1, Blue = 2
    /// \return the number of points in the LUT
    ///
    int getToneMappingCurveSize(int channel);

    ///
    /// \brief setToneMappingFromGamma : set the tone mapping curve from a gamma value
    /// \param channel : Red==0, Green = 1, Blue = 2
    /// \param gamma :  gamma value so that LUT[i] = std::pow(i/size,1/gamma) . Range [1.5 ; 3.5]
    /// \return 0 if success, !=0 otherwise
    ///
    int setToneMappingFromGamma(int channel,float gamma);

    ///
    /// \brief getGamma simply get last set value
    /// \return
    ///
    float getGamma();


    //////////////////////////////////
    ///////////AEAntibanding//////////
    //////////////////////////////////

    ///
    /// \brief setAEAntiBanding
    /// \param mode : OFF, AUTO, 50HZ or 60Hz, linked to argus API
    /// \return 0 if OK, <0 if not open, >0 if error trigger by argus
    ///
    int setAEAntiBanding(AEANTIBANDING mode);


    //////////////////////////////////
    /////////// Denoising   //////////
    //////////////////////////////////

    ///
    /// \brief setColorSaturation
    /// \param saturation : saturation Value [0.0 - 1.0]
    /// \default Value is 0.5
    /// \note -1.0 == use_default=true
    /// \return 0 if success, !=0 otherwise
    ///
    int setDenoisingValue(float value);


    ///
    /// \brief getDenoisingValue
    /// \return [0 - 1.0] value. <0 means error
    ///
    float getDenoisingValue();


    //////////////////////////////////
    //////// EV Compensation /////////
    //////////////////////////////////

    ///
    /// \brief setExposureCompensationTarget
    /// \param ev : exposure compensation [-2.0,2.0]
    /// \note : default value is 0.0
    /// \return -1 if not opened, 0 if success, argus status if >0
    ///
    int setExposureCompensation(float ev);

    ///
    /// \brief getExposureCompensationTarget
    /// \return [-2.0 2.0] value
    /// \return < -2.0 indicates error
    ///
    float getExposureCompensation();



    //////////////////////////////////
    //////// Auto Exp on ROI /////////
    //////////////////////////////////

    ///
    /// \brief setROIforAECAGC
    /// \param roi Rectangle where AEC will be made.
    /// \return 0 if success, !=0 otherwise
    ///
    int setROIforAECAGC(oc::Rect roi);

    ///
    /// \brief resetROIforAECAGC
    /// \return 0 if success, !=0 otherwise
    ///
    int resetROIforAECAGC();

    /// \brief getROIforAECAGC
    /// \return 0 if success, !=0 otherwise
    int getROIforAECAGC(oc::Rect &roi);

    ////////////////////////////////////////////////////////////////////////////////////////////
    ////////////////////////////////////// DETECTION ///////////////////////////////////////////
    ////////////////////////////////////////////////////////////////////////////////////////////

    ///
    /// \brief getGMSLDevices
    /// \return a list of device properties
    ///
    static std::vector<oc::ArgusDevice> getArgusDevices();




protected :
    void control();
    void consume();
    void produce();

private:

    std::thread* producer =nullptr;
    std::thread* controler = nullptr;
    std::thread* consumer = nullptr;
    
    bool exit_=false;
    bool isfrozen_ = false;


    std::mutex mut_internal;
    std::mutex mut_irequest;
    std::mutex mut_access;

    int fd = -1;

    uint64_t current_exp_time = 0;
    float current_analog_gain = 0;
    float current_digital_gain =0;

    std::pair<float,float> analog_gain_limits_range;
    std::pair<float,float> digital_gain_limits_range;
    std::pair<uint64_t,uint64_t> exposure_limits_range;

    std::pair<float,float> analog_gain_current_range;
    std::pair<float,float> digital_gain_current_range;
    std::pair<uint64_t,uint64_t> exposure_current_range;


    std::vector<float> default_tone_mapping_lut_red;
    std::vector<float> default_tone_mapping_lut_green;
    std::vector<float> default_tone_mapping_lut_blue;


    private :

    void dispatchRequest();
    void lock();
    void unlock();
    int dump_dmabuf(int dmabuf_fd, unsigned int plane, argusMonoImage &image);
    void estimageRGGBGainFromColorTemperature(uint32_t KelvinT, float factor, float& r, float &g_even, float&g_odd, float &b);
    void estimageRGGBGainFromColorTemperature_v2(uint32_t KelvinT, float factor, float& r, float &g_even, float&g_odd, float &b);
    int convert(void* iframe, argusMonoImage &image);
    void clearBuffers();
    void clearImageQueues();
    void abortCapture();
    void abortControlThread();

    int mROIMinWidth = 0;
    int mROIMinHeight = 0;

    float current_gamma = 2.5;
    uint32_t color_temperature = 2800;
    bool isManualWhiteBalance = false;
    int mAutoWhiteBalanceSetting = 0;



#if USE_ZMQ_DAEMON
    zmq::socket_type zmq_type = zmq::socket_type::pub;
    std::unique_ptr<zmq::context_t> zmq_context;
    std::unique_ptr<zmq::socket_t> zmq_socket;
#endif

    //Component
    ArgusComponent* h= nullptr;
};




}


#endif

#endif
