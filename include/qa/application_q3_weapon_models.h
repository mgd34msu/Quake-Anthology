#ifndef QA_APPLICATION_Q3_WEAPON_MODELS_H
#define QA_APPLICATION_Q3_WEAPON_MODELS_H

#include "qa/q3_presentation.h"

/* Numeric handles and paths come from the actual original CG model registry.
 * They borrow that registry until mutation or retirement. Source weapon IDs
 * have their full int32 identity and imply no native weapon semantics. Zero
 * handles retain genuine absent registrations; gun_path is NULL for gun zero. */
typedef struct qa_application_q3_weapon_models {
    qa_q3_presentation_assets *assets;
    int32_t source_weapon;
    int32_t gun, hands, barrel, flash, invisibility, battle, quad;
    const char *gun_path;
} qa_application_q3_weapon_models;

#endif
