#version 450 core
#extension GL_ARB_shading_language_include : require

// line comment
/* block comment */

layout(location = 0) in vec3 a_position;
layout(location = 1) in vec2 a_uv;

layout(location = 0) out vec2 v_uv;

uniform mat4 u_mvp;
uniform float u_time;

const int MAX_LIGHTS = 4;
const vec3 UP = vec3(0.0, 1.0, 0.0);

struct Light {
    vec3 position;
    vec3 color;
    float intensity;
};

layout(std140) uniform LightBlock {
    Light lights[MAX_LIGHTS];
};

float attenuate(float d) {
    return 1.0 / (1.0 + d * d);
}

void main() {
    v_uv = a_uv;
    vec4 world = u_mvp * vec4(a_position, 1.0);
    gl_Position = world;
    for (int i = 0; i < MAX_LIGHTS; ++i) {
        world.xyz += lights[i].color * attenuate(length(lights[i].position));
    }
}
