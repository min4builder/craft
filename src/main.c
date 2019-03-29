#include <GL/glew.h>
#include <GLFW/glfw3.h>
#include <arpa/inet.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include "client.h"
#include "config.h"
#include "cube.h"
#include "item.h"
#include "map.h"
#include "matrix.h"
#include "miniz.h"
#include "tinycthread.h"
#include "util.h"

#define MAX_CHUNKS (MAX_CHUNK_COUNT * 10 / 5) /* divide by the max load factor */
#define MAX_CHUNK_COUNT 8192
#define MAX_PLAYERS 128
#define WORKERS (MAX_CHUNK_COUNT / 256)
#define MAX_TEXT_LENGTH 256
#define MAX_NAME_LENGTH 32
#define MAX_PATH_LENGTH 256
#define MAX_ADDR_LENGTH 256

#define ALIGN_LEFT 0
#define ALIGN_CENTER 1
#define ALIGN_RIGHT 2

#define WORKER_IDLE 0
#define WORKER_BUSY 1
#define WORKER_DONE 2

#define CHUNK_FOR_EACH(c, ex, ey, ez, ew) \
    for (int ex = c->p * CHUNK_SIZE; ex < c->p * CHUNK_SIZE + CHUNK_SIZE; ex++) \
        for (int ey = c->q * CHUNK_SIZE; ey < c->q * CHUNK_SIZE + CHUNK_SIZE; ey++) \
            for (int ez = c->r * CHUNK_SIZE; ez < c->r * CHUNK_SIZE + CHUNK_SIZE; ez++) \
                for (int ew = chunk_get(c, ex, ey, ez); c->q >= 0 && ew != 0; ew = 0)
                /* the c->q >= 0 check is there to mitigate a race condition; TODO rewrite this whole thing */

typedef struct {
    unsigned char ws[CHUNK_SIZE*CHUNK_SIZE*CHUNK_SIZE];
    Map lights;
    int p;
    int q;
    int r;
    int dirty;
    int miny;
    int maxy;
    int faces;
    GLuint buffer;
} Chunk;

typedef struct {
    int p;
    int q;
    int r;
    int load;
    Chunk *chunks[3][3][3];
    Map *light_maps[3][3][3];
    int miny;
    int maxy;
    int faces;
    GLfloat *data;
} WorkerItem;

typedef struct {
    int index;
    int state;
    thrd_t thrd;
    mtx_t mtx;
    cnd_t cnd;
    WorkerItem item;
} Worker;

typedef struct {
    int x;
    int y;
    int z;
    int w;
} Block;

typedef struct {
    float x;
    float y;
    float z;
    float rx;
    float ry;
    float t;
} State;

typedef struct {
    int id;
    char name[MAX_NAME_LENGTH];
    State state;
    State state1;
    State state2;
    GLuint buffer;
} Player;

typedef struct {
    GLuint program;
    GLuint position;
    GLuint normal;
    GLuint uv;
    GLuint matrix;
    GLuint sampler;
    GLuint camera;
    GLuint timer;
    GLuint extra1;
    GLuint extra2;
    GLuint extra3;
    GLuint extra4;
} Attrib;

typedef struct {
    GLFWwindow *window;
    Worker workers[WORKERS];
    Chunk chunks[MAX_CHUNKS];
    GLuint world_tex;
    GLuint chunks_tex;
    int chunk_count;
    int create_radius;
    int render_radius;
    int delete_radius;
    Player players[MAX_PLAYERS];
    int player_count;
    int typing;
    char typing_buffer[MAX_TEXT_LENGTH];
    int message_index;
    char messages[MAX_MESSAGES][MAX_TEXT_LENGTH];
    int width;
    int height;
    int flying;
    int item_index;
    int scale;
    float fov;
    int suppress_char;
    int server_changed;
    char server_addr[MAX_ADDR_LENGTH];
    int server_port;
    int day_length;
    int time_changed;
} Model;

static Model model;
static Model *g = &model;

static int chunked(float x) {
    return floorf(roundf(x) / CHUNK_SIZE);
}

static float time_of_day() {
    if (g->day_length <= 0) {
        return 0.5;
    }
    float t;
    t = glfwGetTime();
    t = t / g->day_length;
    t = t - (int)t;
    return t;
}

static int get_scale_factor() {
    int window_width, window_height;
    int buffer_width, buffer_height;
    glfwGetWindowSize(g->window, &window_width, &window_height);
    glfwGetFramebufferSize(g->window, &buffer_width, &buffer_height);
    int result = buffer_width / window_width;
    result = MAX(1, result);
    result = MIN(2, result);
    return result;
}

static void get_sight_vector(float rx, float ry, float *vx, float *vy, float *vz) {
    float m = cosf(ry);
    *vx = cosf(rx - RADIANS(90)) * m;
    *vy = sinf(ry);
    *vz = sinf(rx - RADIANS(90)) * m;
}

static void get_motion_vector(int flying, int sz, int sx, float rx, float ry,
    float *vx, float *vy, float *vz) {
    *vx = 0; *vy = 0; *vz = 0;
    if (!sz && !sx) {
        return;
    }
    float strafe = atan2f(sz, sx);
    if (flying) {
        float m = cosf(ry);
        float y = sinf(ry);
        if (sx) {
            if (!sz) {
                y = 0;
            }
            m = 1;
        }
        if (sz > 0) {
            y = -y;
        }
        *vx = cosf(rx + strafe) * m;
        *vy = y;
        *vz = sinf(rx + strafe) * m;
    }
    else {
        *vx = cosf(rx + strafe);
        *vy = 0;
        *vz = sinf(rx + strafe);
    }
}

static GLuint gen_crosshair_buffer() {
    int x = g->width / 2;
    int y = g->height / 2;
    int p = 10 * g->scale;
    float data[] = {
        x, y - p, x, y + p,
        x - p, y, x + p, y
    };
    return gen_buffer(sizeof(data), data);
}

static GLuint gen_player_buffer(float x, float y, float z, float rx, float ry) {
    GLfloat *data = malloc_faces(10, 6);
    make_player(data, x, y, z, rx, ry);
    return gen_faces(10, 6, data);
}

static GLuint gen_text_buffer(float x, float y, float n, char *text) {
    int length = strlen(text);
    GLfloat *data = malloc_faces(4, length);
    for (int i = 0; i < length; i++) {
        make_character(data + i * 24, x, y, n / 2, n, text[i]);
        x += n;
    }
    return gen_faces(4, length, data);
}

static void draw_triangles_2d(Attrib *attrib, GLuint buffer, int count) {
    glBindBuffer(GL_ARRAY_BUFFER, buffer);
    glEnableVertexAttribArray(attrib->position);
    glEnableVertexAttribArray(attrib->uv);
    glVertexAttribPointer(attrib->position, 2, GL_FLOAT, GL_FALSE,
        sizeof(GLfloat) * 4, 0);
    glVertexAttribPointer(attrib->uv, 2, GL_FLOAT, GL_FALSE,
        sizeof(GLfloat) * 4, (GLvoid *)(sizeof(GLfloat) * 2));
    glDrawArrays(GL_TRIANGLES, 0, count);
    glDisableVertexAttribArray(attrib->position);
    glDisableVertexAttribArray(attrib->uv);
    glBindBuffer(GL_ARRAY_BUFFER, 0);
}

static void draw_lines(Attrib *attrib, GLuint buffer, int components, int count) {
    glBindBuffer(GL_ARRAY_BUFFER, buffer);
    glEnableVertexAttribArray(attrib->position);
    glVertexAttribPointer(
        attrib->position, components, GL_FLOAT, GL_FALSE, 0, 0);
    glDrawArrays(GL_LINES, 0, count);
    glDisableVertexAttribArray(attrib->position);
    glBindBuffer(GL_ARRAY_BUFFER, 0);
}

static void draw_text(Attrib *attrib, GLuint buffer, int length) {
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    draw_triangles_2d(attrib, buffer, length * 6);
    glDisable(GL_BLEND);
}

static Player *find_player(int id) {
    for (int i = 0; i < g->player_count; i++) {
        Player *player = g->players + i;
        if (player->id == id) {
            return player;
        }
    }
    return 0;
}

static void update_player(Player *player,
    float x, float y, float z, float rx, float ry, int interpolate)
{
    if (interpolate) {
        State *s1 = &player->state1;
        State *s2 = &player->state2;
        memcpy(s1, s2, sizeof(State));
        s2->x = x; s2->y = y; s2->z = z; s2->rx = rx; s2->ry = ry;
        s2->t = glfwGetTime();
        if (s2->rx - s1->rx > PI) {
            s1->rx += 2 * PI;
        }
        if (s1->rx - s2->rx > PI) {
            s1->rx -= 2 * PI;
        }
    }
    else {
        State *s = &player->state;
        s->x = x; s->y = y; s->z = z; s->rx = rx; s->ry = ry;
        del_buffer(player->buffer);
        player->buffer = gen_player_buffer(s->x, s->y, s->z, s->rx, s->ry);
    }
}

