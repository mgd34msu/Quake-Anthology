/* Port of src/compat/q2/classic/world-profile.ts. GPL-2.0-or-later. */
#ifndef QA_COMPAT_Q2_CLASSIC_WORLD_PROFILE_H
#define QA_COMPAT_Q2_CLASSIC_WORLD_PROFILE_H
#include <stdbool.h>
#include <stddef.h>
#include "qa/compat/q2/classic/combat_profile.h"
#include "qa/contracts/gameplay.h"
#include "qa/persistence/value.h"

typedef struct qa_compat_q2_classic_unnamed_item { double index; const char *label; qa_item_id item; } qa_compat_q2_classic_unnamed_item;
typedef struct qa_compat_q2_classic_world_profile {
  qa_compat_q2_classic_combat_profile base;
  struct {
    double count, class_name, label, flags, ammo_flag, tag;
    const double *capacities; size_t capacity_count;
    const qa_compat_q2_classic_unnamed_item *unnamed; size_t unnamed_count;
    double empty_index; bool sentinel;
  } inventory_table;
} qa_compat_q2_classic_world_profile;

/* TS classicPrimaryWorldProfile(digest): fills out (borrowed tables),
 * false is the TS null. */
bool qa_compat_q2_classic_get_primary_world_profile(qa_content_digest digest, qa_compat_q2_classic_world_profile *out);
/* Malloc'd lists inside out; release with _free. False only on OOM. */
bool qa_compat_q2_read_classic_primary_world_profile(const qa_save_reader *reader, qa_content_digest digest,
                                                     qa_compat_q2_classic_world_profile *out);
void qa_compat_q2_classic_world_profile_free(qa_compat_q2_classic_world_profile *profile);
#endif
