#ifndef TOUCHSCREEN_HPP
#define TOUCHSCREEN_HPP

#include <chrono>
#include <libevdev/libevdev.h>
#include <log.hpp>

class Touchscreen {
public:
    Touchscreen(const char* file) 
        :touch_state(TouchState::RELEASED)
        {
        fd = open(file, O_RDONLY | O_NONBLOCK);
        if (fd < 0) {
            LOG_ERR << "Failed to open device\n";
        }
        
        rc = libevdev_new_from_fd(fd, &dev);
        if (rc < 0) {
            LOG_ERR << "Failed to init libevdev\n";
        }
/*    
        LOG << "Input device ID: bus " << libevdev_get_id_bustype(dev) << "vendor " <<  libevdev_get_id_vendor(dev) << "product " << libevdev_get_id_product(dev);
        LOG << "Evdev version: " << libevdev_get_driver_version(dev) << "\n";
        LOG << "Input device name: " << libevdev_get_name(dev) << "\n";
        LOG << "Phys location: " << libevdev_get_phys(dev) << "\n";
        LOG << "Uniq identifier: " << libevdev_get_uniq(dev) << "\n";
*/
    }
    ~Touchscreen() {
        libevdev_free(dev);
    }

    void PollEvents() {
        while (true) {
            struct input_event ev;
            rc = libevdev_next_event(dev, LIBEVDEV_READ_FLAG_NORMAL, &ev);
            if (rc == LIBEVDEV_READ_STATUS_SUCCESS) {
                if (ev.type == EV_KEY && ev.code == BTN_TOUCH) { 
                    if (ev.value == 1)
                        HandleTouchDown();
                    else if (ev.value == 0)
                        HandleTouchUp();
                }
                else if (ev.type == EV_ABS && ev.code == ABS_MT_POSITION_X) {
                    HandlePosX(ev.value);
                }
                else if (ev.type == EV_ABS && ev.code == ABS_MT_POSITION_Y) {
                    HandlePosY(ev.value);
                }
                else if (ev.type == EV_SYN && ev.code == SYN_REPORT) {
                    ProcessTouchState();
                }
            }
            else if (rc == -EAGAIN) {
                // No more events available (non-blocking returned)
                break;
            }

       }
    }

    bool ProcessPhotoRequest() {
        bool curr_photo_request = photo_request;
        photo_request = false;
        return curr_photo_request;
    }


private:
    int fd;
    int rc = 1;
    struct libevdev *dev = NULL;
    int pos_x;
    int pos_y;
    std::chrono::time_point<std::chrono::system_clock> last_release;
    const std::chrono::milliseconds cooldown = std::chrono::milliseconds(100);
    bool photo_request = false;

    enum TouchState {
        RELEASED,
        PRESSED,
        TRIGGERED,
        DRAGGED
    };
    TouchState touch_state;

    void HandlePosX(int val) {
        pos_x = val;
    }

    void HandlePosY(int val) {
        pos_y = val;
    }

    void HandleTouchDown() {
        std::chrono::duration<float> elapsed_ms = std::chrono::system_clock::now() - last_release;
        if (touch_state == TouchState::RELEASED && elapsed_ms > cooldown) {
            touch_state = TouchState::PRESSED;
        }
    }

    void HandleTouchUp() {
        touch_state = TouchState::TRIGGERED;
    }
    
    void ProcessTouchState() {
        if (touch_state == TouchState::TRIGGERED) {
            RequestPhoto();
            touch_state = TouchState::RELEASED;
            last_release = std::chrono::system_clock::now();
        }
    }

    void RequestPhoto() {
        photo_request = true;
    }

           
};

#endif // TOUCHSCREEN_HPP