static void delete_player(int id) {
    Player *player = find_player(id);
    if (!player) {
        return;
    }
    int count = g->player_count;
    del_buffer(player->buffer);
    Player *other = g->players + (--count);
    memcpy(player, other, sizeof(Player));
    g->player_count = count;
}

static void delete_all_players() {
    for (int i = 0; i < g->player_count; i++) {
        Player *player = g->players + i;
        del_buffer(player->buffer);
    }
    g->player_count = 0;
}

static float player_player_distance(Player *p1, Player *p2) {
    State *s1 = &p1->state;
    State *s2 = &p2->state;
    float x = s2->x - s1->x;
    float y = s2->y - s1->y;
    float z = s2->z - s1->z;
    return sqrtf(x * x + y * y + z * z);
}

static float player_crosshair_distance(Player *p1, Player *p2) {
    State *s1 = &p1->state;
    State *s2 = &p2->state;
    float d = player_player_distance(p1, p2);
    float vx, vy, vz;
    get_sight_vector(s1->rx, s1->ry, &vx, &vy, &vz);
    vx *= d; vy *= d; vz *= d;
    float px, py, pz;
    px = s1->x + vx; py = s1->y + vy; pz = s1->z + vz;
    float x = s2->x - px;
    float y = s2->y - py;
    float z = s2->z - pz;
    return sqrtf(x * x + y * y + z * z);
}

static Player *player_crosshair(Player *player) {
    Player *result = 0;
    float threshold = RADIANS(5);
    float best = 0;
    for (int i = 0; i < g->player_count; i++) {
        Player *other = g->players + i;
        if (other == player) {
            continue;
        }
        float p = player_crosshair_distance(player, other);
        float d = player_player_distance(player, other);
        if (d < 96 && p / d < threshold) {
            if (best == 0 || d < best) {
                best = d;
                result = other;
            }
        }
    }
    return result;
}

static int chunk_coord_hash(int p_, int q_, int r_) {
    unsigned int p = ABS(p_), q = ABS(q_), r = ABS(r_);
    p = ((p >> 16) ^ p) * 0x45d9f3b;
    p = ((p >> 16) ^ p) * 0x45d9f3b;
    p = (p >> 16) ^ p;
    q = ((q >> 16) ^ q) * 0x45d9f3b;
    q = ((q >> 16) ^ q) * 0x45d9f3b;
    q = (q >> 16) ^ q;
    r = ((r >> 16) ^ r) * 0x45d9f3b;
    r = ((r >> 16) ^ r) * 0x45d9f3b;
    r = (r >> 16) ^ r;
    return (p ^ q ^ r) % MAX_CHUNKS;
}

static Chunk *find_chunk(int p, int q, int r) {
    if (q < 0) return 0;
    int index = chunk_coord_hash(p, q, r);
    for (int i = index; g->chunks[i].q >= 0; i = (i + 1) % MAX_CHUNKS) {
        Chunk *chunk = g->chunks + i;
        if (chunk->p == p && chunk->q == q && chunk->r == r) {
            return chunk;
        }
    }
    return 0;
}

static int mod_euc(int a, int m) {
    return (a % m + m) % m;
}

static int chunk_get(Chunk *chunk, int x, int y, int z) {
    int index = mod_euc(x, CHUNK_SIZE)
      + mod_euc(y, CHUNK_SIZE) * CHUNK_SIZE
      + mod_euc(z, CHUNK_SIZE) * CHUNK_SIZE * CHUNK_SIZE;
    return chunk->ws[index];
}

static int chunk_set(Chunk *chunk, int x, int y, int z, int w) {
    int index = mod_euc(x, CHUNK_SIZE)
      + mod_euc(y, CHUNK_SIZE) * CHUNK_SIZE
      + mod_euc(z, CHUNK_SIZE) * CHUNK_SIZE * CHUNK_SIZE;
    int d = chunk->ws[index] != w;
    chunk->ws[index] = w;
    return d;
}

static int get_block(int x, int y, int z) {
    Chunk *chunk = find_chunk(chunked(x), chunked(y), chunked(z));
    if (chunk) {
        return chunk_get(chunk, x, y, z);
    }
    return 0;
}

static int chunk_distance(Chunk *chunk, int p, int q, int r) {
    int dp = ABS(chunk->p - p);
    int dq = ABS(chunk->q - q);
    int dr = ABS(chunk->r - r);
    return MAX(MAX(dp, dq), dr);
}

static int chunk_visible(float planes[6][4], int p, int q, int r) {
    int x = p * CHUNK_SIZE - 1;
    int y = q * CHUNK_SIZE - 1;
    int z = r * CHUNK_SIZE - 1;
    int d = CHUNK_SIZE + 1;
    float points[8][3] = {
        {x + 0, y + 0, z + 0},
        {x + d, y + 0, z + 0},
        {x + 0, y + 0, z + d},
        {x + d, y + 0, z + d},
        {x + 0, y + d, z + 0},
        {x + d, y + d, z + 0},
        {x + 0, y + d, z + d},
        {x + d, y + d, z + d}
    };
    int n = 6;
    for (int i = 0; i < n; i++) {
        int in = 0;
        int out = 0;
        for (int j = 0; j < 8; j++) {
            float d =
                planes[i][0] * points[j][0] +
                planes[i][1] * points[j][1] +
                planes[i][2] * points[j][2] +
                planes[i][3];
            if (d < 0) {
                out++;
            }
            else {
                in++;
            }
            if (in && out) {
                break;
            }
        }
        if (in == 0) {
            return 0;
        }
    }
    return 1;
}

static int highest_block(float x, float z) {
    int max = 0;
    for (int i = 0; i < MAX_CHUNKS; i++) {
        Chunk *chunk = g->chunks + i;
        if (chunk->q < 0) continue;
        if (chunk->p == chunked(x) && chunk->r == chunked(z)) {
            for (int y = chunk->q * CHUNK_SIZE + CHUNK_SIZE; y > chunk->q * CHUNK_SIZE; y--) {
                if (is_obstacle(chunk_get(chunk, x, y, z))) {
                    max = MAX(max, y);
                    break;
                }
            }
        }
    }
    return max;
}

static int _hit_test(
    float max_distance, int previous,
    float x, float y, float z,
    float vx, float vy, float vz,
    int *hx, int *hy, int *hz)
{
    int m = 32;
    int px = 0;
    int py = 0;
    int pz = 0;
    for (int i = 0; i < max_distance * m; i++) {
        int nx = floorf(x);
        int ny = floorf(y);
        int nz = floorf(z);
        if (nx != px || ny != py || nz != pz) {
            int hw = get_block(nx, ny, nz);
            if (hw > 0) {
                if (previous) {
                    *hx = px; *hy = py; *hz = pz;
                }
                else {
                    *hx = nx; *hy = ny; *hz = nz;
                }
                return hw;
            }
            px = nx; py = ny; pz = nz;
        }
        x += vx / m; y += vy / m; z += vz / m;
    }
    return 0;
}

static int hit_test(
    int previous, float x, float y, float z, float rx, float ry,
    int *bx, int *by, int *bz)
{
    int result = 0;
    float best = 0;
    float vx, vy, vz;
    get_sight_vector(rx, ry, &vx, &vy, &vz);
    int hx, hy, hz;
    int hw = _hit_test(8, previous,
        x, y, z, vx, vy, vz, &hx, &hy, &hz);
    if (hw > 0) {
        float d = sqrtf(
            powf(hx - x, 2) + powf(hy - y, 2) + powf(hz - z, 2));
        if (best == 0 || d < best) {
            best = d;
            *bx = hx; *by = hy; *bz = hz;
            result = hw;
        }
    }
    return result;
}

static int collide(int height, float *x, float *y, float *z) {
    float pad = 0.25;
    int result = 0;
    int nx = floorf(*x);
    int ny = floorf(*y);
    int nz = floorf(*z);
    float px = *x - nx;
    float py = *y - ny;
    float pz = *z - nz;
    for (int dy = 0; dy < height; dy++) {
        if (px < pad && is_obstacle(get_block(nx - 1, ny + dy, nz))) {
            *x = nx + pad;
        }
        if (px > 1 - pad && is_obstacle(get_block(nx + 1, ny + dy, nz))) {
            *x = nx + 1 - pad;
        }
        if (py < pad && is_obstacle(get_block(nx, ny + dy - 1, nz))) {
            *y = ny + pad;
            result = 1;
        }
        if (py > 1 - pad && is_obstacle(get_block(nx, ny + dy + 1, nz))) {
            *y = ny + 1 - pad;
            result = 1;
        }
        if (pz < pad && is_obstacle(get_block(nx, ny + dy, nz - 1))) {
            *z = nz + pad;
        }
        if (pz > 1 - pad && is_obstacle(get_block(nx, ny + dy, nz + 1))) {
            *z = nz + 1 - pad;
        }
    }
    return result;
}

