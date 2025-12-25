
//#include <iostream>
#include <memory>
#include <thread>
#include <queue>
#include <mutex>
#include <condition_variable>
#include <utility>
#define STB_IMAGE_IMPLEMENTATION
#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "stb_image.h"
#include "stb_image_write.h"
#include <PiCamera.hpp>
#include <FrameManager.hpp>
#include <log.hpp>
#include <Drm.hpp>

// added to make render loop easier
static int test_width, test_height, test_nrChannels;
static unsigned int dstFBO, dstTex;
static unsigned int lut_texture;
static unsigned int test_texture;
static unsigned int input_pbo;
static unsigned int lut_pbo;
static unsigned int output_pbo;
static unsigned int y_texture, u_texture, v_texture;
static unsigned int rgb_pbo;
static unsigned int yTextureLoc, uTextureLoc, vTextureLoc, lutTextureLoc;
static GLuint vao,vbo;
static GLuint yuv2rgb_program, yuv2rgb_vert, yuv2rgb_frag;
static EGLDisplay display;
static EGLSurface surface;
static EGLContext context;

const int screen_width = 640;
const int screen_height = 480;


// The following are GLSL shaders for rendering a triangle on the screen
//#define STRINGIFY(x) #x
static const char *vertexShaderCode = R"(
#version 300 es
in vec2 aPos;
in vec2 aTexCoord;
out vec2 TexCoord;
uniform mat4 transform;
void main() {
	gl_Position = transform * vec4(aPos, 0.0, 1.0);
    TexCoord = aTexCoord;
}
)";

static const char *fragmentShaderCode = R"(
#version 300 es
precision highp float;
precision highp sampler3D;
uniform sampler2D image;
uniform sampler3D clut;
in vec2 TexCoord;
out vec4 fragColor;

void main()
{
    vec4 color;
    color = texture(image, TexCoord);
    color = texture(clut, color.rgb);
    fragColor = color;
}
)";

static const char *yuv2rgb_vertex_shader_code = R"(
#version 300 es
in vec2 aPos;
in vec2 aTexCoord;
out vec2 TexCoord;
uniform mat4 rotate;

void main()
{
    gl_Position = rotate * vec4(aPos, 0.0, 1.0);
    TexCoord = aTexCoord;
}
)";

static const char *yuv2rgb_fragment_shader_code = R"(
#version 300 es
precision highp float;
precision highp sampler3D;
in vec2 TexCoord;
out vec4 fragColor;
uniform sampler2D yTexture;
uniform sampler2D uTexture;
uniform sampler2D vTexture;
uniform sampler3D clut;

