#ifndef QA_BOT_SOURCE_MATCH_H
#define QA_BOT_SOURCE_MATCH_H

#include "qa/console.h"
#include "qa/math.h"

enum {
    BOT_SOURCE_TEST_SOLID, BOT_SOURCE_TEST_CLUSTERS,
    BOT_SOURCE_INTERBREED_CHARACTER, BOT_SOURCE_INTERBREED_BOTS,
    BOT_SOURCE_INTERBREED_CYCLE, BOT_SOURCE_INTERBREED_WRITE,
    BOT_SOURCE_MATCH_CVARS
};
typedef struct bot_source_match_cvar {
    char value[256];
    float numeric_value;
    int32_t integer_value;
    uint64_t modification_count;
    bool registered;
} bot_source_match_cvar;
typedef struct bot_source_match_globals {
    bot_source_match_cvar cvars[BOT_SOURCE_MATCH_CVARS];
    int32_t interbreed_match_count;
    bool interbreed;
} bot_source_match_globals;

struct qa_bots;
struct bot_ai_state;
void bot_ai_source_match_init(bot_source_match_globals *);
bool bot_ai_source_match_register(struct qa_bots *, const char *, qa_error *);
bool bot_ai_source_match_setup(struct qa_bots *, qa_error *);
bool bot_ai_source_interbreeding(struct qa_bots *, qa_error *);
bool bot_ai_source_interbreed_admit(struct qa_bots *, struct bot_ai_state *, qa_error *);
bool bot_ai_source_interbreed_end_match(struct qa_bots *, qa_error *);
bool bot_ai_source_test_aas(struct qa_bots *, qa_vec3, qa_error *);
bool qa_bots_interbreed_end_admitted(const struct qa_bots *);
bool qa_bots_interbreed_end_match(struct qa_bots *, qa_error *);
bool qa_bots_test_aas(struct qa_bots *, qa_vec3, qa_error *);

#endif
