#ifndef QA_BOT_BSP_H
#define QA_BOT_BSP_H
#include "qa/bsp.h"
#include "qa/script.h"

/* Botlib handles are one-based. Immutable map tables and token bytes may be
 * shared with other consumers; these queries never copy an entity table. */
int32_t qa_bot_bsp_next(const qa_entities *, int32_t after);
bool qa_bot_bsp_value(const qa_entities *, int32_t entity, const char *key, qa_bytes *out);
/* Called only for actual epairs, newest first. Candidate excludes its NUL;
 * source-memory adapters compare candidate.size bytes and the terminator lazily. */
typedef bool (*qa_bot_bsp_compare)(void *, qa_bytes candidate, bool *equal, qa_error *);
bool qa_bot_bsp_lookup(const qa_entities *, int32_t, qa_bot_bsp_compare, void *,
                       qa_entity_property *, bool *found, qa_error *);
bool qa_bot_bsp_parse_integer(qa_bytes, int32_t *, qa_error *);
bool qa_bot_bsp_parse_float(qa_bytes, float *, qa_error *);
bool qa_bot_bsp_parse_vector(qa_bytes, qa_vec3 *, qa_error *);
bool qa_bot_bsp_integer(const qa_entities *, int32_t, const char *, int32_t *, bool *, qa_error *);
bool qa_bot_bsp_float(const qa_entities *, int32_t, const char *, float *, bool *, qa_error *);
bool qa_bot_bsp_vector(const qa_entities *, int32_t, const char *, qa_vec3 *, bool *, qa_error *);
typedef struct qa_bot_bsp qa_bot_bsp;
/* Source botlib grammar uses the shared raw lexer. Owned compact tables retain
 * borrowed string spans into immutable input; keep the input alive until close.
 * Language errors are reported and produce source-defined empty/partial data. */
bool qa_bot_bsp_load(qa_bytes, const qa_script_lexer_options *, qa_bot_bsp **, qa_error *);
const qa_entities *qa_bot_bsp_entities(const qa_bot_bsp *);
void qa_bot_bsp_close(qa_bot_bsp *);
#endif
