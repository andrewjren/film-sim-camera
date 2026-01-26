#include <memory>
#include <gbm.h>
#include <EGL/egl.h>
//#include <GLES2/gl2.h>
#include <GLES3/gl3.h>
#define STB_IMAGE_IMPLEMENTATION
#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "stb_image.h"
#include "stb_image_write.h"
#include <PiCamera.hpp>
#include <FrameManager.hpp>
#include <log.hpp>
#include <Drm.hpp>
#include <Touchscreen.hpp>
#include <chrono>
#include <ShaderManager.hpp>

/*
 * Finally! We have a connector with a suitable CRTC. We know which mode we want
 * to use and we have a framebuffer of the correct size that we can write to.
 * There is nothing special left to do. We only have to program the CRTC to
 * connect each new framebuffer to each selected connector for each combination
 * that we saved in the global modeset_list.
 * This is done with a call to drmModeSetCrtc().
 *
 * So we are ready for our main() function. First we check whether the user
 * specified a DRM device on the command line, otherwise we use the default
 * /dev/dri/card0. Then we open the device via modeset_open(). modeset_prepare()
 * prepares all connectors and we can loop over "modeset_list" and call
 * drmModeSetCrtc() on every CRTC/connector combination.
 *
 * But printing empty black pages is boring so we have another helper function
 * modeset_draw() that draws some colors into the framebuffer for 5 seconds and
 * then returns. And then we have all the cleanup functions which correctly free
 * all devices again after we used them. All these functions are described below
 * the main() function.
 *
 * As a side note: drmModeSetCrtc() actually takes a list of connectors that we
 * want to control with this CRTC. We pass only one connector, though. As
 * explained earlier, if we used multiple connectors, then all connectors would
 * have the same controlling framebuffer so the output would be cloned. This is
 * most often not what you want so we avoid explaining this feature here.
 * Furthermore, all connectors will have to run with the same mode, which is
 * also often not guaranteed. So instead, we only use one connector per CRTC.
 *
 * Before calling drmModeSetCrtc() we also save the current CRTC configuration.
 * This is used in modeset_cleanup() to restore the CRTC to the same mode as was
 * before we changed it.
 * If we don't do this, the screen will stay blank after we exit until another
 * application performs modesetting itself.
 */

