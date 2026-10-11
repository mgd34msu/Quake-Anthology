#ifndef QA_APPLICATION_UI_NAMES_H
#define QA_APPLICATION_UI_NAMES_H
#include "qa/game_q1.h"
#include "qa/catalog.h"
#include "qa/game_q2.h"

enum { QA_Q2_EFFECT_ITEM_RESPAWN = 256, QA_Q2_EFFECT_LOGOUT };
typedef struct qa_application_q2_effect_name {
    /* Zero is absent; stored source temporary types are offset by one. */
    uint16_t effect, beam;
    bool palette, independent;
} qa_application_q2_effect_name;

typedef struct qa_application_ui_names {
    qa_string_id empty_model;
    qa_item_id q1_weapons[QA_Q1_WEAPON_COUNT], q1_ammo[QA_Q1_AMMO_COUNT], q1_keys[2];
    qa_item_id q1_powers[QA_Q1_POWER_COUNT];
    qa_item_id q2_weapons[QA_Q2_WEAPON_COUNT], q2_ammo[QA_Q2_WEAPON_COUNT], q2_powers[7];
    qa_item_id q3_powers[6], q3_invulnerability;
    qa_string_id music, fog, debug_bounds, colored_explosion, developer_message;
    qa_string_id cutscene, sell_screen, monster_muzzle, entity_event, entity_event_plain;
    qa_string_id cp, chat, tchat, print;
    qa_string_id q2_parasite, q2_medic_cable, q2_grapple_cable, q2_lightning;
    qa_application_q2_effect_name *q2_effects;
    size_t q2_effect_count;
} qa_application_ui_names;
typedef struct qa_application qa_application;
/* IDs belong to the application's retained session table. */
bool qa_application_content_names_bind(qa_application *, const qa_catalog *, qa_error *);
qa_string_id qa_application_content_name(const qa_application *, const qa_catalog *, qa_product_id);
const qa_application_ui_names *qa_application_ui_names_read(const qa_application *);
qa_application_q2_effect_name qa_application_q2_effect_name_read(const qa_application *, qa_string_id);
#endif