static int player_intersects_block(
    int height,
    float x, float y, float z,
    int hx, int hy, int hz)
{
    int nx = floorf(x);
    int ny = floorf(y);
    int nz = floorf(z);
    for (int i = 0; i < height; i++) {
        if (nx == hx && ny + i == hy && nz == hz) {
            return 1;
        }
    }
    return 0;
}

static void dirty_chunk(Chunk *chunk) {
    chunk->dirty = 1;
    for (int dp = -1; dp <= 1; dp++) {
        for (int dq = -1; dq <= 1; dq++) {
            for (int dr = -1; dr <= 1; dr++) {
                Chunk *other = find_chunk(chunk->p + dp, chunk->q + dq, chunk->r + dr);
                if (other) {
                    other->dirty = 1;
                }
            }
        }
    }
}

static void occlusion(
    char neighbors[27], char lights[27], float shades[27],
    float ao[6][4], float light[6][4])
{
    static const int lookup3[6][4][3] = {
        {{0, 1, 3}, {2, 1, 5}, {6, 3, 7}, {8, 5, 7}},
        {{18, 19, 21}, {20, 19, 23}, {24, 21, 25}, {26, 23, 25}},
        {{6, 7, 15}, {8, 7, 17}, {24, 15, 25}, {26, 17, 25}},
        {{0, 1, 9}, {2, 1, 11}, {18, 9, 19}, {20, 11, 19}},
        {{0, 3, 9}, {6, 3, 15}, {18, 9, 21}, {24, 15, 21}},
        {{2, 5, 11}, {8, 5, 17}, {20, 11, 23}, {26, 17, 23}}
    };
   static const int lookup4[6][4][4] = {
        {{0, 1, 3, 4}, {1, 2, 4, 5}, {3, 4, 6, 7}, {4, 5, 7, 8}},
        {{18, 19, 21, 22}, {19, 20, 22, 23}, {21, 22, 24, 25}, {22, 23, 25, 26}},
        {{6, 7, 15, 16}, {7, 8, 16, 17}, {15, 16, 24, 25}, {16, 17, 25, 26}},
        {{0, 1, 9, 10}, {1, 2, 10, 11}, {9, 10, 18, 19}, {10, 11, 19, 20}},
        {{0, 3, 9, 12}, {3, 6, 12, 15}, {9, 12, 18, 21}, {12, 15, 21, 24}},
        {{2, 5, 11, 14}, {5, 8, 14, 17}, {11, 14, 20, 23}, {14, 17, 23, 26}}
    };
    static const float curve[4] = {0.0, 0.25, 0.5, 0.75};
    for (int i = 0; i < 6; i++) {
        for (int j = 0; j < 4; j++) {
            int corner = neighbors[lookup3[i][j][0]];
            int side1 = neighbors[lookup3[i][j][1]];
            int side2 = neighbors[lookup3[i][j][2]];
            int value = side1 && side2 ? 3 : corner + side1 + side2;
            float shade_sum = 0;
            float light_sum = 0;
            int is_light = lights[13] == 15;
            for (int k = 0; k < 4; k++) {
                shade_sum += shades[lookup4[i][j][k]];
                light_sum += lights[lookup4[i][j][k]];
            }
            if (is_light) {
                light_sum = 15 * 4 * 10;
            }
            float total = curve[value] + shade_sum / 4.0;
            ao[i][j] = MIN(total, 1.0);
            light[i][j] = light_sum / 15.0 / 4.0;
        }
    }
}

#define XYZ_SIZE (CHUNK_SIZE * 3 + 2)
#define XYZ_LO (CHUNK_SIZE)
#define XYZ_HI (CHUNK_SIZE * 2 + 1)
#define XYZ(x, y, z) ((y) * XYZ_SIZE * XYZ_SIZE + (x) * XYZ_SIZE + (z))
#define XZ(x, z) ((x) * XYZ_SIZE + (z))

static void light_fill(
    char *opaque, char *light,
    int x, int y, int z, int w, int force)
{
    if (x + w < XYZ_LO || y + w < XYZ_LO || z + w < XYZ_LO) {
        return;
    }
    if (x - w > XYZ_HI || y - w > XYZ_HI || z - w > XYZ_HI) {
        return;
    }
    if (light[XYZ(x, y, z)] >= w) {
        return;
    }
    if (!force && opaque[XYZ(x, y, z)]) {
        return;
    }
    light[XYZ(x, y, z)] = w--;
    light_fill(opaque, light, x - 1, y, z, w, 0);
    light_fill(opaque, light, x + 1, y, z, w, 0);
    light_fill(opaque, light, x, y - 1, z, w, 0);
    light_fill(opaque, light, x, y + 1, z, w, 0);
    light_fill(opaque, light, x, y, z - 1, w, 0);
    light_fill(opaque, light, x, y, z + 1, w, 0);
}

static void compute_chunk(WorkerItem *item) {
    char *opaque = (char *)calloc(XYZ_SIZE*XYZ_SIZE*XYZ_SIZE, sizeof(char));
    char *light = (char *)calloc(XYZ_SIZE*XYZ_SIZE*XYZ_SIZE, sizeof(char));
    char *highest = (char *)calloc(XYZ_SIZE*XYZ_SIZE, sizeof(char));

    int ox = item->p * CHUNK_SIZE - CHUNK_SIZE - 1;
    int oy = item->q * CHUNK_SIZE - CHUNK_SIZE - 1;
    int oz = item->r * CHUNK_SIZE - CHUNK_SIZE - 1;

    // check for lights
    int has_light = 0;
    if (SHOW_LIGHTS) {
        for (int a = 0; a < 3; a++) {
            for (int b = 0; b < 3; b++) {
                for (int c = 0; c < 3; c++) {
                    Map *map = item->light_maps[a][b][c];
                    if (map && map->size) {
                        has_light = 1;
                    }
                }
            }
        }
    }

    // populate opaque array
    for (int a = 0; a < 3; a++) {
        for (int b = 0; b < 3; b++) {
            for (int c = 0; c < 3; c++) {
                Chunk *chunk = item->chunks[a][b][c];
                if (!chunk) {
                    continue;
                }
                CHUNK_FOR_EACH(chunk, ex, ey, ez, ew) {
                    int x = ex - ox;
                    int y = ey - oy;
                    int z = ez - oz;
                    int w = ew;
                    opaque[XYZ(x, y, z)] = w;
                    if (opaque[XYZ(x, y, z)]) {
                        highest[XZ(x, z)] = MAX(highest[XZ(x, z)], y);
                    }
                }
            }
        }
    }

    // flood fill light intensities
    if (has_light) {
        for (int a = 0; a < 3; a++) {
            for (int b = 0; b < 3; b++) {
                for (int c = 0; c < 3; c++) {
                    Map *map = item->light_maps[a][b][c];
                    if (!map) {
                        continue;
                    }
                    MAP_FOR_EACH(map, ex, ey, ez, ew) {
                        int x = ex - ox;
                        int y = ey - oy;
                        int z = ez - oz;
                        light_fill(opaque, light, x, y, z, ew, 1);
                    } END_MAP_FOR_EACH;
                }
            }
        }
    }

    Chunk *chunk = item->chunks[1][1][1];

    // count exposed faces
    int faces = 0;
    CHUNK_FOR_EACH(chunk, ex, ey, ez, ew) {
        int x = ex - ox;
        int y = ey - oy;
        int z = ez - oz;
        if (!ew) continue;
        int f1 = !opaque[XYZ(x - 1, y, z)] ? 1 : 0;
        int f2 = !opaque[XYZ(x + 1, y, z)] ? 1 : 0;
        int f3 = !opaque[XYZ(x, y + 1, z)] ? 1 : 0;
        int f4 = !opaque[XYZ(x, y - 1, z)] && ey > 0 ? 1 : 0;
        int f5 = !opaque[XYZ(x, y, z - 1)] ? 1 : 0;
        int f6 = !opaque[XYZ(x, y, z + 1)] ? 1 : 0;
        int total = f1 + f2 + f3 + f4 + f5 + f6;
        if (total == 0)
            continue;
        if (is_plant(ew))
            total = 4;
        faces += total;
    }

    GLfloat *data = malloc_faces(10, faces);
    int offset = 0;
    CHUNK_FOR_EACH(chunk, ex, ey, ez, ew) {
        int x = ex - ox;
        int y = ey - oy;
        int z = ez - oz;
        if (!ew) continue;
        int f1 = !opaque[XYZ(x - 1, y, z)] ? 1 : 0;
        int f2 = !opaque[XYZ(x + 1, y, z)] ? 1 : 0;
        int f3 = !opaque[XYZ(x, y + 1, z)] ? 1 : 0;
        int f4 = !opaque[XYZ(x, y - 1, z)] && ey > 0 ? 1 : 0;
        int f5 = !opaque[XYZ(x, y, z - 1)] ? 1 : 0;
        int f6 = !opaque[XYZ(x, y, z + 1)] ? 1 : 0;
        int total = f1 + f2 + f3 + f4 + f5 + f6;
        if (total == 0)
            continue;
        char neighbors[27] = {0};
        char lights[27] = {0};
        float shades[27] = {0};
        int index = 0;
        for (int dx = -1; dx <= 1; dx++) {
            for (int dy = -1; dy <= 1; dy++) {
                for (int dz = -1; dz <= 1; dz++) {
                    neighbors[index] = opaque[XYZ(x + dx, y + dy, z + dz)];
                    lights[index] = light[XYZ(x + dx, y + dy, z + dz)];
                    shades[index] = 0;
                    if (y + dy <= highest[XZ(x + dx, z + dz)]) {
                        for (int oy = 0; oy < 8; oy++) {
                            if (opaque[XYZ(x + dx, y + dy + oy, z + dz)]) {
                                shades[index] = 1.0 - oy * 0.125;
                                break;
                            }
                        }
                    }
                    index++;
                }
            }
        }
        float ao[6][4];
        float light[6][4];
        occlusion(neighbors, lights, shades, ao, light);
        if (is_plant(ew)) {
            total = 4;
            float min_ao = 1;
            float max_light = 0;
            for (int a = 0; a < 6; a++) {
                for (int b = 0; b < 4; b++) {
                    min_ao = MIN(min_ao, ao[a][b]);
                    max_light = MAX(max_light, light[a][b]);
                }
            }
            float rotation = abs(ex * 323 + ez * -845) % 360;
            if (offset + total * 60 > faces * 60) continue; /* HACK FIXME */
            make_plant(
                data + offset, min_ao, max_light,
                ex, ey, ez, 1, ew, rotation);
        }
        else {
            if (offset + total * 60 > faces * 60) continue; /* HACK FIXME */
            make_cube(
                data + offset, ao, light,
                f1, f2, f3, f4, f5, f6,
                ex, ey, ez, 1, ew);
        }
        offset += total * 60;
    }

    free(opaque);
    free(light);
    free(highest);

    item->faces = faces;
    item->data = data;
}

