/* Port of src/content/q3/base/game/personal-portal.ts. GPL-2.0-or-later. Runtime body unresolved. */
#ifndef QA_CONTENT_Q3_BASE_GAME_PERSONAL_PORTAL_H
#define QA_CONTENT_Q3_BASE_GAME_PERSONAL_PORTAL_H
typedef struct qa_q3_personal_portal_host { void *ctx; } qa_q3_personal_portal_host;
typedef struct qa_q3_personal_portal_runtime { qa_q3_personal_portal_host host; } qa_q3_personal_portal_runtime;
void qa_q3_personal_portal_runtime_init(qa_q3_personal_portal_runtime *runtime, qa_q3_personal_portal_host host);
#endif
