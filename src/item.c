#include "item.h"
#include "util.h"

const int items[] = {
    // items the user can build
    GRASS,
    SAND,
    STONE,
    BRICK,
    WOOD,
    CEMENT,
    DIRT,
    PLANK,
    SNOW,
    GLASS,
    COBBLE,
    LIGHT_STONE,
    DARK_STONE,
    CHEST,
    LEAVES,
    TALL_GRASS,
    YELLOW_FLOWER,
    RED_FLOWER,
    PURPLE_FLOWER,
    SUN_FLOWER,
    WHITE_FLOWER,
    BLUE_FLOWER,
    COLOR_00,
    COLOR_01,
    COLOR_02,
    COLOR_03,
    COLOR_04,
    COLOR_05,
    COLOR_06,
    COLOR_07,
    COLOR_08,
    COLOR_09,
    COLOR_10,
    COLOR_11,
    COLOR_12,
    COLOR_13,
    COLOR_14,
    COLOR_15,
    COLOR_16,
    COLOR_17,
    COLOR_18,
    COLOR_19,
    COLOR_20,
    COLOR_21,
    COLOR_22,
    COLOR_23,
    COLOR_24,
    COLOR_25,
    COLOR_26,
    COLOR_27,
    COLOR_28,
    COLOR_29,
    COLOR_30,
    COLOR_31
};

const int item_count = sizeof(items) / sizeof(int);

const int blocks[256][7] = {
    // w => (left, right, top, bottom, front, back, disp) tiles
    // disp is whether it should be rounded or not
    {0, 0, 0, 0, 0, 0, 0}, // 0 - empty
    {1, 1, 1, 1, 1, 1, 1}, // 1 - grass
    {2, 2, 2, 2, 2, 2, 1}, // 2 - sand
    {3, 3, 3, 3, 3, 3, 1}, // 3 - stone
    {4, 4, 4, 4, 4, 4, 0}, // 4 - brick
    {20, 20, 36, 4, 20, 20, 1}, // 5 - wood
    {5, 5, 5, 5, 5, 5, 0}, // 6 - cement
    {6, 6, 6, 6, 6, 6, 1}, // 7 - dirt
    {7, 7, 7, 7, 7, 7, 0}, // 8 - plank
    {24, 24, 40, 8, 24, 24, 1}, // 9 - snow
    {9, 9, 9, 9, 9, 9, 0}, // 10 - glass
    {10, 10, 10, 10, 10, 10, 1}, // 11 - cobble
    {11, 11, 11, 11, 11, 11, 1}, // 12 - light stone
    {12, 12, 12, 12, 12, 12, 1}, // 13 - dark stone
    {13, 13, 13, 13, 13, 13, 0}, // 14 - chest
    {14, 14, 14, 14, 14, 14, 1}, // 15 - leaves
    {15, 15, 15, 15, 15, 15, 1}, // 16 - cloud
    {0, 0, 0, 0, 0, 0, 0}, // 17
    {0, 0, 0, 0, 0, 0, 0}, // 18
    {0, 0, 0, 0, 0, 0, 0}, // 19
    {0, 0, 0, 0, 0, 0, 0}, // 20
    {0, 0, 0, 0, 0, 0, 0}, // 21
    {0, 0, 0, 0, 0, 0, 0}, // 22
    {0, 0, 0, 0, 0, 0, 0}, // 23
    {0, 0, 0, 0, 0, 0, 0}, // 24
    {0, 0, 0, 0, 0, 0, 0}, // 25
    {0, 0, 0, 0, 0, 0, 0}, // 26
    {0, 0, 0, 0, 0, 0, 0}, // 27
    {0, 0, 0, 0, 0, 0, 0}, // 28
    {0, 0, 0, 0, 0, 0, 0}, // 29
    {0, 0, 0, 0, 0, 0, 0}, // 30
    {0, 0, 0, 0, 0, 0, 0}, // 31
    {176, 176, 176, 176, 176, 176, 0}, // 32
    {177, 177, 177, 177, 177, 177, 0}, // 33
    {178, 178, 178, 178, 178, 178, 0}, // 34
    {179, 179, 179, 179, 179, 179, 0}, // 35
    {180, 180, 180, 180, 180, 180, 0}, // 36
    {181, 181, 181, 181, 181, 181, 0}, // 37
    {182, 182, 182, 182, 182, 182, 0}, // 38
    {183, 183, 183, 183, 183, 183, 0}, // 39
    {184, 184, 184, 184, 184, 184, 0}, // 40
    {185, 185, 185, 185, 185, 185, 0}, // 41
    {186, 186, 186, 186, 186, 186, 0}, // 42
    {187, 187, 187, 187, 187, 187, 0}, // 43
    {188, 188, 188, 188, 188, 188, 0}, // 44
    {189, 189, 189, 189, 189, 189, 0}, // 45
    {190, 190, 190, 190, 190, 190, 0}, // 46
    {191, 191, 191, 191, 191, 191, 0}, // 47
    {192, 192, 192, 192, 192, 192, 0}, // 48
    {193, 193, 193, 193, 193, 193, 0}, // 49
    {194, 194, 194, 194, 194, 194, 0}, // 50
    {195, 195, 195, 195, 195, 195, 0}, // 51
    {196, 196, 196, 196, 196, 196, 0}, // 52
    {197, 197, 197, 197, 197, 197, 0}, // 53
    {198, 198, 198, 198, 198, 198, 0}, // 54
    {199, 199, 199, 199, 199, 199, 0}, // 55
    {200, 200, 200, 200, 200, 200, 0}, // 56
    {201, 201, 201, 201, 201, 201, 0}, // 57
    {202, 202, 202, 202, 202, 202, 0}, // 58
    {203, 203, 203, 203, 203, 203, 0}, // 59
    {204, 204, 204, 204, 204, 204, 0}, // 60
    {205, 205, 205, 205, 205, 205, 0}, // 61
    {206, 206, 206, 206, 206, 206, 0}, // 62
    {207, 207, 207, 207, 207, 207, 0}, // 63
};

const int plants[256] = {
    // w => tile
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, // 0 - 16
    48, // 17 - tall grass
    49, // 18 - yellow flower
    50, // 19 - red flower
    51, // 20 - purple flower
    52, // 21 - sun flower
    53, // 22 - white flower
    54, // 23 - blue flower
};

int is_plant(int w) {
    switch (w) {
        case TALL_GRASS:
        case YELLOW_FLOWER:
        case RED_FLOWER:
        case PURPLE_FLOWER:
        case SUN_FLOWER:
        case WHITE_FLOWER:
        case BLUE_FLOWER:
            return 1;
        default:
            return 0;
    }
}

int is_obstacle(int w) {
    w = ABS(w);
    if (is_plant(w)) {
        return 0;
    }
    switch (w) {
        case EMPTY:
        case CLOUD:
            return 0;
        default:
            return 1;
    }
}

int is_transparent(int w) {
    if (w == EMPTY) {
        return 1;
    }
    w = ABS(w);
    if (is_plant(w)) {
        return 1;
    }
    switch (w) {
        case EMPTY:
        case GLASS:
        case LEAVES:
            return 1;
        default:
            return 0;
    }
}

int is_destructable(int w) {
    switch (w) {
        case EMPTY:
        case CLOUD:
            return 0;
        default:
            return 1;
    }
}
