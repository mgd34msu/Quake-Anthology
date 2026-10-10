#ifndef QA_COLLISION_BITS_H
#define QA_COLLISION_BITS_H

#include "qa/common.h"

#include "qa/ruleset.h"

/* Native contents and surface words enter this domain once at a boundary.
 * Reserved native bits have stable extension slots, with no guessed meaning. */
typedef struct qa_collision_bits { uint64_t lo, hi; } qa_collision_bits;

typedef enum qa_contents_bit {
    QA_CONTENT_SOLID = 0,
    QA_CONTENT_WINDOW = 1,
    QA_CONTENT_AUX = 2,
    QA_CONTENT_LAVA = 3,
    QA_CONTENT_SLIME = 4,
    QA_CONTENT_WATER = 5,
    QA_CONTENT_MIST = 6,
    QA_CONTENT_Q2_EXTENSION_7 = 7,
    QA_CONTENT_Q2_EXTENSION_8 = 8,
    QA_CONTENT_Q2_EXTENSION_9 = 9,
    QA_CONTENT_Q2_EXTENSION_10 = 10,
    QA_CONTENT_Q2_EXTENSION_11 = 11,
    QA_CONTENT_Q2_EXTENSION_12 = 12,
    QA_CONTENT_NO_WATERJUMP = 13,
    QA_CONTENT_PROJECTILECLIP = 14,
    QA_CONTENT_AREAPORTAL = 15,
    QA_CONTENT_PLAYERCLIP = 16,
    QA_CONTENT_MONSTERCLIP = 17,
    QA_CONTENT_CURRENT_0 = 18,
    QA_CONTENT_CURRENT_90 = 19,
    QA_CONTENT_CURRENT_180 = 20,
    QA_CONTENT_CURRENT_270 = 21,
    QA_CONTENT_CURRENT_UP = 22,
    QA_CONTENT_CURRENT_DOWN = 23,
    QA_CONTENT_ORIGIN = 24,
    QA_CONTENT_MONSTER = 25,
    QA_CONTENT_CORPSE = 26,
    QA_CONTENT_DETAIL = 27,
    QA_CONTENT_TRANSLUCENT = 28,
    QA_CONTENT_LADDER = 29,
    QA_CONTENT_PLAYER = 30,
    QA_CONTENT_PROJECTILE = 31,
    QA_CONTENT_Q3_EXTENSION_1 = 32,
    QA_CONTENT_Q3_EXTENSION_2 = 33,
    QA_CONTENT_FOG = 34,
    QA_CONTENT_NOTTEAM1 = 35,
    QA_CONTENT_NOTTEAM2 = 36,
    QA_CONTENT_NOBOTCLIP = 37,
    QA_CONTENT_Q3_EXTENSION_10 = 38,
    QA_CONTENT_Q3_EXTENSION_11 = 39,
    QA_CONTENT_Q3_EXTENSION_12 = 40,
    QA_CONTENT_Q3_EXTENSION_13 = 41,
    QA_CONTENT_Q3_EXTENSION_14 = 42,
    QA_CONTENT_TELEPORTER = 43,
    QA_CONTENT_JUMPPAD = 44,
    QA_CONTENT_CLUSTERPORTAL = 45,
    QA_CONTENT_DONOTENTER = 46,
    QA_CONTENT_BOTCLIP = 47,
    QA_CONTENT_MOVER = 48,
    QA_CONTENT_BODY = 49,
    QA_CONTENT_STRUCTURAL = 50,
    QA_CONTENT_TRIGGER = 51,
    QA_CONTENT_NODROP = 52,
    QA_CONTENT_SKY = 53,
    QA_CONTENT_Q1_ORIGIN = 54,
    QA_CONTENT_Q1_CLIP = 55,
    QA_CONTENT_WATER_CURRENT_0 = 56,
    QA_CONTENT_WATER_CURRENT_90 = 57,
    QA_CONTENT_WATER_CURRENT_180 = 58,
    QA_CONTENT_WATER_CURRENT_270 = 59,
    QA_CONTENT_WATER_CURRENT_UP = 60,
    QA_CONTENT_WATER_CURRENT_DOWN = 61,
    QA_CONTENT_Q1_TRANS = 62,
    QA_CONTENT_Q1_LADDER = 63,
    QA_CONTENT_Q1_MONSTERCLIP = 64,
    QA_CONTENT_Q1_PLAYERCLIP = 65,
    QA_CONTENT_Q1_CORPSE = 66,
    QA_CONTENT_Q1_OPAQUE = 67,
    QA_CONTENT_COUNT = 68
} qa_contents_bit;

