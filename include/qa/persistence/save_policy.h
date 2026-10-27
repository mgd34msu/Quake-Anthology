/*
 * Port of src/persistence/save-policy.ts: save eligibility, save
 * command paths, contained save names, and the atomic contained
 * save write. C accommodations:
 *  - TS async writeContainedSave is synchronous; it returns false on
 *    I/O failure (the TS promise rejects). RangeError validation
 *    still aborts via qa_range_error with the exact TS message.
 *  - node:path resolve/relative/extname are POSIX on the target
 *    platform and are reproduced for byte strings. TS strings are
 *    UTF-16; lengths here count bytes (observable only for empty
 *    names, where both agree).
 *  - platform/files/contained.ts (openContainedParent) has no C
 *    header yet; per docs/PORTING.md platform facilities are linked
 *    directly, so the directory-descriptor walk uses openat(2)/
 *    mkdirat(2)/renameat(2) with the same O_NOFOLLOW anchoring and
 *    the same containedFileParts validation, inside save_policy.c.
 *  - crypto.randomUUID() in the temporary name becomes a
 *    pid/counter suffix; the name is never observable after commit.
 * Returned path/name strings are malloc'd; the caller frees them.
 * NULL is returned only on allocation failure. GPL-2.0-or-later.
 */
#ifndef QA_PERSISTENCE_SAVE_POLICY_H
#define QA_PERSISTENCE_SAVE_POLICY_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "qa/contracts/content.h" /* qa_game_family */

/* TS SavePurpose = "manual" | "autosave" | "transition". */
typedef enum qa_save_purpose {
  QA_SAVE_PURPOSE_MANUAL,
  QA_SAVE_PURPOSE_AUTOSAVE,
  QA_SAVE_PURPOSE_TRANSITION
} qa_save_purpose;

/* TS SaveEligibility.authority = "offline" | "server" | "remote". */
typedef enum qa_save_authority {
  QA_SAVE_AUTHORITY_OFFLINE,
  QA_SAVE_AUTHORITY_SERVER,
  QA_SAVE_AUTHORITY_REMOTE
} qa_save_authority;

/* TS SaveEligibility.mode = "singleplayer" | "coop" | "deathmatch". */
typedef enum qa_save_mode {
  QA_SAVE_MODE_SINGLEPLAYER,
  QA_SAVE_MODE_COOP,
  QA_SAVE_MODE_DEATHMATCH
} qa_save_mode;

typedef struct qa_save_eligibility {
  qa_game_family family;
  qa_save_authority authority;
  qa_save_mode mode;
  bool match_save; /* TS optional matchSave; false when absent */
  bool active;
  bool intermission;
  const double *player_health; /* borrowed */
  size_t player_health_count;
} qa_save_eligibility;

/* NULL when saving is allowed, else the exact TS reason string. */
const char *qa_save_unavailable(const qa_save_eligibility *state,
                                qa_save_purpose purpose);

/* TS saveCommandPath: malloc'd resolved path; empty name RangeErrors. */
char *qa_save_command_path(const char *directory, const char *name);

/* TS containedSaveName: malloc'd relative name; RangeErrors when the
 * path escapes the directory, uses a reserved "current" part, or
 * does not end in .sav. */
char *qa_contained_save_name(const char *directory, const char *path);

/* TS writeContainedSave (synchronous). False only on I/O failure. */
bool qa_write_contained_save(const char *directory, const char *path,
                             const uint8_t *bytes, size_t length);

#endif
