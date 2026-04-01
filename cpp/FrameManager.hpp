#ifndef FRAMEMANAGER_HPP
#define FRAMEMANAGER_HPP

#include <memory>
#include <mutex>
#include <vector>
#include <cstring>
#include <condition_variable>
#include <log.hpp>

/* instead of implementing a synchronous queue, allow for camera processor thread to continuously update 
   frame data. this avoids the possibility of the camera thread overflowing the queue. order is less important for the 
   preview DRM window */
class FrameManager {
private:
    //std::queue<FrameData> queue;
    std::pair<bool, std::vector<uint8_t>> frame_data; // <data available, pointer to data>
    std::pair<bool, std::vector<uint8_t>> capture_data; // <data available, pointer to data>
    std::mutex mutex;
    std::condition_variable cv;
    bool shutdown;

public:
    FrameManager() {}
    
    void update(const void* ptr, size_t size) {
        std::unique_lock<std::mutex> lock(mutex);

		if (frame_data.second.size() != size) {
			frame_data.second.resize(size);
		}

		memcpy(frame_data.second.data(), ptr, size);
        frame_data.first = true; // indicate that new data is available
        cv.notify_one();
    }
    
    void update_capture(const void *ptr, size_t size) {
        std::unique_lock<std::mutex> lock(mutex);

        if (capture_data.second.size() != size) {
            capture_data.second.resize(size);
        }

        memcpy(capture_data.second.data(), ptr, size);
        capture_data.first = true;
    }

    bool data_available() {
        std::unique_lock<std::mutex> lock(mutex);
        return frame_data.first;
    }

    bool swap_buffers(std::vector<uint8_t> &vector_in) {
        std::unique_lock<std::mutex> lock(mutex);
        cv.wait(lock, [this]{ return frame_data.first || shutdown; });
        if (shutdown) {
            return false;
        }

        frame_data.second.swap(vector_in);
        frame_data.first = false; // processed this data
        return true;
    }

    void swap_capture(std::vector<uint8_t> &vector_in) {
        std::unique_lock<std::mutex> lock(mutex);
        capture_data.first = false; // processed this data
        capture_data.second.swap(vector_in);
    }

    void clear_buffers() {
        std::unique_lock<std::mutex> lock(mutex);
        frame_data.first = false;
        frame_data.second.clear();
    }

    void Stop() {
        std::unique_lock<std::mutex> lock(mutex);
        shutdown = true;
        cv.notify_all();
    }
};

#endif
