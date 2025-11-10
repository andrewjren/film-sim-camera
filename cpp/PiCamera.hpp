#ifndef CPP_PICAMERA_HPP
#define CPP_PICAMERA_HPP

#include <iomanip>
#include <memory>
#include <thread>
#include <iostream> // TODO: Remove and create a logger class
#include <libcamera/libcamera.h>
#include <FrameManager.hpp>

enum CaptureMode{
	eViewfinder,
	eStillCapture
};

class PiCamera {
    private:

    static std::shared_ptr<libcamera::Camera> camera = nullptr;
    static std::unique_ptr<libcamera::CameraManager> camera_manager = nullptr;
    static std::vector<std::unique_ptr<libcamera::Request>> requests;
    static libcamera::FrameBufferAllocator *allocator = new libcamera::FrameBufferAllocator(camera);

    PiCamera();

    
    static void requestComplete(libcamera::Request*);

    public:

    static CaptureMode capture_mode = eViewfinder; 
    static std::shared_ptr<FrameManager> frame_manager = nullptr;
    
    static void Initialize();
    static void AllocateBuffers();
    static std::shared_ptr<libcamera::Camera> GetCamera();
    static void ConfigureViewfinder();
    static void ConfigureStillCapture();
    static void StartCamera();
    static void Cleanup();
};

#endif // CPP_PICAMERA_HPP