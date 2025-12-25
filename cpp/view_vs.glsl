#version 300 es
in vec2 aPos;
in vec2 aTexCoord;
out vec2 TexCoord;
uniform mat4 transform;
void main() {
	gl_Position = transform * vec4(aPos, 0.0, 1.0);
    TexCoord = aTexCoord;
}