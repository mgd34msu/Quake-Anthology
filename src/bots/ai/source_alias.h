#ifndef QA_BOT_AI_SOURCE_ALIAS_H
#define QA_BOT_AI_SOURCE_ALIAS_H
#include "qa/bots_memory.h"
#include "qa/binary.h"

struct qa_bots;
struct bot_ai_state;
bool bot_ai_source_alias_bind(struct qa_bots *, struct bot_ai_state *, qa_error *);

/* The enclosing source record is qualified once by source_alias_bind. These
 * fixed offsets address that same allocation; they never overlay host structs. */
static inline qa_vec3 bot_source_vec3_read(const uint8_t *bytes)
{
    return qa_v3(qa_load_f32le(bytes), qa_load_f32le(bytes + 4), qa_load_f32le(bytes + 8));
}
static inline void bot_source_vec3_write(uint8_t *bytes, qa_vec3 value)
{
    qa_store_f32le(bytes, value.x); qa_store_f32le(bytes + 4, value.y);
    qa_store_f32le(bytes + 8, value.z);
}
#endif