typedef enum qa_surface_bit {
    QA_SURFACE_LIGHT = 0,
    QA_SURFACE_SLICK = 1,
    QA_SURFACE_SKY_NOIMPACT = 2,
    QA_SURFACE_WARP = 3,
    QA_SURFACE_TRANS33 = 4,
    QA_SURFACE_TRANS66 = 5,
    QA_SURFACE_FLOWING = 6,
    QA_SURFACE_NODRAW = 7,
    QA_SURFACE_Q2_EXTENSION_8 = 8,
    QA_SURFACE_Q2_EXTENSION_9 = 9,
    QA_SURFACE_Q2_EXTENSION_10 = 10,
    QA_SURFACE_Q2_EXTENSION_11 = 11,
    QA_SURFACE_Q2_EXTENSION_12 = 12,
    QA_SURFACE_Q2_EXTENSION_13 = 13,
    QA_SURFACE_Q2_EXTENSION_14 = 14,
    QA_SURFACE_Q2_EXTENSION_15 = 15,
    QA_SURFACE_Q2_EXTENSION_16 = 16,
    QA_SURFACE_Q2_EXTENSION_17 = 17,
    QA_SURFACE_Q2_EXTENSION_18 = 18,
    QA_SURFACE_Q2_EXTENSION_19 = 19,
    QA_SURFACE_Q2_EXTENSION_20 = 20,
    QA_SURFACE_Q2_EXTENSION_21 = 21,
    QA_SURFACE_Q2_EXTENSION_22 = 22,
    QA_SURFACE_Q2_EXTENSION_23 = 23,
    QA_SURFACE_Q2_EXTENSION_24 = 24,
    QA_SURFACE_ALPHATEST = 25,
    QA_SURFACE_Q2_EXTENSION_26 = 26,
    QA_SURFACE_Q2_EXTENSION_27 = 27,
    QA_SURFACE_N64_UV = 28,
    QA_SURFACE_SCROLL_X = 29,
    QA_SURFACE_SCROLL_Y = 30,
    QA_SURFACE_SCROLL_FLIP = 31,
    QA_SURFACE_NODAMAGE = 32,
    QA_SURFACE_SKY = 33,
    QA_SURFACE_LADDER = 34,
    QA_SURFACE_NOIMPACT = 35,
    QA_SURFACE_NOMARKS = 36,
    QA_SURFACE_FLESH = 37,
    QA_SURFACE_HINT = 38,
    QA_SURFACE_SKIP = 39,
    QA_SURFACE_NOLIGHTMAP = 40,
    QA_SURFACE_POINTLIGHT = 41,
    QA_SURFACE_METALSTEPS = 42,
    QA_SURFACE_NOSTEPS = 43,
    QA_SURFACE_NONSOLID = 44,
    QA_SURFACE_LIGHTFILTER = 45,
    QA_SURFACE_ALPHASHADOW = 46,
    QA_SURFACE_NODLIGHT = 47,
    QA_SURFACE_DUST = 48,
    QA_SURFACE_Q3_EXTENSION_19 = 49,
    QA_SURFACE_Q3_EXTENSION_20 = 50,
    QA_SURFACE_Q3_EXTENSION_21 = 51,
    QA_SURFACE_Q3_EXTENSION_22 = 52,
    QA_SURFACE_Q3_EXTENSION_23 = 53,
    QA_SURFACE_Q3_EXTENSION_24 = 54,
    QA_SURFACE_Q3_EXTENSION_25 = 55,
    QA_SURFACE_Q3_EXTENSION_26 = 56,
    QA_SURFACE_Q3_EXTENSION_27 = 57,
    QA_SURFACE_Q3_EXTENSION_28 = 58,
    QA_SURFACE_Q3_EXTENSION_29 = 59,
    QA_SURFACE_Q3_EXTENSION_30 = 60,
    QA_SURFACE_Q3_EXTENSION_31 = 61,
    QA_SURFACE_RESERVED_62 = 62,
    QA_SURFACE_RESERVED_63 = 63,
    QA_SURFACE_Q1_EXTENSION_0 = 64,
    QA_SURFACE_Q1_EXTENSION_1 = 65,
    QA_SURFACE_Q1_EXTENSION_2 = 66,
    QA_SURFACE_Q1_EXTENSION_3 = 67,
    QA_SURFACE_Q1_EXTENSION_4 = 68,
    QA_SURFACE_Q1_EXTENSION_5 = 69,
    QA_SURFACE_Q1_EXTENSION_6 = 70,
    QA_SURFACE_Q1_EXTENSION_7 = 71,
    QA_SURFACE_Q1_EXTENSION_8 = 72,
    QA_SURFACE_Q1_EXTENSION_9 = 73,
    QA_SURFACE_Q1_EXTENSION_10 = 74,
    QA_SURFACE_Q1_EXTENSION_11 = 75,
    QA_SURFACE_Q1_EXTENSION_12 = 76,
    QA_SURFACE_Q1_EXTENSION_13 = 77,
    QA_SURFACE_Q1_EXTENSION_14 = 78,
    QA_SURFACE_Q1_EXTENSION_15 = 79,
    QA_SURFACE_Q1_EXTENSION_16 = 80,
    QA_SURFACE_Q1_EXTENSION_17 = 81,
    QA_SURFACE_Q1_EXTENSION_18 = 82,
    QA_SURFACE_Q1_EXTENSION_19 = 83,
    QA_SURFACE_Q1_EXTENSION_20 = 84,
    QA_SURFACE_Q1_EXTENSION_21 = 85,
    QA_SURFACE_Q1_EXTENSION_22 = 86,
    QA_SURFACE_Q1_EXTENSION_23 = 87,
    QA_SURFACE_Q1_EXTENSION_24 = 88,
    QA_SURFACE_Q1_EXTENSION_25 = 89,
    QA_SURFACE_Q1_EXTENSION_26 = 90,
    QA_SURFACE_Q1_EXTENSION_27 = 91,
    QA_SURFACE_Q1_EXTENSION_28 = 92,
    QA_SURFACE_Q1_EXTENSION_29 = 93,
    QA_SURFACE_Q1_EXTENSION_30 = 94,
    QA_SURFACE_Q1_EXTENSION_31 = 95,
    QA_SURFACE_COUNT = 96
} qa_surface_bit;

