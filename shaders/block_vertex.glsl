#version 120

uniform mat4 matrix;
uniform float aspect_ratio;

attribute vec3 position;

varying vec3 ray;

void main() {
    ray = (matrix * vec4(position.x * aspect_ratio, position.y, -1.0, 0.0)).xyz;
    gl_Position = vec4(position, 1.0);
}

