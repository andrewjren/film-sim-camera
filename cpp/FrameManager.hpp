#ifndef FRAMEMANAGER_HPP
#define FRAMEMANAGER_HPP

#include <memory>
#include <mutex>
#include <vector>

/* instead of implementing a synchronous queue, allow for camera processor thread to continuously update 
   frame data. this avoids the possibility of the camera thread overflowing the queue. order is less important for the 
   preview DRM window */
class FrameManager {
private:
    //std::queue<FrameData> queue;
    std::pair<bool, std::vector<uint8_t>> frame_data; // <data available, pointer to data>
    std::mutex mutex;
    //std::condition_variable cv;

public:
    FrameManager() {}
    
    void update(const void* ptr, size_t size) {
        std::unique_lock<std::mutex> lock(mutex);

		if (frame_data.second.size() != size) {
			frame_data.second.resize(size);
		}

		memcpy(frame_data.second.data(), ptr, size);
        frame_data.first = true; // indicate that new data is available
    }

    bool data_available() {
        std::unique_lock<std::mutex> lock(mutex);
        return frame_data.first;
    }

    void swap_buffers(std::vector<uint8_t> &vector_in) {
        std::unique_lock<std::mutex> lock(mutex);
        frame_data.first = false; // processed this data
        frame_data.second.swap(vector_in);
    }
};

#endif