static inline qa_collision_bits qa_collision_bit(unsigned bit)
{
    return bit < 64 ? (qa_collision_bits){UINT64_C(1) << bit, 0} :
        (qa_collision_bits){0, UINT64_C(1) << (bit - 64)};
}

static inline qa_collision_bits qa_collision_bits_union(qa_collision_bits a, qa_collision_bits b)
{ return (qa_collision_bits){a.lo | b.lo, a.hi | b.hi}; }

static inline qa_collision_bits qa_collision_bits_intersection(qa_collision_bits a, qa_collision_bits b)
{ return (qa_collision_bits){a.lo & b.lo, a.hi & b.hi}; }

static inline qa_collision_bits qa_collision_bits_difference(qa_collision_bits a, qa_collision_bits b)
{ return (qa_collision_bits){a.lo & ~b.lo, a.hi & ~b.hi}; }

static inline bool qa_collision_bits_any(qa_collision_bits a)
{ return (a.lo | a.hi) != 0; }

static inline bool qa_collision_bits_overlap(qa_collision_bits a, qa_collision_bits b)
{ return ((a.lo & b.lo) | (a.hi & b.hi)) != 0; }

static inline bool qa_collision_bits_equal(qa_collision_bits a, qa_collision_bits b)
{ return a.lo == b.lo && a.hi == b.hi; }

/* A named Q1 terminal needs only bits. An unknown terminal keeps its exact
 * signed token here; its value is never used as an allocation or table index. */
typedef struct qa_collision_terminal {
    qa_collision_bits bits;
    int32_t opaque_token;
} qa_collision_terminal;

qa_collision_terminal qa_collision_q1_terminal(int32_t token);
/* Q1 opaque contents use qa_collision_q1_terminal to retain their provenance. */
qa_collision_bits qa_collision_contents_decode(int32_t native, qa_game_family);
int32_t qa_collision_contents_export(qa_collision_bits, qa_game_family, int32_t opaque_q1_token);
/* Ordinary Q1 point contents folds current terminals to WATER as SV_PointContents. */
int32_t qa_collision_point_contents_export(qa_collision_bits, qa_game_family, int32_t opaque_q1_token);
/* Original Q1 solid/liquid precedence used by mixed Q2/Q3 hull walkers. */
int32_t qa_collision_q1_medium_class(qa_collision_bits);
/* A source mask accepts every canonical bit whose source projection matches.
 * Q1 accepts SOLID terminal projections and ignores the native mask argument. */
qa_collision_bits qa_collision_contents_mask(uint32_t native_mask, qa_game_family);
qa_collision_bits qa_collision_surface_decode(int32_t native, qa_game_family);
int32_t qa_collision_surface_export(qa_collision_bits, qa_game_family);

#endif
