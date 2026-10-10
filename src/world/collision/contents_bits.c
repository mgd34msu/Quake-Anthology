#include "qa/collision_bits.h"

#include <string.h>

/* Native definitions: WinQuake/bspfile.h:137-151, FTE common/bspfile.h:174-192,
 * Q2 game/q_shared.h:338-394, rerelease/game.h:228-291,
 * Q3 game/surfaceflags.h:30-80. The fixed slot groups below preserve every
 * native bit and the existing mixed-game boundary projections. */
#define SHARED_CONTENTS UINT32_C(0x0f038079)
#define Q3_SHARED_CONTENTS UINT32_C(0x0d038039)
#define Q1_SOLID_BITS UINT64_C(0x00020000c6000003)
#define Q1_CURRENT_BITS UINT64_C(0x3f00000000000000)
#define Q1_TERMINALS_LOW UINT64_C(0xffe0000000000000)
#define Q1_TERMINALS_HIGH UINT64_C(0x7)
#define BIT(slot) (UINT64_C(1) << (slot))

static int32_t signed_word(uint32_t word)
{
    int32_t value;
    memcpy(&value, &word, sizeof(value));
    return value;
}

static unsigned first_bit(uint64_t word)
{
#if defined(__GNUC__) || defined(__clang__)
    return (unsigned)__builtin_ctzll(word);
#else
    unsigned bit = 0;
    while ((word & 1) == 0) { word >>= 1; ++bit; }
    return bit;
#endif
}

static qa_collision_bits q3_contents(uint32_t word)
{
    return (qa_collision_bits){
        (word & Q3_SHARED_CONTENTS) |
        ((word & UINT32_C(0x20000000)) >> 1) |
        ((uint64_t)(word & UINT32_C(0x00000006)) << 31) |
        ((uint64_t)(word & UINT32_C(0x00007fc0)) << 28) |
        ((uint64_t)(word & UINT32_C(0x00fc0000)) << 25) |
        ((uint64_t)(word & UINT32_C(0x02000000)) << 24) |
        ((uint64_t)(word & UINT32_C(0x10000000)) << 22) |
        ((uint64_t)(word & UINT32_C(0xc0000000)) << 21), 0};
}

qa_collision_terminal qa_collision_q1_terminal(int32_t token)
{
    switch (token) {
        case -1: return (qa_collision_terminal){0};
        case -2: return (qa_collision_terminal){qa_collision_bit(QA_CONTENT_SOLID), 0};
        case -3: return (qa_collision_terminal){qa_collision_bit(QA_CONTENT_WATER), 0};
        case -4: return (qa_collision_terminal){qa_collision_bit(QA_CONTENT_SLIME), 0};
        case -5: return (qa_collision_terminal){qa_collision_bit(QA_CONTENT_LAVA), 0};
        default:
            if (token <= -6 && token >= -19) {
                qa_collision_bits bits = qa_collision_bit((unsigned)(47 - token));
                if (token <= -9 && token >= -14)
                    bits.lo |= BIT(QA_CONTENT_WATER) | BIT((unsigned)(QA_CONTENT_CURRENT_0 - token - 9));
                return (qa_collision_terminal){bits, 0};
            }
            return (qa_collision_terminal){qa_collision_bit(QA_CONTENT_Q1_OPAQUE), token};
    }
}

qa_collision_bits qa_collision_contents_decode(int32_t native, qa_game_family family)
{
    if (family == QA_GAME_Q1) return qa_collision_q1_terminal(native).bits;
    if (family == QA_GAME_Q2) return (qa_collision_bits){(uint32_t)native, 0};
    return q3_contents((uint32_t)native);
}

int32_t qa_collision_q1_medium_class(qa_collision_bits bits)
{
    if ((bits.lo & Q1_SOLID_BITS) != 0) return -2;
    if ((bits.lo & BIT(QA_CONTENT_LAVA)) != 0) return -5;
    if ((bits.lo & BIT(QA_CONTENT_SLIME)) != 0) return -4;
    if ((bits.lo & BIT(QA_CONTENT_WATER)) != 0) return -3;
    return -1;
}