static void generate_chunk(Chunk *chunk, WorkerItem *item) {
    chunk->faces = item->faces;
    del_buffer(chunk->buffer);
    chunk->buffer = gen_faces(10, item->faces, item->data);
    int diameter = g->render_radius * 2 * CHUNK_SIZE;
    glActiveTexture(GL_TEXTURE3);
    glBindTexture(GL_TEXTURE_3D, g->world_tex);
    glTexSubImage3D(GL_TEXTURE_3D, 0,
        mod_euc(item->p * CHUNK_SIZE, diameter),
        mod_euc(item->q * CHUNK_SIZE, diameter),
        mod_euc(item->r * CHUNK_SIZE, diameter),
        CHUNK_SIZE, CHUNK_SIZE, CHUNK_SIZE,
        GL_ALPHA, GL_UNSIGNED_BYTE, chunk->ws);
}

static void gen_chunk_buffer(Chunk *chunk) {
    WorkerItem _item;
    WorkerItem *item = &_item;
    item->p = chunk->p;
    item->q = chunk->q;
    item->r = chunk->r;
    for (int dp = -1; dp <= 1; dp++) {
        for (int dq = -1; dq <= 1; dq++) {
            for (int dr = -1; dr <= 1; dr++) {
                Chunk *other = chunk;
                if (dp || dq || dr) {
                    other = find_chunk(chunk->p + dp, chunk->q + dq, chunk->r + dr);
                }
                if (other) {
                    item->chunks[dp + 1][dq + 1][dr + 1] = other;
                    item->light_maps[dp + 1][dq + 1][dr + 1] = &other->lights;
                }
                else {
                    item->chunks[dp + 1][dq + 1][dr + 1] = 0;
                    item->light_maps[dp + 1][dq + 1][dr + 1] = 0;
                }
            }
        }
    }
    compute_chunk(item);
    generate_chunk(chunk, item);
    chunk->dirty = 0;
}

static void request_chunk(int p, int q, int r) {
    client_chunk(p, q, r);
}

static void init_chunk(Chunk *chunk, int p, int q, int r) {
    chunk->p = p;
    chunk->q = q;
    chunk->r = r;
    dirty_chunk(chunk);
    Map *light_map = &chunk->lights;
    int dx = p * CHUNK_SIZE - 1;
    int dy = q * CHUNK_SIZE - 1;
    int dz = r * CHUNK_SIZE - 1;
    memset(chunk->ws, 0, sizeof(chunk->ws));
    map_alloc(light_map, dx, dy, dz, 0xf);
}

static void create_chunk(Chunk *chunk, int p, int q, int r) {
    init_chunk(chunk, p, q, r);

    request_chunk(p, q, r);
}

static void delete_chunks() {
    int count = g->chunk_count;
    State *s = &g->players->state;
    for (int i = 0; i < MAX_CHUNKS; i++) {
        Chunk *chunk = g->chunks + i;
        if (chunk->q < 0) continue;
        int p = chunked(s->x);
        int q = chunked(s->y);
        int r = chunked(s->z);
        if (chunk_distance(chunk, p, q, r) < g->delete_radius) {
            continue;
        }
        map_free(&chunk->lights);
        del_buffer(chunk->buffer);
        chunk->q = -1;
        int index = i;
        while (g->chunks[index].q >= 0) {
            Chunk *c = g->chunks + index;
            int pref = chunk_coord_hash(c->p, c->q, c->r);
            if (pref <= i) {
                memcpy(chunk, c, sizeof(Chunk));
                chunk = c;
                chunk->q = -1;
            }
            index = (index + 1) % MAX_CHUNKS;
        }
        count--;
    }
    g->chunk_count = count;
}

static void delete_all_chunks() {
    for (int i = 0; i < MAX_CHUNKS; i++) {
        Chunk *chunk = g->chunks + i;
        if (chunk->q < 0) continue;
        map_free(&chunk->lights);
        del_buffer(chunk->buffer);
        chunk->q = -1;
    }
    g->chunk_count = 0;
}

static void check_workers() {
    for (int i = 0; i < WORKERS; i++) {
        Worker *worker = g->workers + i;
        mtx_lock(&worker->mtx);
        if (worker->state == WORKER_DONE) {
            WorkerItem *item = &worker->item;
            Chunk *chunk = find_chunk(item->p, item->q, item->r);
            if (chunk) {
                if (item->load) {
                    request_chunk(item->p, item->q, item->r);
                }
                generate_chunk(chunk, item);
            }
            worker->state = WORKER_IDLE;
        }
        mtx_unlock(&worker->mtx);
    }
}

static void force_chunks(Player *player) {
    State *s = &player->state;
    int p = chunked(s->x);
    int q = chunked(s->y);
    int r = chunked(s->z);
    int rad = 2;
    for (int dp = -rad; dp <= rad; dp++) {
        for (int dq = -rad; dq <= rad; dq++) {
            for (int dr = -rad; dr <= rad; dr++) {
                int a = p + dp;
                int b = q + dq;
                int c = r + dr;
                if (b < 0)
                    continue;
                Chunk *chunk = find_chunk(a, b, c);
                if (chunk) {
                    if (chunk->dirty) {
                        gen_chunk_buffer(chunk);
                    }
                }
                else if (g->chunk_count < MAX_CHUNK_COUNT) {
                    int index = chunk_coord_hash(a, b, c);
                    while (g->chunks[index].q >= 0) index = (index + 1) % MAX_CHUNKS;
                    chunk = g->chunks + index;
                    g->chunk_count++;
                    create_chunk(chunk, a, b, c);
                    gen_chunk_buffer(chunk);
                }
            }
        }
    }
}

