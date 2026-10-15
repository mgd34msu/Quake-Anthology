#ifndef QA_APPLICATION_UI_NAMES_H
#define QA_APPLICATION_UI_NAMES_H
#include "qa/game_q1.h"
#include "qa/catalog.h"
#include "qa/game_q2.h"

#define QA_Q1_TEMP_SOUND_NAMES(X) \
    X(EXPLOSION, "weapons/r_exp3.wav") \
    X(TINK, "weapons/tink1.wav") \
    X(RIC1, "weapons/ric1.wav") \
    X(RIC2, "weapons/ric2.wav") \
    X(RIC3, "weapons/ric3.wav") \
    X(WIZARD, "wizard/hit.wav") \
    X(KNIGHT, "hknight/hit.wav")
#define QA_Q1_BEAM_NAMES(X) \
    X(0, 5, "progs/bolt.mdl") \
    X(1, 6, "progs/bolt2.mdl") \
    X(2, 9, "progs/bolt3.mdl") \
    X(3, 13, "progs/beam.mdl")
typedef enum frontend_fx_q1_sound {
    FRONTEND_FX_Q1_SOUND_NONE,
#define Q1_SOUND_ENUM(name, path) FRONTEND_FX_Q1_SOUND_##name,
    QA_Q1_TEMP_SOUND_NAMES(Q1_SOUND_ENUM)
#undef Q1_SOUND_ENUM
    FRONTEND_FX_Q1_SOUND_COUNT
} frontend_fx_q1_sound;
enum { QA_Q1_BEAM_MODELS = 4 };

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
    qa_item_id q2_remote_armor;
    qa_item_id q3_powers[6], q3_invulnerability;
    qa_string_id music, fog, debug_bounds, debug_graph, debug_graph_packet, colored_explosion, developer_message, source_pickup;
    qa_string_id q1_rerelease_debug;
    qa_string_id q1_temp_sounds[FRONTEND_FX_Q1_SOUND_COUNT], q1_beam_models[QA_Q1_BEAM_MODELS], talk_sound;
    qa_string_id flare_image, material_default, material_ladder;
    qa_string_id ranking_status, ranking_menu;
    qa_string_id client_counts[257];
    qa_string_id landing_sounds[3], q1_player_model, q2_health_icon, q2_help_icon, q2_talk_sound;
    qa_string_id cutscene, sell_screen, monster_muzzle, entity_event, entity_event_plain;
    qa_string_id cp, chat, tchat, print, trigger_hurt;
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
