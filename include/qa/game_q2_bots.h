#ifndef QA_GAME_Q2_BOTS_H
#define QA_GAME_Q2_BOTS_H
#include "qa/game_q2.h"

typedef struct qa_q2_bot_entity {
    qa_string_id model,classname;
    float max_health;
    int32_t frame;
    bool present,hidden,worldspawn;
} qa_q2_bot_entity;
typedef struct qa_q2_bot_arsenal_configuration {
    qa_q2_edition edition;
    bool deathmatch;
} qa_q2_bot_arsenal_configuration;
bool qa_q2_bot_arsenal_configuration_read(const qa_q2_game *,qa_q2_bot_arsenal_configuration *,qa_error *);
bool qa_q2_bot_arsenal_register_multiplayer(qa_q2_game *,qa_q2_weapon_rules,
                                           bool native_hook,qa_q2_edition hook_edition,qa_error *);
bool qa_q2_bot_arsenal_rules_read(const qa_q2_game *,qa_q2_weapon_rules *,qa_error *);
bool qa_q2_bot_equipment_register_hook(qa_q2_game *,qa_q2_weapon_rules,
                                      qa_q2_edition,qa_error *);
bool qa_q2_bot_arsenal_definition_read(const qa_q2_game *,uint32_t ordinal,
                                      const qa_q2_weapon_definition **,qa_error *);
bool qa_q2_bot_arsenal_definition_count(const qa_q2_game *,uint32_t *,qa_error *);
bool qa_q2_bot_entity_read(qa_q2_game *,qa_actor_id,qa_q2_bot_entity *,qa_error *);
bool qa_q2_bot_clock_read(const qa_q2_game *,uint64_t *,bool *,uint64_t *,qa_error *);
bool qa_q2_bot_max_clients(const qa_q2_game *,uint32_t *,qa_error *);
bool qa_q2_bot_activate(qa_q2_game *,qa_actor_id,qa_error *);
qa_actor_id qa_q2_bot_world_actor(const qa_q2_game *);
/* The caller frees genuine supply receipts with qa_supply_preview_free. */
bool qa_q2_bot_supply_preview(qa_q2_game *,qa_actor_id pickup,qa_actor_id recipient,
                              qa_supply_preview_result *,bool *eligible,bool *found,qa_error *);
#endif