static void ensure_chunks_worker(Player *player, Worker *worker) {
    State *s = &player->state;
    float matrix[16];
    set_matrix_3d(
        matrix, g->width, g->height,
        s->x, s->y, s->z, s->rx, s->ry, g->fov, 0, g->render_radius);
    float planes[6][4];
    frustum_planes(planes, g->render_radius, matrix);
    int p = chunked(s->x);
    int q = chunked(s->y);
    int r = chunked(s->z);
    int rad = g->create_radius;
    int start = 0x0fffffff;
    int best_score = start;
    int best_a = 0;
    int best_b = 0;
    int best_c = 0;
    for (int dp = -rad; dp <= rad; dp++) {
        for (int dq = -rad; dq <= rad; dq++) {
            for (int dr = -rad; dr <= rad; dr++) {
                int a = p + dp;
                int b = q + dq;
                int c = r + dr;
                if (b < 0)
                    continue;
                int index = (ABS(a) ^ ABS(b) ^ ABS(c)) % WORKERS;
                if (index != worker->index) {
                    continue;
                }
                Chunk *chunk = find_chunk(a, b, c);
                if (chunk && !chunk->dirty) {
                    continue;
                }
                int distance = MAX(ABS(dp), ABS(dq));
                int invisible = !chunk_visible(planes, a, b, c);
                int priority = 0;
                if (chunk) {
                    priority = chunk->dirty;
                }
                int score = (invisible << 24) | (priority << 16) | distance;
                if (score < best_score) {
                    best_score = score;
                    best_a = a;
                    best_b = b;
                    best_c = c;
                }
            }
        }
    }
    if (best_score == start) {
        return;
    }
    int a = best_a;
    int b = best_b;
    int c = best_c;
    int load = 0;
    Chunk *chunk = find_chunk(a, b, c);
    if (!chunk) {
        load = 1;
        if (g->chunk_count < MAX_CHUNK_COUNT) {
            int index = chunk_coord_hash(a, b, c);
            while (g->chunks[index].q >= 0) index = (index + 1) % MAX_CHUNKS;
            chunk = g->chunks + index;
            g->chunk_count++;
            init_chunk(chunk, a, b, c);
        }
        else {
            return;
        }
    }
    WorkerItem *item = &worker->item;
    item->p = chunk->p;
    item->q = chunk->q;
    item->r = chunk->r;
    item->load = load;
    for (int dp = -1; dp <= 1; dp++) {
        for (int dq = -1; dq <= 1; dq++) {
            for (int dr = -1; dr <= 1; dr++) {
                Chunk *other = chunk;
                if (dp || dq || dr) {
                    other = find_chunk(chunk->p + dp, chunk->q + dq, chunk->r + dr);
                }
                if (other) {
                    item->chunks[dp + 1][dq + 1][dr + 1] = other;
                    item->light_maps[dp + 1][dq + 1][dr + 1] = &other->lights;
                }
                else {
                    item->chunks[dp + 1][dq + 1][dr + 1] = 0;
                    item->light_maps[dp + 1][dq + 1][dr + 1] = 0;
                }
            }
        }
    }
    chunk->dirty = 0;
    worker->state = WORKER_BUSY;
    cnd_signal(&worker->cnd);
}

static void ensure_chunks(Player *player) {
    check_workers();
    force_chunks(player);
    for (int i = 0; i < WORKERS; i++) {
        Worker *worker = g->workers + i;
        mtx_lock(&worker->mtx);
        if (worker->state == WORKER_IDLE) {
            ensure_chunks_worker(player, worker);
        }
        mtx_unlock(&worker->mtx);
    }
}

static int worker_run(void *arg) {
    Worker *worker = (Worker *)arg;
    int running = 1;
    while (running) {
        mtx_lock(&worker->mtx);
        while (worker->state != WORKER_BUSY) {
            cnd_wait(&worker->cnd, &worker->mtx);
        }
        mtx_unlock(&worker->mtx);
        WorkerItem *item = &worker->item;
        compute_chunk(item);
        mtx_lock(&worker->mtx);
        worker->state = WORKER_DONE;
        mtx_unlock(&worker->mtx);
    }
    return 0;
}

static void toggle_light(int x, int y, int z) {
    int p = chunked(x);
    int q = chunked(y);
    int r = chunked(z);
    Chunk *chunk = find_chunk(p, q, r);
    if (chunk) {
        Map *map = &chunk->lights;
        int w = map_get(map, x, y, z) ? 0 : 15;
        map_set(map, x, y, z, w);
        client_light(x, y, z, w);
        dirty_chunk(chunk);
    }
}

static void set_light(int x, int y, int z, int w) {
    Chunk *chunk = find_chunk(chunked(x), chunked(y), chunked(z));
    if (chunk) {
        Map *map = &chunk->lights;
        if (map_set(map, x, y, z, w)) {
            dirty_chunk(chunk);
        }
    }
}

static void set_block(int x, int y, int z, int w) {
    Chunk *chunk = find_chunk(chunked(x), chunked(y), chunked(z));
    if (chunk) {
        if (chunk_set(chunk, x, y, z, w)) {
            dirty_chunk(chunk);
        }
    }
    if (w == 0) {
        set_light(x, y, z, 0);
    }
}

static void put_block(int x, int y, int z, int w) {
    set_block(x, y, z, w);
    client_block(x, y, z, w);
}

static void resize_world(int radius) {
    g->create_radius = radius;
    g->delete_radius = radius + 2;
    g->render_radius = radius;
    if (!g->world_tex) {
        glGenTextures(1, &g->world_tex);
        glActiveTexture(GL_TEXTURE3);
        glBindTexture(GL_TEXTURE_3D, g->world_tex);
        glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_WRAP_S, GL_REPEAT);
        glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_WRAP_T, GL_REPEAT);
        glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_WRAP_R, GL_REPEAT);
    }
    int diameter = g->render_radius * 2 * CHUNK_SIZE;
    State *s = &g->players[0].state;
    Chunk *chunk = find_chunk(chunked(s->x), chunked(s->y), chunked(s->z));
    if (chunk)
        dirty_chunk(chunk);
    glActiveTexture(GL_TEXTURE3);
    glTexImage3D(GL_TEXTURE_3D, 0, GL_ALPHA8, diameter, diameter, diameter, 0, GL_ALPHA, GL_UNSIGNED_BYTE, 0);
}

static int render_world(Attrib *attrib, Player *player) {
    State *s = &player->state;
    ensure_chunks(player);
    int p = chunked(s->x), q = chunked(s->y), r = chunked(s->z);

    float matrix[16];
    set_matrix_3d(
        matrix, g->width, g->height,
        s->x, s->y + 1.7, s->z, s->rx, s->ry, g->fov, 0, g->render_radius);
    float planes[6][4];
    frustum_planes(planes, g->render_radius, matrix);

    glUseProgram(attrib->program);

    glUniformMatrix4fv(attrib->matrix, 1, GL_FALSE, matrix);
    glUniform3f(attrib->camera, s->x, s->y + 1.7, s->z);

    glUniform1i(attrib->sampler, 3);
    glUniform1i(attrib->extra3, 0);

    glUniform1i(attrib->extra1, g->render_radius * 2 * CHUNK_SIZE);
    glUniform1i(attrib->extra2, g->render_radius * CHUNK_SIZE);

    glUniform1f(attrib->timer, time_of_day());

    glEnableVertexAttribArray(attrib->position);
//    glEnableVertexAttribArray(attrib->normal);
//    glEnableVertexAttribArray(attrib->uv);

    for (int i = 0; i < MAX_CHUNKS; i++) {
        Chunk *chunk = g->chunks + i;
        if (chunk->q < 0) continue;
        if (chunk_distance(chunk, p, q, r) > g->render_radius)
            continue;
        if (!chunk_visible(planes, chunk->p, chunk->q, chunk->r))
            continue;

        glBindBuffer(GL_ARRAY_BUFFER, chunk->buffer);

        glVertexAttribPointer(attrib->position, 3, GL_FLOAT, GL_FALSE,
            sizeof(GLfloat) * 10, 0);
//        glVertexAttribPointer(attrib->normal, 3, GL_FLOAT, GL_FALSE,
//            sizeof(GLfloat) * 10, (GLvoid *)(sizeof(GLfloat) * 3));
//        glVertexAttribPointer(attrib->uv, 4, GL_FLOAT, GL_FALSE,
//            sizeof(GLfloat) * 10, (GLvoid *)(sizeof(GLfloat) * 6));

        glDrawArrays(GL_TRIANGLES, 0, chunk->faces * 6);

        glBindBuffer(GL_ARRAY_BUFFER, 0);
    }

//    glDisableVertexAttribArray(attrib->uv);
//    glDisableVertexAttribArray(attrib->normal);
    glDisableVertexAttribArray(attrib->position);

    return 0;
}

static void render_crosshairs(Attrib *attrib) {
    float matrix[16];
    set_matrix_2d(matrix, g->width, g->height);
    glUseProgram(attrib->program);
    glLineWidth(4 * g->scale);
    glEnable(GL_COLOR_LOGIC_OP);
    glUniformMatrix4fv(attrib->matrix, 1, GL_FALSE, matrix);
    GLuint crosshair_buffer = gen_crosshair_buffer();
    draw_lines(attrib, crosshair_buffer, 2, 4);
    del_buffer(crosshair_buffer);
    glDisable(GL_COLOR_LOGIC_OP);
}

