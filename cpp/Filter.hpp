#ifndef FILTER_HPP
#define FILTER_HPP

#include <gbm.h>
#include <EGL/egl.h>
//#include <GLES2/gl2.h>
#include <GLES3/gl3.h>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>

// Filter Class is used to access OpenGL interface

class Filter {

private: 
// The following code was adopted from
// https://github.com/matusnovak/rpi-opengl-without-x/blob/master/triangle.c
// and is licensed under the Unlicense.
static const EGLint configAttribs[] = {
    EGL_RED_SIZE, 8,
    EGL_GREEN_SIZE, 8,
    EGL_BLUE_SIZE, 8,
    EGL_DEPTH_SIZE, 8,
    EGL_RENDERABLE_TYPE, EGL_OPENGL_ES2_BIT,
    EGL_NONE};

static const EGLint contextAttribs[] = {
    EGL_CONTEXT_CLIENT_VERSION, 2,
    EGL_NONE};

// Setup full screen quad
float quad[] = {
  -1, -1, 0, 0,
  1, -1, 1, 0,
  1,  1, 1, 1,
  -1,  1, 0, 1
};

GLuint viewfinder_program;
GLuint stillcapture_program;
int device;

// Get the EGL error back as a string. Useful for debugging.
static const char *eglGetErrorStr();
static void CheckGlCompileErrors(GLuint);

void LoadViewfinderShader();
void LoadStillCaptureShader();

public:
void InitializeOpenGL(const int, const int);





}; 




#endif // FILTER_HPP