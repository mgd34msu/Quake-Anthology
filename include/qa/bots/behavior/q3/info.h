/* Port of src/bots/behavior/q3/info.ts. GPL-2.0-or-later. */
#ifndef QA_BOTS_BEHAVIOR_Q3_INFO_H
#define QA_BOTS_BEHAVIOR_Q3_INFO_H
/* Returns malloc'd string (caller frees), NULL only on OOM. */
char *qa_bot_info_set_value_for_key(const char *input, const char *key, const char *value,
                                    void (*print)(void *ctx, const char *text), void *print_ctx);
#endif
