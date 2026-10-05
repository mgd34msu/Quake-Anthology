#ifndef QA_FRONTEND_CONFIG_WEAPON_DEFAULTS_H
#define QA_FRONTEND_CONFIG_WEAPON_DEFAULTS_H
#include "qa/application_startup_prepare.h"
#include "qa/inventory.h"
#include "qa/input.h"

#define FRONTEND_CONFIG_WEAPON_CAPACITY ((size_t)QA_Q1_WEAPON_COUNT + (size_t)QA_Q2_WEAPON_COUNT + (size_t)QA_Q3_WEAPON_COUNT)

typedef struct frontend_config_weapon_catalog {
    qa_item_definition items[FRONTEND_CONFIG_WEAPON_CAPACITY];
    size_t count;
} frontend_config_weapon_catalog;

/* Native declarations from the actual selected ARSENAL. External programs
 * produce an empty catalog; this does not allocate or admit a GAME. */
bool frontend_config_weapon_defaults(qa_application *, const qa_launch_snapshot *,
    qa_launch_scope, qa_strings *, frontend_config_weapon_catalog *, qa_error *);

typedef struct frontend_config_weapon_binding_catalog {
    qa_input_weapon_binding items[FRONTEND_CONFIG_WEAPON_CAPACITY];
    char identities[FRONTEND_CONFIG_WEAPON_CAPACITY][64];
    size_t count;
} frontend_config_weapon_binding_catalog;
/* Boot binding rows from the borrowed draft's selected product. Rows borrow
 * native labels and this output's identity storage; do not copy the structure
 * while retaining its row pointers. No GAME or inventory admission occurs. */
bool frontend_config_weapon_bindings(const qa_launch_draft *, qa_launch_scope,
    frontend_config_weapon_binding_catalog *, qa_error *);
#endif
