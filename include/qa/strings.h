#ifndef QA_STRINGS_H
#define QA_STRINGS_H

#include "qa/common.h"

typedef uint32_t qa_string_id;
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
#define QA_NAME_Q3_RED_FLAG_BASE (1u << 13)
#define QA_NAME_Q3_BLUE_FLAG_BASE (1u << 14)
#define QA_NAME_Q3_RED_OBELISK (1u << 15)
#define QA_NAME_Q3_BLUE_OBELISK (1u << 16)
#define QA_NAME_Q3_NEUTRAL_OBELISK (1u << 17)
#define QA_NAME_Q3_TEAM_BASES (QA_NAME_Q3_RED_FLAG_BASE | QA_NAME_Q3_BLUE_FLAG_BASE | \
    QA_NAME_Q3_RED_OBELISK | QA_NAME_Q3_BLUE_OBELISK | QA_NAME_Q3_NEUTRAL_OBELISK)
typedef struct qa_strings qa_strings;

/* Exact byte identity, with no case folding or path normalization. Normalize
 * at the domain boundary before interning. IDs belong to this table lifetime;
 * persist the string, never the process-local ID. */
bool qa_strings_create(qa_strings **out, qa_error *error);
/* Retained snapshots share this table; destruction releases one reference. */
void qa_strings_retain(qa_strings *strings);
void qa_strings_destroy(qa_strings *strings);
bool qa_strings_intern(qa_strings *strings, qa_bytes text, qa_string_id *out, qa_error *error);
bool qa_strings_intern_cstr(qa_strings *strings, const char *text, qa_string_id *out, qa_error *error);
/* Normalize ASCII A-Z into the same exact-byte table, without a temporary
 * input copy. Find only matches the normalized spelling; other IDs stay exact. */
bool qa_strings_intern_ascii_lower(qa_strings *, qa_bytes, qa_string_id *, qa_error *);
qa_string_id qa_strings_find_ascii_lower(const qa_strings *, qa_bytes);
/* Bind a static name table once. The table pointer and names must remain valid
 * for the string table lifetime. The returned IDs are owned by strings. */
bool qa_strings_bind_names(qa_strings *, const char *const *, size_t count,
    const qa_string_id **out, qa_error *);
bool qa_strings_bind_bytes(qa_strings *, const qa_string_id **out, qa_error *);
qa_string_id qa_strings_find(const qa_strings *strings, qa_bytes text);
/* Data addresses remain stable until destruction. An extra trailing NUL is
 * stored for native APIs, but embedded NUL bytes retain their counted identity. */
qa_bytes qa_strings_text(const qa_strings *strings, qa_string_id id);
const char *qa_strings_cstr(const qa_strings *strings, qa_string_id id);
size_t qa_strings_count(const qa_strings *strings);
/* Classname prefixes are classified when the name is admitted, including
 * externally authored names. Runtime callers only read the stored bits. */
uint32_t qa_strings_name_flags(const qa_strings *, qa_string_id);

#endif
