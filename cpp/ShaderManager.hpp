#ifndef SHADERMANAGER_HPP
#define SHADERMANAGER_HPP

#include <string>
#include <filesystem>
#include <cstdlib>
#include <fstream>
#include <sstream>
#include <functional>
#include <map>
#include <EGL/egl.h>
#include <GLES3/gl3.h>
#include <log.hpp>
#include <vector>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <ft2build.h>
#include FT_FREETYPE_H

/// Holds all state information relevant to a character as loaded using FreeType
struct Character {
    unsigned int TextureID; // ID handle of the glyph texture
    glm::ivec2   Size;      // Size of glyph
    glm::ivec2   Bearing;   // Offset from baseline to left/top of glyph
    unsigned int Advance;   // Horizontal offset to advance to next glyph
};

class ShaderManager {

private:
    int test_nrChannels;
    unsigned int dstFBO, dstTex;
    unsigned int lut_texture;
    unsigned int test_texture;
    unsigned int input_pbo[3];
    unsigned int lut_pbo;
    unsigned int output_pbo[3];
    const unsigned int num_buffers = 3;
    unsigned int y_texture, u_texture, v_texture;
    unsigned int rgb_pbo;
    unsigned int yTextureLoc, uTextureLoc, vTextureLoc, lutTextureLoc;
    GLuint vao,vbo;
    GLuint text_vao, text_vbo;
    GLuint program, vert, frag;
    GLuint yuv2rgb_program, yuv2rgb_vert, yuv2rgb_frag;
    EGLDisplay display;
    EGLSurface surface;
    EGLContext context;
    int lut_width, lut_height, lut_depth, lut_nrChannels;
    std::string lut_dir = std::string(std::getenv("HOME")) + "/codac/lut/";
    std::vector<std::filesystem::path> lut_files;
    std::vector<unsigned char *> lut_data;
    std::string viewfinder_vs_path = std::string(std::getenv("HOME")) + "/codac/shader/viewfinder_vs.glsl";
    std::string viewfinder_fs_path = std::string(std::getenv("HOME")) + "/codac/shader/viewfinder_fs.glsl";
    std::string stillcapture_vs_path = std::string(std::getenv("HOME")) + "/codac/shader/stillcapture_vs.glsl";
    std::string stillcapture_fs_path = std::string(std::getenv("HOME")) + "/codac/shader/stillcapture_fs.glsl";
    std::string text_vs_path = std::string(std::getenv("HOME")) + "/codac/shader/text_vs.glsl";
    std::string text_fs_path = std::string(std::getenv("HOME")) + "/codac/shader/text_fs.glsl";

    GLuint text_program, text_vert, text_frag;

    size_t image_size; 
    int read_index;
    int write_index;
    int lut_index = 0;
    int desiredWidth = 1296; // TODO: Remove instances of desired width and height
    int desiredHeight = 972;
    int test_width = 1296;
    int test_height = 972;
    const int screen_width = 640;
    const int screen_height = 480;

    glm::mat4 trans_mat;
    glm::mat4 rot_mat;

    std::map<GLchar, Character> Characters;

    static inline const EGLint configAttribs[] = {
        EGL_RED_SIZE, 8,
        EGL_GREEN_SIZE, 8,
        EGL_BLUE_SIZE, 8,
        EGL_DEPTH_SIZE, 8,
        EGL_RENDERABLE_TYPE, EGL_OPENGL_ES2_BIT,
        EGL_NONE};

    static inline const EGLint contextAttribs[] = {
        EGL_CONTEXT_CLIENT_VERSION, 2,
        EGL_NONE};

    // Setup full screen quad
    static inline const float quad[] = {
      -1, -1, 0, 0,
      1, -1, 1, 0,
      1,  1, 1, 1,
      -1,  1, 0, 1
    };

    static void CheckGlCompileErrors(GLuint);
    static const char *eglGetErrorStr();
    static void ValidateProgram(GLuint);

public:
    ShaderManager() {
        trans_mat = glm::mat4(1.0f);
        rot_mat = glm::mat4(1.0f);

    }

    void SwitchLUT(int);
    void LoadLUTs();
    void LoadShader(GLuint &, const std::string &);
    int InitOpenGL();
    void InitTransformationMatrix(); 
    void BindTextures();
    void TestProgram();
    void InitCaptureProgram();
    void InitViewfinderProgram();
    void ViewfinderRender(std::vector<uint8_t> &,  std::function<void(void*, size_t)>);
    void StillCaptureRender(std::vector<uint8_t> &, int, std::function<void(void*, size_t)>); 
    void IncReadWriteIndex(int);

    // Font Management
    void InitFreetype();
    void RenderText(std::string, float, float, float, glm::vec3);

    int GetHeight();
    int GetWidth();
    int GetNumLuts();
}; // ShaderManager 

#endif // SHADERMANAGER_HPP
