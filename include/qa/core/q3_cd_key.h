/*
 * Port of src/core/q3-cd-key.ts: common CD-key state from id Software's
 * common.c, cl_main.c and cl_ui.c. C accommodations:
 *  - The TS imports CvarFlag/CvarRegistry from cvars/index (not yet
 *    ported) but uses only `markModifiedFlags(CvarFlag.Archive)`;
 *    that becomes a callback plus the Archive flag value 1.
 *  - The async Q3KeyStorage (Promise-based) becomes synchronous
 *    callbacks; load_text returns a borrowed string or NULL.
 *  - sourceCommandText (commands/text.ts, not yet ported) cuts at an
 *    embedded NUL and checks charCodes; inherent to C byte strings.
 *    A TS charCode > 255 makes validation fail there; here the same
 *    inputs fail by byte length or alphabet instead.
 * GPL-2.0-or-later.
 */
#ifndef QA_CORE_Q3_CD_KEY_H
#define QA_CORE_Q3_CD_KEY_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef enum qa_q3_key_build {
  QA_Q3_KEY_BUILD_CLIENT,
  QA_Q3_KEY_BUILD_DEDICATED,
} qa_q3_key_build;

const char *qa_q3_key_build_name(qa_q3_key_build build);
bool qa_q3_key_build_parse(const char *text, qa_q3_key_build *out);

typedef struct qa_q3_key_storage {
  void *ctx;
  const char *(*load_text)(void *ctx, const char *name);
  void (*dump)(void *ctx, const char *name, const char *contents);
} qa_q3_key_storage;

typedef struct qa_q3_key_ui_writes {
  void *ctx;
  void (*copy)(void *ctx, const uint8_t *bytes, size_t length);
  void (*set_byte)(void *ctx, size_t offset, uint8_t value);
} qa_q3_key_ui_writes;

/* `checksum` NULL mirrors the TS null. */
bool qa_validate_q3_cd_key(const char *key, const char *checksum);

typedef void (*qa_q3_mark_modified_fn)(void *ctx, int64_t flags);

typedef struct qa_q3_cd_key_state {
  uint8_t bytes[34];
  qa_q3_mark_modified_fn mark_modified_flags; /* may be NULL */
  void *cvars_ctx;
  qa_q3_key_build build;
} qa_q3_cd_key_state;

void qa_q3_cd_key_state_init(qa_q3_cd_key_state *state,
                             qa_q3_mark_modified_fn mark_modified_flags,
                             void *cvars_ctx, qa_q3_key_build build);
void qa_q3_cd_key_state_read_file(qa_q3_cd_key_state *state,
                                  const qa_q3_key_storage *storage);
void qa_q3_cd_key_state_append_file(qa_q3_cd_key_state *state,
                                    const qa_q3_key_storage *storage);
/* `offset` is 0 or 16, as in the TS parameter type. */
void qa_q3_cd_key_state_write_file(qa_q3_cd_key_state *state,
                                   const qa_q3_key_storage *storage,
                                   int64_t offset);
void qa_q3_cd_key_state_read_authorization(const qa_q3_cd_key_state *state,
                                           uint8_t *destination,
                                           size_t destination_length);
/* `writes` NULL writes `destination` directly; its length is checked
 * either way, as in the TS. */
void qa_q3_cd_key_state_read_ui(const qa_q3_cd_key_state *state,
                                int64_t unique_key_result,
                                const char *game_directory,
                                uint8_t *destination,
                                size_t destination_length,
                                const qa_q3_key_ui_writes *writes);
void qa_q3_cd_key_state_write_ui(qa_q3_cd_key_state *state,
                                 int64_t unique_key_result,
                                 const char *game_directory,
                                 const uint8_t *source, size_t source_length);

#endif
