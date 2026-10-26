/*
 * Port of src/foundation/strings.ts: interned string identities. Text is
 * admitted once and every runtime comparison afterwards is a uint32
 * identity check. Exact byte identity, no case folding. IDs belong to
 * this table lifetime; persist the string, never the ID.
 * GPL-2.0-or-later.
 */
#ifndef QA_FOUNDATION_STRINGS_H
#define QA_FOUNDATION_STRINGS_H

#include <stddef.h>
#include <stdint.h>

/** The invalid/absent string identity. */
#define QA_STRING_NONE 0u

#define QA_NAME_MONSTER (1u << 0)
#define QA_NAME_PLAYER_START (1u << 1)
#define QA_NAME_ITEM_FLAG (1u << 2)
#define QA_NAME_BRUSH_PATH (1u << 3)
#define QA_NAME_SEXED_SOUND (1u << 4)
#define QA_NAME_INFO_FLAG (1u << 5)
#define QA_NAME_FUNC (1u << 6)
#define QA_NAME_TEAM_RED_EXACT (1u << 7)
#define QA_NAME_TEAM_BLUE_EXACT (1u << 8)
#define QA_NAME_TEAM_RED_ALIAS (1u << 9)
#define QA_NAME_TEAM_BLUE_ALIAS (1u << 10)
#define QA_NAME_TEAM_COLOR_NAMESPACE (1u << 11)
#define QA_NAME_Q1_AMMO (1u << 12)

/* Classifies an admitted name once, mirroring the stored bits. */
uint32_t qa_quake_name_flags(const char *text);

typedef uint32_t (*qa_string_classify_fn)(const char *text, void *ctx);

typedef struct qa_string_table qa_string_table;

/* classify may be NULL for qa_quake_name_flags. Returns NULL on OOM. */
qa_string_table *qa_string_table_create(qa_string_classify_fn classify, void *classify_ctx);
void qa_string_table_destroy(qa_string_table *table);

size_t qa_string_table_count(const qa_string_table *table);
/* Admits text (copied) and returns its stable id. Aborts when exhausted. */
uint32_t qa_string_table_intern(qa_string_table *table, const char *text);
/* Returns QA_STRING_NONE without admitting the text. */
uint32_t qa_string_table_find(const qa_string_table *table, const char *text);
/* Interns each name into out_ids (caller storage, count entries). */
void qa_string_table_bind_names(qa_string_table *table, const char *const *names,
                                size_t count, uint32_t *out_ids);
const char *qa_string_table_text(const qa_string_table *table, uint32_t id);
uint32_t qa_string_table_name_flags(const qa_string_table *table, uint32_t id);

#endif