void main()
{
    float y = texture(yTexture, TexCoord).r;
    float u = texture(uTexture, TexCoord).r - 0.5;
    float v = texture(vTexture, TexCoord).r - 0.5;
    
    //YUV to RGB conversion matrix (BT.601)
    //Note that opengl defines columns first, so the first three elements are in column 1
    mat3 yuvToRgb = mat3(
        1.000, 1.000, 1.000,
        0.000, -0.3441, 1.7720,
        1.4020, -0.7141, 0.000
    );

    // BT.709 (HDTV standard)
    //mat3 yuvToRgb = mat3(
    //    1.000,  0.000,  1.5748,
    //    1.000, -0.1873, -0.4681,
    //    1.000,  1.8556,  0.000
    //);   

    vec3 orig_color;
    orig_color = yuvToRgb * vec3(y, u, v);
    
    // Clamp to valid range
    orig_color = clamp(orig_color, 0.0, 1.0);
    fragColor = texture(clut, orig_color);
}
)";



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

    std::unique_ptr<PiCamera> picamera(new PiCamera());
	picamera->Initialize();
    //picamera.StartViewfinder();
	picamera->SetFrameManager(frame_manager);

    /* check which DRM device to open */
    if (argc > 1)
        card = argv[1];
    else
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

    // We will use the screen resolution as the desired width and height for the viewport.
    int desiredWidth = 1296;
    int desiredHeight = 972;
    int test_width = 1296;
    int test_height = 972;


    EGLint count;
    EGLint numConfigs;
    eglGetConfigs(display, NULL, 0, &count);
    EGLConfig *configs = static_cast<EGLConfig*>(malloc(count * sizeof(configs)));

    if (!eglChooseConfig(display, configAttribs, configs, count, &numConfigs))
    {
        fprintf(stderr, "Failed to get EGL configs! Error: %s\n", eglGetErrorStr());
        eglTerminate(display);
        gbmClean();
        return EXIT_FAILURE;
    }

    // I am not exactly sure why the EGL config must match the GBM format.
    // But it works!
    int configIndex = matchConfigToVisual(display, GBM_FORMAT_XRGB8888, configs, numConfigs);
    if (configIndex < 0)
    {
        fprintf(stderr, "Failed to find matching EGL config! Error: %s\n", eglGetErrorStr());
        eglTerminate(display);
        gbm_surface_destroy(gbmSurface);
        gbm_device_destroy(gbmDevice);
        return EXIT_FAILURE;
    }

    context = eglCreateContext(display, configs[configIndex], EGL_NO_CONTEXT, contextAttribs);
    if (context == EGL_NO_CONTEXT)
    {
        fprintf(stderr, "Failed to create EGL context! Error: %s\n", eglGetErrorStr());
        eglTerminate(display);
        gbmClean();
        return EXIT_FAILURE;
    }

    surface = eglCreateWindowSurface(display, configs[configIndex], (EGLNativeWindowType) gbmSurface, NULL);
    if (surface == EGL_NO_SURFACE)
    {
        fprintf(stderr, "Failed to create EGL surface! Error: %s\n", eglGetErrorStr());
        eglDestroyContext(display, context);
        eglTerminate(display);
        gbmClean();
        return EXIT_FAILURE;
    }

    free(configs);
    eglMakeCurrent(display, surface, surface, context);

    // Set GL Viewport size, always needed!
    glViewport(0, 0, desiredWidth, desiredHeight);

    // Get GL Viewport size and test if it is correct.
    GLint viewport[4];
    glGetIntegerv(GL_VIEWPORT, viewport);

    // viewport[2] and viewport[3] are viewport width and height respectively
    printf("GL Viewport size: %dx%d\n", viewport[2], viewport[3]);

    if (viewport[2] != desiredWidth || viewport[3] != desiredHeight)
    {
        fprintf(stderr, "Error! The glViewport returned incorrect values! Something is wrong!\n");
        eglDestroyContext(display, context);
        eglDestroySurface(display, surface);
        eglTerminate(display);
        gbmClean();
        return EXIT_FAILURE;
    }

	// Transformation Matrix
	float scale = float(screen_width) / float(test_width);
	glm::mat4 trans_mat = glm::mat4(1.0f);
	trans_mat = glm::translate(trans_mat, glm::vec3(-0.6f, -0.4f, 0.0f));
	trans_mat = glm::rotate(trans_mat, glm::radians(-90.0f), glm::vec3(0.0, 0.0, 1.0));
	trans_mat = glm::scale(trans_mat, glm::vec3(scale*4.0/3.0, scale*3.0/4.0, 1.0f));

    glm::mat4 rot_mat = glm::mat4(1.0f);
    rot_mat = glm::rotate(rot_mat, glm::radians(180.0f), glm::vec3(0.0, 0.0, 1.0));
	
    // Create shader program for YUV to RGB conversion
    yuv2rgb_program = glCreateProgram();
    yuv2rgb_vert = glCreateShader(GL_VERTEX_SHADER);
    glShaderSource(yuv2rgb_vert, 1, &yuv2rgb_vertex_shader_code, NULL);
    glCompileShader(yuv2rgb_vert);
    checkGlCompileErrors(yuv2rgb_vert);

    yuv2rgb_frag = glCreateShader(GL_FRAGMENT_SHADER);
    glShaderSource(yuv2rgb_frag, 1, &yuv2rgb_fragment_shader_code, NULL);
    glCompileShader(yuv2rgb_frag);
    checkGlCompileErrors(yuv2rgb_frag);
    GLint isCompiled = 0;
    glGetShaderiv(yuv2rgb_frag, GL_COMPILE_STATUS, &isCompiled);
    if(isCompiled == GL_FALSE)
    {
        GLchar infoLog[512];
        glGetShaderInfoLog(yuv2rgb_frag, 512, NULL, infoLog);
        LOG_ERR << "ERROR: Shader Compilation Fail: " << infoLog << std::endl;
    }

    glAttachShader(yuv2rgb_program, yuv2rgb_frag);
    glAttachShader(yuv2rgb_program, yuv2rgb_vert);
    glLinkProgram(yuv2rgb_program);
    LOG << "link yuv2rgb shader: " << glGetError() << std::endl;

    //glDeleteShader(yuv2rgb_frag);
    //glDeleteShader(yuv2rgb_vert);
    LOG << "creating YUV to RGB program: " << glGetError() << std::endl;

