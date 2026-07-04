#version 330 core

uniform float u_time;

void main() {
    gl_Position = vec4(u_time);
}
