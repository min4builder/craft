#version 120

uniform mat4 matrix;

attribute vec4 position;
attribute vec3 normal;
attribute vec2 uv;

varying vec2 fragment_uv;
varying vec2 sky_position;

const float pi = 3.141592653;

void main() {
    gl_Position = matrix * position;
    sky_position = position.xz * exp(1.0 - log(position.y));
    fragment_uv = uv;
}
