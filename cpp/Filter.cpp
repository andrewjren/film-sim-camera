#include <Filter.hpp>
#include <iterator>
#include <fstream>
#include <string>
#include <log.hpp>

// Get the EGL error back as a string. Useful for debugging.
const char *Filter::eglGetErrorStr()
{
    switch (eglGetError())
    {
    case EGL_SUCCESS:
        return "The last function succeeded without error.";
    case EGL_NOT_INITIALIZED:
        return "EGL is not initialized, or could not be initialized, for the "
               "specified EGL display connection.";
    case EGL_BAD_ACCESS:
        return "EGL cannot access a requested resource (for example a context "
               "is bound in another thread).";
    case EGL_BAD_ALLOC:
        return "EGL failed to allocate resources for the requested operation.";
    case EGL_BAD_ATTRIBUTE:
        return "An unrecognized attribute or attribute value was passed in the "
               "attribute list.";
    case EGL_BAD_CONTEXT:
        return "An EGLContext argument does not name a valid EGL rendering "
               "context.";
    case EGL_BAD_CONFIG:
        return "An EGLConfig argument does not name a valid EGL frame buffer "
               "configuration.";
    case EGL_BAD_CURRENT_SURFACE:
        return "The current surface of the calling thread is a window, pixel "
               "buffer or pixmap that is no longer valid.";
    case EGL_BAD_DISPLAY:
        return "An EGLDisplay argument does not name a valid EGL display "
               "connection.";
    case EGL_BAD_SURFACE:
        return "An EGLSurface argument does not name a valid surface (window, "
               "pixel buffer or pixmap) configured for GL rendering.";
    case EGL_BAD_MATCH:
        return "Arguments are inconsistent (for example, a valid context "
               "requires buffers not supplied by a valid surface).";
    case EGL_BAD_PARAMETER:
        return "One or more argument values are invalid.";
    case EGL_BAD_NATIVE_PIXMAP:
        return "A NativePixmapType argument does not refer to a valid native "
               "pixmap.";
    case EGL_BAD_NATIVE_WINDOW:
        return "A NativeWindowType argument does not refer to a valid native "
               "window.";
    case EGL_CONTEXT_LOST:
        return "A power management event has occurred. The application must "
               "destroy all contexts and reinitialise OpenGL ES state and "
               "objects to continue rendering.";
    default:
        break;
    }
    return "Unknown error!";
}

void Filter::CheckGlCompileErrors(GLuint shader)
{
    GLint isCompiled = 0;
    glGetShaderiv(shader, GL_COMPILE_STATUS, &isCompiled);
    if(isCompiled == GL_FALSE)
    {
        GLchar infoLog[512];
        glGetShaderInfoLog(shader, 512, NULL, infoLog);
        LOG_ERR << "ERROR: Shader Compilation Fail: " << infoLog << std::endl;
    }
}

void Filter::LoadViewfinderShader() {
    // Read input files 
    std::ifstream vs_file {"view_vs.glsl"};
    std::ifstream fs_file {"view_fs.glsl"};

    std::string vs_code { istreambuf_iterator<char>(vs_file), istreambuf_iterator<char>() };
    std::string fs_code { istreambuf_iterator<char>(fs_file), istreambuf_iterator<char>() };

    // Create shader program for Viewfinder
    // Create a shader program
    // NO ERRRO CHECKING IS DONE! (for the purpose of this example)
    viewfinder_program = glCreateProgram();
    GLuint vert = glCreateShader(GL_VERTEX_SHADER);
    glShaderSource(vert, 1, vs_code.c_str(), NULL);
    glCompileShader(vert);
    checkGlCompileErrors(vert);

    GLuint frag = glCreateShader(GL_FRAGMENT_SHADER);
    glShaderSource(frag, 1, fs_code.c_str(), NULL);
    glCompileShader(frag);
    checkGlCompileErrors(frag);

    glAttachShader(viewfinder_program, frag);
    glAttachShader(viewfinder_program, vert);

    glLinkProgram(viewfinder_program);
}

void Filter::LoadStillCaptureShader() {
    // Read input files 
    std::ifstream vs_file {"still_vs.glsl"};
    std::ifstream fs_file {"still_fs.glsl"};

    std::string vs_code { istreambuf_iterator<char>(vs_file), istreambuf_iterator<char>() };
    std::string fs_code { istreambuf_iterator<char>(fs_file), istreambuf_iterator<char>() };

    // Create shader program for Viewfinder
    // Create a shader program
    // NO ERRRO CHECKING IS DONE! (for the purpose of this example)
    stillcapture_program = glCreateProgram();
    GLuint vert = glCreateShader(GL_VERTEX_SHADER);
    glShaderSource(vert, 1, vs_code.c_str(), NULL);
    glCompileShader(vert);
    checkGlCompileErrors(vert);

    GLuint frag = glCreateShader(GL_FRAGMENT_SHADER);
    glShaderSource(frag, 1, fs_code.c_str(), NULL);
    glCompileShader(frag);
    checkGlCompileErrors(frag);

    glAttachShader(stillcapture_program, frag);
    glAttachShader(stillcapture_program, vert);

    glLinkProgram(stillcapture_program);
}

int Filter::InitializeOpenGL(const int width, const int height) {
    int major, minor;
    GLint colorLoc, result;

    device = open("/dev/dri/card1", O_RDWR | O_CLOEXEC);
    if (getDisplay(&display) != 0)
    {
        fprintf(stderr, "Unable to get EGL display\n");
        close(device);
        return -1;
    }
    if (eglInitialize(display, &major, &minor) == EGL_FALSE)
    {
        fprintf(stderr, "Failed to get EGL version! Error: %s\n", eglGetErrorStr());
        eglTerminate(display);
        gbmClean();
        return EXIT_FAILURE;
    }

    // Make sure that we can use OpenGL in this EGL app.
    eglBindAPI(EGL_OPENGL_API);

    printf("Initialized EGL version: %d.%d\n", major, minor);
}