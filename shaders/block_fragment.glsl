#version 120

uniform sampler3D world;
uniform sampler2D texture;
uniform int world_size;
uniform int render_dist;
uniform vec3 camera;

varying vec3 ray;

const vec4 sky_color = vec4(1.);
const vec4 a_tiny_bit = vec4(0.01);
const float tex_max = 16.0;

vec3 normal(vec3 pos) {
    vec3 fpos = fract(pos) - 0.5;
    if (fpos.x < -abs(fpos.y) && fpos.x < -abs(fpos.z)) {
        return vec3(-1.0, 0.0, 0.0);
    } else if (fpos.y < -abs(fpos.x) && fpos.y < -abs(fpos.z)) {
        return vec3(0.0, -1.0, 0.0);
    } else if (fpos.z < -abs(fpos.x) && fpos.z < -abs(fpos.y)) {
        return vec3(0.0, 0.0, -1.0);
    } else if (fpos.x > abs(fpos.y) && fpos.x > abs(fpos.z)) {
        return vec3(1.0, 0.0, 0.0);
    } else if (fpos.y > abs(fpos.x) && fpos.y > abs(fpos.z)) {
        return vec3(0.0, 1.0, 0.0);
    } else {
        return vec3(0.0, 0.0, 1.0);
    }
}

vec4 color_at(float step, vec4 block, vec3 pos) {
    vec3 normal = normal(pos);
    vec2 uv;
    if (normal.x != 0.0) {
        uv = fract(pos.zy);
    } else if (normal.y != 0.0) {
        uv = fract(pos.xz);
    } else {
        uv = fract(pos.xy);
    }
    vec2 block_tex = vec2(mod(block.a * 255.0 - 1.0, tex_max), floor((block.a * 255.0 - 1.0) / tex_max));
    vec4 color = texture2D(texture, (uv + block_tex) / tex_max);
    return mix(color, sky_color, step / render_dist);
}

vec4 blend(vec4 a, vec4 b) {
    return vec4(mix(a.rgb, b.rgb, b.a), a.a * (1.0 - b.a) + b.a);
}

float refr_idx(float b) {
    if (b == 0.0)
        return 1.0;
    else
        return 1.2;
}

vec4 trace(vec3 camera, vec3 ray, float step) {
    vec3 pos;
    vec4 color = vec4(0.0);
    float pblock = 0.0;
    while (step < render_dist) {
        if (step < render_dist / 4.0) {
            pos = camera + step * ray;
            vec3 dists = fract(pos * sign(-ray)) / abs(ray);
            float dist = min(min(dists.x, dists.y), dists.z);
            step += dist + 0.001;
        } else {
            step += 16.0 * step / render_dist;
        }
        pos = camera + step * ray;
        vec4 block = texture3D(world, (floor(pos) + a_tiny_bit.xyz) / world_size);
        if (block.a > 0.0) {
            color = blend(color_at(step, block, camera + step * ray), color);
            if (color.a == 1.0)
                return color;
            if (block.a != pblock) {
                vec3 normal = normal(pos);
                if (true) {
                    ray = refract(ray, normal, refr_idx(pblock) / refr_idx(block.a));
                    pblock = block.a;
                } else {
                    ray = reflect(ray, normal);
                }
                ray = normalize(ray);
                camera = pos - step * ray;
            }
        }
    }
    return mix(sky_color, color, color.a);
}

void main() {
    gl_FragColor = trace(camera, normalize(ray), 0.0);
}

