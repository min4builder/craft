#version 120

uniform sampler2D texture;
uniform int render_dist;
uniform vec3 camera;
uniform float timer;

varying vec2 fuv;
varying float ao;
varying float dist;
varying vec3 fnormal;

const vec4 sky_color = vec4(1.);

void main() {
    vec4 color = texture2D(texture, fuv);
    if (fnormal.y == 0.0)
        color.rgb /= 2.0;
    color.rgb *= 1.0 - ao;
    gl_FragColor = mix(color, sky_color, dist / render_dist);
}

