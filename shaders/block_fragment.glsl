#version 120

uniform sampler2D texture;
uniform int render_dist;
uniform vec3 camera;
uniform float timer;

varying vec2 fuv;
varying float ao;
varying float light;
varying float dist;
varying vec3 fnormal;

const vec4 sky_color = vec4(1.);

float quantize(float x, int n) {
    return float(int(x) / n);
}

float dither(float a) {
    float n = mod(gl_FragCoord.x + gl_FragCoord.y, 6.0);
    float q = trunc(a * 2.0) * 0.5;
    return q + ((a - q) * 6.0 * 4.0 < n ? 0.0 : 0.5);
}

void main() {
    vec4 color = texture2D(texture, fuv);
    float l = 1.0;
    if (fnormal.y == 0.0)
        l *= 0.5;
    l *= light + 1.0 - ao*ao;
    l = mix(l, 1.0, dist / render_dist);
    color.rgb *= dither(l);
    gl_FragColor = color;
}

