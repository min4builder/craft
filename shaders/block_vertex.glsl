#version 120

uniform mat4 matrix;
uniform vec3 camera;

attribute vec4 position;
attribute vec3 normal;
attribute vec4 uv;

varying vec3 ray;
varying vec3 pos;
varying vec3 fnormal;

void main() {
    gl_Position = matrix * position;
    pos = position.xyz;
    ray = pos - camera;
    fnormal = normal;
}

