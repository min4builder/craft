#version 120

uniform mat4 matrix;
uniform vec3 player_position;

attribute vec4 position;
attribute vec3 normal;
attribute vec2 uv;

varying vec2 fragment_uv;
varying vec3 sky_position;
varying vec3 sky_offset;

void main() {
    gl_Position = matrix * position;
    sky_position = position.xyz;
    sky_offset = player_position;
    fragment_uv = uv;
}
