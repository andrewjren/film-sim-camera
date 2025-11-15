#ifndef CPP_PICAMERA_HPP
#define CPP_PICAMERA_HPP

#include <iomanip>
#include <memory>
#include <thread>
#include <iostream> // TODO: Remove and create a logger class
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
    static std::unique_ptr<libcamera::CameraManager> camera_manager;
    static std::vector<std::unique_ptr<libcamera::Request>> requests;
    static libcamera::FrameBufferAllocator *allocator;
    static std::unique_ptr<libcamera::CameraConfiguration> config;
    static libcamera::Stream *stream;
    //static std::map<const libcamera::Stream *, libcamera::FrameBuffer *> &buffers;
    static std::map<libcamera::FrameBuffer *, std::vector<libcamera::Span<uint8_t>>> mapped_buffers;
    static std::map<libcamera::Stream *, std::vector<std::unique_ptr<libcamera::FrameBuffer>>> frame_buffers;
    static DmaHeap dma_heap_;

    PiCamera();

    
    static void requestComplete(libcamera::Request*);

    public:

    static CaptureMode capture_mode; 
    static std::shared_ptr<FrameManager> frame_manager;
    
    static void Initialize();
    static void AllocateBuffers();
    static std::shared_ptr<libcamera::Camera> GetCamera();
    static void ConfigureViewfinder();
    static void ConfigureStillCapture();
    static void StartCamera();
    static void SetFrameManager(std::shared_ptr<FrameManager>);
    static void Cleanup();
    static void MapBuffers();
};

#endif // CPP_PICAMERA_HPP
