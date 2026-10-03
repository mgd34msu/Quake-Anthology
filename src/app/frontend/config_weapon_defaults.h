#ifndef QA_FRONTEND_CONFIG_WEAPON_DEFAULTS_H
#define QA_FRONTEND_CONFIG_WEAPON_DEFAULTS_H
#include "qa/application_startup_prepare.h"
#include "qa/inventory.h"

typedef struct frontend_config_weapon_catalog {
    qa_item_definition items[(size_t)QA_Q1_WEAPON_COUNT + (size_t)QA_Q2_WEAPON_COUNT + (size_t)QA_Q3_WEAPON_COUNT];
    size_t count;
} frontend_config_weapon_catalog;

/* Native declarations from the actual selected ARSENAL. External programs
 * produce an empty catalog; this does not allocate or admit a GAME. */
bool frontend_config_weapon_defaults(qa_application *, const qa_launch_snapshot *,
    qa_launch_scope, qa_strings *, frontend_config_weapon_catalog *, qa_error *);
#endif
