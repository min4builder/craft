#version 120

uniform sampler2D sampler;
uniform float timer;

varying vec2 fragment_uv;
varying vec3 sky_position;
varying vec3 sky_offset;

const float PI = 3.141592653;

vec2 rand2(vec2 p) {
    p = vec2(dot(p, vec2(127.1, 311.7)),
             dot(p, vec2(269.5, 183.3)));
    return -1.0 + 2.0 * fract(sin(p) * 43758.5453123);
}

float noise(vec2 p) {
    vec2 i = floor(p);
    vec2 f = fract(p);

    vec2 u = f * f * (3.0 - 2.0 * f);

    return mix(mix(dot(rand2(i), f),
                   dot(rand2(i + vec2(1.0, 0.0)), f - vec2(1.0, 0.0)), u.x),
               mix(dot(rand2(i + vec2(0.0, 1.0)), f - vec2(0.0, 1.0)),
                   dot(rand2(i + vec2(1.0, 1.0)), f - vec2(1.0, 1.0)), u.x), u.y);
}

float fbm(vec2 p) {
    return noise(p * 8.0) * 0.125
         + noise(p * 4.0) * 0.125
         + noise(p * 2.0) * 0.25
         + noise(p * 1.0) * 0.5;
}

float cloudify(float n) {
    n = 2.0 * (1.0 - exp(n));
    if(n < 0.0)
        return 0.0;
    return n;
}

void main() {
    vec2 uv = vec2(timer, fragment_uv.t);
    float n = 0.0;
    if(sky_position.y > 0.0) {
        float olen = length(sky_position.xz);
        float dist = tan(PI * 0.5 - asin(sky_position.y));
        vec2 pos = sky_offset.xz * 0.125 + vec2(dist * sky_position.x / olen, dist * sky_position.z / olen);
        n = cloudify(fbm(pos + timer * 10.0));
    }
    gl_FragColor = n + texture2D(sampler, uv);
}

