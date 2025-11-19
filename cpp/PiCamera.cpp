#include <PiCamera.hpp>
#include <cstdlib>
#include <sys/mman.h>
#include <linux/dma-buf.h>
#include <sys/ioctl.h>

std::shared_ptr<libcamera::Camera> PiCamera::camera;
std::unique_ptr<libcamera::CameraManager> PiCamera::camera_manager;
std::vector<std::unique_ptr<libcamera::Request>> PiCamera::requests;
libcamera::FrameBufferAllocator *PiCamera::allocator;
std::unique_ptr<libcamera::CameraConfiguration> PiCamera::config;
libcamera::Stream *PiCamera::stream;

CaptureMode PiCamera::capture_mode; 
std::shared_ptr<FrameManager> PiCamera::frame_manager;

//std::map<const libcamera::Stream *, libcamera::FrameBuffer *> &buffers;
std::map<libcamera::FrameBuffer *, std::vector<libcamera::Span<uint8_t>>> PiCamera::mapped_buffers;
std::map<libcamera::Stream *, std::vector<std::unique_ptr<libcamera::FrameBuffer>>> PiCamera::frame_buffers;
DmaHeap PiCamera::dma_heap_;

PiCamera::PiCamera() {
}


void PiCamera::Initialize() {
    capture_mode = eViewfinder;
    camera_manager = std::make_unique<libcamera::CameraManager>();
    camera_manager->start();

    for (auto const &camera : camera_manager->cameras())
        std::cout << camera->id() << std::endl;

    auto cameras = camera_manager->cameras();
    if (cameras.empty()) {
        std::cout << "No cameras were identified on the system." << std::endl;
        camera_manager->stop();
        return;
    }

    std::string cameraId = cameras[0]->id();

    camera = camera_manager->get(cameraId);
    camera->acquire();
    allocator = new libcamera::FrameBufferAllocator(camera);

    /*
    ConfigureViewfinder();
    //ConfigureStillCapture();
    MapBuffers();
    AllocateBuffers();

    camera->requestCompleted.connect(requestComplete);
    */
}

void PiCamera::StartViewfinder() {
    ConfigureViewfinder();
    MapBuffers();
    AllocateBuffers();

    camera->requestCompleted.connect(requestComplete);
}

void PiCamera::StartStillCapture() {
    ConfigureStillCapture();
    MapBuffers();
    AllocateBuffers();

    camera->requestCompleted.connect(requestComplete);
}

void PiCamera::AllocateBuffers() {

    for (libcamera::StreamConfiguration &cfg : *config) {
        int ret = allocator->allocate(cfg.stream());
        if (ret < 0) {
            std::cerr << "Can't allocate buffers" << std::endl;
            return;// -ENOMEM;
        }

        size_t allocated = allocator->buffers(cfg.stream()).size();
        std::cout << "Allocated " << allocated << " buffers for stream" << std::endl;
    }

    //libcamera::Stream *stream = streamConfig.stream();
    const std::vector<std::unique_ptr<libcamera::FrameBuffer>> &buffers = allocator->buffers(stream);

    for (unsigned int i = 0; i < buffers.size(); ++i) {
        std::unique_ptr<libcamera::Request> request = camera->createRequest();
        if (!request)
        {
            std::cerr << "Can't create request" << std::endl;
            return;// -ENOMEM;
        }

        const std::unique_ptr<libcamera::FrameBuffer> &buffer = buffers[i];
        int ret = request->addBuffer(stream, buffer.get());
        if (ret < 0)
        {
            std::cerr << "Can't set buffer for request"
                << std::endl;
            return;// ret;
        }

        requests.push_back(std::move(request));
    }
}

void PiCamera::MapBuffers() {
    for (libcamera::StreamConfiguration &stream_config : *config) {
        libcamera::Stream *stream = stream_config.stream();
        std::vector<std::unique_ptr<libcamera::FrameBuffer>> fb;

        for (unsigned int i = 0; i < stream_config.bufferCount; i++) {
            std::string name("film-sim" + std::to_string(i));
            libcamera::UniqueFD fd = dma_heap_.alloc(name.c_str(), stream_config.frameSize);

            if (!fd.isValid())
                throw std::runtime_error("failed to allocate capture buffers for stream");

            std::vector<libcamera::FrameBuffer::Plane> plane(1);
            plane[0].fd = libcamera::SharedFD(std::move(fd));
            plane[0].offset = 0;
            plane[0].length = stream_config.frameSize;

            fb.push_back(std::make_unique<libcamera::FrameBuffer>(plane));
            void *memory = mmap(NULL, stream_config.frameSize, PROT_READ | PROT_WRITE, MAP_SHARED, plane[0].fd.get(), 0);
            mapped_buffers[fb.back().get()].push_back(
                        libcamera::Span<uint8_t>(static_cast<uint8_t *>(memory), stream_config.frameSize));
        }

        frame_buffers[stream] = std::move(fb);
    }
}

