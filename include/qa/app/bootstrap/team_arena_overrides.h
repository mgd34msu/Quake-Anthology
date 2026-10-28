/* Port of src/app/bootstrap/team-arena-overrides.ts over landed
 * cvar/save types. GPL-2.0-or-later. */
#ifndef QA_APP_BOOTSTRAP_TEAM_ARENA_OVERRIDES_H
#define QA_APP_BOOTSTRAP_TEAM_ARENA_OVERRIDES_H
#include <stdbool.h>
#include <stddef.h>
#include "qa/contracts/session.h"
#include "qa/core/cvars/index.h"
#include "qa/persistence/value.h"
typedef struct qa_app_override_seat { double seat, client; } qa_app_override_seat;
typedef struct qa_app_override_registry { double seat, client; qa_cvar_registry *cvars; } qa_app_override_registry;
typedef struct qa_app_team_arena_override_row2 { double seat, client; char *baseline, *effective; } qa_app_team_arena_override_seat_row;
typedef struct qa_app_team_arena_overrides {
  const char *phase; /* "active"|"released" */
  qa_app_team_arena_override_seat_row *seats; size_t seat_count;
} qa_app_team_arena_overrides;
void qa_app_team_arena_overrides_free(qa_app_team_arena_overrides *state);
qa_app_team_arena_overrides qa_app_capture_team_arena_overrides(const qa_app_override_registry *seats, size_t count);
/* False = TS null (no checkpoint). */
bool qa_app_read_team_arena_overrides(const qa_save_image *image, const qa_app_override_seat *seats, size_t count, qa_app_team_arena_overrides *out);
qa_app_team_arena_overrides qa_app_decode_team_arena_overrides(const qa_save_value *value, const qa_app_override_seat *seats, size_t count);
/* Returned image shallow-copies input; its providers array + new entry
 * bytes are malloc'd (caller frees providers array and last bytes). */
qa_save_image qa_app_save_team_arena_overrides(qa_save_image image, const qa_app_team_arena_overrides *state,
                                               const qa_app_override_registry *seats, size_t count);
void qa_app_apply_team_arena_overrides(const qa_app_team_arena_overrides *state, const qa_app_override_registry *seats, size_t count);
/* Returns released copy (or the input state borrowed when null/released:
 * *out_is_new distinguishes). */
qa_app_team_arena_overrides qa_app_release_team_arena_overrides(const qa_app_team_arena_overrides *state,
                                                                const qa_app_override_registry *seats, size_t count, bool *out_is_new);
/* seat < 0 = TS undefined. Entries are landed archive entries (owned). */
qa_cvar_archive_entry *qa_app_team_arena_archive_entries(qa_cvar_registry *registry, const qa_app_team_arena_overrides *state, bool has_seat, double seat, size_t *out_count);
void qa_app_cvar_archive_entries_free(qa_cvar_archive_entry *entries, size_t count);
#endif
