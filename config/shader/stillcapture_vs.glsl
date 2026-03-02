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