static void render_text(
    Attrib *attrib, int justify, float x, float y, float n, char *text)
{
    float matrix[16];
    set_matrix_2d(matrix, g->width, g->height);
    glUseProgram(attrib->program);
    glUniformMatrix4fv(attrib->matrix, 1, GL_FALSE, matrix);
    glUniform1i(attrib->sampler, 1);
    int length = strlen(text);
    x -= n * justify * (length - 1) / 2;
    GLuint buffer = gen_text_buffer(x, y, n, text);
    draw_text(attrib, buffer, length);
    del_buffer(buffer);
}

static void add_message(const char *text) {
    printf("%s\n", text);
    snprintf(
        g->messages[g->message_index], MAX_TEXT_LENGTH, "%s", text);
    g->message_index = (g->message_index + 1) % MAX_MESSAGES;
}

static void parse_command(const char *buffer, int forward) {
    char server_addr[MAX_ADDR_LENGTH];
    int server_port = DEFAULT_PORT;
    char filename[MAX_PATH_LENGTH];
    int radius, count, xc, yc, zc;
    if (sscanf(buffer,
        "/server %128s %d", server_addr, &server_port) >= 1)
    {
        g->server_changed = 1;
        strncpy(g->server_addr, server_addr, MAX_ADDR_LENGTH);
        g->server_port = server_port;
    }
    else if (sscanf(buffer, "/view %d", &radius) == 1) {
        if (radius >= 1 && radius <= 24) {
            resize_world(radius);
        }
        else {
            add_message("Viewing distance must be between 1 and 24.");
        }
    }
    else if (forward) {
        client_talk(buffer);
    }
}

static void on_light() {
    State *s = &g->players->state;
    int hx, hy, hz;
    int hw = hit_test(0, s->x, s->y + 1.7, s->z, s->rx, s->ry, &hx, &hy, &hz);
    if (hy > 0 && is_destructable(hw)) {
        toggle_light(hx, hy, hz);
    }
}

static void on_left_click() {
    State *s = &g->players->state;
    int hx, hy, hz;
    int hw = hit_test(0, s->x, s->y + 1.7, s->z, s->rx, s->ry, &hx, &hy, &hz);
    if (hy > 0 && is_destructable(hw)) {
        put_block(hx, hy, hz, 0);
        if (is_plant(get_block(hx, hy + 1, hz))) {
            put_block(hx, hy + 1, hz, 0);
        }
    }
}

static void on_right_click() {
    State *s = &g->players->state;
    int hx, hy, hz;
    int hw = hit_test(1, s->x, s->y + 1.7, s->z, s->rx, s->ry, &hx, &hy, &hz);
    if (hy > 0 && is_obstacle(hw)) {
        if (!player_intersects_block(2, s->x, s->y, s->z, hx, hy, hz)) {
            put_block(hx, hy, hz, items[g->item_index]);
        }
    }
}

static void on_middle_click() {
    State *s = &g->players->state;
    int hx, hy, hz;
    int hw = hit_test(0, s->x, s->y + 1.7, s->z, s->rx, s->ry, &hx, &hy, &hz);
    for (int i = 0; i < item_count; i++) {
        if (items[i] == hw) {
            g->item_index = i;
            break;
        }
    }
}

static void on_key(GLFWwindow *window, int key, int scancode, int action, int mods) {
    int control = mods & (GLFW_MOD_CONTROL | GLFW_MOD_SUPER);
    int exclusive =
        glfwGetInputMode(window, GLFW_CURSOR) == GLFW_CURSOR_DISABLED;
    if (action == GLFW_RELEASE) {
        return;
    }
    if (key == GLFW_KEY_BACKSPACE) {
        if (g->typing) {
            int n = strlen(g->typing_buffer);
            if (n > 0) {
                g->typing_buffer[n - 1] = '\0';
            }
        }
    }
    if (action != GLFW_PRESS) {
        return;
    }
    if (key == GLFW_KEY_ESCAPE) {
        if (g->typing) {
            g->typing = 0;
        }
        else if (exclusive) {
            glfwSetInputMode(window, GLFW_CURSOR, GLFW_CURSOR_NORMAL);
        }
    }
    if (key == GLFW_KEY_ENTER) {
        if (g->typing) {
            if (mods & GLFW_MOD_SHIFT) {
                int n = strlen(g->typing_buffer);
                if (n < MAX_TEXT_LENGTH - 1) {
                    g->typing_buffer[n] = '\r';
                    g->typing_buffer[n + 1] = '\0';
                }
            }
            else {
                g->typing = 0;
                if (g->typing_buffer[0] == '/') {
                    parse_command(g->typing_buffer, 1);
                }
                else {
                    client_talk(g->typing_buffer);
                }
            }
        }
        else {
            if (control) {
                on_right_click();
            }
            else {
                on_left_click();
            }
        }
    }
    if (control && key == 'V') {
        const char *buffer = glfwGetClipboardString(window);
        if (g->typing) {
            g->suppress_char = 1;
            strncat(g->typing_buffer, buffer,
                MAX_TEXT_LENGTH - strlen(g->typing_buffer) - 1);
        }
        else {
            parse_command(buffer, 0);
        }
    }
    if (!g->typing) {
        if (key == CRAFT_KEY_FLY) {
            g->flying = !g->flying;
        }
        if (key >= '1' && key <= '9') {
            g->item_index = key - '1';
        }
        if (key == '0') {
            g->item_index = 9;
        }
        if (key == CRAFT_KEY_ITEM_NEXT) {
            g->item_index = (g->item_index + 1) % item_count;
        }
        if (key == CRAFT_KEY_ITEM_PREV) {
            g->item_index--;
            if (g->item_index < 0) {
                g->item_index = item_count - 1;
            }
        }
    }
}

static void on_char(GLFWwindow *window, unsigned int u) {
    if (g->suppress_char) {
        g->suppress_char = 0;
        return;
    }
    if (g->typing) {
        if (u >= 32 && u < 128) {
            char c = (char)u;
            int n = strlen(g->typing_buffer);
            if (n < MAX_TEXT_LENGTH - 1) {
                g->typing_buffer[n] = c;
                g->typing_buffer[n + 1] = '\0';
            }
        }
    }
    else {
        if (u == CRAFT_KEY_CHAT) {
            g->typing = 1;
            g->typing_buffer[0] = '\0';
        }
        if (u == CRAFT_KEY_COMMAND) {
            g->typing = 1;
            g->typing_buffer[0] = '/';
            g->typing_buffer[1] = '\0';
        }
    }
}

static void on_scroll(GLFWwindow *window, double xdelta, double ydelta) {
    static double ypos = 0;
    ypos += ydelta;
    if (ypos < -SCROLL_THRESHOLD) {
        g->item_index = (g->item_index + 1) % item_count;
        ypos = 0;
    }
    if (ypos > SCROLL_THRESHOLD) {
        g->item_index--;
        if (g->item_index < 0) {
            g->item_index = item_count - 1;
        }
        ypos = 0;
    }
}

static void on_mouse_button(GLFWwindow *window, int button, int action, int mods) {
    int control = mods & (GLFW_MOD_CONTROL | GLFW_MOD_SUPER);
    int exclusive =
        glfwGetInputMode(window, GLFW_CURSOR) == GLFW_CURSOR_DISABLED;
    if (action != GLFW_PRESS) {
        return;
    }
    if (button == GLFW_MOUSE_BUTTON_LEFT) {
        if (exclusive) {
            if (control) {
                on_right_click();
            }
            else {
                on_left_click();
            }
        }
        else {
            glfwSetInputMode(window, GLFW_CURSOR, GLFW_CURSOR_DISABLED);
        }
    }
    if (button == GLFW_MOUSE_BUTTON_RIGHT) {
        if (exclusive) {
            if (control) {
                on_light();
            }
            else {
                on_right_click();
            }
        }
    }
    if (button == GLFW_MOUSE_BUTTON_MIDDLE) {
        if (exclusive) {
            on_middle_click();
        }
    }
}

static void create_window() {
    int window_width = WINDOW_WIDTH;
    int window_height = WINDOW_HEIGHT;
    GLFWmonitor *monitor = NULL;
    if (FULLSCREEN) {
        int mode_count;
        monitor = glfwGetPrimaryMonitor();
        const GLFWvidmode *modes = glfwGetVideoModes(monitor, &mode_count);
        window_width = modes[mode_count - 1].width;
        window_height = modes[mode_count - 1].height;
    }
    g->window = glfwCreateWindow(
        window_width, window_height, "Craft", monitor, NULL);
}

