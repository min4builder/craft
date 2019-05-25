#version 120

uniform mat4 matrix;
uniform vec3 camera;

attribute vec4 position;
attribute vec3 normal;
attribute vec4 uv;

varying vec2 fuv;
varying float ao;
varying float light;
varying float dist;
varying vec3 fnormal;

void main() {
    gl_Position = matrix * position;
    ao = uv.z;
    light = uv.w;
    dist = distance(position.xyz, camera);
    fuv = uv.xy;
    fnormal = normal;
}

