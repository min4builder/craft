#version 120

uniform sampler3D world;
uniform sampler2D texture;
uniform int world_size;
uniform int render_dist;
uniform vec3 camera;

varying vec3 ray;
varying vec3 pos;
varying vec3 fnormal;

const vec4 sky_color = vec4(1.);
const vec4 a_tiny_bit = vec4(0.01);
const float tex_max = 16.0;

vec3 normal_approx(vec3 pos) {
    vec3 fpos = pos - 0.5;
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

vec4 color_at(float dist, vec4 block, vec3 pos, vec3 normal) {
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
    return mix(color, sky_color, dist / render_dist);
}

/*
vec4 blend(vec4 a, vec4 b) {
    return vec4(mix(a.rgb, b.rgb, b.a), a.a * (1.0 - b.a) + b.a);
}

float refr_idx(float b) {
    if (b == 0.0)
        return 1.0;
    else
        return 1.2;
}
*/

vec4 trace(vec3 camera, vec3 ray, float start_dist) {
    vec3 pos = floor(camera);
    vec3 delta = abs(1.0 / ray);
    vec3 step = sign(ray);
    vec3 next = delta * fract(-step * camera);
    float pblock = 0.0;
    vec4 color = vec4(0.0);
    vec3 normal = fnormal;
    vec3 exact_pos = camera;
    float dist = 0.0;
    while (dist + start_dist < render_dist) {
        vec4 block = texture3D(world, (pos + a_tiny_bit.xyz) / world_size);
        if (block.a != 0.0) {
            color = /*blend(*/color_at(start_dist + distance(camera, exact_pos), block, exact_pos, normal)/*, color)*/;
            color.a = 1.0;
            if (color.a == 1.0) {
                if (normal.y == 0.0)
                    color.rgb /= 2.0;
                return color;
            }
            /*
            if (block.a != pblock) {
                ray = normalize(refract(ray, normal, refr_idx(pblock) / refr_idx(block.a)));
                pblock = block.a;
                delta = abs(1.0 / ray);
                step = sign(ray);
                next = delta * fract(-step * exact_pos);
                camera = exact_pos - dist * ray;
            }
            */
        }
        float m = min(min(next.x, next.y), next.z);
        if (m == next.x) {
            next.x += delta.x;
            pos.x += step.x;
            dist = (pos.x - camera.x + (1.0 - step.x) / 2.0) / ray.x;
        } else if (m == next.y) {
            next.y += delta.y;
            pos.y += step.y;
            dist = (pos.y - camera.y + (1.0 - step.y) / 2.0) / ray.y;
        } else {
            next.z += delta.z;
            pos.z += step.z;
            dist = (pos.z - camera.z + (1.0 - step.z) / 2.0) / ray.z;
        }
        exact_pos = camera + dist * ray;
        normal = normal_approx(exact_pos - pos);
    }
    return mix(sky_color, color, color.a);
}

void main() {
    gl_FragColor = trace(pos, normalize(ray), distance(pos, camera));
}