static void handle_mouse_input() {
    int exclusive =
        glfwGetInputMode(g->window, GLFW_CURSOR) == GLFW_CURSOR_DISABLED;
    static double px = 0;
    static double py = 0;
    State *s = &g->players->state;
    if (exclusive && (px || py)) {
        double mx, my;
        glfwGetCursorPos(g->window, &mx, &my);
        float m = 0.0025;
        s->rx += (mx - px) * m;
        if (INVERT_MOUSE) {
            s->ry += (my - py) * m;
        }
        else {
            s->ry -= (my - py) * m;
        }
        if (s->rx < 0) {
            s->rx += RADIANS(360);
        }
        if (s->rx >= RADIANS(360)){
            s->rx -= RADIANS(360);
        }
        s->ry = MAX(s->ry, -RADIANS(90));
        s->ry = MIN(s->ry, RADIANS(90));
        px = mx;
        py = my;
    }
    else {
        glfwGetCursorPos(g->window, &px, &py);
    }
}

static void handle_movement(double dt) {
    static float dy = 0;
    State *s = &g->players->state;
    int sz = 0;
    int sx = 0;
    if (!g->typing) {
        float m = dt * 1.0;
        g->fov = glfwGetKey(g->window, CRAFT_KEY_ZOOM) ? 15 : 80;
        if (glfwGetKey(g->window, CRAFT_KEY_FORWARD)) sz--;
        if (glfwGetKey(g->window, CRAFT_KEY_BACKWARD)) sz++;
        if (glfwGetKey(g->window, CRAFT_KEY_LEFT)) sx--;
        if (glfwGetKey(g->window, CRAFT_KEY_RIGHT)) sx++;
        if (glfwGetKey(g->window, GLFW_KEY_LEFT)) s->rx -= m;
        if (glfwGetKey(g->window, GLFW_KEY_RIGHT)) s->rx += m;
        if (glfwGetKey(g->window, GLFW_KEY_UP)) s->ry += m;
        if (glfwGetKey(g->window, GLFW_KEY_DOWN)) s->ry -= m;
    }
    float vx, vy, vz;
    get_motion_vector(g->flying, sz, sx, s->rx, s->ry, &vx, &vy, &vz);
    if (!g->typing) {
        if (glfwGetKey(g->window, CRAFT_KEY_JUMP)) {
            if (g->flying) {
                vy = 1;
            }
            else if (dy == 0) {
                dy = 8;
            }
        }
    }
    float speed = g->flying ? 20 : 5;
    int estimate = roundf(sqrtf(
        powf(vx * speed, 2) +
        powf(vy * speed + ABS(dy) * 2, 2) +
        powf(vz * speed, 2)) * dt * 8);
    int step = MAX(8, estimate);
    float ut = dt / step;
    vx = vx * ut * speed;
    vy = vy * ut * speed;
    vz = vz * ut * speed;
    for (int i = 0; i < step; i++) {
        if (g->flying) {
            dy = 0;
        }
        else {
            dy -= ut * 25;
            dy = MAX(dy, -250);
        }
        s->x += vx;
        s->y += vy + dy * ut;
        s->z += vz;
        if (collide(2, &s->x, &s->y, &s->z)) {
            dy = 0;
        }
    }
    if (s->y < 0) {
        s->y = highest_block(s->x, s->z);
    }
}

static void parse_buffer(char *buf, size_t tsize) {
    Player *me = g->players;
    State *s = &g->players->state;
    int pid;
    float ux, uy, uz, urx, ury;
    int bx, by, bz, bw;
    float px, py, pz, prx, pry;
    double elapsed;
    int day_length;
    while (tsize) {
        size_t bsize = *(size_t *)buf;
        char *buffer = buf + sizeof(size_t);
        tsize -= bsize + sizeof(size_t);
        buf += bsize + sizeof(size_t);
        if (buffer[0] == 'C') {
#define B64R(x) (((int64_t)(x)[0] << 56) | ((int64_t)(x)[1] << 48) | ((int64_t)(x)[2] << 40) | ((int64_t)(x)[3] << 32) | ((int64_t)(x)[4] << 24) | ((int64_t)(x)[5] << 16) | ((int64_t)(x)[6] << 8) | ((int64_t)(x)[7] << 0))
            int64_t p = B64R(buffer+1);
            int64_t q = B64R(buffer+9);
            int64_t r = B64R(buffer+17);
#undef B64R
            buffer += 25;
            bsize -= 26;
            Chunk *chunk = find_chunk(p, q, r);
            if (!chunk) {
                int index = chunk_coord_hash(p, q, r);
                if (g->chunk_count < MAX_CHUNK_COUNT) {
                    while (g->chunks[index].q >= 0) index = (index + 1) % MAX_CHUNKS;
                    chunk = g->chunks + index;
                    g->chunk_count++;
                    init_chunk(chunk, p, q, r);
                }
            }
            if (chunk) {
                size_t len = tinfl_decompress_mem_to_mem(chunk->ws, sizeof(chunk->ws), buffer, bsize, 0);
                dirty_chunk(chunk);
                if (chunked(s->x) == p && chunked(s->z) == r) {
                    if (player_intersects_block(2, s->x, s->y, s->z, s->x, s->y, s->z)) {
                        s->y = highest_block(s->x, s->z);
                    }
                }
            } else {
                printf("Chunk discarded\n");
            }
        } else if (sscanf(buffer, "U,%d,%f,%f,%f,%f,%f",
            &pid, &ux, &uy, &uz, &urx, &ury) == 6)
        {
            me->id = pid;
            s->x = ux; s->y = uy; s->z = uz; s->rx = urx; s->ry = ury;
            force_chunks(me);
            if (uy == 0) {
                s->y = highest_block(s->x, s->z);
            }
        } else if (sscanf(buffer, "B,%d,%d,%d,%d",
            &bx, &by, &bz, &bw) == 6)
        {
            set_block(bx, by, bz, bw);
            if (player_intersects_block(2, s->x, s->y, s->z, bx, by, bz)) {
                s->y = highest_block(s->x, s->z) + 2;
            }
            Chunk *chunk = find_chunk(chunked(bx), chunked(by), chunked(bz));
            if (chunk) {
                dirty_chunk(chunk);
            }
        } else if (sscanf(buffer, "L,%d,%d,%d,%d",
            &bx, &by, &bz, &bw) == 6)
        {
            set_light(bx, by, bz, bw);
            Chunk *chunk = find_chunk(chunked(bx), chunked(by), chunked(bz));
            if (chunk) {
                dirty_chunk(chunk);
            }
        } else if (sscanf(buffer, "P,%d,%f,%f,%f,%f,%f",
            &pid, &px, &py, &pz, &prx, &pry) == 6)
        {
            Player *player = find_player(pid);
            if (!player && pid != me->id && g->player_count < MAX_PLAYERS) {
                player = g->players + g->player_count;
                g->player_count++;
                player->id = pid;
                player->buffer = 0;
                snprintf(player->name, MAX_NAME_LENGTH, "player%d", pid);
                update_player(player, px, py, pz, prx, pry, 1); // twice
            }
            if (player) {
                update_player(player, px, py, pz, prx, pry, 1);
            }
        } else if (sscanf(buffer, "D,%d", &pid) == 1) {
            delete_player(pid);
        } else if (sscanf(buffer, "E,%lf,%d", &elapsed, &day_length) == 2) {
            glfwSetTime(fmod(elapsed, day_length));
            g->day_length = day_length;
            g->time_changed = 1;
        } else if (buffer[0] == 'T' && buffer[1] == ',') {
            char *text = buffer + 2;
            add_message(text);
        }
        char format[64];
        snprintf(
            format, sizeof(format), "N,%%d,%%%ds", MAX_NAME_LENGTH - 1);
        char name[MAX_NAME_LENGTH];
        if (sscanf(buffer, format, &pid, name) == 2) {
            Player *player = find_player(pid);
            if (player) {
                strncpy(player->name, name, MAX_NAME_LENGTH);
            }
        }
    }
}

static void reset_model() {
    for (int i = 0; i < MAX_CHUNKS; i++) {
        memset(g->chunks + i, 0, sizeof(Chunk));
        g->chunks[i].q = -1;
    }
    g->chunk_count = 0;
    memset(g->players, 0, sizeof(Player) * MAX_PLAYERS);
    g->player_count = 0;
    g->flying = 0;
    g->item_index = 0;
    memset(g->typing_buffer, 0, sizeof(char) * MAX_TEXT_LENGTH);
    g->typing = 0;
    memset(g->messages, 0, sizeof(char) * MAX_MESSAGES * MAX_TEXT_LENGTH);
    g->message_index = 0;
    g->day_length = DAY_LENGTH;
    glfwSetTime(g->day_length / 3.0);
    g->time_changed = 1;
}

