#ifndef CPP_PICAMERA_HPP
#define CPP_PICAMERA_HPP

#include <iomanip>
#include <memory>
#include <thread>
#include <libcamera/libcamera.h>
#include <FrameManager.hpp>
#include "dma_heaps.hpp"

enum CaptureMode{
	eViewfinder,
	eStillCapture
};

class PiCamera {
    private:

    static std::shared_ptr<libcamera::Camera> camera;
    std::unique_ptr<libcamera::CameraManager> camera_manager;
    std::vector<std::unique_ptr<libcamera::Request>> requests;
    libcamera::FrameBufferAllocator *allocator;
    static std::unique_ptr<libcamera::CameraConfiguration> config;
    libcamera::Stream *stream;
    static std::map<libcamera::FrameBuffer *, std::vector<libcamera::Span<uint8_t>>> mapped_buffers;
    std::map<libcamera::Stream *, std::vector<std::unique_ptr<libcamera::FrameBuffer>>> frame_buffers;
    DmaHeap dma_heap_;
    
    static void requestComplete(libcamera::Request*);

    public:
    PiCamera() {}
    CaptureMode capture_mode; 
    static std::shared_ptr<FrameManager> frame_manager;
    std::shared_ptr<libcamera::StreamConfiguration> viewfinder_config;
    std::shared_ptr<libcamera::StreamConfiguration> stillcapture_config;
    unsigned int stride; // for correct YUV decoding
    
    void Initialize();
    void AllocateBuffers();
    std::shared_ptr<libcamera::Camera> GetCamera();
    void ConfigureViewfinder();
    void ConfigureStillCapture();
    void StartCamera();
    void StartViewfinder();
    void StartStillCapture();
    void StopCamera();
    void SetFrameManager(std::shared_ptr<FrameManager>);
    void Cleanup();
    void MapBuffers();
    void Configure();
    void CreateRequests();
};

#endif // CPP_PICAMERA_HPP