glValidateProgram(yuv2rgb_program);

GLint valid;
glGetProgramiv(yuv2rgb_program, GL_VALIDATE_STATUS, &valid);
if (!valid) {
    GLchar infoLog[1024];
    glGetProgramInfoLog(yuv2rgb_program, 1024, NULL, infoLog);
    LOG_ERR << "Validation error:\n" << infoLog << std::endl;
}
    glUseProgram(yuv2rgb_program);
     LOG << "Using yuv program: " << glGetError() << std::endl;

   
    yTextureLoc = glGetUniformLocation(yuv2rgb_program, "yTexture");
    uTextureLoc = glGetUniformLocation(yuv2rgb_program, "uTexture");
    vTextureLoc = glGetUniformLocation(yuv2rgb_program, "vTexture");
    lutTextureLoc = glGetUniformLocation(yuv2rgb_program, "clut");
    unsigned int rot_loc = glGetUniformLocation(yuv2rgb_program, "rotate");
 
    glActiveTexture(GL_TEXTURE2);
    glBindTexture(GL_TEXTURE_2D, y_texture);
    glUniform1i(yTextureLoc, 2);

    glActiveTexture(GL_TEXTURE3);
    glBindTexture(GL_TEXTURE_2D, u_texture);
    glUniform1i(uTextureLoc, 3);

    glActiveTexture(GL_TEXTURE4);
    glBindTexture(GL_TEXTURE_2D, v_texture);
    glUniform1i(vTextureLoc, 4);

    glActiveTexture(GL_TEXTURE1);
    glBindTexture(GL_TEXTURE_3D, lut_texture);
    glUniform1i(lutTextureLoc, 1);

    LOG << "yuv texture locs: " << yTextureLoc << ", " << uTextureLoc << ", " << vTextureLoc << ", " << lutTextureLoc << ", " << rot_loc << std::endl;
    glUniformMatrix4fv(rot_loc, 1, GL_FALSE, glm::value_ptr(rot_mat));



    // Create shader program for Viewfinder
    // Create a shader program
    // NO ERRRO CHECKING IS DONE! (for the purpose of this example)
    // Read an OpenGL tutorial to properly implement shader creation
    program = glCreateProgram();
    vert = glCreateShader(GL_VERTEX_SHADER);
    glShaderSource(vert, 1, &vertexShaderCode, NULL);
    glCompileShader(vert);
    checkGlCompileErrors(vert);

    frag = glCreateShader(GL_FRAGMENT_SHADER);
    glShaderSource(frag, 1, &fragmentShaderCode, NULL);
    glCompileShader(frag);
    checkGlCompileErrors(frag);

    glAttachShader(program, frag);
    glAttachShader(program, vert);
    LOG << "attaching shaders: " << glGetError() << std::endl;
    glLinkProgram(program);
    LOG << "linking program: " << glGetError() << std::endl;
    glUseProgram(program);

    //glDeleteShader(frag);
    //glDeleteShader(vert);

    size_t image_size = test_width * test_height * 4; // RGBA

    LOG << "Set Image Size: " << test_width << ", " << test_height << std::endl;

    // setup texture for input images (from camera)
    glGenTextures(1, &test_texture); 
    glBindTexture(GL_TEXTURE_2D, test_texture);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, test_width, test_height, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr); 
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glBindTexture(GL_TEXTURE_2D, 0);

    // setup pbo for input images (from camera)
    glGenBuffers(1, &input_pbo);
    glBindBuffer(GL_PIXEL_UNPACK_BUFFER, input_pbo);
    glBufferData(GL_PIXEL_UNPACK_BUFFER, image_size, nullptr, GL_DYNAMIC_DRAW);
    glBindBuffer(GL_PIXEL_UNPACK_BUFFER, 0); // unbind

    // setup pbo for output image (to screen)
    glGenBuffers(1, &output_pbo);
    glBindBuffer(GL_PIXEL_PACK_BUFFER, output_pbo);
    glBufferData(GL_PIXEL_PACK_BUFFER, image_size, nullptr, GL_DYNAMIC_READ);
    glBindBuffer(GL_PIXEL_PACK_BUFFER, 0); // unbind

    // setup texture for YUV input images (from camera)
    glGenTextures(1, &y_texture);
    glBindTexture(GL_TEXTURE_2D, y_texture);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_R8, test_width, test_height, 0, GL_RED, GL_UNSIGNED_BYTE, nullptr); 
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glBindTexture(GL_TEXTURE_2D, 0);

    glGenTextures(1, &u_texture);
    glBindTexture(GL_TEXTURE_2D, u_texture);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_R8, test_width/2, test_height/2, 0, GL_RED, GL_UNSIGNED_BYTE, nullptr); 
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glBindTexture(GL_TEXTURE_2D, 0);

    glGenTextures(1, &v_texture);
    glBindTexture(GL_TEXTURE_2D, v_texture);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_R8, test_width/2, test_height/2, 0, GL_RED, GL_UNSIGNED_BYTE, nullptr); 
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glBindTexture(GL_TEXTURE_2D, 0);



    // setup pbo for output image (to file)
    glGenBuffers(1, &rgb_pbo);
    glBindBuffer(GL_PIXEL_PACK_BUFFER, rgb_pbo);
    glBufferData(GL_PIXEL_PACK_BUFFER, image_size, nullptr, GL_DYNAMIC_READ);
    glBindBuffer(GL_PIXEL_PACK_BUFFER, 0); // unbind


    // load lut image 
    int lut_width, lut_height, lut_depth, lut_nrChannels;
    unsigned char *lut_data = stbi_load("Fuji Velvia 50.png", &lut_width, &lut_height, &lut_nrChannels, 0); 
    LOG << "CLUT dimensions: " << lut_width << " x " << lut_height << " x " << lut_depth << " x " << lut_nrChannels << "total size: " << sizeof(*lut_data) << std::endl;
    if (!lut_data)
    {
        LOG << "Failed to load texture" << std::endl;
        return 0;
    }

    size_t lut_size = lut_width * lut_height * 3; // 3D RGBA
    // setup lut texture
    glGenTextures(1, &lut_texture); 
    glBindTexture(GL_TEXTURE_3D, lut_texture);
    glTexImage3D(GL_TEXTURE_3D, 0, GL_RGB, 144, 144, 144, 0, GL_RGB, GL_UNSIGNED_BYTE, nullptr);
    glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_WRAP_R, GL_CLAMP_TO_EDGE);
    glBindTexture(GL_TEXTURE_3D, 0);

    // setup pbo for lut 
    glGenBuffers(1, &lut_pbo);
    glBindBuffer(GL_PIXEL_UNPACK_BUFFER, lut_pbo);
    glBufferData(GL_PIXEL_UNPACK_BUFFER, lut_size, nullptr, GL_DYNAMIC_DRAW);
    glBindBuffer(GL_PIXEL_UNPACK_BUFFER, 0);

    glBindBuffer(GL_PIXEL_UNPACK_BUFFER, lut_pbo);
    void* ptr = glMapBufferRange(GL_PIXEL_UNPACK_BUFFER, 0, lut_size, GL_MAP_WRITE_BIT | GL_MAP_INVALIDATE_BUFFER_BIT);
    memcpy(ptr, lut_data, lut_size);
    glUnmapBuffer(GL_PIXEL_UNPACK_BUFFER);

    glBindTexture(GL_TEXTURE_3D, lut_texture);
    glTexSubImage3D(GL_TEXTURE_3D, 0, 0, 0, 0, 144, 144, 144, GL_RGB, GL_UNSIGNED_BYTE, nullptr);
    glBindTexture(GL_TEXTURE_3D, 0);
    glBindBuffer(GL_PIXEL_UNPACK_BUFFER, 0);
    stbi_image_free(lut_data);


    LOG << "before setting uniforms: " << glGetError() << std::endl;
    // Set uniforms
    glUniform1i(glGetUniformLocation(program, "image"), 0);
    glUniform1i(glGetUniformLocation(program, "clut"), 1);

	unsigned int trans_loc = glGetUniformLocation(program, "transform");
	glUniformMatrix4fv(trans_loc, 1, GL_FALSE, glm::value_ptr(trans_mat));

    LOG << "after setting uniforms: " << glGetError() << std::endl;

    // create fbo bound to output 
    //unsigned int dstFBO, dstTex;
    
    glGenFramebuffers(1, &dstFBO);
    glBindFramebuffer(GL_FRAMEBUFFER, dstFBO);

    glGenTextures(1, &dstTex);
    glBindTexture(GL_TEXTURE_2D, dstTex);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, test_width, test_height, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, dstTex, 0);
    if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) {
        switch(glCheckFramebufferStatus(GL_FRAMEBUFFER)) {

        case GL_FRAMEBUFFER_INCOMPLETE_ATTACHMENT:
        // Attached image has width/height of 0
        LOG << "incomplete" << std::endl;
        break;
		
    	case GL_FRAMEBUFFER_INCOMPLETE_MISSING_ATTACHMENT:
        // No images attached to FBO
    	LOG << "missing attachemnt" << std::endl;
        break;

    	case GL_FRAMEBUFFER_UNSUPPORTED:
        // Format combination not supported
    	LOG << "unsupported" << std::endl;
        break;

    	default:
        printf("Unknown framebuffer error: 0x%x\n", glCheckFramebufferStatus(GL_FRAMEBUFFER));
        }

        return 0;
    }
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    

    // run GL program?
    GLuint vao,vbo;
    glGenVertexArrays(1,&vao);
    glBindVertexArray(vao);
    glGenBuffers(1,&vbo);
    glBindBuffer(GL_ARRAY_BUFFER,vbo);
    glBufferData(GL_ARRAY_BUFFER,sizeof(quad),quad,GL_STATIC_DRAW);
    GLint posLoc = glGetAttribLocation(program,"aPos");
    GLint uvLoc = glGetAttribLocation(program,"aTexCoord");
    glVertexAttribPointer(posLoc,2,GL_FLOAT,GL_FALSE,4*sizeof(float),(void*)0);
    glEnableVertexAttribArray(posLoc);
    glVertexAttribPointer(uvLoc,2,GL_FLOAT,GL_FALSE,4*sizeof(float),(void*)(2*sizeof(float)));
    glEnableVertexAttribArray(uvLoc);
    glBindBuffer(GL_ARRAY_BUFFER,0);

    LOG << "after running gL program: " << glGetError() << std::endl;
    LOG << "image_size: " << image_size << std::endl;

  
    // Bind textures
    glViewport(0,0,test_width,test_height);
    glBindFramebuffer(GL_FRAMEBUFFER, dstFBO);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, test_texture);
    glActiveTexture(GL_TEXTURE1);
    glBindTexture(GL_TEXTURE_3D, lut_texture);
    LOG << "after binding textures: " << glGetError() << std::endl;

	picamera->StartCamera();
    //picamera.CreateRequests();
    
    int once = 0;
    bool capture_started = false;
    std::vector<uint8_t> vec_frame;
	vec_frame.resize(test_width * test_height * 4);
    std::vector<uint8_t> cap_frame;
    cap_frame.resize(test_width * test_height * 1.5); // YUV420 encoding 