void GLAPIENTRY ogl_debug_callback(GLenum src, GLenum type, GLuint id, GLenum sev, GLsizei len, GLchar const *msg, void const *arg) {
    fprintf(stderr, "OpenGL%s: type=%x sev=%x msg=%s\n", type == GL_DEBUG_TYPE_ERROR ? " ERROR" : "", type, sev, msg);
}

int main(int argc, char **argv) {
    // INITIALIZATION //
    srand(time(NULL));
    rand();

    // WINDOW INITIALIZATION //
    if (!glfwInit()) {
        return -1;
    }
    create_window();
    if (!g->window) {
        glfwTerminate();
        return -1;
    }

    glfwMakeContextCurrent(g->window);
    glfwSwapInterval(VSYNC);
//    glfwSetInputMode(g->window, GLFW_CURSOR, GLFW_CURSOR_DISABLED);
    glfwSetKeyCallback(g->window, on_key);
    glfwSetCharCallback(g->window, on_char);
    glfwSetMouseButtonCallback(g->window, on_mouse_button);
    glfwSetScrollCallback(g->window, on_scroll);

    if (glewInit() != GLEW_OK) {
        return -1;
    }

    glEnable(GL_TEXTURE_3D);
    glEnable(GL_CULL_FACE);
    glEnable(GL_DEPTH_TEST);
    glLogicOp(GL_INVERT);
    glClearColor(1, 1, 1, 1);

    glEnable(GL_DEBUG_OUTPUT);
    glDebugMessageCallback(ogl_debug_callback, 0);

    // LOAD TEXTURES //
    GLuint texture;
    glGenTextures(1, &texture);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, texture);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    load_png_texture("textures/texture.png");

    GLuint font;
    glGenTextures(1, &font);
    glActiveTexture(GL_TEXTURE1);
    glBindTexture(GL_TEXTURE_2D, font);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    load_png_texture("textures/font.png");

    GLuint sky;
    glGenTextures(1, &sky);
    glActiveTexture(GL_TEXTURE2);
    glBindTexture(GL_TEXTURE_2D, sky);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    load_png_texture("textures/sky.png");

    // LOAD SHADERS //
    Attrib block_attrib = {0};
    Attrib line_attrib = {0};
    Attrib text_attrib = {0};
    GLuint program;

    program = load_program(
        "shaders/block_vertex.glsl", "shaders/block_fragment.glsl");
    block_attrib.program = program;
    block_attrib.position = glGetAttribLocation(program, "position");
    block_attrib.normal = glGetAttribLocation(program, "normal");
    block_attrib.uv = glGetAttribLocation(program, "uv");
    block_attrib.matrix = glGetUniformLocation(program, "matrix");
    block_attrib.sampler = glGetUniformLocation(program, "world");
    block_attrib.camera = glGetUniformLocation(program, "camera");
    block_attrib.timer = glGetUniformLocation(program, "timer");
    block_attrib.extra1 = glGetUniformLocation(program, "world_size");
    block_attrib.extra2 = glGetUniformLocation(program, "render_dist");
    block_attrib.extra3 = glGetUniformLocation(program, "texture");

    program = load_program(
        "shaders/line_vertex.glsl", "shaders/line_fragment.glsl");
    line_attrib.program = program;
    line_attrib.position = glGetAttribLocation(program, "position");
    line_attrib.matrix = glGetUniformLocation(program, "matrix");

    program = load_program(
        "shaders/text_vertex.glsl", "shaders/text_fragment.glsl");
    text_attrib.program = program;
    text_attrib.position = glGetAttribLocation(program, "position");
    text_attrib.uv = glGetAttribLocation(program, "uv");
    text_attrib.matrix = glGetUniformLocation(program, "matrix");
    text_attrib.sampler = glGetUniformLocation(program, "sampler");

    // CHECK COMMAND LINE ARGUMENTS //
    if (argc == 2 || argc == 3) {
        strncpy(g->server_addr, argv[1], MAX_ADDR_LENGTH);
        g->server_port = argc == 3 ? atoi(argv[2]) : DEFAULT_PORT;
    }
    else {
        fprintf(stderr, "Usage: %s server [port]\n", argv[0]);
        return 1;
    }

    // INITIALIZE WORKER THREADS
    for (int i = 0; i < WORKERS; i++) {
        Worker *worker = g->workers + i;
        worker->index = i;
        worker->state = WORKER_IDLE;
        mtx_init(&worker->mtx, mtx_plain);
        cnd_init(&worker->cnd);
        thrd_create(&worker->thrd, worker_run, worker);
    }

    // OUTER LOOP //
    int running = 1;
    while (running) {
        // CLIENT INITIALIZATION //
        client_enable();
        client_connect(g->server_addr, g->server_port);
        client_start();
        client_version(2);

        // LOCAL VARIABLES //
        reset_model();
        resize_world(CHUNK_RADIUS);
        FPS fps = {0, 0, 0};
        double last_update = glfwGetTime();

        Player *me = g->players;
        State *s = &g->players->state;
        me->id = 0;
        me->name[0] = '\0';
        me->buffer = 0;
        g->player_count = 1;

        // BEGIN MAIN LOOP //
        double previous = glfwGetTime();
        while (1) {
            // WINDOW SIZE AND SCALE //
            g->scale = get_scale_factor();
            glfwGetFramebufferSize(g->window, &g->width, &g->height);
            glViewport(0, 0, g->width, g->height);

            // FRAME RATE //
            if (g->time_changed) {
                g->time_changed = 0;
                last_update = glfwGetTime();
                memset(&fps, 0, sizeof(fps));
            }
            update_fps(&fps);
            double now = glfwGetTime();
            double dt = now - previous;
            dt = MIN(dt, 0.2);
            dt = MAX(dt, 0.0);
            previous = now;

            // HANDLE MOUSE INPUT //
            handle_mouse_input();

            // HANDLE MOVEMENT //
            handle_movement(dt);

            // HANDLE DATA FROM SERVER //
            size_t size;
            char *buffer = client_recv(&size);
            if (buffer) {
                parse_buffer(buffer, size);
                free(buffer);
            }

            // SEND POSITION TO SERVER //
            if (now - last_update > 0.1) {
                last_update = now;
                client_position(s->x, s->y, s->z, s->rx, s->ry);
            }

            // PREPARE TO RENDER //
            delete_chunks();

            // RENDER 3-D SCENE //
            glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
            render_world(&block_attrib, me);

            // RENDER HUD //
            glClear(GL_DEPTH_BUFFER_BIT);
            if (SHOW_CROSSHAIRS) {
                render_crosshairs(&line_attrib);
            }

            // RENDER TEXT //
            char text_buffer[1024];
            float ts = 12 * g->scale;
            float tx = ts / 2;
            float ty = g->height - ts;
            if (SHOW_INFO_TEXT) {
                int hour = time_of_day() * 24;
                char am_pm = hour < 12 ? 'a' : 'p';
                hour = hour % 12;
                hour = !hour && am_pm == 'p' ? 12 : hour;
                snprintf(
                    text_buffer, 1024,
                    "(%d, %d, %d) (%.2f, %.2f, %.2f) [%d, %d] %d%cm %dfps",
                    chunked(s->x), chunked(s->y), chunked(s->z), s->x, s->y, s->z,
                    g->player_count, g->chunk_count,
                    hour, am_pm, fps.fps);
                render_text(&text_attrib, ALIGN_LEFT, tx, ty, ts, text_buffer);
                ty -= ts * 2;
            }
            if (SHOW_CHAT_TEXT) {
                for (int i = 0; i < MAX_MESSAGES; i++) {
                    int index = (g->message_index + i) % MAX_MESSAGES;
                    if (strlen(g->messages[index])) {
                        render_text(&text_attrib, ALIGN_LEFT, tx, ty, ts,
                            g->messages[index]);
                        ty -= ts * 2;
                    }
                }
            }
            if (g->typing) {
                snprintf(text_buffer, 1024, "> %s", g->typing_buffer);
                render_text(&text_attrib, ALIGN_LEFT, tx, ty, ts, text_buffer);
                ty -= ts * 2;
            }
            if (SHOW_PLAYER_NAMES) {
                Player *other = player_crosshair(me);
                if (other) {
                    render_text(&text_attrib, ALIGN_CENTER,
                        g->width / 2, g->height / 2 - ts - 24, ts,
                        other->name);
                }
            }

            // SWAP AND POLL //
            glfwSwapBuffers(g->window);
            glfwPollEvents();
            if (glfwWindowShouldClose(g->window)) {
                running = 0;
                break;
            }
            if (g->server_changed) {
                g->server_changed = 0;
                break;
            }
        }

        // SHUTDOWN //
        client_stop();
        client_disable();
        delete_all_chunks();
        delete_all_players();
    }

    glfwTerminate();
    return 0;
}
