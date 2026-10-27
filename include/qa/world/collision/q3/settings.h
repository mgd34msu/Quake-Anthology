/* Port of src/world/collision/q3/settings.ts. Unregistered cvar aborts
 * with the TS Error text. GPL-2.0-or-later. */
#ifndef QA_WORLD_COLLISION_Q3_SETTINGS_H
#define QA_WORLD_COLLISION_Q3_SETTINGS_H
#include <stdbool.h>
#include "qa/core/cvars/index.h"
typedef struct qa_cvar_definition { const char *name; const char *value; double flags; } qa_collision_cvar_definition;
extern const qa_collision_cvar_definition qa_collision_map_cvar_definitions[3];
typedef struct qa_collision_map_settings { qa_cvar_registry *cvars; } qa_collision_map_settings;
void qa_collision_map_settings_init(qa_collision_map_settings *s, qa_cvar_registry *cvars);
void qa_collision_map_settings_register_map(qa_collision_map_settings *s);
bool qa_collision_map_settings_no_areas(qa_collision_map_settings *s);
bool qa_collision_map_settings_no_curves(qa_collision_map_settings *s);
bool qa_collision_map_settings_player_curve_clip(qa_collision_map_settings *s);
#endif