glBindBuffer(GL_PIXEL_UNPACK_BUFFER, 0);

    while(once < 1000) {

        if (once == 100) {
            LOG << "Frame: " << once << std::endl;
            LOG << "Starting Capture..." << std::endl;
	        frame_manager->swap_capture(cap_frame); 
            
            std::vector<uint8_t>::const_iterator y_end = cap_frame.begin() + picamera->stride*test_height;
            std::vector<uint8_t>::const_iterator u_end = y_end + picamera->stride*test_height/4;
            std::vector<uint8_t>::const_iterator v_end = cap_frame.end();

            std::vector<uint8_t> y_data(cap_frame.cbegin(), y_end);
            std::vector<uint8_t> u_data(y_end, u_end);
            std::vector<uint8_t> v_data(u_end, v_end); 
            std::vector<uint8_t> uv_data(y_end, cap_frame.cend());

            glPixelStorei(GL_UNPACK_ROW_LENGTH, picamera->stride);
            glPixelStorei(GL_UNPACK_ALIGNMENT, 1);

            glBindTexture(GL_TEXTURE_2D, y_texture);
            glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, test_width, test_height, GL_RED, GL_UNSIGNED_BYTE, y_data.data());

            glPixelStorei(GL_UNPACK_ROW_LENGTH, picamera->stride/2);

            glBindTexture(GL_TEXTURE_2D, u_texture);
            glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, test_width/2, test_height/2, GL_RED, GL_UNSIGNED_BYTE, u_data.data());

            glBindTexture(GL_TEXTURE_2D, v_texture);
            glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, test_width/2, test_height/2, GL_RED, GL_UNSIGNED_BYTE, v_data.data());

            glBindTexture(GL_TEXTURE_3D, lut_texture);

            glBindTexture(GL_TEXTURE_2D, 0);
            glPixelStorei(GL_UNPACK_ROW_LENGTH, 0);

