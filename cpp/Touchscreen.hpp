#ifndef TOUCHSCREEN_HPP
#define TOUCHSCREEN_HPP

#include <chrono>
#include <libevdev/libevdev.h>
#include <log.hpp>
#include <cmath>

struct ScreenPosition {
    int pos_x;
    int pos_y;
};

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

    bool ProcessNextShader() {
        bool curr_next_shader = next_shader;
        next_shader = false;
        return curr_next_shader;
    }

    bool ProcessPrevShader() {
        bool curr_prev_shader = prev_shader;
        prev_shader = false;
        return curr_prev_shader;
    }
       


private:
    int fd;
    int rc = 1;
    struct libevdev *dev = NULL;
    ScreenPosition touchdown_pos;
    ScreenPosition touchup_pos;
    std::chrono::time_point<std::chrono::system_clock> last_release;
    const std::chrono::milliseconds cooldown = std::chrono::milliseconds(100);
    bool photo_request = false;
    bool next_shader = false;
    bool prev_shader = false;

    enum TouchState {
        RELEASED,
        PRESSED,
        TRIGGERED,
        DRAGGED
    };
    TouchState touch_state;

    enum DragDirection {
        NONE,
        LEFT,
        RIGHT,
        UP,
        DOWN
    };
    DragDirection drag_direction;

    void HandlePosX(int val) {
        if (touch_state == TouchState::PRESSED) {
            touchup_pos.pos_x = val;
        }
        else {
            touchdown_pos.pos_x = val;
            touchup_pos.pos_x = val;
        }
    }

    void HandlePosY(int val) {
        if (touch_state == TouchState::PRESSED) {
            touchup_pos.pos_y = val;
        }
        else {
            touchdown_pos.pos_y = val;
            touchup_pos.pos_y = val;
        }
    }

    void HandleTouchDown() {
        std::chrono::duration<float> elapsed_ms = std::chrono::system_clock::now() - last_release;
        if (touch_state == TouchState::RELEASED && elapsed_ms > cooldown) {
            touch_state = TouchState::PRESSED;
        }
    }

    void HandleTouchUp() {
        DetectDirection(touchdown_pos, touchup_pos);
        touch_state = TouchState::TRIGGERED;
    }
    
    void ProcessTouchState() {
        if (touch_state == TouchState::TRIGGERED && drag_direction == DragDirection::NONE) {
            RequestPhoto();
            touch_state = TouchState::RELEASED;
            last_release = std::chrono::system_clock::now();
        }
        else if (touch_state == TouchState::TRIGGERED && drag_direction == DragDirection::LEFT) {
            NextShader();
            touch_state = TouchState::RELEASED;
            last_release = std::chrono::system_clock::now();
        }
        else if (touch_state == TouchState::TRIGGERED && drag_direction == DragDirection::RIGHT) {
            PrevShader();
            touch_state = TouchState::RELEASED;
            last_release = std::chrono::system_clock::now();
        }
        else if (touch_state == TouchState::TRIGGERED && drag_direction == DragDirection::UP) {
            touch_state = TouchState::RELEASED;
            last_release = std::chrono::system_clock::now();
        }
        else if (touch_state == TouchState::TRIGGERED && drag_direction == DragDirection::DOWN) {
            touch_state = TouchState::RELEASED;
            last_release = std::chrono::system_clock::now();
        }

    }

    void RequestPhoto() {
        photo_request = true;
    }

    void NextShader() {
        next_shader = true;
    }

    void PrevShader() {
        prev_shader = true;
    }

    void DetectDirection(ScreenPosition initial_pos, ScreenPosition final_pos) {
        int delta_x = final_pos.pos_x - initial_pos.pos_x;
        int delta_y = final_pos.pos_y - initial_pos.pos_y;

        const int mean_square_threshold = 400;
        int mean_square = delta_x*delta_x + delta_y*delta_y;
        LOG << "<" << initial_pos.pos_x << ", " << initial_pos.pos_y << ">   <" << final_pos.pos_x << ", " << final_pos.pos_y << ">\n";
        bool is_horizontal = std::abs(delta_x) < std::abs(delta_y); // screen treats vertical as normal orientation

        if (mean_square < mean_square_threshold) {
            drag_direction = DragDirection::NONE;
            LOG << "drag none\n";
        }
        else if (is_horizontal && delta_y < 0) {
            drag_direction = DragDirection::LEFT;
            LOG << "drag left\n";
        }
        else if (is_horizontal && delta_y > 0) {
            drag_direction = DragDirection::RIGHT;
            LOG << "drag right\n";
        }
        else if (!is_horizontal && delta_x > 0) {
            drag_direction = DragDirection::UP;
            LOG << "drag up\n";
        }
        else if (!is_horizontal && delta_x < 0) {
            drag_direction = DragDirection::DOWN;
            LOG << "drag down\n";
        }
        else {
            LOG << "drag not detected properly\n";
        }
    }
        
        
          
};

#endif // TOUCHSCREEN_HPP
