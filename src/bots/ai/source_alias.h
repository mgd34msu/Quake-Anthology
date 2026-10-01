#ifndef QA_BOT_AI_SOURCE_ALIAS_H
#define QA_BOT_AI_SOURCE_ALIAS_H
#include "qa/bots_memory.h"
#include <string.h>

struct qa_bots;
struct bot_ai_state;
bool bot_ai_source_alias_bind(struct qa_bots *, struct bot_ai_state *, qa_error *);

/* The enclosing source record is qualified once by source_alias_bind. These
 * fixed offsets address that same allocation; they never overlay host structs. */
static inline uint32_t bot_source_word_read(const uint8_t *bytes)
{
    return (uint32_t)bytes[0] | ((uint32_t)bytes[1] << 8) |
        ((uint32_t)bytes[2] << 16) | ((uint32_t)bytes[3] << 24);
}
static inline void bot_source_word_write(uint8_t *bytes, uint32_t value)
{
    for (uint32_t i = 0; i < 4; ++i) bytes[i] = (uint8_t)(value >> (i * 8));
}
static inline int32_t bot_source_i32_read(const uint8_t *bytes)
{
    uint32_t bits = bot_source_word_read(bytes); int32_t value;
    memcpy(&value, &bits, sizeof(value)); return value;
}
static inline void bot_source_i32_write(uint8_t *bytes, int32_t value)
{
    uint32_t bits; memcpy(&bits, &value, sizeof(bits)); bot_source_word_write(bytes, bits);
}
static inline float bot_source_f32_read(const uint8_t *bytes)
{
    uint32_t bits = bot_source_word_read(bytes); float value;
    memcpy(&value, &bits, sizeof(value)); return value;
}
static inline void bot_source_f32_write(uint8_t *bytes, float value)
{
    uint32_t bits; memcpy(&bits, &value, sizeof(bits)); bot_source_word_write(bytes, bits);
}
static inline qa_vec3 bot_source_vec3_read(const uint8_t *bytes)
{
    return qa_v3(bot_source_f32_read(bytes), bot_source_f32_read(bytes + 4), bot_source_f32_read(bytes + 8));
}
static inline void bot_source_vec3_write(uint8_t *bytes, qa_vec3 value)
{
    bot_source_f32_write(bytes, value.x); bot_source_f32_write(bytes + 4, value.y);
    bot_source_f32_write(bytes + 8, value.z);
}
#endif