void PiCamera::requestComplete(libcamera::Request *request)
{
    if (request->status() == libcamera::Request::RequestCancelled)
        return;

/*
    struct dma_buf_sync dma_sync {};
	dma_sync.flags = DMA_BUF_SYNC_START | DMA_BUF_SYNC_READ;
	for (auto const &buffer_map : request->buffers())
	{
		auto it = mapped_buffers.find(buffer_map.second);
		if (it == mapped_buffers.end())
			throw std::runtime_error("failed to identify request complete buffer");

		int ret = ::ioctl(buffer_map.second->planes()[0].fd.get(), DMA_BUF_IOCTL_SYNC, &dma_sync);
		if (ret)
			throw std::runtime_error("failed to sync dma buf on request complete");
	}

    libcamera::CompletedRequest *r = new libcamera::CompletedRequest(sequence_++, request);
	libcamera::CompletedRequestPtr payload(r, [this](libcamera::CompletedRequest *cr) { this->queueRequest(cr); });
	{
		std::lock_guard<std::mutex> lock(completed_requests_mutex_);
		completed_requests_.insert(r);
	}
        */

    const std::map<const libcamera::Stream *, libcamera::FrameBuffer *> &buffers = request->buffers();

    for (auto bufferPair : buffers) {
        libcamera::FrameBuffer *buffer = bufferPair.second;
        const libcamera::FrameMetadata &metadata = buffer->metadata();

        for (const libcamera::FrameBuffer::Plane &plane : buffer->planes()) {
            if (!plane.fd.isValid()) {
                break;
            }
            int fd = plane.fd.get();

            // TODO: permanent mmap? 
            uint8_t * addr = (uint8_t *) mmap(0, plane.length, PROT_READ, MAP_PRIVATE, fd, 0);
            if (addr == MAP_FAILED) {
                std::cout << "Map Failed" << std::endl;
            }

            if (capture_mode == eViewfinder)
                frame_manager->update(addr, plane.length);
        }

        if (capture_mode == eViewfinder)
        {
            request->reuse(libcamera::Request::ReuseBuffers);
            camera->queueRequest(request); // make request happen each time 
        }
        else if (capture_mode == eStillCapture)
        {
            // DON'T requeue - single capture
            // Switch back to viewfinder after delay
            std::thread([&]() {
                std::this_thread::sleep_for(std::chrono::milliseconds(100));
                //cameraController.configureForViewfinder();
                //cameraController.startCapture();
            }).detach();
        }
    }

}

std::shared_ptr<libcamera::Camera> PiCamera::GetCamera() {
    if (camera == nullptr) {
        Initialize();
    }

    return camera;
}

void PiCamera::ConfigureViewfinder() { // TODO: pass in parameters?
    config = camera->generateConfiguration( { libcamera::StreamRole::Viewfinder } );
    
    std::cout << "Default viewfinder configuration is: " << config->at(0).toString() << std::endl;
    config->at(0).size.width = 1296;
    config->at(0).size.height = 972;
    config->validate();
    std::cout << "Validated viewfinder configuration is: " << config->at(0).toString() << std::endl;
    camera->configure(config.get());
    stream = config->at(0).stream();// this now happens before the full config is validated? idk
}

void PiCamera::ConfigureStillCapture() {
    config = camera->generateConfiguration( { libcamera::StreamRole::StillCapture } );

    std::cout << "Default still capture configuration is: " << config->at(0).toString() << std::endl;
    config->at(0).size.width = 1296;
    config->at(0).size.height = 972;
    config->validate();
    std::cout << "Validated still capture configuration is: " << config->at(0).toString() << std::endl;
    camera->configure(config.get());
}

void PiCamera::StartCamera() {
    camera->start();
    for (std::unique_ptr<libcamera::Request> &request : requests)
        camera->queueRequest(request.get());
}

void PiCamera::StopCamera() {
    camera->stop();
    camera->requestCompleted.disconnect(this, requestComplete);
    allocator->free(stream);
    camera_manager->stop();
}

void PiCamera::Cleanup() {
    camera->stop();
    allocator->free(stream);
    delete allocator;
    camera->release();
    camera.reset();
    camera_manager->stop();
}

void PiCamera::SetFrameManager(std::shared_ptr<FrameManager> input_frame_manager) {
    frame_manager = input_frame_manager;
}
