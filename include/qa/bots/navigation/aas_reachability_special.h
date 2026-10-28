/* Port of src/bots/navigation/aas-reachability-special.ts. GPL-2.0-or-later. */
#ifndef QA_BOTS_NAVIGATION_AAS_REACHABILITY_SPECIAL_H
#define QA_BOTS_NAVIGATION_AAS_REACHABILITY_SPECIAL_H
#include <stdbool.h>
#include <stdint.h>
#include "qa/bots/navigation/aas_reachability.h"
typedef struct qa_aas_reachability_special { qa_aas_reachability_context *context; } qa_aas_reachability_special;
void qa_aas_reachability_special_init(qa_aas_reachability_special *special, qa_aas_reachability_context *context);
int64_t qa_aas_reachability_special_travel_flags_for_team(qa_aas_reachability_special *special, int64_t entity);
void qa_aas_reachability_special_teleport(qa_aas_reachability_special *special);
void qa_aas_reachability_special_elevator(qa_aas_reachability_special *special);
void qa_aas_reachability_special_func_bobbing(qa_aas_reachability_special *special);
void qa_aas_reachability_special_jump_pad(qa_aas_reachability_special *special);
bool qa_aas_reachability_special_grapple(qa_aas_reachability_special *special, int64_t from, int64_t to);
void qa_aas_reachability_special_set_weapon_jump_area_flags(qa_aas_reachability_special *special);
bool qa_aas_reachability_special_weapon_jump(qa_aas_reachability_special *special, int64_t from, int64_t to);
#endif
