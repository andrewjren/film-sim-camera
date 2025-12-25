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