LOG << "Program ID: " << yuv2rgb_program << std::endl;
if (yuv2rgb_program == 0) {
    LOG_ERR << "ERROR: Program ID is 0 (not created)" << std::endl;
}

GLboolean isProgram = glIsProgram(yuv2rgb_program);
LOG << "Is valid program: " << (isProgram ? "yes" : "no") << std::endl;
glValidateProgram(yuv2rgb_program);

GLint valid;
glGetProgramiv(yuv2rgb_program, GL_VALIDATE_STATUS, &valid);
if (!valid) {
    GLchar infoLog[1024];
    glGetProgramInfoLog(yuv2rgb_program, 1024, NULL, infoLog);
    LOG_ERR << "Validation error:\n" << infoLog << std::endl;
}
            glUseProgram(yuv2rgb_program);
            LOG << "Use program: " << glGetError() << std::endl;
            
            glActiveTexture(GL_TEXTURE0);
            glBindTexture(GL_TEXTURE_2D, y_texture);
            glUniform1i(yTextureLoc, 0);

            glActiveTexture(GL_TEXTURE1);
            glBindTexture(GL_TEXTURE_2D, u_texture);
            glUniform1i(uTextureLoc, 1);

            glActiveTexture(GL_TEXTURE2);
            glBindTexture(GL_TEXTURE_2D, v_texture);
            glUniform1i(vTextureLoc, 2);

            glActiveTexture(GL_TEXTURE3);
            glBindTexture(GL_TEXTURE_3D, lut_texture);
            glUniform1i(lutTextureLoc, 3);

            glBindVertexArray(vao);
            glDrawArrays(GL_TRIANGLE_FAN, 0, 4);

            // Read Framebuffer
            glBindBuffer(GL_PIXEL_PACK_BUFFER, output_pbo);
            glReadPixels(0, 0, test_width, test_height, GL_RGBA, GL_UNSIGNED_BYTE, 0);
            //ptr = (GLubyte*) glMapBuffer(GL_PIXEL_PACK_BUFFER, GL_READ_ONLY);
            ptr = glMapBufferRange(GL_PIXEL_PACK_BUFFER, 0, test_width * test_height * 4, GL_MAP_READ_BIT);

			std::vector<unsigned char> rgb_out(test_width*test_height* 4);
			if (ptr) {
				// Get data out of buffer
				memcpy(rgb_out.data(), ptr, test_width * test_height * 4);
				glUnmapBuffer(GL_PIXEL_PACK_BUFFER);
			}
			else {
				LOG << "full frame pointer fail" << std::endl;
			}
			glBindBuffer(GL_PIXEL_PACK_BUFFER, 0);


            stbi_write_png("debug-capture.png", test_width, test_height, 4, rgb_out.data(), test_width*4);
            once++;
        }
        
        if (frame_manager->data_available()) {
            LOG << "Frame: " << once << std::endl;
            // get data 
            frame_manager->swap_buffers(vec_frame);

            // Provide buffer to write to
            glBindBuffer(GL_PIXEL_UNPACK_BUFFER, input_pbo);
            void* ptr = glMapBufferRange(GL_PIXEL_UNPACK_BUFFER, 0, vec_frame.size(), GL_MAP_WRITE_BIT);
            //GLubyte* ptr = (GLubyte*) glMapBuffer(GL_PIXEL_UNPACK_BUFFER, GL_WRITE_ONLY);
            if (ptr) {
                memcpy(ptr, vec_frame.data(), vec_frame.size());
                //stbi_write_png("debug-camera.png", test_width, test_height, 4, vec_frame.data(), test_width*4); // just make sure camera is actually r
                glUnmapBuffer(GL_PIXEL_UNPACK_BUFFER);
            }
            else {
                LOG << "camera ptr error" << std::endl;
            }

            glUseProgram(program);

            // Transfer to texture
            glBindTexture(GL_TEXTURE_2D, test_texture);
            glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, test_width, test_height, GL_RGBA, GL_UNSIGNED_BYTE, 0);

            glBindTexture(GL_TEXTURE_2D, 0);
            glBindBuffer(GL_PIXEL_UNPACK_BUFFER, 0);

            // Render to Framebuffer
            glBindFramebuffer(GL_FRAMEBUFFER, dstFBO);
            glViewport(0,0,test_width,test_height);

            //glUseProgram(program);

            glActiveTexture(GL_TEXTURE0);
            glBindTexture(GL_TEXTURE_2D, test_texture);
            glActiveTexture(GL_TEXTURE1);
            glBindTexture(GL_TEXTURE_3D, lut_texture);

            glBindVertexArray(vao);
            glDrawArrays(GL_TRIANGLE_FAN, 0, 4);

            glBindTexture(GL_TEXTURE_2D, 0);
            glBindTexture(GL_TEXTURE_3D, 0);

            // Read Framebuffer
            glBindBuffer(GL_PIXEL_PACK_BUFFER, output_pbo);
            glReadPixels(0, 0, test_width, test_height, GL_RGBA, GL_UNSIGNED_BYTE, 0);
            //ptr = (GLubyte*) glMapBuffer(GL_PIXEL_PACK_BUFFER, GL_READ_ONLY);
            ptr = glMapBufferRange(GL_PIXEL_PACK_BUFFER, 0, test_width * test_height * 4, GL_MAP_READ_BIT);

			std::vector<unsigned char> full_frame(test_width*test_height* 4);
			std::vector<unsigned char> drm_preview(640*480*4);
			if (ptr) {
				// Get data out of buffer
				memcpy(full_frame.data(), ptr, test_width * test_height * 4);
				//stbi_write_png("debug-output.png", test_width, test_height, 4, full_frame.data(), test_width*4);
				glUnmapBuffer(GL_PIXEL_PACK_BUFFER);
			}
			else {
				LOG << "full frame pointer fail" << std::endl;
			}
			glBindBuffer(GL_PIXEL_PACK_BUFFER, 0);

			// Read Framebuffer for DRM preview
            glBindBuffer(GL_PIXEL_PACK_BUFFER, output_pbo);
            glReadPixels(0, 0, 480, 640, GL_RGBA, GL_UNSIGNED_BYTE, 0);
            //ptr = (GLubyte*) glMapBuffer(GL_PIXEL_PACK_BUFFER, GL_READ_ONLY);
            ptr = glMapBufferRange(GL_PIXEL_PACK_BUFFER, 0, 640 * 480 * 4, GL_MAP_READ_BIT);

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
            once++;
			
			
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