int32_t qa_collision_contents_export(qa_collision_bits bits, qa_game_family family, int32_t opaque_q1_token)
{
    uint64_t lo = bits.lo;
    if (family == QA_GAME_Q1) {
        int32_t medium = qa_collision_q1_medium_class(bits);
        if (medium == -3 && (lo & Q1_CURRENT_BITS) != 0)
            return 47 - (int32_t)first_bit(lo & Q1_CURRENT_BITS);
        if (medium != -1) return medium;
        if ((lo & Q1_TERMINALS_LOW) != 0) return 47 - (int32_t)first_bit(lo & Q1_TERMINALS_LOW);
        if ((bits.hi & Q1_TERMINALS_HIGH) != 0) return -17 - (int32_t)first_bit(bits.hi & Q1_TERMINALS_HIGH);
        if ((bits.hi & (BIT(QA_CONTENT_Q1_OPAQUE - 64))) != 0) return opaque_q1_token;
        return -1;
    }
    if (family == QA_GAME_Q2) {
        uint32_t word = (uint32_t)lo;
        if ((lo & BIT(QA_CONTENT_FOG)) != 0) word |= 64;
        if ((lo & BIT(QA_CONTENT_BODY)) != 0) word |= UINT32_C(0x42000000);
        if ((lo & BIT(QA_CONTENT_SKY)) != 0) word |= 1;
        if ((lo & Q1_CURRENT_BITS) != 0)
            word |= 32 | (uint32_t)((lo >> 38) & UINT32_C(0x00fc0000));
        return signed_word(word);
    }
    uint32_t word = (uint32_t)lo & SHARED_CONTENTS;
    word |= (uint32_t)((lo >> 31) & UINT32_C(0x00000006)) |
        (uint32_t)((lo >> 28) & UINT32_C(0x00007fc0)) |
        (uint32_t)((lo >> 25) & UINT32_C(0x00fc0000)) |
        (uint32_t)((lo >> 24) & UINT32_C(0x02000000)) |
        (uint32_t)((lo >> 22) & UINT32_C(0x10000000)) |
        (uint32_t)((lo >> 21) & UINT32_C(0xc0000000));
    if ((lo & BIT(QA_CONTENT_WINDOW)) != 0 || (lo & BIT(QA_CONTENT_SKY)) != 0) word |= 1;
    if ((lo & (BIT(QA_CONTENT_PLAYER) | BIT(QA_CONTENT_PROJECTILE))) != 0) word |= UINT32_C(0x02000000);
    if ((lo & BIT(QA_CONTENT_TRANSLUCENT)) != 0) word |= UINT32_C(0x20000000);
    if ((lo & Q1_CURRENT_BITS) != 0) word |= 32;
    return signed_word(word);
}

int32_t qa_collision_point_contents_export(qa_collision_bits bits, qa_game_family family, int32_t opaque_q1_token)
{
    int32_t value = qa_collision_contents_export(bits, family, opaque_q1_token);
    return family == QA_GAME_Q1 && value <= -9 && value >= -14 ? -3 : value;
}

qa_collision_bits qa_collision_contents_mask(uint32_t native_mask, qa_game_family family)
{
    if (family == QA_GAME_Q1) return (qa_collision_bits){Q1_SOLID_BITS, 0};
    if (family == QA_GAME_Q2) {
        uint64_t lo = native_mask;
        if ((native_mask & 64) != 0) lo |= BIT(QA_CONTENT_FOG);
        if ((native_mask & UINT32_C(0x42000000)) != 0) lo |= BIT(QA_CONTENT_BODY);
        if ((native_mask & 1) != 0) lo |= BIT(QA_CONTENT_SKY);
        lo |= (uint64_t)(native_mask & UINT32_C(0x00fc0000)) << 38;
        if ((native_mask & 32) != 0) lo |= Q1_CURRENT_BITS;
        return (qa_collision_bits){lo, 0};
    }
    qa_collision_bits mask = q3_contents(native_mask);
    mask.lo |= native_mask & SHARED_CONTENTS;
    if ((native_mask & 1) != 0) mask.lo |= BIT(QA_CONTENT_WINDOW) | BIT(QA_CONTENT_SKY);
    if ((native_mask & UINT32_C(0x02000000)) != 0) mask.lo |= BIT(QA_CONTENT_PLAYER) | BIT(QA_CONTENT_PROJECTILE);
    if ((native_mask & 32) != 0) mask.lo |= Q1_CURRENT_BITS;
    return mask;
}

qa_collision_bits qa_collision_surface_decode(int32_t native, qa_game_family family)
{
    uint32_t word = (uint32_t)native;
    if (family == QA_GAME_Q1) return (qa_collision_bits){0, word};
    if (family == QA_GAME_Q2) return (qa_collision_bits){word, 0};
    return (qa_collision_bits){(word & UINT32_C(0x82)) |
        ((uint64_t)(word & 1) << 32) |
        ((uint64_t)(word & UINT32_C(0x7c)) << 31) |
        ((uint64_t)(word & UINT32_C(0xffffff00)) << 30), 0};
}

int32_t qa_collision_surface_export(qa_collision_bits bits, qa_game_family family)
{
    if (family == QA_GAME_Q1) return signed_word((uint32_t)bits.hi);
    uint64_t lo = bits.lo;
    if (family == QA_GAME_Q2)
        return signed_word((uint32_t)lo | ((lo & BIT(QA_SURFACE_SKY)) != 0 ? 4 : 0));
    uint32_t word = (uint32_t)lo & UINT32_C(0x86);
    word |= (uint32_t)((lo >> 32) & 1) |
        (uint32_t)((lo >> 31) & UINT32_C(0x7c)) |
        (uint32_t)((lo >> 30) & UINT32_C(0xffffff00));
    if ((lo & BIT(QA_SURFACE_SKY_NOIMPACT)) != 0) word |= 16;
    return signed_word(word);
}
