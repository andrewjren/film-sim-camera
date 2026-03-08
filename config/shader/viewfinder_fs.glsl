#version 300 es
precision highp float;
precision highp sampler3D;
//uniform sampler2D image;
uniform sampler2D yTexture;
uniform sampler2D uTexture;
uniform sampler2D vTexture;
uniform sampler3D clut;
in vec2 TexCoord;
out vec4 fragColor;

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

    vec3 orig_color;
    orig_color = yuvToRgb * vec3(y, u, v);
    
    // Clamp to valid range
    orig_color = clamp(orig_color, 0.0, 1.0);
    fragColor = texture(clut, orig_color);
}
