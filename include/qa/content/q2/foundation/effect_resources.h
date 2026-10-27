/* Port of src/content/q2/foundation/effect-resources.ts. GPL-2.0-or-later. */
#ifndef QA_CONTENT_Q2_FOUNDATION_EFFECT_RESOURCES_H
#define QA_CONTENT_Q2_FOUNDATION_EFFECT_RESOURCES_H
#include <stddef.h>
typedef struct qa_q2_transient_models { const char *cable, *parasite, *explosion, *flash, *rocket_explosion, *smoke, *lightning, *bfg_explosion; } qa_q2_transient_models;
extern const qa_q2_transient_models QA_Q2_TRANSIENT_MODELS;
extern const char *const QA_Q2_TRANSIENT_SOUNDS[];
extern const size_t QA_Q2_TRANSIENT_SOUNDS_COUNT;
extern const char *const QA_Q2_ROGUE_TRANSIENT_SOUNDS[];
extern const size_t QA_Q2_ROGUE_TRANSIENT_SOUNDS_COUNT;
#endif