int main(int argc, char **argv)
{
    int ret, fd;
    const char *card;
    struct modeset_dev *iter;
    //EGLDisplay display;

	std::shared_ptr<FrameManager> frame_manager = std::make_shared<FrameManager>();
    std::unique_ptr<ShaderManager> shader_manager(new ShaderManager());

    std::unique_ptr<PiCamera> picamera(new PiCamera());
	picamera->Initialize();
    //picamera.StartViewfinder();
	picamera->SetFrameManager(frame_manager);

    if (argc < 2) {
        LOG << "not enough arguments\n";
        return -1;
    }
    std::unique_ptr<Touchscreen> touchscreen(new Touchscreen(argv[1]));
    //touchscreen->PollEvents();

    /* check which DRM device to open */
//    if (argc > 1)
//        card = argv[1];
//    else
        card = "/dev/dri/card1";

    fprintf(stderr, "using card '%s'\n", card);

    /* open the DRM device */
    ret = modeset_open(&fd, card);
    if (ret)
    {
        if (ret) {
            errno = -ret;
            fprintf(stderr, "modeset failed with error %d: %m\n", errno);
        } 
        else {
            fprintf(stderr, "exiting\n");
        }
        return ret;
    }

    /* prepare all connectors and CRTCs */
    ret = modeset_prepare(fd);

    if (ret) {
        close(fd);
        if (ret) {
            errno = -ret;
            fprintf(stderr, "modeset failed with error %d: %m\n", errno);
        } 
        else {
            fprintf(stderr, "exiting\n");
        }
        return ret;
    }

    /* perform actual modesetting on each found connector+CRTC */
    for (iter = modeset_list; iter; iter = iter->next) {
        iter->saved_crtc = drmModeGetCrtc(fd, iter->crtc);
        ret = drmModeSetCrtc(fd, iter->crtc, iter->fb, 0, 0,
                     &iter->conn, 1, &iter->mode);
        if (ret)
            fprintf(stderr, "cannot set CRTC for connector %u (%d): %m\n",
                iter->conn, errno);
    }

    /* OpenGL stuff */ 
    shader_manager->Initialize_OpenGL();
    shader_manager->Init_Transformation_Matrix();
	
    shader_manager->InitCaptureProgram();
    shader_manager->InitViewfinderProgram();
    shader_manager->TestProgram();
    shader_manager->BindTextures();

	picamera->StartCamera();
    //picamera.CreateRequests();
    
    // initialize variables
    int num_frame = 0;
    bool photo_requested = false;
    int pixel_dims = shader_manager->GetHeight() * shader_manager->GetWidth();
    std::vector<uint8_t> vec_frame;
	vec_frame.resize(pixel_dims * 4);
    std::vector<uint8_t> cap_frame;
    cap_frame.resize(pixel_dims * 1.5); // YUV420 encoding 
    //glBindBuffer(GL_PIXEL_UNPACK_BUFFER, 0);
    std::vector<unsigned char> drm_preview(640*480*4);
    std::vector<unsigned char> rgb_out(pixel_dims * 4);
    void* ptr; 
    int lut_index = 0;
    while(num_frame < 1000) {

            std::chrono::time_point<std::chrono::system_clock> start_time = std::chrono::system_clock::now();
        touchscreen->PollEvents();
        photo_requested = touchscreen->ProcessPhotoRequest();
        shader_manager->IncReadWriteIndex(num_frame);
        

        if (photo_requested) {
            LOG << "Frame: " << num_frame << std::endl;
            LOG << "Starting Capture..." << std::endl;
	        frame_manager->swap_capture(cap_frame); 
            
            ptr = shader_manager->StillCaptureRender(cap_frame, picamera->stride);
			if (ptr) {
				// Get data out of buffer
				memcpy(rgb_out.data(), ptr, pixel_dims * 4);
				glUnmapBuffer(GL_PIXEL_PACK_BUFFER);
			}
			else {
				LOG << "full frame pointer fail" << std::endl;
			}
			glBindBuffer(GL_PIXEL_PACK_BUFFER, 0);


            stbi_write_png("debug-capture.png", shader_manager->GetWidth(), shader_manager->GetHeight(), 4, rgb_out.data(), shader_manager->GetWidth()*4);
            lut_index = (lut_index + 1) % shader_manager->GetNumLuts(); 
            shader_manager->load_lut(lut_index);
            num_frame++;
        }
        
        if (frame_manager->data_available()) {
            LOG << "Frame: " << num_frame << std::endl;
            // get data 
            frame_manager->swap_buffers(vec_frame);
            void* ptr = shader_manager->ViewfinderRender(vec_frame);

			if (ptr) {
				// write to DRM display
				for (iter = modeset_list; iter; iter = iter->next) {
					memcpy(&iter->map[0],ptr,640*480*4);
				}
				glUnmapBuffer(GL_PIXEL_PACK_BUFFER);
			}
			else {
				LOG << "drm pointer fail" << std::endl;
			}
			glBindBuffer(GL_PIXEL_PACK_BUFFER, 0);

/* testing for GBM
            eglSwapBuffers(display, surface);
            struct gbm_bo * gbm_buffer_object = gbm_surface_lock_front_buffer(gbmSurface);
            int fb_id = get_drm_fb_for_bo(gbm_buffer_object);
            drmModePageFlip(fd, iter->crtc, fb_id, DRM_MODE_PAGE_FLIP_EVENT, NULL);

            gbm_surface_release_buffer(gbmSurface, gbm_buffer_object);
*/

            std::chrono::duration<float> elapsed_ms = std::chrono::system_clock::now() - start_time;
            LOG << "Frame Time: " << elapsed_ms.count() << "\n";
            num_frame++;
			
			
        }

    }

    /* cleanup everything */
    modeset_cleanup(fd);

    ret = 0;

    close(fd);
    if (ret) {
        errno = -ret;
        fprintf(stderr, "modeset failed with error %d: %m\n", errno);
    } else {
        fprintf(stderr, "exiting\n");
    }
    return ret;
}

