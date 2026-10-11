#ifndef QA_APPLICATION_UI_NAMES_H
#define QA_APPLICATION_UI_NAMES_H
#include "qa/game_q1.h"
#include "qa/game_q2.h"

typedef struct qa_application_ui_names {
    qa_item_id q1_weapons[QA_Q1_WEAPON_COUNT], q1_ammo[QA_Q1_AMMO_COUNT], q1_keys[2];
    qa_item_id q1_powers[QA_Q1_POWER_COUNT];
    qa_item_id q2_weapons[QA_Q2_WEAPON_COUNT], q2_ammo[QA_Q2_WEAPON_COUNT], q2_powers[7];
    qa_item_id q3_powers[6], q3_invulnerability;
} qa_application_ui_names;
typedef struct qa_application qa_application;
/* IDs belong to the application's retained session table. */
const qa_application_ui_names *qa_application_ui_names_read(const qa_application *);
#endif
