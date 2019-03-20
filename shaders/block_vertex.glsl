#version 120

uniform mat4 matrix;

attribute vec3 position;

varying vec3 ray;

void main() {
    ray = (matrix * vec4(position.x, position.y, -1.0, 0.0)).xyz;
    gl_Position = vec4(position, 1.0);
}

